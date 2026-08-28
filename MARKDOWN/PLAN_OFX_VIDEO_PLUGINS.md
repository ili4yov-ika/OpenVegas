# Видеоплагины Vegas Pro / OFX — план и реализация

Как OpenVegas хостит **видеоплагины OpenFX**, включая собственные бандлы VEGAS Pro, и что
делать на платформах, где бинарники VEGAS не существуют.

Родительские планы: [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md)
(фаза **P4**), [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md) (фаза **9**).
Журнал: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md).

**Обновлено:** 2026-08-11 — реальный рендер через настоящие OFX-бинарники VEGAS
заработал end-to-end; кроссплатформенное обнаружение плагинов и ABI-гейт.

---

## Цель

1. Настоящие `.ofx` работают в preview и в Render As, а не подменяются эмуляцией.
2. Плагины VEGAS Pro 22 работают там, где они есть (Windows), и **не ломают** сборку там,
   где их нет (Linux / macOS).
3. Любой сторонний OFX-плагин (Resolve, Natron, TuttleOFX, open-source) находится и
   работает на всех трёх платформах.
4. Диагностируемость: почему конкретный плагин не загрузился — вопрос с ответом, а не
   с реверс-инжинирингом.

## Non-goals

- GPU-рендер OFX (`OfxImageEffectOpenGLRenderSuite`, OpenCL, DirectX) — CPU-путь достаточен для MVP.
- Собственные UI-оверлеи плагинов (`OfxInteractSuite`, `OfxHWndInteractSuite`) — параметры
  рисует OpenVegas.
- Vegas-suite'ы (`OfxVegasEffectSuite` и др.) — см. «Чего хост не даёт».

---

## Разбор барьера `DescribeInContext` (решено 2026-08-11)

До этой итерации настоящие бандлы VEGAS доходили до
`kOfxImageEffectActionDescribeInContext` и возвращали `kOfxStatErrMissingHostFeature`
(status 4), объявив ровно один клип `Source`. Предыдущее расследование (gdb + objdump +
Ghidra с восстановлением RTTI, найден класс `chromablurPlugin`) точную причину не нашло и
записало вывод «вероятен license/version gate внутри бинарника».

**Это было неверно.** Причина — дефект хоста, и она видна за один прогон, если логировать
каждый host-колбэк. Добавлен постоянный трассировщик
([`OfxTrace.h`](../src/plugins/OfxTrace.h), `OPENVEGAS_OFX_TRACE`), и лог показал:

```text
DescribeInContext(OfxImageEffectContextFilter) begin
  getString props@…[OfxImageEffectPropContext][0] = OfxImageEffectContextFilter -> 0
  clipDefine("Source")
  getDimension OfxTypeClip:Source[OfxImageEffectPropSupportedComponents][0] = <missing> -> 3
DescribeInContext(OfxImageEffectContextFilter) -> status 4
```

Плагин вызывает `ClipDescriptor::addSupportedComponent()`, которая в OFX C++ support
library растит список так:

```cpp
int n = propGetDimension(kOfxImageEffectPropSupportedComponents);
propSetString(kOfxImageEffectPropSupportedComponents, value, n);
```

Наш `clipDefine` создавал property set только с `kOfxPropName` и `kOfxPropType`, поэтому
`propGetDimension` отвечал `kOfxStatErrUnknown`. А `OFX::throwPropertyException` переводит
**любую** неудачу чтения свойства в `OFX::Exception::HostInadequate`, которая доходит до
хоста как `kOfxStatErrMissingHostFeature`. То есть код ошибки говорил «хосту не хватает
фичи», а не хватало ему собственной инициализации дескриптора.

**Урок для этого хоста:** OFX требует отдавать плагину **полностью заполненный** property
set на каждый дескриптор — плагин вправе прочитать любое свойство из спецификации до того,
как сам его записал. Отсюда `seedClipDescriptorProps` / `seedEffectDescriptorProps` /
`seedParamDescriptorProps` / `seedEffectInstanceProps` / `seedClipInstanceProps` в
[`OfxHost.cpp`](../src/plugins/OfxHost.cpp).

### Что понадобилось дальше по цепочке

Каждый следующий барьер находился тем же логом за одну итерацию:

| Действие | Чего не хватало | Исправление |
|---|---|---|
| `Load` | `OfxProgressSuite`, `OfxTimeLineSuite`, `OfxInteractSuite`, `OfxParametricParameterSuite`, `OfxMessageSuite` v2 | Реальные минимальные реализации; **не** null — support library кидает `HostInadequate` на любой null-suite |
| `DescribeInContext` | Свойства дескрипторов (выше) + Vegas-only in-arg `kOfxImageEffectPropVegasContext` | `seed*Props`, + in-arg |
| `CreateInstance` | `kOfxImageEffectPropContext` на инстансе | `seedEffectInstanceProps` |
| `Render` | `kOfxPropVegasTimeCode`, `kOfxImageEffectPropViewsToRender`, `kOfxImageEffectPropRenderView`, `kOfxImageEffectPropRenderQuality`, `kOfxImagePropUniqueIdentifier`, `kOfxImageEffectPropRenderScale` на image props | Полные render in-args + image props |
| `Render`, многопоточно | Полоса на поток меньше радиуса ядра → порча кучи внутри плагина | Кап потоков по высоте кадра |

### Результат

`com.vegascreativesoftware:chromablur` из настоящего `Vfx1.ofx`:
`Load → Describe → DescribeInContext → CreateInstance → Render`, все `status 0`,
два клипа (`Source`, `Output`), реальные параметры `HorizontalPixels` / `VerticalPixels` +
страница `Controls` — ровно то, что показывает UI самого VEGAS. При переданных параметрах
пиксели реально меняются.

Регрессии: `[video-fx][ofx][vegas-video]` в
[`tests/test_vegas_video_catalog.cpp`](../tests/test_vegas_video_catalog.cpp).

### ✅ Эффект из `.veg` работает (2026-08-25)

Долго не работал, и причина была одна: **блоб параметров не декодировался**. Слот приезжал с
пустой картой, инстанс брал объявленные плагином дефолты, а у Chroma Blur это `0` — радиус
нулевой, рендер вырождался в округление цветового преобразования. Плагин отрабатывал честно,
просто ему нечего было делать.

Разобрано в [`src/io/VegOfxParams.*`](../src/io/VegOfxParams.h). Запись самопроверяемая:

```
u32 idBytes, presetBytes; id; preset; u32 paramsBytes, count
на параметр: u32 valueBytes, nameBytes; name; значение; u32 keyCount;
             кейфрейм[keyCount]  (по 52 байта: double время (мс), u32 флаги,
                                  double значение, in/out по (время, значение))
```

Ширина значения нигде не записана — 4 байта у целого, по 8 на компоненту у точки или цвета.
Угадывать не нужно: счётчик кейфреймов идёт **сразу за значением** и записан независимо от
`valueBytes`, поэтому подходит ровно одна ширина. Не сошлось — читать отказываемся: числа из
середины чужой структуры доедут до плагина и отрисуются как что-то намеренное, а это хуже
эффекта на дефолтах.

Ручки Безье — абсолютные времена, а не смещения: у ключа на t=0 они на −0,1 и +0,1, у ключа
на 1037,001 — на 1036,901 и 1037,101.

Одно чтение служит обоим местам, где эта запись встречается: переход на затухании
(`parseOfxTransitions`) и эффект на событии.

**Анимация применяется.** Кривая едет в самом слоте (`__paramCurves` в его state,
[`FxParamCurves`](../src/plugins/FxParamCurves.h)), а не в дорожках автоматизации события:
цепочку применяют из мест, которые события не видят — превью-компоновщик, проход рендера,
картинка в окне FX. Кривая, доступная только части из них, анимировала бы в одном окне и
стояла в другом.

Замеры (шахматка 480×360 из **насыщенных** цветов — блюр хроматический, на серой мерить
нечем; метрика — сколько осталось горизонтального перепада в красном канале):

| Что подаётся плагину | Осталось детали |
|---|---|
| Дефолты плагина | **13,26** |
| Параметры из проекта (H=2,99 / V=4,49) | **9,53** |
| Кривая на t=0 (H=0, V=4,49) | **6,59** |
| Кривая на t=6,35 (H=10, V=4,49) | **3,29** |

Три ошибки по дороге были в самом измерении, не в коде: инстанс **помнит** параметры между
рендерами, а пустая карта ничего не перезаписывает, поэтому «контрольный» прогон без
параметров, стоявший вторым, тихо переиспользовал первые; шахматка была почти серой; и каталог
резолвил плагин по настройкам машины, беря VEGAS Pro 18, у которого рендер отказывает.

### Собственные подмены отключены (2026-08-12)

`OPENVEGAS_EMULATED_VIDEO_FX = 0` в [`OfxHost.h`](../src/plugins/OfxHost.h). Выключено:

| Что было | Чем притворялось |
|---|---|
| `processEmulated()` | box blur вместо Chroma Blur / Soften; инверсия каналов вместо Invert; sepia-матрица вместо Sepia; brightness/contrast; gain |
| Fallback в `processFrame()` | любой упавший `Render` заминался применением gain и возвращал «успех» |
| Ветка `isBrightnessContrastName` в `applyVideoFxChain()` | шорткат на подмену вместо реального плагина |

Смысл: подмены создавали впечатление, что приложение крутит эффекты VEGAS, тогда как оно
крутило их приближения — и ровно это скрывало настоящую проблему (нулевые параметры) почти
всю дорогу. Теперь эффект, который нельзя отрендерить по-настоящему, не рендерится вовсе,
а упавший `Render` возвращает `false`, а не «успех».

Код не удалён, а закомментирован препроцессором — чтобы флаг можно было вернуть для
A/B-сравнения при работе над декодером параметров.

> **Новые собственные реализации видеоэффектов не писать.** Путь вперёд один — разобрать
> настоящие параметры из `.veg`; расширять имитацию нельзя.

Регрессии на это: `applyVideoFxChain leaves the frame untouched without a real plug-in`,
`applyVideoFxChain does not substitute anything for an unknown effect`,
`OfxHost stand-in renderers are off`.

---

## Сопутствующие дефекты, вскрытые этим же исправлением

1. **Загружался не тот эффект.** `Vfx1.ofx` содержит десятки эффектов; неразрешённый индекс
   молча означал `0`. Пока `DescribeInContext` всегда падал, это было незаметно. Теперь
   `resolvePluginIndex()` сверяет `pluginIdentifier` с запрошенным `effectId` и
   переразрешает индекс, а при отсутствии эффекта — честно отказывает, вместо того чтобы
   отрендерить чужой фильтр.
2. **Source и Output указывали в один буфер.** Фильтр вправе читать любой пиксель source,
   записывая любой пиксель output; блюр по собственному частично записанному выходу даёт
   мусор. Теперь source — отдельная копия кадра.
3. **`clipGetRegionOfDefinition` возвращал 1×1.** Пространственные эффекты считают по RoD
   размер окна выборки. Теперь — реальный размер кадра.
4. **Повторные попытки загрузки на каждом кадре.** Неудачные модули кэшируются с причиной,
   иначе неработающий плагин делал бы `LoadLibrary` 60 раз в секунду за эмулируемым
   fallback'ом.

---

## Не всякий видеоэффект VEGAS — это OFX

Проверено перечислением `OfxGetPlugin` по всем установленным бандлам (и в копии
`SAMPLES/`, и в реальной инсталляции VEGAS Pro 22):

| Бандл | Эффектов |
|---|---|
| `Vfx1.ofx` | 78 |
| `MagixCVFx.ofx` | 11 |
| `MagixAiFx.ofx` | 10 |
| остальные 8 бандлов | по 1 |

Манифест `Vfx1.ofx.bundle/Contents/Resources/Vfx1.xml` при этом объявляет **102**
эффекта. Разница — не наша ошибка чтения: **Glint («Мерцание») и Soft Contrast среди
экспортируемых отсутствуют**, хотя внутри бинарника есть классы
`glintvelvetmatterPlugin` / `AVglintvelvetmatterOpenCLProcessor`. Строка
`com.vegascreativesoftware:glintvelvetmatter` в бинарнике не встречается вовсе, а
`com.vegascreativesoftware:chromablur` — встречается.

Вывод: это **legacy-плагины VEGAS**, не OFX. Их состояние лежит в проекте не блобом
OFX-параметров, а обычным XML (`<Glint>`, `<Softlight>`), и в `.veg` рядом с Chroma Blur
видно `kOfxImageEffectPropVegasUpliftGUID` — механизм замены старых плагинов на
OFX-версии, до которого эти два не дошли. Хостить тут нечего: OFX-бинарника не
существует ни в одной инсталляции.

Отсюда правило: **отсутствие OFX-бинарника не повод показывать выдуманные значения.**

### Что читается из `<Glint>` / `<Softlight>`

`VegReader::parseLegacyVideoFxStates` восстанавливает полное состояние
(`VegOpenResult::legacyFxStates`):

- **значения параметров**, сразу в единицах, которые показывает диалог VEGAS
  (доли → проценты, углы остаются градусами, `true`/`false` → 1/0);
- **анимацию** — каждый следующий XML-блоб предваряется тегом `0x1c` и 8-байтовым
  временем в тиках (10 МГц) за 28 байт до `<?xml`; блоб без такого разбега — текущее
  значение плагина, а не кейфрейм. Кейфреймы кладутся в `automationLanes` события,
  и диалог рисует их теми же ромбиками, что и VEGAS. Лейн заводится только для
  параметров, которые реально меняются: VEGAS пишет в каждый блоб все параметры;
- **имя пресета** («Sparkle», «Soft Moderate Contrast») — показывается в строке Preset.

Вложенные `<Mask>` / `<VignetteEffect>` не разбираются, и их одноимённые элементы
(`Enabled`, `Strength`) намеренно игнорируются — иначе они затирали бы значения самого
эффекта.

### Сопутствующий дефект: состояние терялось при разрешении слота

`VegasVideoPluginCatalog::resolveVideoFxSlot` собирал **новый** `FxSlot` из записи
каталога. Это выбрасывало всё, что вызывающая сторона уже восстановила: параметры из
проекта, `bypass` и `hostKey`, по которому хост находит загруженный инстанс. Через эту
функцию проходит каждый OFX-эффект, импортированный из `.veg`. Теперь разрешение только
дописывает путь к бинарнику и индекс эффекта.

---

## Многопоточность рендера

Плагины сами делят окно рендера по `threadIndex` / `threadMax`, полученным от
`OfxMultiThreadSuite::multiThread`. Замер на настоящем VEGAS Chroma Blur (radius 8):

| Кадр | Потоков | Результат |
|---|---|---|
| 512×512 | 16 (полоса 32 строки) | ок |
| 64×64 | 2 (полоса 32 строки) | ок |
| 64×64 | 4 (полоса 16 строк) | порча кучи внутри плагина |
| 64×64 | 8 / 16 | падение |

Порог — не число потоков, а **высота полосы** относительно радиуса ядра. Хост не может
узнать радиус, поэтому держит полосы заведомо крупными: `kMinRowsPerRenderThread = 64`.
На реальных размерах кадра это не мешает загрузить CPU (1080p / 64 = 16 полос).

`OPENVEGAS_OFX_THREADS=1` — принудительно однопоточный рендер, первое, что стоит
попробовать при падении стороннего плагина.

Рабочие потоки входят в COM-апартамент процесса (`CoInitializeEx`, Windows) — бандлы VEGAS
тянут COM-зависимости (`sharedk.dll`, OpenColorIO) из корня установки, а VEGAS вызывает их
из потоков, где апартамент уже есть. На других платформах — no-op.

---

## Vegas-расширения OFX

VEGAS собирает свои бандлы против форка OFX C++ support library с `OFX_EXTENSIONS_VEGAS`.
Имена свойств, действий и suite'ов восстановлены из самих поставляемых бинарников (они
хранят полные литеральные строки) и собраны в
[`OfxVegasExtensions.h`](../src/plugins/OfxVegasExtensions.h) — только имена, это факты
интерфейса, а не чужой код.

Хост объявляет: `kOfxImageEffectHostPropNativeOrigin` (= `…TopLeft`, как строки `QImage`),
`kOfxPropVegasHostAppDataDirectory`, `kOfxPropVegasHostHWnd`, и передаёт
`kOfxImageEffectPropVegasContext` в `DescribeInContext`, `kOfxPropVegasTimeCode` +
стереоскопические view-аргументы в `Render`.

Отдельная деталь, на которой легко ошибиться: **значения**
`kOfxImageEffectHostPropNativeOrigin` несут литеральный префикс `k`
(`"kOfxImageEffectHostPropNativeOriginTopLeft"`), тогда как имя свойства — нет. Так
определено в VEGAS SDK, а плагин сравнивает строки побайтово.

### Чего хост не даёт

`OfxVegasEffectSuite`, `OfxVegasKeyframeSuite`, `OfxVegasProgressSuite`,
`OfxVegasStereoscopicImageEffectSuite`, `OfxHWndInteractSuite`, `OfxHWndOverlayInteractSuite`,
`OfxOpenCLProgramSuite`, `OfxDirectXProgramSuite`, `OfxImageEffectOpenGLRenderSuite` —
`fetchSuite` отвечает `NULL`.

Для `OfxVegas*` и `OfxHWnd*` раскладка структур не опубликована и не восстановлена. Подсунуть
заглушку с угаданной раскладкой — значит выдать плагину указатели на функции неверной арности
и уронить приложение; честный `NULL` лучше. По трассировке Chroma Blur все они опциональны:
плагин запрашивает их на `Load` и работает дальше без них.

**Поправка от 2026-08-26: для трёх GPU-suite'ов это не так, и не для всех плагинов они
опциональны.** `OfxImageEffectOpenGLRenderSuiteV1` и `OfxOpenCLProgramSuiteV1` — часть
стандарта OpenFX, их раскладка лежит в уже подключённом
[`thirdparty/openfx/include/ofxGPURender.h`](../thirdparty/openfx/include/ofxGPURender.h).
Не восстановлен только `OfxDirectXProgramSuite` — вот он расширение VEGAS.

Раз раскладка известна, оба реализованы честно: правильная арность из заголовка, а функции
отказывают — GL-контекста в этой сборке нет, и `clipLoadTexture` говорит об этом статусом, а не
делает вид. Хост заодно отвечает на `kOfxImageEffectPropOpenGLRenderSupported` строкой `false`
вместо молчания: GPU-плагины спрашивают это свойство по имени.

### Что держит `VEGAS Warp Flow` и `VEGAS GL Transition` — и чего **не** держит (2026-08-26)

Восемь из одиннадцати эффектов `MagixCVFx.ofx` на `DescribeInContext` возвращают 0 с **нулём
клипов и нулём параметров**: motionblur, denoisingnlm, flickerreducer, gltransition,
lenscorrection, meshwarp, timewarp, warpflowtransition. Описываются ровно три —
motiontracker (16 параметров), shotdetector (12), stabilize (68).

**Поправка: первое объяснение было неверным.** Здесь стояло, что причина — `NULL` на три
GPU-suite'а. Это была догадка по корреляции (раскол шёл ровно по GPU-эффектам), и проверка её
не подтвердила. Проверено и отвергнуто пять кандидатов:

| гипотеза | проверка | результат |
|---|---|---|
| `fetchSuite` отдаёт `NULL` на GPU-suite'ы | реализованы `OfxImageEffectOpenGLRenderSuiteV1` и `OfxOpenCLProgramSuiteV1` | 8 из 11 по-прежнему молчат |
| хост не объявляет `OpenGLRenderSupported` | выставлено `false` | без изменений |
| хост объявляет его в `false` | временно выставлено `true` | без изменений |
| плагин смотрит на имя хоста | `OfxPropName` временно `com.vegascreativesoftware.vegas` (строка есть в бинарнике) | без изменений |
| нет зависимостей рядом с бандлом | `opencv_*490.dll`, `opencl.dll`, `sharedk.dll` на месте, и `stabilize` через OpenCV описывается | не то |
| нет оконной платформы для GL | `QT_QPA_PLATFORM=windows` вместо `offscreen` | без изменений |

Где именно обрыв — видно точно. `Describe` у работающего `stabilize` и у молчащего
`warpflowtransition` **одинаков**: оба ставят только метки и группу. Расходятся они в
`DescribeInContext`: `stabilize` читает контекст и идёт в `clipDefine`, а `warpflowtransition`
читает контекст и сразу возвращается — **для всех четырёх контекстов, включая собственный
Transition**. При этом ни одного `<missing>` в трассировке не остаётся: хост отвечает на всё,
о чём его спросили.

То есть проверка не на уровне OFX-протокола — плагин смотрит на что-то своё. Следующие
кандидаты: он сам опрашивает устройство (OpenCL/Vulkan/OSMesa — все эти рантаймы в его таблице
импорта) либо спрашивает `sharedk.dll` о редакции VEGAS. Разобрать это можно только
дизассемблером.

### Отказ на `Load` отравляет весь бинарник (2026-08-26)

Найдено по падению, которое воспроизвёл пользователь: открыть
`SAMPLES/veg_project/project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg` и подвинуть
курсор — SIGSEGV **внутри чужого кода**, в `Vfx1.ofx` из VEGAS Pro 18, на первом же кадре с
Chroma Blur. Стек: `MainWindow::refreshPreviewFrame` → `VideoCompositor::compose` →
`applyVideoFxChain` → `OfxHost::processSlot` → `processFrame` →
`mainEntry(kOfxImageEffectActionRender)` → и дальше без символов.

Трассировка (`OPENVEGAS_OFX_TRACE`) показала последовательность, которую иначе не увидеть.
За 35 секунд до падения, на превью переходов:

```
=== …/VEGAS Pro 18.0/…/Vfx1.ofx [#71 com.vegascreativesoftware:zoom] ===
Load begin
  …
  getString host[OfxPropVegasSpikeKey][0] = <missing> -> 3
Load -> status 4                     ← kOfxStatErrMissingHostFeature
=== …/SAMPLES/VEGAS-PRO-22-…/Vfx1.ofx [#71 com.vegascreativesoftware:zoom] ===
Load -> status 0                     ← откат сработал, переход отрисован
```

и потом, уже на эффекте из проекта:

```
=== …/VEGAS Pro 18.0/…/Vfx1.ofx [#13 com.vegascreativesoftware:chromablur] ===
Load begin
Load -> status 0                     ← ни одного fetchSuite: «уже загружено»
Describe -> status 0
DescribeInContext(Filter) -> status 0, 2 clip(s), 3 param(s)
Render begin (t=81.9466, 792x594)
                                     ← SIGSEGV
```

**Механика.** `kOfxActionLoad` в support library — операция на **весь бинарник**, а не на
плагин, и она считает вызовы: настоящая работа делается на первом, дальше возвращается
`kOfxStatOK`. Первый вызов у VEGAS 18 бросил `HostInadequate` (см. ниже) — но счётчик уже
увеличился. Поэтому **любой следующий** эффект из того же файла получает `kOfxStatOK`, ни
одного suite при этом не получив, спокойно описывается — и падает в рендере на
неинициализированных глобалах. Сам файл при этом остаётся в процессе: `QLibrary` не
выгружает библиотеку в деструкторе.

У нас неудачная загрузка запоминалась **по эффекту** (`path#effectId`), а не по файлу.
Отказ на `zoom` не мешал попробовать `chromablur` из того же `Vfx1.ofx` — и это ровно тот
путь, по которому пришло падение.

**Почему VEGAS 18 отказывается.** Её форк support library читает при построении описания
хоста свойство `OfxPropVegasSpikeKey` — в общем ряду обычных полей, между `OfxPropName` и
`OfxPropVersion` (строки рядом по смещению `0x11006A8`: `Tried to create host description
when we already have one.`, `OfxPropName`, `OfxPropVegasSpikeKey`, `OfxPropVersion`,
`OfxImageEffectHostPropIsBackground`, …). Чтение бросает, если свойства нет. В `Vfx1.ofx`
из VEGAS Pro 22 этой строки **нет вовсе** — поле убрали. Значение и смысл неизвестны, имя
читается как авторизационный токен, а цель проекта — VEGAS Pro 22; поэтому свойство
намеренно **не выдаётся**, только записано по имени в `OfxVegasExtensions.h`, чтобы отказ
старого бандла был узнаваем.

**Исправлено тремя вещами:**

1. `Impl::failedBinaries` — отказ на `kOfxActionLoad` (и всё, что относится к файлу
   целиком: не та архитектура, нет точек входа, ноль плагинов) записывается **по пути**, и
   весь файл дальше пропускается. То же в `enumerateEffects()`: после первого отказа
   остальные плагины файла перечисляются по идентификатору, без `Load`/`Describe`.
2. `VegasVideoPluginCatalog::alternateBinaries()` + `OfxHost::instantiateFromAlternate()` —
   каталог теперь помнит **все** бинарники, объявляющие эффект, а не только победивший в
   поиске. Порядок поиска задаёт путь из Preferences и ничего не говорит о работоспособности:
   на этой машине `plugins/ofxPath` указывал на VEGAS Pro 18, и он затенял VEGAS 22 из
   `SAMPLES`. Если выбранный бандл не создаёт инстанс — берётся следующий, объявляющий тот
   же `effectId`.
3. `OfxImageEffectPropPixelOrder` (VEGAS-расширение, значения `OfxImagePixelOrderRGBA` /
   `…BGRA`, восстановлены из бинарника) теперь объявляется на клипах и изображениях. Плагин
   спрашивал его перед каждым `clipGetImage` и получал промах; наши буферы и правда RGBA.

**Проверка.** Тот же сценарий под gdb после исправления: падения нет, приложение закрылось
штатно, в трассе `"com.vegascreativesoftware:chromablur" taken from "…SAMPLES/VEGAS-PRO-22-…"
instead` и то же для `sepia`, **153 рендера — все со статусом 0**.

---

## Кроссплатформенность

Windows-зависимости здесь двух разных сортов, и лечатся они по-разному.

### 1. Windows-специфичный код хоста → условная компиляция

| Что | Где | Не-Windows |
|---|---|---|
| `SetDllDirectory` для соседних runtime-DLL VEGAS | `ScopedOfxDllDirectory` | no-op; `dlopen` уже ищет по `RPATH`/`LD_LIBRARY_PATH` |
| `CoInitializeEx` на рабочих потоках рендера | `ScopedComApartment` | no-op |
| `kOfxPropVegasHostHWnd` | host props | `nullptr` |
| `kOfxPropVegasHostAppDataDirectory` | host props | `QStandardPaths::AppDataLocation` |

### 2. Windows-only **бинарники** плагинов → ABI-гейт + два рабочих пути

Бандлы VEGAS содержат только `Contents/Win64` — это PE-DLL, которые на Linux и macOS
загрузить нельзя в принципе. Раньше выбор папки архитектуры был жёстко «Win64 первым», то
есть Linux-сборка выбрала бы PE и отдала его `dlopen`.

[`OfxPluginPaths`](../src/plugins/OfxPluginPaths.h) разделяет два вопроса:

- **`loadableArchFolderNames()`** — папки ABI, которые *эта* сборка может загрузить
  (Windows x64 → `Win64`; macOS → `MacOS-arm-64` / `MacOS` / `MacOS-x86-64`; Linux →
  `Linux-x86-64` / `Linux-arm-64` / …). Имя в этом списке — обещание, что попытка загрузки
  допустима.
- **`archIncompatibilityReason()`** — человекочитаемая причина отказа
  («built for Win64; this is a Linux-x86-64 build»).

`checkArchLoadable()` стоит перед **каждым** `LoadLibrary`/`dlopen`. Чужеархитектурный
бандл при этом **остаётся в каталоге**: он находится, называется и показывается, проект со
ссылкой на него открывается — просто не загружается, а `OfxPluginDesc::archNote` объясняет
почему.

Что при этом реально происходит на Linux / macOS с проектом VEGAS:

| Слой | Поведение |
|---|---|
| Открытие `.veg` с Chroma Blur | Работает: цепочка Event FX восстанавливается по имени |
| Реальный `.ofx` | Не загружается — ABI; причина в `archNote` и в трассе |
| Рендер | Эмулируемый fallback `processEmulated()` по displayName |
| Сторонние OFX-плагины для этой ОС | Работают полноценно (см. ниже) |

### 3. Обнаружение плагинов по стандарту OFX

Раньше поиск был только по путям установки VEGAS — на Linux и macOS видеоплагинов не
находилось вообще. Добавлены стандартные корни OpenFX
([`OfxPluginPaths::standardRoots()`](../src/plugins/OfxPluginPaths.h)), добавляемые в
`PluginScanner::candidateRoots()` последними, чтобы явно настроенный путь и путь VEGAS
по-прежнему были приоритетнее:

| Платформа | Корни |
|---|---|
| Все | `OFX_PLUGIN_PATH` (`;` на Windows, `:` иначе) — стандартный override |
| Windows | `%CommonProgramFiles%\OFX\Plugins`, `%CommonProgramFiles(x86)%\OFX\Plugins` |
| macOS | `/Library/OFX/Plugins`, `~/Library/OFX/Plugins` |
| Linux | `/usr/OFX/Plugins`, `/usr/local/OFX/Plugins`, `/opt/OFX/Plugins`, `~/OFX/Plugins`, `~/.local/share/OFX/Plugins` |

### 4. Каталог без манифестов VEGAS

Каталог строился парсингом `Contents/Resources/*.xml` бандлов VEGAS. У стороннего плагина
такого манифеста нет — то есть у **всех** плагинов на Linux и macOS.

`OfxHost::enumerateEffects()` спрашивает сам бинарник (`OfxGetNumberOfPlugins` + `Describe`)
и отдаёт `effectId`, `kOfxPropLabel`, `kOfxImageEffectPluginPropGrouping`.
`parseBundle()` уходит на этот путь, когда манифеста нет или он ничего не описал.
Разбор по корням больше не останавливается на первом непустом — машина может иметь и
бандлы VEGAS, и сторонние плагины, оба принадлежат каталогу.

---

## Диагностика

```bash
# Windows (PowerShell)
$env:OPENVEGAS_OFX_TRACE = "C:/temp/ofx.log"; .\OpenVegas.exe
# Linux / macOS
OPENVEGAS_OFX_TRACE=/tmp/ofx.log ./OpenVegas
```

| Переменная | Действие |
|---|---|
| `OPENVEGAS_OFX_TRACE` | `1` → `<temp>/openvegas-ofx-trace.log`, либо явный путь. Пишет каждый host-колбэк: `fetchSuite`, `propGet*`/`propSet*` с именем свойства и статусом, `clipDefine`, `paramDefine`, границы каждого action |
| `OPENVEGAS_OFX_THREADS` | Потолок потоков рендера; `1` — однопоточно |
| `OFX_PLUGIN_PATH` | Стандартный OFX-override путей поиска |

Строка вида `… = <missing> -> 3` в трассе — почти всегда и есть причина отказа плагина:
это свойство, которое хост обязан был отдать.

---

## Тесты

| Тег | Покрытие |
|---|---|
| `[video-fx][ofx][vegas-video]` | Настоящий `Vfx1.ofx`: `Load`+`Describe`, реальные параметры Chroma Blur, реальный рендер меняет пиксели, мелкий кадр не роняет процесс (кап потоков) |
| `[plugins][state][veg][video-fx]` | Реальные значения, кейфреймы и пресет Glint / Soft Contrast из `.veg`; открытый проект несёт их в слоте и в automation lanes |
| `[media][ofx][paths]` | `OFX_PLUGIN_PATH`, ABI-гейт, приоритет нативной архитектуры, стандартные корни в `PluginScanner` |
| `[media][ofx][paths][plugins]` | `enumerateEffects` на фикстуре `Gain.ofx`; бандл без манифеста VEGAS попадает в каталог |
| `[media][ofx][plugins]` | Прежние: describe бандла, fail-soft, эмуляция, `Gain.ofx` E2E |

Запуск:

```bash
build/Windows_MinGW-x64/openvegas_media_tests.exe "[ofx]"
build/Windows_MinGW-x64/openvegas_video_tests.exe "[vegas-video]"
```

---

## Что осталось

| Тема | Заметки |
|---|---|
| **Собственные формы плагинов (нативный UI)** | VEGAS показывает у Chroma Blur свою белую панель, а не ползунки. Это не «страница параметров OFX», а **HWND-овый custom UI**: плагин объявляет `OfxImageEffectPluginPropHWndInteractV1` и ждёт от хоста `OfxHWndInteractSuite`, чья раскладка не опубликована. План реверса — [`PLAN_OFX_HWND_INTERACT_RE.md`](PLAN_OFX_HWND_INTERACT_RE.md). **Важно:** у Glint формы не будет никогда по другой причине — он вообще не OFX-плагин, его белое окно рисует сам VEGAS |
| **OFX-блоб параметров из `.veg`** | **Главный блокер видимости.** Без него эффекты из проекта рендерятся на дефолтах (у Chroma Blur это 0) и в превью не видны — замер выше. Формат читаемый: UTF-16 имя + double'ы + кейфреймы |
| Собственный рендер Glint / Soft Contrast | Значения и анимация читаются из проекта, но `processEmulated()` их пока не рисует — Glint в превью не даёт картинки |
| `<Mask>` / `<VignetteEffect>` внутри legacy-эффектов | Не разбираются; в VEGAS это вкладка Mask рядом с Effect |
| Многопоточный рендер VEGAS-плагинов на мелких кадрах | Обойдено капом полосы; настоящая причина внутри плагина |
| Suite'ы `OfxVegas*` | Раскладка структур неизвестна; по трассе — опциональны |
| Свои UI-оверлеи плагинов | `OfxInteractSuite` — no-op; параметры рисует OpenVegas |
| GPU-рендер (OpenGL / OpenCL / DirectX) | `fetchSuite` → `NULL`, плагины уходят на CPU-путь |
| Бандлы VEGAS на Linux / macOS | Только эмулируемый fallback. Полноценно — только out-of-process мост под Wine; проектируется, в MVP не входит |
| Изоляция процесса | Сейчас плагины грузятся в процесс приложения: падение плагина = падение OpenVegas. Тот же мост закрыл бы и это |
| Float-глубина пикселя | Хост объявляет только `kOfxBitDepthByte` |

---

## Полный инвентарь видео- и OFX-плагинов (обновлено 2026-08-27)

Детальный обзор всех плагинов VEGAS Pro 22 в
[`VEGAS_VIDEO_PLUGINS_INVENTORY.md`](VEGAS_VIDEO_PLUGINS_INVENTORY.md).

### Legacy COM/DXT (`Video Plug-Ins/`) — 0 OFX

| DLL | Размер | Эффекты | Архитектура |
|-----|--------|---------|-------------|
| `PluginWrapper.dll` | 183 KB | 0 | COM wrapper |
| `SfPagePeel.dll` | 528 KB | 9 | COM DXT, `sharedk.dll` |
| `sftrans1.dll` | 2.5 MB | 20+ | COM DXT + SkynUI, 316 classes |
| `vfx1.dll` | 1.8 MB | 10 | COM DXT + 3D/spline, 283 classes |
| `vidpcore.dll` | 6.0 MB | 32 | COM DXT, 53 classes |

### OFX (`OFX Video Plug-Ins/`) — 11 бандлов, ~72 плагина

| Бандл | Бинарник | Размер | Плагины | Тип |
|-------|---------|--------|---------|-----|
| Vfx1 | Vfx1.ofx | 10.2 MB | 40+ | Monolithic |
| MagixCVFx | MagixCVFx.ofx | 5.6 MB | 11 | Monolithic (8 GPU-dependent) |
| spica_cutout | spica_cutout.ofx | 3.6 MB | 1 | Monolithic, OpenCL (Sony-era) |
| spica_resizer | spica_resizer.ofx | 3.6 MB | 1 | Monolithic, OpenCL (Sony-era) |
| MagixAiFx | MagixAiFx.ofx | 1.9 MB | 11 | Monolithic (DL Models) |
| Stabilize | Stabilize.ofx | 492 KB | 1 | Monolithic |
| Filters | Filters.ofx | 524 KB | 1 | Monolithic |
| TitlesAndText | TitlesAndText.ofx | 475 KB | 1 | Monolithic (Generator) |
| ofx360Stabilizer | ofx360Stabilizer.ofx | 60 KB | 1 | Thin proxy + cpu/ocl.exec |
| ofxRotation | VegasOfxRotation.ofx | 60 KB | 1 | Thin proxy + cpu/ocl.exec |
| ofxStitch | VegasOfxStitch.ofx | 221 KB | 1 | Thin proxy + helper DLLs |

### Экспортная ABI OFX-бандлов (подтверждено ghidra + dumpbin, 2026-08-27)

Все 11 бандлов используют **упрощённый OFX ABI без `OfxSetHost`**:

| Экспорт | Сигнатура | Назначение |
|---------|-----------|------------|
| `OfxGetNumberOfPlugins` | `int()` | Число плагинов в бандле |
| `OfxGetPlugin` | `void* (int index)` | Структура `OfxPlugin` по индексу |

Хост (`OfxHost.cpp:2345-2346`) вызывает их через `lib.resolve()` — совпадает.
`OfxGetPlugin` возвращает `OfxPlugin`:
`{ apiVersion, pluginVersion, "OfxImageEffectPluginAPI", pluginIdentifier, pluginDescriptor, setHost, mainEntry }`
(декодировано из `Stabilize.ofx @ 0x180020aa0`).

### Кроссплагинные приватные C++-экспорты (out-of-process rendering)

4 бандла экспортируют классы `sfmemorytoken` — механизм **shared-memory IPC**
между хостом и out-of-process рендером:

| Класс | Методы | Назначение |
|-------|--------|------------|
| `CMappingOfSfMemoryToken` | `GetPointer`, `Close`, `DataSize`, `Pointer`, `Dispose` | Маппинг shared memory в адресное пространство |
| `COutOfProcessMemoryToken` | `GetMemoryToken` | Импорт токена чужого процесса |

Бандлы с этими экспортами: **Stabilize, Filters, MagixCVFx, Vfx1**.
`Filters.ofx` дополнительно экспортирует `CScanlineIntersect(Pool)` (сканирующая
растеризация), `Vfx1.ofx` — `A_Buffer`. Спокойные бандлы (TitlesAndText,
ofx360Stabilizer, ofxRotation, ofxStitch, spica_*, MagixAiFx) — только OFX ABI.

### Каталог плагинов по plugin identifiers (подтверждено, 2026-08-27)

Всего **107 плагинов**: Vfx1=78, MagixCVFx=11, MagixAiFx=10, и по 1 в
Stabilize/Filters/TitlesAndText/VegasOfxStitch/spica_cutout/spica_resizer/
ofx360Stabilizer/VegasOfxRotation. Полный список — в
[`VEGAS_VIDEO_PLUGINS_INVENTORY.md`](VEGAS_VIDEO_PLUGINS_INVENTORY.md) §2A.

### Vfx1.ofx — приватный VEGAS SDK в экспортах

`Vfx1.ofx` (10 MB) экспортирует не только OFX, но и приватный C++ SDK (~380 имён):

- **UI**: `SfScope_*` / `ScopeInst_*` (осциллограф/векторскоп), `CSfMenuManager`
  (меню через `HMENU`), `SfWnd_ForwardMsg` / `SfWnd_DisregardFutureMsgs` (оконный wrapper)
- **Графика**: `CSfDib*` (DIB-операции: autolevels, alpha blend, glow, HSL adjust,
  radial blur, japеg load), `CSfSurface` (pixel aspect), newsprint-блендеры шаблонные
- **Математика**: `Vector`/`Bounds`/`Path`/`Mask`/`Anchor`/`Tangent` (безе-пути),
  `CSfHermiteCubicEval`/`CSfLinearEval` (сглаживание), `CScanlineIntersect(Pool)`
- **Прочее**: `CSfXML_Document`, `CSfGlyphSet`, `CSfMonitored_CoInitializeEx`

Это статически слинкованная копия сонатских компонентов (CSf = "Creative Studio/sf")
внутри плагина — не требует отдельных DLL.

### MagixCVFx — компьютерное зрение в экспортах

`MagixCVFx.ofx` экспортирует ядра CV: `opticalFlow`/`opticalFlow8/32`,
`patchTracker`/`patchTrackerSingle`, `calcHomography`, `calcTransformationParameter`,
`MotionCompensatedCrossFade8/32`, `MotionCompensatedY32`.
`MagixAiFx.ofx`: `QueryDirectX`, `runAiTests`, `runAiTestsW`.

### OpenColorIO

- `OpenColorIO_2_0.dll` — 3.84 MB, 943 exports, полный OCIO v2.0 SDK
- `OpenColorIO/` — 460 MB ACES LUTs (3 конфига: aces_0.7.1, aces_1.2, aces__OLD)
- `ColorGradingWindow.dll` — 3.17 MB, .NET OFX UI wrapper (34 OFX references)
- `ColorGradingTools.dll` — 19 KB, .NET UI helper
