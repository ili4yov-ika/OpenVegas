# OpenVegas: перенос UI с HTML-макетов на C++/Qt 6.8 и открытие `.veg` (Vegas Pro 22)

Документ суммирует эталоны из `SAMPLES/` для реализации нативного редактора на **C++ / Qt 6.8** (GPL) и для **чтения проектов VEGAS Pro 22**.

Источники:

| Путь | Содержание |
|------|------------|
| ~~`SAMPLES/pages/`, `css/`, `js/`~~ | HTML UI-макеты **удалены** из репозитория; эталон — Qt UI + `SAMPLES/veg_project/` |
| `SAMPLES/docs_veg/` | Реверс `.veg` |
| `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/` | Runtime Vegas 22.0.250 + ScriptPortal API |
| `SAMPLES/screenshots/`, `example_project_*.veg` | Визуальные и бинарные эталоны |

---

## Статус на 2026-08-03

| Тема | Статус |
|------|--------|
| Qt 6.8 / MinGW / MSVC kit | Работает; предпочтительно `build/Windows_MinGW-x64` |
| Open `.veg` + Project Media + timeline | **Done** (v1 timings + Event FX recovery) |
| Video Preview compositor | **Done** MVP (`src/video/*`) |
| Audio / Builtin / VST1–3 / OFX | **Done** lean; VST3 `IPlugView` |
| Event FX UI | Video + Audio **немодальные**; FX на клипе ≠ Chooser |
| Render As / interchange | **Done** MVP |
| Write `.veg` | Нет |

Детали стека: [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md), [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md), [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md).

---

## 1. Цель переноса

Не «переписать HTML один-в-один», а:

1. Воспроизвести **каркас workspace** и поведение таймлайна как в макетах / Vegas.
2. Вынести **доменную модель** (Project / Track / Event / Media) независимо от UI.
3. Сделать **импортёр `.veg` v0** (метаданные + медиа-пул + оценка состава), затем наращивать полный timeline.

Рекомендуемый стек Qt 6.8:

| Слой | Qt / свои классы |
|------|------------------|
| Shell / chrome | `QMainWindow`, `QMenuBar`, `QToolBar`, dock (`QDockWidget`) |
| Сплиттеры | `QSplitter` (upper ↔ timeline; media ↔ preview) |
| Таймлайн | свой `QWidget`/`QGraphicsView` + scene или custom paint |
| Диалоги | `QDialog` + вкладки `QTabWidget` (тёмный Fusion / custom stylesheet) |
| Медиа preview | `QMediaPlayer` + `QVideoWidget` / FFmpeg/libav отдельно |
| Waveform | кастомный paint / OpenGL |
| Настройки | `QSettings` |
| I/O проектов | свой `VegReader` / позже `VegWriter` |

---

## 2. Карта UI: HTML → Qt

### 2.1. Workspace (каркас)

Макет (`layout.css` + `upper-resize.js`):

```
┌─ title / menu / toolbar ─────────────────────────┐
│ upper: tabs | Preview | Media bin                  │  ~55%
├─ horizontal splitter ────────────────────────────┤
│ timeline: ruler + track headers | tracks area      │  ~45%
│ tools + scroll chrome                              │
└────────────────────────────────────────────────────┘
```

| Макет | Qt |
|-------|-----|
| `.workspace` + `.upper` / `.timeline-panel` | `QMainWindow` central = vertical `QSplitter` |
| Preview + Media columns | horizontal `QSplitter` в upper |
| `.track-headers` \| `.tracks-area` | горизонтальный splitter / фиксированная колонка `--track-header-w` |
| `.timeline-tools`, `.tl-scroll` | нижняя полоса + кастомные scroll thumbs |
| Welcome | отдельное окно / стартовый `QWidget` |

Токены из `css/tokens.css` → `QPalette` + QSS или тема `OpenVegasTheme`:

- фон: `#1a1a1a` / `#1e1e1e` / `#2b2b2b`
- video rail/event: `#6b4a8a` / `#5c4a6e`
- audio: `#9a4a72` / `#c9a0ad`
- selection: `#f0d040` / `#c9a227`
- CF: синий «X»

### 2.2. Модули JS → модули C++

| JS (макет) | Назначение | Qt-модуль (предложение) |
|------------|------------|-------------------------|
| `chrome.js` | chrome окна, timecode | `MainWindow`, `StatusTimecode` |
| `menus.js` | меню + context (video/audio event) | `MenuBuilder`, `EventContextMenu` |
| `dock-tabs.js` | вкладки панелей | `QTabBar` / `QDockWidget` |
| `timeline-chrome.js` | ticks, CF-зоны, fade curves, upgrade event chrome | `TimelineRuler`, `CrossfadeLayer`, `FadeCurve` |
| `timeline-scroll.js` | `--px-per-sec`, H/V zoom | `TimelineView::setPixelsPerSecond`, zoom |
| `timeline-select.js` | select, Ctrl/Shift, CF badges | `SelectionModel` |
| `timeline-move.js` | move, trim edges, track reorder | `TimelineInteractor` |
| `timeline-group.js` | A/V groups | `EventGroupStore` |
| `timeline-event-actions.js` | cut/copy/paste, split, hotkeys | `EditCommands` (`QUndoStack`) |
| `timeline-tracks.js` | insert/delete/rename/mute/solo | `TrackListModel` |
| `timeline-resize.js` | ширина колонки + высота трека | splitter + per-track height |
| `event-properties.js` | Video/Audio Properties | `EventPropertiesDialog` |
| `trimmer.js` | Trimmer window | `TrimmerWindow` |
| `project-properties.js` | Project Properties | `ProjectPropertiesDialog` |
| `render-as.js` | Render As | `RenderAsDialog` |
| `preferences.js` | Preferences | `PreferencesDialog` |
| `pan-crop.js` / `audio-fx.js` | FX UI stubs | позже / OFX host |

### 2.3. Поведение, которое обязательно сохранить

**Таймлайн**

- Масштаб времени: `pixelsPerSecond` (в макете `--px-per-sec`, база 40).
- Events: `left`/`width` в px ↔ timecode через pps.
- Video/audio только на дорожки своего типа; порядок треков любой (audio над video — ок).
- Высота трека: drag нижнего края header (36…420 px); колонка контролов — вертикальный splitter.
- Trim ширины: края event (L/R).
- Multi-select: Ctrl toggle, Shift range; move/delete/copy по всему выделению.
- A/V group: автопары; выделение/сдвиг по времени вместе; lane change — только kind перетаскиваемого.
- Fades: кривые на краю клипа; CF — дуги «X» при overlap; hard cut без X.
- Авто-CF при overlap после move.
- Markers / loop / playhead на ruler.

**Горячие клавиши (как в макете)**

| Клавиши | Действие |
|---------|----------|
| Ctrl+X/C/V | Cut / Copy / Paste |
| Delete | Delete |
| Ctrl+A | Select All |
| S | Split at playhead |
| Alt+[ / ] | Trim start/end to playhead |
| G / U | Group / Ungroup |
| Ctrl+Shift+U | Ignore Event Grouping |
| Alt+Enter | Project Properties |
| Ctrl+Shift+M | Render As |

**Диалоги (тёмный Vegas chrome)**

- Project Properties (6 вкладок)
- Event Properties: Video Event | Audio Event + Media + General  
  (для audio в A/V-группе открывать **audio** Properties по кликнутому event)
- Trimmer, Preferences, Render As

### 2.4. Доменная модель (ориентир ScriptPortal)

Из `ScriptPortal.Vegas.xml` / поведения макета — минимальный C++ слой:

```
Project
  mediaPool: Media[]
  tracks: Track[]          // VideoTrack | AudioTrack
  markers, regions
  properties: fps, sampleRate, size, tempo…

Track
  name, color, mute, solo, height, index
  events: TrackEvent[]

TrackEvent (VideoEvent | AudioEvent)
  start, length          // Timecode / samples / seconds
  media / take
  fadeIn, fadeOut, fadeCurve
  groupId
  switches (mute, lock, loop, …)
  playbackRate / stretch (Classic / élastique)
  envelopes, effects[]

Media
  path, streams, length, size…
```

Время лучше хранить в **наносекундах / тиках** (как Timecode Vegas) или `double` seconds + fps для UI; не в пикселях.

Маппинг макет → модель:

| UI (px) | Модель |
|---------|--------|
| `style.left / pps` | `event.start` |
| `style.width / pps` | `event.length` |
| `data-fade-in` (px) | `fadeIn` duration |
| `data-group-id` | `TrackEventGroup` |
| `data-rate` | playback rate |

### 2.5. Рекомендуемые фазы Qt

| Фаза | Scope |
|------|--------|
| **P0** | MainWindow shell, dark theme, empty timeline + tracks, ruler, pps zoom, playhead |
| **P1** | Events paint, select/move/trim, track height/reorder, mute/solo |
| **P2** | Groups, fades/CF, clipboard, hotkeys, undo |
| **P3** | Dialogs: Project/Event Properties, Preferences |
| **P4** | Trimmer, Preview, Media bin |
| **P5** | `.veg` reader v0 → заполнение модели → UI |
| **P6** | FX/PanCrop stubs, Render As UI |

Не переносить: Electron Capture, AI models, полный OFX host — отдельно.

### 2.6. Что брать из Program Files как справочник

| Ресурс | Зачем в Qt |
|--------|------------|
| `ScriptPortal.Vegas.xml` | Имена типов/свойств API |
| `Script Menu\*.cs` | Эталон операций (group, EDL, batch) |
| `Standard Layouts\*.VegasWindowLayout` | Раскладки dock |
| `Icons\`, Protein Bitmaps | Иконки |
| `language\`, локали | Не UI-строки timeline (они в бинарниках) |

---

## 3. Открытие файлов проектов Vegas Pro 22 (`.veg`)

### 3.1. Что такое `.veg`

- Проприетарный файл **edit decisions** VEGAS Pro (не контейнер медиа).
- Медиа **не встроены** — только **абсолютные пути** + параметры.
- Контейнер: **кастомный RIFF-подобный** поток, magic **`riff`** (lowercase).
- Сэмплы OpenVegas: **VEGAS Pro 22**, Build **22.0.250**, типично **12–20 KB**.
- Публичной полной спецификации **нет**; ниже — рабочая модель из `docs_veg/`.

### 3.2. Структура (обзор)

```
[0x00] Outer header 64 B
[0x40] Project properties (version, sr, fps, tempo, …)
[~0xF8] UTF-16LE paths (свой .veg, Documents, AppData\VEGAS Pro\22.0\)
[…]    ProjectNotes (.NET BinaryFormatter-like, ScriptPortal.Vegas.ProjectNotes.*)
[…]    Media pool — UTF-16 пути .mp4/.wav + 1920/1080
[…]    Timeline / events (плотный бинарь; лейблы клипов UTF-16)
[…]    Track FX default strings (Compressor / Noise Gate / EQ)
[…]    Trailer paths
```

### 3.3. Outer header (обязательная проверка)

| Offset | Тип | Значение / смысл |
|--------|-----|------------------|
| 0x00 | `char[4]` | `"riff"` |
| 0x04 | 12 bytes | константа `2E 91 CF 11 A5 D6 28 DB 04 C1 00 00` |
| 0x10 | `u64` LE | размер файла (= filesize) |
| 0x18 | GUID | `46C429EF-904A-11D2-8722-00C04F8EDB8A` |
| 0x28 | GUID | `B28F2D5A-230F-11D2-86AF-00C04F8EDB8A` |
| 0x38 | `u64` LE | размер «раннего» блоба (~3800–3850) |

Суффикс GUID `00C04F8EDB8A` — наследие Sonic Foundry CLSID.

### 3.4. Project properties (с ~0x40)

Наблюдения по всем сэмплам OpenVegas:

| Offset | Тип | Пример | Смысл |
|--------|-----|--------|--------|
| 0x46 | `u16` | `22` | версия Vegas Pro |
| 0x4C | `u32` | `48000` | sample rate |
| 0x50 | `f64` | ≈59.94006 | frame rate |
| 0x58 | `f64` | `120.0` | tempo BPM |

В файле также встречаются `u32` **1920** / **1080**.  
В ASCII: `Version=22.0.0.250`, типы `ScriptPortal.Vegas.ProjectNotes.*`.

### 3.5. Строки

- Пути и имена клипов: **UTF-16LE**.
- Имена .NET-типов: **ASCII**.
- Типичные медиа в сэмплах: `sample_for_project_video.mp4`, `sample_for_project_audio.wav`.

### 3.6. Что растёт с проектом

| Сложность | Размер (ориентир) |
|-----------|-------------------|
| only audio | ~12.2 KB |
| only video | ~13.6 KB |
| A/V ± crossfade | ~14.3 KB |
| 2 videos / rate | ~17.8 KB |
| many trimmers | ~19.4 KB |

Crossfade vs base ≈ +32 B (параметры, не новое медиа).  
Playback rate почти не меняет размер файла.

### 3.7. Парсер OpenVegas — уровни

**v0 (достаточно для «Open project» stub)**

1. Проверить magic `riff` + GUID/константу header.
2. Прочитать version, sample rate, fps, tempo.
3. Извлечь все UTF-16 пути к `.mp4`/`.wav`/`.veg` (скан или по известным зонам).
4. Подсчитать/собрать лейблы event (`sample_for_project_*` и др.).
5. Показать в UI: свойства проекта + список медиа; предложить **relink**, если файлов нет.

**v1**

- Разбор media pool → `Media` objects.
- Сопоставление с `Track`/`Event` по контролируемым diff сэмплам (`docs_veg/files/`, COMPARE compressed vs stretched).
- Игнор/вырезка ProjectNotes при импорте.

**v2**

- Точные start/length/fade/track index/group/rate из бинарных структур (нужны дополнительные сэмплы с одним изменённым полем).

### 3.8. Практические правила импорта

1. Файл **не portable** без медиа → UI Relink / Search folder (как Vegas).
2. Служебные `*.veg.bak`, `*.sfk`, `*.veg.sfap0` **не** нужны для открытия проекта.
3. Строки `VEGAS Track Compressor/EQ/Noise Gate` часто **дефолт**, не значит «пользователь повесил FX».
4. Нельзя считать UI-число клипов = числу строк лейблов 1:1 (корреляция есть, особенно trimmers).
5. Эталонные файлы: `SAMPLES/example_project_*.veg` + карточки `docs_veg/files/`.
6. Скрипты повторного анализа: `docs_veg/_analyze_veg.py`, `_analyze_deep.py`, `_analyze_props.py`.

### 3.9. Минимальный C++ API (эскиз)

```cpp
struct VegHeaderInfo {
  quint64 fileSize = 0;
  quint16 vegasVersion = 0;   // 22
  quint32 sampleRate = 0;     // 48000
  double frameRate = 0;       // 59.94…
  double tempoBpm = 0;        // 120
};

struct VegOpenResult {
  VegHeaderInfo header;
  QStringList mediaPaths;     // UTF-16 извлечённые
  QStringList eventLabels;
  QString projectPathHint;
  QStringList warnings;       // missing media, unknown version…
};

class VegReader {
public:
  static bool looksLikeVeg(const QByteArray& data);
  static VegOpenResult open(const QString& path, QString* error = nullptr);
};
```

Дальше: `VegOpenResult` → заполнение `Project` → `TimelineView::setProject()`.

### 3.10. Связь макет ↔ `.veg` (QA)

| Макет | `.veg` |
|-------|--------|
| `project-video-audio.html` | `example_project_with_video_and_audio.veg` |
| `project-only-video.html` | `…_only_video.veg` |
| `project-only-audio.html` | `…_only_audio.veg` |
| `project-crossfade.html` | `…_with_crossfade.veg` |
| `project-with-fades-and-crossfades.html` | `…_fades_and_crossfades.veg` |
| `project-trimmers*.html` | `…_trimmers*.veg` |
| `project-with-2-videos*.html` | `example_project_with_2_videos*.veg` |

После импорта v1+: визуально сверять с Qt UI, `SAMPLES/veg_project/` и `screenshots/`.

---

## 4. Чеклист старта разработки на Qt 6.8

1. [x] CMake + Qt 6.8 (Widgets), тёмная палитра / QSS.
2. [x] `MainWindow` + splitter upper/timeline + tracks.
3. [x] `TimelineView`: pps, ruler, playhead, scroll/zoom.
4. [x] Модель `ProjectModel` / tracks / events / media pool.
5. [x] Select / move / trim / track height (MVP).
6. [x] `VegReader` v0→v1 + Open + relink + Event FX recovery.
7. [x] Диалоги Properties / Render As / Preferences / Event FX / Plugin Chooser.
8. [ ] Groups + CF + clipboard + `QUndoStack` polish.
9. [ ] Углубление парсера (media in/out, markers, полный OFX blob).

---

## 5. Ссылки внутри репозитория

- Правила: [`INIT.MD`](INIT.MD)
- Issues / планы: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md)
- VegReader: [`VEG_READER_V0.md`](VEG_READER_V0.md)
- A/V stack: [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md)
- Plugins: [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md)
- Макеты / samples: [`SAMPLES/README.md`](../SAMPLES/README.md)
- Формат `.veg`: [`SAMPLES/docs_veg/00_format_overview.md`](../SAMPLES/docs_veg/00_format_overview.md), [`01_header_and_props.md`](../SAMPLES/docs_veg/01_header_and_props.md)
- Peak `.sfk`: [`SAMPLES/docs_veg/02_sfk_peak_files.md`](../SAMPLES/docs_veg/02_sfk_peak_files.md)
- Runtime Vegas: [`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md`](../SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md)
- ScriptPortal: `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/ScriptPortal.Vegas.xml`
