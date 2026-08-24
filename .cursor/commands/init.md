# Правила разработки проекта OpenVegas

Документ для разработчиков и ИИ-агентов. Соблюдать при любых изменениях кода и документации.

Краткая пользовательская документация — в `docs/`. Технические разборы и планы — в `MARKDOWN/`.

**Актуальность стека (2026-08-03):** audio/video playback, Builtin DSP (+Delay/Reverb), VST1–3 (`IPlugView`), OFX MVP, Video compositor, VEG v1 + Event FX recovery, Render As / interchange MVP. Детали — `PLAN_VIDEOAUDIOSTACK.md`, `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`, `ISSUES_AND_PLANS.md`.

---

## Общие принципы

### Продукт
- **OpenVegas** — GPL open-source NLE, UI и поведение ориентированы на **VEGAS Pro 22**.
- Эталоны: Qt UI + `SAMPLES/veg_project/`, скриншоты, `example_project_*.veg`, реверс в `SAMPLES/docs_veg/`.
- Runtime Vegas (`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/`, `build/vegas-runtime/`) — **только справочник**, не GPL, **не коммитить** копии в git.

### Архитектура
- **Язык**: C++17+
- **UI**: Qt 6.8+ Widgets, `.ui` в `ui/`, тема через `resources/openvegas.qss`
- **Сборка**: CMake (предпочтительно `build/Windows_MinGW-x64`) и `OpenVegas.pro` (qmake) — **синхронизировать** оба
- **Домен**: `ProjectModel` (tracks / events / media pool / FX slots) отдельно от chrome UI
- **I/O**: `VegReader` для `.veg` (v1 timings + Event FX recovery); запись `.veg` / `.ovp` — позже
- **Audio / Video / Media**: `src/audio/*`, `src/video/*`, `src/media/*`
- **Plugins**: Builtin DSP, VST1–3 host, OFX host, Event FX dialogs

### Структура директорий
```
OpenVegas/
├── src/
│   ├── app/           # main, MainWindow
│   ├── ui/            # диалоги (Event FX, Chooser, Theme, MenuBuilder, …)
│   ├── timeline/      # TimelineView
│   ├── model/         # ProjectModel
│   ├── io/            # VegReader, filmstrip, interchange
│   ├── audio/         # AudioEngine, BuiltinDsp, Vst2/Vst3Host, graph/mixer
│   ├── video/         # VideoCompositor, decode/cache, color/FX apply
│   ├── media/         # MediaEngine, FFmpeg encode/render
│   └── plugins/       # types, registry, OFX, CompositePluginHost
├── ui/                # Qt Designer .ui
├── resources/         # openvegas.qss, resources.qrc
├── tests/             # Catch2: audio / video / media / plugin state
├── docs/              # краткая документация для пользователей
├── MARKDOWN/          # INIT, ISSUES, PLAN_*, VEG_READER, QT68
├── SAMPLES/           # .veg, docs_veg, veg_project, скриншоты (эталоны)
├── thirdparty/        # vst3sdk (и пр.)
├── CMakeLists.txt
├── OpenVegas.pro
├── LICENSE            # GPL-3.0-or-later
└── build/             # артефакты сборки (gitignore)
```

Заголовки лежат рядом с исходниками под `src/` (не отдельный корневой `include/`).

---

## Стандарты кодирования

### Именование
- **Классы / enum class**: PascalCase (`MainWindow`, `TimelineView`, `TrackKind`)
- **Методы / переменные**: camelCase (`openProjectPath`, `pixelsPerSecond`)
- **Члены**: `m_` + camelCase (`m_timeline`, `m_project`)
- **Константы / macros**: UPPER_SNAKE или Qt-стиль
- **Файлы**: PascalCase как класс (`MainWindow.cpp`, `VegReader.h`) — как в текущем дереве
- **Namespace**: `openvegas`

### Заголовки
- Предпочтительно `#pragma once`
- Группировка includes: Qt → STL → локальные (`"model/…"`, `"io/…"`)
- Forward declarations где достаточно
- `Q_OBJECT` только для классов с сигналами/слотами/метаданными

### Исходники
- `const` где уместно; параметры по `const T &` вместо лишних копий
- Ошибки I/O: `QString *error` / `warnings` / `QMessageBox`, не молчаливый fail
- Временно неиспользуемые параметры: `Q_UNUSED()` или `[[maybe_unused]]`
- **Не** оставлять заглушки без записи в `MARKDOWN/ISSUES_AND_PLANS.md`
- Не коммитить секреты, Vegas binaries, `vegas-runtime/`

### Сигналы / слоты Qt
- Functor-connect: `connect(sender, &Class::signal, receiver, &Class::slot)`
- Lambda-слоты: **не** использовать `Qt::UniqueConnection` (assert в Qt)
- После смены objectName в рантайме помнить про QSS-селекторы

---

## Qt / UI

### Макеты и fidelity
1. Сначала смотреть **эталонный `.veg`** (`SAMPLES/veg_project/`) и/или **скриншот** Vegas.
2. Потом править `.ui` / `MainWindow` / QSS / `IconFactory`.
3. Не выдумывать chrome, которого нет в эталоне, без записи в ISSUES.

### Компоненты
- **MainWindow** — shell: меню, toolbars, splitters, media/preview/master/timeline, transport
- **TimelineView** — custom paint: ruler, tracks, events, playhead, FX chrome
- **WelcomeDialog** — стартовый экран (New / Open / Getting Started)
- **Диалоги** — Project Properties, Render As, Preferences, Event Properties, Trimmer, Plugin Chooser
- **Event FX** — `VideoEventFxDialogExact`, `AudioEventFxDialog` (**немодальные**); клик FX на клипе → Event FX, не Chooser
- **IconFactory** — SVG icons
- **Theme** — загрузка `:/openvegas.qss`

### Ресурсы
- Всё через `resources/resources.qrc`
- Тема: `resources/openvegas.qss`
- Иконки предпочтительно inline SVG через `IconFactory`, не россыпь PNG

### Горячие клавиши (ориентир Vegas / макет)
| Клавиши | Действие |
|---------|----------|
| Ctrl+N | New Project |
| Ctrl+O | Open `.veg` |
| Alt+Enter | Project Properties |
| Ctrl+Shift+M | Render As |
| Ctrl+A | Select All (events) |
| Delete | Delete (когда будет) |
| S | Split (план) |
| Up / Down | Zoom time (план / частично) |

Полный список по мере появления — в `docs/` и ISSUES.

---

## Домен и I/O

### ProjectModel
- Tracks: `Video` / `Audio`, events: `startSec`, `lengthSec`, `name`, `selected`, FX chains
- Media pool: `MediaItem` (path, kind, missing)
- `loadEmptyProject` / `loadDemoProject` / `applyVegImport` / interchange apply

### VegReader
- Подробно: [`VEG_READER_V0.md`](VEG_READER_V0.md)
- Кратко: [`../docs/VEG_OPEN.md`](../docs/VEG_OPEN.md)
- Открытие: File → Open, Welcome, CLI `OpenVegas.exe file.veg`
- **v1:** binary `start/length/rate`, A/V pairing, overlap fades
- **Event FX:** `recoverVideoEventFxNames` (Glint XML, skip Magix AutoFrame / Softlight-sepia)
- Не цель: полный OFX/VST3 param blob, запись `.veg`

### Plugins
- Полный статус: [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md), [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md)
- **Builtin audio DSP** — Gate / EQ / Comp / Chorus / Delay / Reverb (+ UI без бренда «VEGAS »)
- **VST1/VST2** — process + HWND editor + stretch
- **VST3** — lean host + `IPlugView` / `IPlugFrame` / stretch (`thirdparty/vst3sdk`)
- **OFX** — discover + process + emulated Soften/Invert/Sepia; video chain в compositor
- **Hosts:** `CompositePluginHost`, `Vst3Host`, `OfxHost`
- Backlog: CI `.vst3` fixture, soft bypass fade, полный VEG blob reverse

---

## Сборка

### CMake
- Минимум 3.16, C++17, Qt6 Widgets + Svg (+ зависимости audio/video)
- `AUTOUIC` / `AUTOMOC` / `AUTORCC`
- Предпочтительный out-of-tree: `build/Windows_MinGW-x64` (preset `windows-mingw-debug`)
- Опция `OPENVGAS_COPY_VEGAS_RUNTIME` → только в `build/vegas-runtime/` (gitignore)
- Перед rebuild закрывать `OpenVegas.exe`, если линковка падает из‑за занятого файла

### qmake
- `OpenVegas.pro` должен содержать те же sources/headers, что и CMake
- При добавлении файла — править **оба** манифеста

### Компиляторы
- MSVC 2022, MinGW (GCC 9+), Clang 12+
- Не дублировать `/W4` поверх Qt `/W3` (D9025) — см. `.pro` / CMake

---

## Интерфейс и тема

- Тёмная тема Vegas-like: `#1a1a1a` / `#2a2a2a`, акцент `#0078d7`
- Video events: пурпурные тона; audio: розово-коричневые; selection/playhead: жёлтый
- Timecode на empty/demo: меры `1.1.000` (как Vegas); preview Frame из `frameRate`
- Не включать status bar постоянно; краткие сообщения после Open `.veg` допустимы

---

## Документация

### Разделение папок
| Папка | Назначение |
|-------|------------|
| **`MARKDOWN/`** | INIT, ISSUES, развёрнутые планы, реверс-заметки для разработчиков/ИИ |
| **`docs/`** | Краткие гайды для пользователей и ссылок из README |
| **`SAMPLES/**/README.md`** | Пояснения к эталонам в подпапках |
| **`README.md`** | Обзор проекта + ссылки |

### Правила .md
- Собственные пояснения / рапорты / глубокие разборы → **`MARKDOWN/`**
- Краткие пользовательские шпаргалки → **`docs/`**
- При новой фиче: обновить соответствующий `.md` и при необходимости README
- При баге / отключении: **`MARKDOWN/ISSUES_AND_PLANS.md`**
- Не плодить дубликаты; лучше ссылка «подробно ↔ кратко»
- **`MARKDOWN/INIT.MD` и `.cursor/commands/init.md` должны быть байт-в-байт одинаковыми** — править оба (или копировать один в другой) при любом изменении правил

### Актуальные документы
| Файл | Содержание |
|------|------------|
| [`INIT.MD`](INIT.MD) / [`.cursor/commands/init.md`](../.cursor/commands/init.md) | Правила разработки (синхронная копия) |
| [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md) | Баги, stub, планы, журнал исправлений |
| [`UI_STUBS_AUDIT.md`](UI_STUBS_AUDIT.md) | Пустышки интерфейса: кнопки, меню, поля без функций |
| [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md) | Video/Audio MediaEngine roadmap |
| [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md) | VST / OFX / Builtin / Event FX |
| [`VEG_READER_V0.md`](VEG_READER_V0.md) | Импорт `.veg` подробно |
| [`QT68_PORT_AND_VEG_OPEN.md`](QT68_PORT_AND_VEG_OPEN.md) | Порт UI + уровни парсера |
| [`../docs/VEG_OPEN.md`](../docs/VEG_OPEN.md) | Open `.veg` кратко |
| [`../SAMPLES/docs_veg/`](../SAMPLES/docs_veg/README.md) | Бинарный формат `.veg` |

---

## ISSUES_AND_PLANS

Обязательный журнал в `MARKDOWN/ISSUES_AND_PLANS.md`:

1. **Баг найден** → сразу в «Известные баги»
2. **Фича отключена / stub** → «Неработающие/отключённые» + план
3. **Новая крупная задача** → «Планы» с приоритетом
4. **Исправлено** → статус «Исправлено» + кратко как

TODO в коде без записи в ISSUES — не оставлять.

---

## Пустышки интерфейса

Отдельный обязательный журнал: [`MARKDOWN/UI_STUBS_AUDIT.md`](UI_STUBS_AUDIT.md) — построчный
список элементов, которые **видны, выглядят рабочими и ничего не делают**: кнопки без
обработчика, пункты меню без `connect`, поля и ползунки, чьё значение никто не читает,
осиротевшие `.ui`.

Правило простое и работает в обе стороны:

1. **Появилась пустышка** — кнопка, пункт меню, поле или ползунок добавлены без рабочей
   привязки — строка `file:line` добавляется в `UI_STUBS_AUDIT.md` **в том же коммите**.
2. **Пустышка ожила** — элементу дали функциональность — строка из отчёта **удаляется**, а
   счётчики в сводке пересчитываются. Ожившее в отчёте не оставлять: устаревший список
   вреднее отсутствующего.
3. **Изменились цифры** — сводку править по факту, а не на глаз:
   ```text
   python tools/audit_ui_stubs.py           # счётчики
   python tools/audit_ui_stubs.py --json    # полный список, удобно диффать
   ```

Инструмент даёт **кандидатов, а не приговор**. Каждый кандидат перед попаданием в отчёт
читается в коде; формы, которые он отсеивает сам, и его слепое пятно описаны в §7 отчёта.
Главное из слепого пятна: контрол, связанный только с «пометить диалог изменённым», для
инструмента неотличим от рабочего. В диалогах с кнопкой Apply проверять надо не наличие
`connect`, а доходит ли значение до модели.

Намеренно отключённый элемент (`setEnabled(false)`) — не то же самое, что мёртвый: на экране
он честен. Такие помечаются статусом DISABLED и остаются в отчёте до реализации.

---

## Настройки (QSettings)

- Organization: `OpenVegas`
- Application: `OpenVegas`
- Примеры ключей: `welcome/showOnStartup`, `plugins/ofxPath`, пути VST / Vegas Pro

Не хранить абсолютные пути Vegas Program Files как обязательные; только опциональные подсказки.

---

## Тестирование

- Unit-тесты Catch2 в `tests/` → targets `openvegas_audio_tests` / `openvegas_video_tests` / `openvegas_media_tests`
- Теги плагинов: `[plugins]`, `[vst3]`, `[ofx]`, `[builtin-dsp]`, `[video-fx]`, `[state]`, `[perf]`
- Ручная проверка Open `.veg`:
  ```text
  build\Windows_MinGW-x64\OpenVegas.exe SAMPLES\example_project_with_video_and_audio.veg
  ```
- FX sample: `SAMPLES/veg_project/project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg`
- Регресс UI: сравнить с `SAMPLES/veg_project/` и `SAMPLES/screenshots/` (если есть)
- Скрипты анализа `.veg`: `SAMPLES/docs_veg/_analyze_*.py`

---

## Git и версия

- Сообщения коммитов: зачем, не дамп файлов
- Не коммитить: `build/`, `vegas-runtime/`, бинарники Vegas, крупные кэши
- Версия приложения: `0.1.0` (semver по мере релизов)
- Лицензия вкладов: GPL-3.0-or-later, совместимо с [`LICENSE`](../LICENSE)

---

## Безопасность и IP

- Не встраивать и не распространять код/ассеты MAGIX Vegas под GPL
- Внутренние FX Vegas — чужая IP; не LoadLibrary proprietary hosts
- OFX/VST — чужие плагины по пользовательским путям; fail soft при crash
- Валидировать пути при Open; missing media — warning, не crash
- Не выполнять произвольный код из `.veg` (файл data-only; VST/OFX — отдельные DLL пользователя)

---

## Производительность timeline

- Рисовать только видимую область по мере роста сцен
- `pixelsPerSecond` clamp (сейчас ~8…400)
- Тяжёлый decode медиа / waveform / preview — асинхронно / кэш (`src/video`, filmstrip)
- Не парсить весь `.veg` повторно без нужды
- Audio callback / video frame — держать бюджеты из plugins plan

---

## Обязательные действия при разработке

1. Эталон UI → `SAMPLES/veg_project/` / screenshot перед полировкой chrome  
2. Новый `.cpp/.h` → **CMakeLists.txt и OpenVegas.pro**  
3. Баг / stub / план → `ISSUES_AND_PLANS.md`  
3a. Элемент UI без привязки или, наоборот, получил её → `MARKDOWN/UI_STUBS_AUDIT.md` в том же коммите  
4. Существенная фича → `MARKDOWN/` (развёрнуто) + при необходимости `docs/` (кратко) + ссылка в README  
5. Open `.veg` меняется → обновить `VEG_READER_V0.md` и `docs/VEG_OPEN.md`  
6. Audio/video/plugins стек меняется → обновить `PLAN_VIDEOAUDIOSTACK.md` / `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`  
7. Изменение правил → синхронизировать **`MARKDOWN/INIT.MD`** и **`.cursor/commands/init.md`**  
8. Не трогать Cursor plan-файлы вне репозитория, если пользователь не просил  

---

## Архитектурные модули (текущий MVP)

| Модуль | Ответственность |
|--------|-----------------|
| `MainWindow` | Shell, transport, open/demo, wiring диалогов / Event FX |
| `TimelineView` | Ruler, tracks, events, playhead, context menus |
| `ProjectModel` | Состояние проекта, FX slots, interchange apply |
| `VegReader` | Импорт `.veg` (timings + Event FX + chunks) |
| `AudioEngine` / graph / mixer | Playback clock, DSP chain, renderToWav |
| `Vst2Host` / `Vst3Host` / `CompositePluginHost` | Audio plugin process + editors |
| `OfxHost` | Video OFX discover/process + emulated |
| `VideoCompositor` | Preview frames, Pan/Crop, Color, video FX chain |
| `MediaEngine` | Offline render / FFmpeg encode |
| `WelcomeDialog` | Старт / recent samples |
| `IconFactory` / `MenuBuilder` / Theme | Chrome |
| `AudioEventFxDialog` / `VideoEventFxDialogExact` | Event FX UI (немодальные) |
| `PluginScanner` / registry | Discovery OFX / VST / builtins |

Детали: [`QT68_PORT_AND_VEG_OPEN.md`](QT68_PORT_AND_VEG_OPEN.md), [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md).

---

*Правила обязательны для консистентности OpenVegas (C++/Qt / Vegas-like NLE). При конфликте с устаревшим текстом приоритет у актуального кода и эталонов в `SAMPLES/`.*
