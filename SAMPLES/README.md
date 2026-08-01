# SAMPLES — OpenVegas

Подпапка репозитория **OpenVegas**: эталонные материалы для разработки UI и разбора формата проектов **VEGAS Pro 22**.

Здесь собраны:

- HTML/CSS/JS-макеты интерфейса редактора (по скриншотам Vegas);
- каталог [`veg_project/`](veg_project/) — актуальные `.veg`, медиа и interchange-экспорты (EDL / XML / FCPXML / Premiere);
- документация и скрипты анализа `.veg` — [`veg_analyzators/`](veg_analyzators/);
- снимок Program Files Vegas Pro 22 (Steam, Build 250) — справочник по runtime/API.

Цель — визуально и структурно зафиксировать поведение Vegas (таймлайн, events, fades/crossfades, grouping, Trimmer, Properties, markers, velocity), чтобы OpenVegas мог повторить те же паттерны.

---

## Быстрый старт

1. Откройте в браузере [`index.html`](index.html) — каталог всех UI-макетов.
2. Актуальные проекты Vegas: [`veg_project/README.md`](veg_project/README.md).
3. Реверс формата `.veg`: [`veg_analyzators/README.md`](veg_analyzators/README.md).
4. Медиа для HTML-макетов: [`assets/`](assets/).
5. Анализ установки Vegas: [`VEGAS-PRO-22-PROGRAM-FILES/README.md`](VEGAS-PRO-22-PROGRAM-FILES/README.md).

Открыть проект в OpenVegas:

```text
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny.veg
```

Локальный сервер для HTML не обязателен (`file://`). Для удобства:

```bash
# из каталога SAMPLES
npx --yes serve .
# или
python -m http.server 8080
```

**Интерактивные** страницы (`pages/*.html`) подключают JS.  
**Статичные** снимки (`pages/*_static.html`) — тот же визуальный DOM после инициализации chrome, **без `<script>`** (удобно для сверки вёрстки / Qt). Пересборка:

```bash
# из каталога SAMPLES (нужен Playwright Chromium)
npm install
npx playwright install chromium
npm run generate:static
```

---

## Структура каталога

```
SAMPLES/
├── index.html                      # оглавление UI-макетов
├── README.md                       # этот файл
│
├── pages/                          # экраны редактора (+ *_static.html снимки)
├── css/                            # токены, layout, компоненты
├── js/                             # поведение таймлайна, меню, dock, диалоги
├── scripts/                        # генерация статичных страниц
│
├── assets/                         # медиа и превью для HTML-макетов
├── screenshots/                    # эталонные скриншоты Vegas Pro
│
├── veg_project/                    # актуальные .veg + медиа + Export-эталоны
├── veg_analyzators/                # реверс .veg (docs + скрипты; ранее docs_veg/)
├── VEGAS-PRO-22-PROGRAM-FILES/     # снимок установки Vegas Pro 22 (+ README)
│
├── sample_for_project_*.mp4/.wav   # копии медиа у корня (удобный доступ)
└── package.json / node_modules/    # Playwright для generate:static
```

| Путь | Назначение |
|------|------------|
| `pages/` | HTML-страницы: Welcome, пустой проект, таймлайны; рядом `*_static.html` |
| `scripts/` | `generate-static-pages.mjs` — DOM-снимки без JS |
| `css/` | `tokens.css`, `layout.css`, `components.css`, `ui-catalog.css` |
| `js/` | Интерактив: выделение, move/group, меню events, Trimmer, Properties, FX/Pan-Crop |
| `assets/` | `sample_for_project_video.mp4`, `sample_for_project_audio.wav`, `video-thumb.jpg` |
| `screenshots/` | Скриншоты Vegas для сверки вёрстки |
| **`veg_project/`** | **Актуальные** `.veg` (BBB + sample audio), 4K-ролик, экспорты EDL/XML/FCPXML/prproj |
| **`veg_analyzators/`** | Документация и скрипты анализа `.veg` (карточки старых `example_project_*`) |
| `VEGAS-PRO-22-PROGRAM-FILES/` | Runtime Vegas 22.0.250 (Steam) — layouts, ScriptPortal, OFX… (в `.gitignore`) |

---

## UI-макеты (`pages/`)

Интерактивные макеты интерфейса по скриншотам. Общие возможности:

- вкладки dock / панели Preview–Media;
- меню и контекстные меню (в т.ч. **video-event** / **audio-event** как в Vegas);
- выделение клипов; **группировка A/V** (автопары + Group / Ungroup);
- перенос video/audio events между дорожками того же типа (и сдвиг по времени; группа двигается вместе);
- перестановка дорожек вверх/вниз; контекстное меню дорожки;
- горизонтальный зум (Ctrl/Meta + колесо, grips на scrollbar);
- вертикальный сплиттер верхней зоны и таймлайна;
- fades, crossfades (дуги как в Vegas), trimmers, markers, velocity («гармошка»);
- **Open in Trimmer** — плавающее окно Trimmer (video / waveform);
- **Properties** — Video Event / Audio Event + Media + General (тёмный UI);
- диалоги Project Properties, Render As, Preferences.

У каждой интерактивной страницы есть статичный близнец `*_static.html` (DOM после fill chrome / upgrade events / открытых диалогов, без JS).

Отдельно: **UI Catalog** — все меню File–Help с подменю, все context menus, вкладки Welcome и Preview dropdowns (интерактив + static).

| Страница | Static | Описание |
|----------|--------|----------|
| [`ui-catalog.html`](pages/ui-catalog.html) | [`_static`](pages/ui-catalog_static.html) | **Каталог UI:** menu bar, context menus, Welcome ×3, Preview DD |
| [`welcome.html`](pages/welcome.html) | [`_static`](pages/welcome_static.html) | Стартовый экран: New / Open / Getting Started |
| [`empty-project.html`](pages/empty-project.html) | [`_static`](pages/empty-project_static.html) | Пустой каркас редактора без events |
| [`project-properties.html`](pages/project-properties.html) | [`_static`](pages/project-properties_static.html) | Диалог **Project Properties** (6 вкладок, тёмный UI) |
| [`render-as.html`](pages/render-as.html) | [`_static`](pages/render-as_static.html) | Диалог **Render As** (форматы / шаблоны, тёмный UI) |
| [`preferences.html`](pages/preferences.html) | [`_static`](pages/preferences_static.html) | Диалог **Preferences** (General + вкладки, тёмный UI) |
| [`project-video-audio.html`](pages/project-video-audio.html) | [`_static`](pages/project-video-audio_static.html) | Базовый A/V: видео- и аудио-events |
| [`project-only-video.html`](pages/project-only-video.html) | [`_static`](pages/project-only-video_static.html) | Только видео на таймлайне |
| [`project-only-audio.html`](pages/project-only-audio.html) | [`_static`](pages/project-only-audio_static.html) | Только аудио / waveform |
| [`project-with-2-videos.html`](pages/project-with-2-videos.html) | [`_static`](pages/project-with-2-videos_static.html) | Два видео на разных треках + аудио |
| [`project-with-2-videos-one-compressed.html`](pages/project-with-2-videos-one-compressed.html) | [`_static`](pages/project-with-2-videos-one-compressed_static.html) | Второе видео ускорено (velocity ~245,6%, «гармошка») |
| [`project-with-2-videos-one-stretched.html`](pages/project-with-2-videos-one-stretched.html) | [`_static`](pages/project-with-2-videos-one-stretched_static.html) | Второе видео замедлено (velocity ~36,3%) |
| [`project-crossfade.html`](pages/project-crossfade.html) | [`_static`](pages/project-crossfade_static.html) | Перекрытие клипов — синий «X», длительность CF при выделении |
| [`project-fades.html`](pages/project-fades.html) | [`_static`](pages/project-fades_static.html) | Fade In/Out на отдельных event (без overlap) |
| [`project-with-fades-and-crossfades.html`](pages/project-with-fades-and-crossfades.html) | [`_static`](pages/project-with-fades-and-crossfades_static.html) | Цепочка: fade-in + hard cut + несколько crossfade |
| [`project-trimmers.html`](pages/project-trimmers.html) | [`_static`](pages/project-trimmers_static.html) | Trim-handles на краях event |
| [`project-with-trimmers-markers.html`](pages/project-with-trimmers-markers.html) | [`_static`](pages/project-with-trimmers-markers_static.html) | Несколько trim-сегментов и маркеры на линейке |
| [`pan-crop.html`](pages/pan-crop.html) | [`_static`](pages/pan-crop_static.html) | Окно эффекта Pan/Crop |
| [`pan-crop-color-corrector.html`](pages/pan-crop-color-corrector.html) | [`_static`](pages/pan-crop-color-corrector_static.html) | Панель Pan/Crop + Color Corrector |

### UI Catalog (меню и вкладки)

Страница [`ui-catalog.html`](pages/ui-catalog.html) / [`ui-catalog_static.html`](pages/ui-catalog_static.html) раскрывает:

| Раздел | Содержание |
|--------|------------|
| **Menu bar** | File, Edit, View, Insert, Tools, Options, Help — каждый пункт и отдельная карточка для каждого подменю (Import, Export, …) |
| **Context menus** | `video-event`, `audio-event`, `*-track-header`, `*-track-empty`, `timeline-empty`, `timeline-ruler`, `timeline-marker`, `time-display`, `preview`, `*-event-more` + все nested submenu |
| **Welcome** | New Project, Open Project, Getting Started — все три панели рядом |
| **Preview DD** | Split Screen, Quality, Zoom, Overlays (включая flyout’ы) |

Данные берутся из `VegasMenus.MENU_DATA` / `CONTEXT_MENUS` и chrome Preview toolbar.

### Диалоги и горячие клавиши

| Диалог / действие | Как открыть |
|-------------------|-------------|
| **Project Properties** | File → Project Properties…, toolbar, **Alt+Enter** |
| **Render As** | File → Render As…, toolbar, **Ctrl+Shift+M** |
| **Preferences** | Options → Preferences… |
| **Event Properties** | ПКМ по клипу → Properties… (video → Video Event; audio → Audio Event; в A/V-группе — по кликнутому виду) |
| **Trimmer** | ПКМ → Open in Trimmer; двойной клик по media-card |
| Cut / Copy / Paste / Delete | **Ctrl+X / C / V**, **Delete** |
| Split | **S** |
| Trim Start / End | **Alt+[** / **Alt+]** |
| Group / Ungroup | **G** / **U** |
| Ignore Event Grouping | **Ctrl+Shift+U** |

### Fades и crossfades (соглашения UI)

- **Fade In/Out** — белая диагональ/кривая на краю одного клипа; длительность на fade-in/out в бейджах **не** показывается.
- **Crossfade** — синий «X» в зоне перекрытия двух клипов (дуги fade как в Vegas, не прямые линии); при выделении — бейдж длительности (`data-dur` или width / px-per-sec).
- Hard cut (стык без overlap) — **без** «X»; следующий клип может иметь свой fade-in.
- При перетаскивании events overlap на одной дорожке автоматически создаёт crossfade (кривые fade в зоне CF скрыты).
- Кнопки event tools (Pan/Crop, FX, More) сдвигаются влево от исходящего crossfade.

Пример раскладки на `project-with-fades-and-crossfades.html` (масштаб ~40 px/s):

| Клип | Диапазон | Особенности |
|------|----------|-------------|
| #1 | 0–15 с | fade-in ~3 с; CF → #2 (~2 с) |
| #2 | 13–26 с | hard cut с #3 — **без** CF |
| #3 | 26–35 с | **fade-in** ~5 с; короткий CF → #4 (~1 с) |
| #4 | 34–46 с | CF → #5 (~3 с) |
| #5 | 43–70 с | длинный хвост |

### Grouping (A/V)

- По умолчанию video+audio с близким стартом образуют группу (угловые метки `.is-grouped`).
- Выделение / сдвиг по времени — по всей группе; смена дорожки — только для вида перетаскиваемого клипа.
- Context: Group, Ungroup, Ignore Event Grouping, Clear Group, Select Events in Group.

### Event Properties

Общий диалог `.ep-*` (тёмный chrome как Project Properties):

- **Video:** switches, loop/resample, playback / undersample rate.
- **Audio:** Mute/Lock/Loop/Invert/Normalize, Gain, Time stretch (по умолчанию **Classic**).
- Вкладки **Media** и **General** для обоих видов.
- OK применяет switches/rate/stretch к клипу; Cancel закрывает без записи.

### Trimmer

Плавающее окно: превью видео (`sample_for_project_video.mp4`) или waveform аудио; транспорт, In/Out, меню More, добавление на таймлайн. API: `VegasTrimmer.openFromEvent(el)`.

---

## JavaScript

| Файл | Роль |
|------|------|
| `chrome.js` | Общий chrome окна редактора |
| `menus.js` | Меню / контекстные меню (в т.ч. video-event / audio-event); экспорт `buildMenuItems` |
| `ui-catalog.js` | Сборка эталонного каталога меню / Welcome / Preview DD |
| `dock-tabs.js` | Вкладки dock-панелей |
| `welcome.js` | Логика Welcome |
| `timeline-chrome.js` | Chrome таймлайна, ticks, CF-дуги, сдвиг tools от CF, авто-crossfade |
| `timeline-scroll.js` | Горизонтальный скролл и зум времени (`--px-per-sec`) |
| `timeline-select.js` | Выделение events, бейджи длительности CF |
| `timeline-move.js` | Перенос events; авто-CF; перестановка дорожек; учёт групп |
| `timeline-group.js` | Авто A/V-группы, Group/Ungroup, Ignore Event Grouping |
| `timeline-event-actions.js` | Действия контекстного меню клипа |
| `hotkeys.js` | Глобальные хоткеи (`e.code`, RU/EN): S, G/U, Ctrl+X/C/V/A, Alt+[/], … |
| `timeline-tracks.js` | Контекстное меню дорожки: insert/delete/rename/color/mute/solo |
| `timeline-resize.js` | Изменение высоты треков |
| `upper-resize.js` | Сплиттер upper ↔ timeline (дефолт ~55% / 45%) |
| `event-properties.js` | Диалог Event Properties (Video/Audio + Media + General) |
| `trimmer.js` | Окно Trimmer (video / audio) |
| `project-properties.js` | Диалог Project Properties |
| `render-as.js` | Диалог Render As |
| `preferences.js` | Диалог Preferences |
| `pan-crop.js` | UI Pan/Crop |
| `audio-fx.js` | UI Audio FX |

Ключевые глобалы: `VegasTimelineChrome`, `VegasTimelineSelect`, `VegasEventGroup`, `VegasEventProperties`, `VegasTrimmer`.

Порядок подключения на страницах проекта (важное): `timeline-group.js` до select/move; `event-properties.js` и `trimmer.js` до / рядом с `timeline-event-actions.js`.

---

## CSS

| Файл | Содержание |
|------|------------|
| `tokens.css` | CSS-переменные: цвета треков, selection, CF, размеры chrome |
| `layout.css` | Workspace, splitter, preview/media columns, index-page |
| `components.css` | Events, fades, CF-дуги, markers, tools, Trimmer, `.ep-*` Properties, диалоги |
| `ui-catalog.css` | Раскладка каталога меню (все flyout’ы раскрыты) |

Масштаб времени задаётся через `--px-per-sec` на корне таймлайна; зум пересчитывает позиции events, fades, CF и playhead.

---

## Эталонные проекты `.veg` (`veg_project/`)

Актуальные проекты лежат в **[`veg_project/`](veg_project/)** — подробности: [`veg_project/README.md`](veg_project/README.md).

| Файл | Смысл |
|------|--------|
| `project_big--buck-bunny.veg` | Базовый A/V, полный ролик BBB (~634 s, 60 fps) |
| `project_big--buck-bunny_fades.veg` | Несколько дорожек + **разные fade curves** |
| `project_big--buck-bunny_markers.veg` | Markers на линейке |
| `project_big--buck-bunny_trims-and-crossfade.veg` | 6 trim-сегментов + crossfade |
| `project_sample_for_project_audio.veg` | Только wav (~10.3 s) |
| `project_sample_for_project_audio_trims-and-crossfade.veg` | Audio trims / overlaps |

Рядом: медиа (`big-buck-bunny_video-60fps-4k.mp4` ≈726 MB, `sample_for_project_audio.wav`), `.sfk`, и **экспорты Vegas**:

| Подпапка | Формат |
|----------|--------|
| `edl-text-file/` | EDL Text (CSV `;`, времена в мс) |
| `final-cut-pro-7_davinci-resolve/` | FCP7 / Resolve `.xml` |
| `final-cut-pro-x/` | FCPX `.fcpxml` |
| `premiere_after-effect/` | Premiere `.prproj` |

Служебные файлы Vegas (не нужны для open, но встречаются):

- `*.veg.bak` — бэкап;
- `*.sfk` — waveform peaks ([`veg_analyzators/02_sfk_peak_files.md`](veg_analyzators/02_sfk_peak_files.md));
- `*.veg.sfap0` — audio proxy;
- `*.log` рядом с экспортами — лог Vegas Export.

---

## Документация формата `.veg` (`veg_analyzators/`)

Реверс-инжиниринг бинарных `.veg` (публичной спецификации нет). Ранее каталог назывался `docs_veg/`.

| Документ | Содержание |
|----------|------------|
| [`veg_analyzators/README.md`](veg_analyzators/README.md) | Оглавление |
| [`00_format_overview.md`](veg_analyzators/00_format_overview.md) | RIFF-подобный контейнер, header, props |
| [`01_header_and_props.md`](veg_analyzators/01_header_and_props.md) | Версия, fps, sample rate, notes |
| [`02_sfk_peak_files.md`](veg_analyzators/02_sfk_peak_files.md) | `.sfk` peaks (SFPK) |
| [`files/`](veg_analyzators/files/) | Карточки **старых** сэмплов `example_project_*` |
| `*_strings.md`, `_analyze_*.py` | Строки и скрипты повторного анализа |

Типичные параметры: **48000 Hz**; BBB-проекты — **60 fps** / 4K; старые example — часто **59.94 fps** / 1920×1080.

```bash
cd SAMPLES/veg_analyzators
python _analyze_veg.py
python _analyze_deep.py
python _analyze_props.py
```

Сравнение compressed vs stretched (legacy): [`veg_analyzators/files/COMPARE_compressed_vs_stretched.md`](veg_analyzators/files/COMPARE_compressed_vs_stretched.md).

---

## Снимок Vegas Pro 22 (`VEGAS-PRO-22-PROGRAM-FILES/`)

Каталог установки **VEGAS Pro 22 Steam Edition**, Build **22.0.250** (~5.4 GB). Подробный разбор: [`VEGAS-PRO-22-PROGRAM-FILES/README.md`](VEGAS-PRO-22-PROGRAM-FILES/README.md).

Для OpenVegas особенно полезны:

| Ресурс | Зачем |
|--------|--------|
| `ScriptPortal.Vegas.xml` / `.dll` | Доменная модель: Event, Track, Media, Envelope, FX… |
| `Script Menu\*.cs` | Эталон скриптовых операций (group A/V, EDL, render) |
| `Standard Layouts\*.VegasWindowLayout` | Пресеты раскладки окон |
| `Icons\`, `Protein\Bitmaps` | Иконки / скины |
| `OFX Video Plug-Ins\`, `FileIO Plug-Ins\` | Справочник эффектов и кодеков (не копировать целиком в репо UI) |

Не публиковать `install.cfg` с пользовательскими путями/ключами Hub как есть.

---

## Медиа (`assets/` и `veg_project/`)

| Файл | Где | Назначение |
|------|-----|------------|
| `sample_for_project_video.mp4` | `assets/`, корень `SAMPLES/` | Превью / Trimmer в HTML-макетах |
| `sample_for_project_audio.wav` | `assets/`, `veg_project/`, корень | Waveform / audio-only `.veg` |
| `video-thumb.jpg` | `assets/` | Статичный thumbnail на video-events |
| `big-buck-bunny_video-60fps-4k.mp4` | `veg_project/` (~726 MB) | Основное видео актуальных BBB-проектов |

Пути внутри `.veg` обычно абсолютные (как сохранил Vegas). В HTML-макетах медиа подключается относительно `assets/`.

---

## Связь HTML ↔ `.veg`

HTML-макеты — **визуальная** реплика UI. Источник правды по бинарному формату — `veg_analyzators/` + файлы в `veg_project/`.

| Макет (UI) | Близкий актуальный `.veg` |
|------------|---------------------------|
| `project-video-audio.html` | `veg_project/project_big--buck-bunny.veg` |
| `project-only-audio.html` | `veg_project/project_sample_for_project_audio.veg` |
| `project-fades.html` / fade curves | `veg_project/project_big--buck-bunny_fades.veg` |
| `project-with-fades-and-crossfades.html` | `veg_project/project_big--buck-bunny_trims-and-crossfade.veg` |
| `project-trimmers.html` / markers | `…_trims-and-crossfade.veg` / `…_markers.veg` |

Карточки **legacy** `example_project_*` (velocity compressed/stretched и т.д.) остаются в [`veg_analyzators/files/`](veg_analyzators/files/) — сами `.veg` перенесены/заменены набором в `veg_project/`.

Доменная модель API Vegas — `ScriptPortal.Vegas.xml` в Program Files.

---

## Для кого и зачем

- **UI OpenVegas** — сверка вёрстки и поведения с Vegas по `screenshots/` и `pages/` (интерактив + `*_static.html`).
- **Парсер / импорт `.veg`** — `veg_project/` + `veg_analyzators/`.
- **Import/Export interchange** — эталоны в `veg_project/edl-text-file/`, `final-cut-pro-*`, `premiere_after-effect/`.
- **Runtime / API Vegas** — `VEGAS-PRO-22-PROGRAM-FILES/` (layouts, ScriptPortal, плагины).
- **QA** — одинаковые сцены (fades, CF, grouping, Properties, Trimmer, velocity, markers) в макете и в настоящем `.veg`.
