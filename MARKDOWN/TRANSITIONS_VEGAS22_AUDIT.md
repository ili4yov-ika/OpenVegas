# Переходы VEGAS Pro 22 против OpenVegas: сверка по бинарникам

**Дата:** 2026-10-25
**Источники:** `vegas220.exe` в Ghidra (MCP), манифесты и `PresetPackage.xml` OFX-бандлов,
реестровые строки legacy-DLL, ресурсы `vegas220.exe`, `TransitionWPFLibrary.dll`.
**Связано:** [VEG_TRANSITIONS_REVERSE.md](VEG_TRANSITIONS_REVERSE.md) — разбор хранения
переходов в `.veg`; его §12 «Чего ещё нет» устарел (см. итог ниже).

## 1. Что VEGAS 22 считает переходами

Полный список — плагины, у которых ресурсный манифест бандла объявляет контекст
`OfxImageEffectContextTransition`, и пресеты с `context="Transition"` из `PresetPackage.xml`.

| Бандл | Переходов | Штатных пресетов |
|---|---:|---:|
| `Vfx1.ofx` | 25 | 215 (у Gradient Wipe пресетов нет) |
| `MagixCVFx.ofx` | 2 — GL Transition, Warp Flow | 62 (все у GL Transition) |
| **Итого** | **27** | **277** |

Legacy DirectX-плагины (`vfx1.dll`, `sftrans1.dll`, `SfPagePeel.dll`, `vidpcore.dll`)
регистрируют те же переходы под ProgID `DXTSf*` / `DX*`. OFX-версии объявляют, какой
legacy-GUID они заменяют (`OfxImageEffectPropVegasUpliftGUID` в геттере свойств
`FUN_140693a70`), поэтому в окне переходов legacy-копии не видны. Сверх OFX-списка в
legacy есть `DXTAdditiveDissolve` («VEGAS Additive Dissolve») и `DXTSfTeeth` («VEGAS
Teeth»); видны ли они в окне VEGAS 22 — **не проверено**.

**Gradient Wipe** объявлен в манифесте `Vfx1`, но кода в `Vfx1.ofx` нет (строки
`gradientwipe` / «Gradient Wipe» в бинарнике отсутствуют). Настоящий Gradient Wipe в
VEGAS 22 — только legacy `vidpcore.dll`.

## 2. Сверка по группам

«Своя геометрия» — рендерер в `src/video/TransitionApply.cpp`; «плагином» — через
`TransitionPluginHook`, когда VEGAS установлен (замер из CHECKLIST: 18 групп).
Пресеты OpenVegas генерируются из того же `PresetPackage.xml` и проверяются тестом.

| Переход | В каталоге | Пресеты | Своя геометрия | Плагином VEGAS |
|---|---|---|---|---|
| 3D Blinds, 3D Cascade, 3D Fly In/Out, 3D Shuffle | да | все | да | нет (legacy) |
| Venetian Blinds, Portals | да | все | да | нет (legacy) |
| Linear Wipe, Barn Door, Iris, Clock Wipe, Zoom | да | все | да | да |
| Push, Slide, Squeeze, Split, Flash | да | все | да | да |
| Star Wipe, Swap, Spiral, Dissolve, Cross Effect | да | все | да | да |
| Page Peel, Page Roll, Page Loop | да | все | да | да |
| **Gradient Wipe** | да | — (у VEGAS тоже нет) | **нет — кросс-фейд** | нет |
| **GL Transition** (`de.magix:glTransition`) | **нет** | **0 из 62** | нет | нет |
| **Warp Flow** (`de.magix:WarpFlowTransition`) | **нет** | — | нет | нет |

Итог: из 27 переходов VEGAS 22 **24 сделаны** (каталог, все штатные пресеты, рендер),
**Gradient Wipe** есть только именем и рисуется кросс-фейдом, **GL Transition и Warp
Flow отсутствуют** — вместе с 62 пресетами GL Transition. Оба MagixCVFx-перехода требуют
GPU-suite'ов, и в нашем хосте `DescribeInContext` у них возвращает ноль клипов и
параметров (CHECKLIST), так что подключить их плагином пока нельзя.

## 3. Как VEGAS рисует превью переходов

Окно переходов VEGAS 22 — WPF (`TransitionWPFLibrary.dll`, команда
`View.TransitionWPFPaneCommand`). Своих картинок для превью в нём нет, только иконки.
Есть `ThumbnailGenerator` / `PresetThumbnail` / `UpdateThumbnail` /
`CancelPendingThumbRequest` — превью запрашивается у нативной части по каждому пресету —
и `AnimatePlugin` / `CanAnimate` / `animObjectMouseHover`: при наведении переход
проигрывается.

Нативная часть в `vegas220.exe`:

| Функция | Что делает |
|---|---|
| `FUN_14053ff90` | один раз грузит исходные кадры из ресурсов exe в слоты объекта |
| `FUN_140542210` | собирает превью; вызывается через COM-vtable (`0x140d94028`) из WPF |
| `FUN_140541610` | освобождает слоты и сбрасывает кэш `fx_thumbnail_cache.bin` |

Размер превью — **400×240 / 200×120 / 100×60** в зависимости от DPI (5:3; у нас 130×78,
та же пропорция). Режим (поле `+0x730`) выбирает исходники:

| Слот | Роль | Ресурсы 100 / 200 / 400 px | Содержимое |
|---|---|---|---|
| `+0x6f8` | кадр A (режимы 0, 3) | `0x487` / `0x4ee` / `0x552` (BMP) | плоский `#467d96`, белая «A» |
| `+0x700` | кадр B (режимы 0, 3) | `0x4a6` / `0x4ef` / `0x553` (BMP) | плоский `#8cb7ca`, белая «B» |
| `+0x708` | исходник Video FX (режим 1) | `0x481` / `0x4f0` / `0x554` (BMP) | фотография глаза |
| `+0x710` | наложение с альфой | `0x497` / `0x4f1` / `0x555` (PNG) | ромб и сердце на прозрачном |

Слот `+0x710` берётся, когда плагин объявляет
`OfxProbPluginVegasPresetThumbnail = …ImageWithAlpha` (поле `+0x14` записи плагина;
есть ещё `…Default` и `…SolidImage`). Буква — белая `#fbfbfb`, **обычного** начертания.

## 4. Превью в OpenVegas: перенесено

`renderTransitionPreview()` в `TransitionApply.cpp` устроен так же — переход прогоняется
через два кадра A/B, — и наведение так же проигрывает его (`TransitionsPane`, ~15 к/с).

**Кадры A/B перенесены (2026-10-25)** — своей отрисовкой, битмапы VEGAS не копируются
(в GPL-дерево их класть нельзя, это ресурсы MAGIX). `testCard()` заливает кадр плоским
цветом и рисует белую `#fbfbfb` букву обычного начертания контуром, масштабируя сам
контур до 58,3 % высоты кадра: так пропорция не зависит от шрифта платформы. По вертикали
буква центрируется по контуру, по горизонтали — по ширине символа, как обычная вёрстка
текста: «B» у VEGAS стоит на 0,509 ширины, правее центра своего контура на левый отступ
глифа. Центровка по контуру сдвигала её на 3,6 пикселя.

Сверка с битмапами VEGAS при 400×240 (шрифт платформы — Segoe UI): среднее расхождение
**0,32 из 255** у A и **0,12** у B, пикселей с отличием больше 64 — 0,07 % и 0. Тест
`Preview frames A and B are VEGAS's flat cards…` держит цвета, высоту и положение буквы
и проверяет, что это действительно буква: на offscreen-платформе Windows шрифтов нет,
каждый глиф выходит квадратом-заглушкой той же высоты, и проверка геометрии прошла бы
впустую — там тест проверяет только цвета.

Что ещё расходится:

| | VEGAS 22 | OpenVegas |
|---|---|---|
| Подложка | не установлена | шахматка под всем превью |
| Кадр-постер | не установлен (решает WPF) | `kPosterProgress = 0.45` |

## 5. Что осталось открытым

1. Прогресс, на котором VEGAS рисует неподвижное превью: он передаётся из .NET-кода
   окна, а для его чтения нужен декомпилятор .NET (ilspy), которого здесь нет.
2. Видны ли `Additive Dissolve` и `Teeth` в окне переходов VEGAS 22.
3. Рендер Gradient Wipe: нужны карты градиентов legacy `vidpcore.dll`.
4. GL Transition: 62 пресета — это шейдеры gl-transitions; без GPU-пути в композиторе их
   не нарисовать, а плагин в нашем хосте себя не описывает.
