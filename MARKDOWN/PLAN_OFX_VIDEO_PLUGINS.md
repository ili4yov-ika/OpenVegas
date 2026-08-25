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

Раскладка их структур не опубликована и не восстановлена. Подсунуть заглушку с угаданной
раскладкой — значит выдать плагину указатели на функции неверной арности и уронить
приложение; честный `NULL` лучше. По трассировке Chroma Blur все они опциональны: плагин
запрашивает их на `Load` и работает дальше без них.

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
