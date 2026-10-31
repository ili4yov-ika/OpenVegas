# Media Generators и Titles & Text VEGAS Pro 22 против OpenVegas

**Дата:** 2026-10-25
**Эталоны:** `SAMPLES/veg_projects/project_media-generators-without-titles-and-text.veg`
(сделан под эту проверку) и `project_titles-and-text.veg`; манифесты и `PresetPackage.xml`
бандлов `Vfx1` и `TitlesAndText`; реестровые строки legacy-DLL; .NET-сборки
`ScriptPortal.MediaSoftware.TextGen.*`.
**Как проверялось импортом:** проект открывался `VegReader::open` и проходил
`ProjectModel::applyVegImport` — тем же путём, что в приложении; печатались пул медиа,
дорожки, события и их цепочки FX.

## 1. Какие генераторы есть в VEGAS 22

| Генератор | Где живёт | Штатных пресетов | В проекте-эталоне |
|---|---|---:|---:|
| Checkerboard | `Vfx1.ofx` | 12 | 13 |
| Noise Texture | `Vfx1.ofx` | 20 | 20 |
| Solid Color | `Vfx1.ofx` | 10 | 10 |
| Test Pattern | `Vfx1.ofx` | 9 | 14 |
| Color Gradient | **legacy** `{8B5DEABC-53BD-49F6-9803-367478307A59}` | — | 18 |
| Credit Roll | **legacy** `DXSCredits`, `{02FD3B60-986B-11D4-9F6F-00485481BD31}` | — | 8 |
| Titles & Text | `TitlesAndText.ofx` + .NET TextGen | 52 | — |

`com.vegascreativesoftware:colorgradient` объявлен в манифесте `Vfx1`, но кода в
`Vfx1.ofx` нет (как и у Gradient Wipe, см.
[TRANSITIONS_VEGAS22_AUDIT.md](TRANSITIONS_VEGAS22_AUDIT.md)). Color Gradient и Credit
Roll в VEGAS 22 — legacy DirectX; в `.veg` их медиа записаны путём
`META:\Video Generator\{CLSID} (n)[k]` без `{Svfx:…}`.

## 2. Импорт генераторов из `.veg` — исправлен (2026-10-25)

Было: пул медиа читался верно, но **на таймлайн генераторы не попадали** — 78 пустых
видеособытий, а первому событию как **Event FX** цеплялись все четыре OFX-генератора
разом; Color Gradient и Credit Roll не опознавались (`generatorId` знал только `{Svfx:…}`),
параметры не читались.

Что сделано:

- **Связь «событие → медиа».** В событии лежит take `{168D206D-…}`; uint32 по смещению
  payload+36 — id медиа в пуле, а id записи пула — uint32 по payload+60 в
  `{B28F2D5D-…}`. Проверено на всех событиях трёх проектов (генераторы 78/78, файлы 4/4,
  Titles & Text 56/56). «(n)» в пути генерируемого медиа — лишь подпись: в проекте с
  титрами id 5 подписан «(4)». `VegEventInfo::mediaRef`, `VegPoolMedia::mediaId`.
- **Legacy-генераторы** опознаются по CLSID из пути `META:\Video Generator\{CLSID} (n)`;
  берётся имя пресета («Linear White to Black», «Plain Scrolling on Black»), двоичные
  параметры после него не разобраны.
- **Параметры OFX-генераторов** декодируются `vegOfxDecodeEffect` с того же
  `{Svfx:…}`, что стоит в записи пула отдельной строкой после имени: Checkerboard —
  13 параметров, Noise Texture — 18, Solid Color и Test Pattern — по одному.
- **Event FX** больше не собираются из записей пула: идентификаторы внутри
  `{B28F2D5D-…}` принадлежат медиа, а не цепочке события.
- **На таймлайне** событие становится генератором (`EventMediaKind::Title`, слот
  `builtin:MediaGenerator:…`). Слот хранит плагин VEGAS, пресет и все параметры с
  кривыми (`vegasPluginId`, `vegasPreset`, `vegasParams`, `vegasCurves`); поля
  заглушки-рендерера заполняются из них, где он это умеет — цвета, ширина клетки,
  сплошная заливка.

Тесты: `tests/test_veg_generators.cpp`. Рисуются генераторы по-прежнему заглушкой (§3).

## 3. Генераторы в OpenVegas — имитации

**Пресеты исправлены (2026-10-25).** Браузер генераторов строится из таблицы
`src/video/GeneratorPresetData.cpp`, которую пишет `tools/gen_generator_presets.py`:
OFX-генераторы — из `PresetPackage.xml` с их значениями, Color Gradient и Credit Roll —
по таблице строк `sftrans1.dll` (только имена), «(Default)» — как его сохраняет VEGAS.
Теперь там 13 / 18 / 8 / 20 / 10 / 10 пресетов, имена совпадают с VEGAS, и тест сверяет,
что для каждого OFX-генератора из проекта-эталона значения таблицы равны сохранённым
VEGAS. Пресет, перетащенный из браузера, несёт плагин, имя и значения VEGAS — те же, что
у такого же генератора из `.veg` (`mediaGeneratorFromVegas()` — общий путь).

**Рендер остался заглушкой** — таблица ниже описывает его по-прежнему: у Noise Texture
все 20 миниатюр одинаковы с точностью до цветов, у Color Gradient и Credit Roll — все
одинаковы. Различить их может только настоящий плагин.

Было до исправления пресетов:

`MediaGeneratorPane` / `MediaGeneratorApply` рисуют **приближения**, а не генераторы VEGAS —
это противоречит правилу проекта (`OPENVEGAS_EMULATED_VIDEO_FX = 0`, README):

| Генератор | VEGAS 22 | OpenVegas |
|---|---|---|
| Checkerboard | «(Default)» + 12 пресетов пакета, 10 параметров (размер клетки, сглаживание краёв, 2 цвета, сдвиг сетки) | 13 пресетов: **Grille** вместо **Grate**, **Letterbox** вместо **Letterhead**; цвета и размеры выдуманы |
| Noise Texture | 20 пресетов (Standard Noise … Chalk Scribbles), 15 параметров | 1 пресет, рисуется узором «Bumps» |
| Solid Color | 10 пресетов (White … Orange), параметр Color | 1 пресет, синий `#0078d7` |
| Test Pattern | 9 пресетов (SMPTE Bars NTSC/PAL, Color Bars, Porches, Ramp, Gradient) | 1 пресет, узор «Grille» |
| Color Gradient | legacy, параметры не разобраны | 1 пресет, двухцветный градиент |
| Credit Roll | legacy, параметры не разобраны | 1 пресет, **градиент** вместо титров |

«(Default)» — настоящий пресет VEGAS: в пакете его нет, но проект сохраняет это имя у
Checkerboard и Test Pattern. Имена и значения остальных пресетов четырёх OFX-генераторов
лежат в `Vfx1/Contents/Presets/PresetPackage.xml`
— тот же источник, из которого уже генерируются пресеты переходов
(`tools/gen_transition_presets.py`). Рендер: все четыре есть в `Vfx1.ofx`, и хост их
грузит — через `OfxHost` их можно рисовать настоящим плагином там, где VEGAS установлен.

## 4. Titles & Text

### 4.1 Из чего сделан

`TitlesAndText.ofx` (475 КБ) — только нативная OFX-оболочка: импортирует GDI, USER,
KERNEL и CRT, никакого DirectWrite/D2D. Вёрстка, анимации и эффекты — в **.NET**-сборках
`ScriptPortal.MediaSoftware.TextGen.CoreGraphics.dll` и `…TextGen.OFXInterop.dll`.
Поэтому Ghidra здесь не тот инструмент: она не декомпилирует IL, а нативной части в
проекте Ghidra (`decompile-src/VEGAS_Pro_22.gpr`) нет — `TitlesAndText.ofx` туда не
импортирован, открыт только `vegas220.exe`.

**Анимации — это данные, а не код.** В `OFXInterop.dll` (и в `CoreGraphics.dll`) лежат
50 XML-ресурсов `MetaTextCollectionItem`, по одному на каждое значение AnimationName:
24 именованных и `_Title01`…`_Title25` (у `_None` своего файла нет). В них граф
узлов текста с `transform`, `signal`/`function`/`range`/`cascade`/`ControlValue` и
эффектами узла (GaussianBlur, Glow, DropShadow, InnerShadow, градиентная заливка).
Это настоящие анимации VEGAS. В OpenVegas их кривые **сочинены** (`presetTable()` в
`TitlesTextApply.cpp` сам это признаёт), а 25 Title-пресетов ходят по кругу из десяти
выдуманных движений.

XML — данные MAGIX, копировать их в GPL-дерево нельзя; читать их можно на лету из
установленного VEGAS — так же, как проект уже хостит плагины VEGAS. Точная семантика
`function`/`cascade` — в IL `CoreGraphics.dll`, для неё нужен .NET-декомпилятор.

### 4.2 Параметры

Манифест: 20 параметров — Text, Scale, Location, TextColor, collection (анимация),
Alignment, FitBackgroundColor, Background, Tracking, LineSpacing, OutlineWidth,
OutlineColor, ShadowEnable, ShadowColor, ShadowOffsetX/Y, ShadowBlur (и три группы).
`TitlesTextParams` в OpenVegas покрывает все. **Alignment — это точка привязки**
(0 Top Left … 4 Center … 7 Bottom Center … 9 Bottom Right), а не выравнивание абзаца.

### 4.3 Импорт из `.veg` — сверка 49 экземпляров с `PresetPackage.xml`

`project_titles-and-text.veg` собран из штатных пресетов, так что значения обязаны
совпасть с пакетом.

| Поле | Расхождений | Причина |
|---|---:|---|
| TextColor, Scale, Location, Tracking, LineSpacing, Outline | 0 | — |
| **Alignment (точка привязки)** | 0 | параметр `Alignment` читается отдельно от RTF-абзаца и переносится в `anchor` |
| **ShadowOffsetX/Y, ShadowBlur** | 0 | окно поиска ограничено следующим идентификатором, поэтому длинные записи больше не обрезаются на 2000 байтах |
| **ShadowColor** | 0 | читается в полном окне экземпляра |
| Background RGB при альфе 0 | 0 значимых | RGB сохраняется; альфа 0 делает цвет невидимым |

### 4.4 Пресеты в панели

До исправления `presetTable()` хранила на пресет только AnimationName, цвет текста,
масштаб и фон. Из-за этого терялись значения, перечисленные ниже; теперь они берутся из
полной записи `TitlesTextParams`:

- **Location**: у большинства именованных пресетов не центр (Bounce 0,39/0,64, Slide
  0,15/0,80, Scroll 0,19/0,20, Fly In 0,25/0,23 …);
- **Outline**: Action Flip 4 px белый, Bounce 6 px `#91ed91`, Scroll Left 8 px, Fly in
  from Right и Menace 3 px;
- **Alignment**: Title02/16/17 — Bottom Center (7), остальные Title — Center (4);
- **LineSpacing**: у Title-пресетов 0,725…0,85, а не 1; **Tracking**: Title07 −0,06,
  Title25 0,5;
- **Subtitles**: тень включена, текст внизу (y 0,10), межстрочный 0,8;
- **Rough Day**: масштаб 0,75 (у нас 1,0); цвет текста у VEGAS полностью прозрачный — у
  нас подменён чёрным;
- **Placeholder**: красный текст;
- лишний пресет «Sample Text», которого нет в пакете (у нас 53, у VEGAS 52).

Все эти значения лежат в `TitlesAndText/Contents/Presets/PresetPackage.xml`.

Каталог панели теперь хранит полный TitlesTextParams из PresetPackage.xml для каждого
пресета. Миниатюра и размещение используют одну и ту же запись: шрифт, размер, italic/bold,
абзацное выравнивание, положение, Alignment-якорь, tracking, line spacing, outline и shadow
больше не заменяются синтетической надписью по центру. Для рендера миниатюры сначала
используется увеличенный буфер и затем плавное уменьшение, поэтому малые размеры шрифта и
тонкие эффекты не исчезают в сетке 100x62.

В drag-and-drop имя образца и имя пресета передаются в line-oriented MIME-пакете в Base64.
Это сохраняет переносы строк Title01 и других многострочных пресетов; старый незакодированный
формат по-прежнему читается. Оставшееся ограничение относится к визуальной точности самого
движка анимаций: renderTitlesText воспроизводит статическую геометрию и текущий кадр
известных motion-спек, но не декодирует графы MetaTextCollectionItem из .NET-бандла VEGAS.

## 5. Перенос генераторов (2026-10-25)

Сравнение со скриншотами VEGAS (`SAMPLES/screenshots/Media Generator`) показало: у
Color Gradient и Credit Roll все миниатюры одинаковы, у Noise Texture и Test Pattern —
один узор на всех, окна «Video Media Generators» нет вовсе.

### 5.1 Эталон — сами плагины

Тест `[generator-ref]` (`tests/test_generator_reference.cpp`) запускает четыре OFX-генератора
`Vfx1.ofx` через наш хост: выгружает их параметры (тип, подпись, группа, значение по
умолчанию, диапазон, варианты — в порядке объявления) и рисует каждый штатный пресет. Из
выгрузки `tools/gen_generator_presets.py --schema` пишет `src/video/GeneratorParamSchema.cpp`.

По дороге нашлись четыре ошибки хоста OFX — все общие, не только для генераторов:

| Что | Как выдало себя | Исправление |
|---|---|---|
| Кадры перевёрнуты по вертикали: QImage идёт сверху вниз, OFX — снизу вверх | SMPTE-полосы внизу, Letterhead отсчитан от нижнего угла; у VEGAS наоборот | переворот входов и выхода в `processFrame` |
| Буферы объявлены непремультиплицированными, плагины VEGAS пишут премультиплицированные | мягкие края жалюзи темнели (12,7 из 255) | `kOfxImagePreMultiplied`, буфер `RGBA8888_Premultiplied` |
| Генераторы падают в многопоточном рендере | Noise Texture: `GlobalFree` в `A_Buffer::operator=` на чужом блоке; Test Pattern: сбой внутри OpenMP | эффект без входного клипа получает кадр одним вызовом `multiThread` |
| Не вызывались `Begin/EndSequenceRender` | — | вызываются вокруг каждого кадра |

Переворот был и в замерах переходов: вывод «имена страничных пресетов говорят, куда уходит
страница» сделан по перевёрнутому выводу плагина. С исправленным хостом Page Peel открывает
названный угол, и угол снова читается в экранных координатах (см. CHECKLIST).

### 5.2 Свои рендереры (`src/video/VegasGenerators.cpp`)

Среднее расхождение с плагином на 1280×720, из 255:

| Генератор | Пресетов | Расхождение |
|---|---:|---|
| Test Pattern | 10 | 0,00–0,10 |
| Solid Color | 10 | 0,00 (у плагина в 8 битах перепутаны R и B — он не хостится) |
| Checkerboard | 13 | 0,00–1,00 |
| Noise Texture | 20 | в среднем 31 — тот же материал, шум свой |

Checkerboard: клетка Width × ширина и Height × высота кадра (Square — высота в пикселях равна
ширине), сетка от левого верхнего угла со сдвигом Grid offset в долях кадра, первая клетка —
Color 1, смешивание краёв линейное по центру границы на долю клетки, две оси — «мягкий XOR»,
пиксель на границе берёт долю каждой стороны по площади, цвета смешиваются премультиплицированно.
Test Pattern: уровни студийные (16 чёрный, 235 белый, 7 ниже чёрного), раскладки — в комментарии
`testPattern()`.

Когда VEGAS установлен, OFX-генераторы рисует сам плагин (`plugins/OfxGeneratorSource`, хук
`video/GeneratorPluginHook`), кроме Solid Color.

### 5.3 Legacy-генераторы: параметры прочитаны (`src/io/VegLegacyGenerators.cpp`)

Блок параметров идёт сразу за строкой имени пресета в записи пула.

**Color Gradient** (блок 0x150): +0 число точек + 1; +4 тип (0 Linear, 1 Elliptical,
2 Rectangular); +8 угол в радианах (double); цвет точки i — float BGRA по +0x10 + 16·i; X —
double по +0xB0 + 8·i, Y — по +0x100 + 8·i; индексы с 1, до девяти точек. Рендер: точки
окрашивают кадр по расстоянию от центра — вдоль угла у Linear, по эллипсу или прямоугольнику
с пропорциями от угла (45° — пропорции кадра) у остальных. Все 18 пресетов совпадают с
миниатюрами VEGAS.

**Credit Roll** (фиксированная часть 712 байт, её размер лежит в +4): +8 эффект (0 прокрутка,
1 последовательность); +0x18/+0x20/+0x28 Left/Right/Top (double); +0x30/+0x34/+0x38
In/Out/Display; +0x3C заполнитель между колонками (два WCHAR: «.», «. », «-»); +0x54 фон BGRA;
+0x64 направление прокрутки; +0x70 пять стилей по 120 байт (шрифт WCHAR[32], размер, жирность,
цвет BGRA, выравнивание, два числа отступов и одно 1.0 — их смысл не подтверждён); затем текст:
длина в байтах, число строк, UTF-16. Ведущий пробел — заголовок, табуляция — две колонки (так
и описано в строках 6048–6057 `sftrans1.dll`).

### 5.4 Окно «Video Media Generators» (`src/ui/MediaGeneratorDialog.cpp`)

Шапка, размер кадра, длительность (таймкод), пресеты — штатные и свои, сохранение и удаление.
Страница OFX-генератора строится по схеме из плагина так, как VEGAS строит страницу любого OFX-
эффекта: группы со стрелкой, ползунок и значение, флажок, список, цветовая строка с пикером
(модель, плоскость «(GxB) R», пипетка, плоскость, ползунок канала, альфа). Страницы Color
Gradient и Credit Roll повторяют раскладку диалогов `sftrans1.dll` 191 и 228–236 (координаты
контролов и строки — из ресурсов DLL, сами ресурсы в дерево не скопированы).

### 5.5 Проверка `sftrans1.dll` в Ghidra (2026-10-31)

В Ghidra загружен именно `sftrans1.dll`; это PE32+ с секциями `.text`, `.rdata`, `.data`,
`.pdata`, `.rsrc` и `.reloc`. Экспорт `DllEnumObjects2W` перечисляет таблицу объектов DLL:
по индексу возвращаются две 16-байтовые GUID-структуры (CLSID и категория/тип объекта),
а также отображаемое имя. Среди экспортированных COM-типов подтверждены `IDXSCredits`,
`CCred`/`CCreditProps` и `CSFVideoPlugin<CCreditProps>`, то есть Credit Roll действительно
является DirectX/COM-генератором, а не OFX-страницей.

В строковом пуле DLL найдены пять именованных стилей Credit Roll: `DUALCENTERSTYLE`,
`DUALRIGHTSTYLE`, `DUALLEFTSTYLE`, `SINGLESTYLE` и `HEADERSTYLE`; отдельно присутствует
`DUALCENTERSPACE`. Также есть классы/ресурсы `SFCTEC_STYLES`, `Sftrans1_20_StyledTxt_Class`,
`CCredPP_StylesDual`, `CCredPP_StylesHeader` и `CCredPP_StylesSingle`. Эти строки подтверждают
разбиение блока пяти стилей и режимы одной/двух колонок, уже отражённые в декодере
`VegLegacyGenerators`, но сами по себе не раскрывают смысл двух чисел отступов.

Для текстового движка экспортированы методы `CSfTxtRender::IsWrappingEnabled`,
`SetWrappingEnabled`, `GetWrappingWidth`, `SetWrappingWidth` и `GetWordBreakCount`;
декомпиляция показывает флаг wrapping по смещению `this+0x1a0` и ширину по `this+0x1a8`.
Это подтверждает наличие отдельного режима переноса слов и числовой ширины, однако
точная формула высоты строки/скорости Credit Roll и длительности In/Out по доступным
символам и экспортам пока не доказана. Поэтому пункт точной математики остаётся открытым.

## 6. Что из этого следует

1. ~~Импорт генераторов~~ — сделано (§2); ~~двоичные параметры legacy Color Gradient и
   Credit Roll~~ — разобраны (§5.3).
2. Пресеты четырёх OFX-генераторов и Titles & Text — генерировать из `PresetPackage.xml`,
   как пресеты переходов, а не держать вручную.
3. ~~Рендер OFX-генераторов — настоящим `Vfx1.ofx` через `OfxHost`~~ — сделано (§5.2), свои
   рендереры для машин без VEGAS. Осталось: ключевые кадры параметров генераторов, точная
   математика Credit Roll (нужен `sftrans1.dll` в Ghidra).
4. Titles & Text: читать параметр Alignment; ограничивать поиск полей концом экземпляра,
   а не фиксированными 2000 байтами.
5. Анимации Titles & Text — интерпретатор `MetaTextCollectionItem` по XML из
   установленного VEGAS; для точной семантики — .NET-декомпилятор.
