# Правила разработки проекта OpenVegas

Документ для разработчиков и ИИ-агентов. Соблюдать при любых изменениях кода и документации.

Краткая пользовательская документация — в `docs/`. Технические разборы и планы — в `MARKDOWN/`.

---

## Общие принципы

### Продукт
- **OpenVegas** — GPL open-source NLE, UI и поведение ориентированы на **VEGAS Pro 22**.
- Эталоны: HTML/CSS/JS в `SAMPLES/`, скриншоты, `example_project_*.veg`, реверс в `SAMPLES/docs_veg/`.
- Runtime Vegas (`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/`, `build/vegas-runtime/`) — **только справочник**, не GPL, **не коммитить** копии в git.

### Архитектура
- **Язык**: C++17+
- **UI**: Qt 6.8+ Widgets, `.ui` в `ui/`, тема через `resources/openvegas.qss`
- **Сборка**: CMake (предпочтительно) и `OpenVegas.pro` (qmake / Qt Creator) — **синхронизировать** оба
- **Домен**: `ProjectModel` (tracks / events / media pool) отдельно от chrome UI
- **I/O**: `VegReader` для `.veg`; запись `.veg` / `.ovp` — позже

### Структура директорий
```
OpenVegas/
├── src/
│   ├── app/           # main, MainWindow
│   ├── ui/            # диалоги, Theme, MenuBuilder, IconFactory
│   ├── timeline/      # TimelineView
│   ├── model/         # ProjectModel
│   ├── io/            # VegReader
│   └── plugins/       # PluginScanner stubs
├── ui/                # Qt Designer .ui
├── resources/         # openvegas.qss, resources.qrc
├── docs/              # краткая документация для пользователей / README-ссылки
├── MARKDOWN/          # развёрнутые планы, INIT, ISSUES, реверс-заметки
├── SAMPLES/           # макеты UI, .veg, docs_veg, скриншоты (эталоны)
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
1. Сначала смотреть **статический HTML** (`SAMPLES/pages/*_static.html`) и/или **скриншот** Vegas.
2. Потом править `.ui` / `MainWindow` / QSS / `IconFactory`.
3. Не выдумывать chrome, которого нет в эталоне, без записи в ISSUES.

### Компоненты
- **MainWindow** — shell: меню, toolbars, splitters, media/preview/master/timeline
- **TimelineView** — custom paint: ruler, tracks, events, playhead
- **WelcomeDialog** — стартовый экран (New / Open / Getting Started)
- **Диалоги** — Project Properties, Render As, Preferences, Event Properties, Trimmer, Plugin Chooser
- **IconFactory** — SVG из макетов `SAMPLES/js/chrome.js`
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
- Tracks: `Video` / `Audio`, events: `startSec`, `lengthSec`, `name`, `selected`
- Media pool: `MediaItem` (path, kind, missing)
- `loadEmptyProject` / `loadDemoProject` / `applyVegImport`

### VegReader (v0)
- Подробно: [`VEG_READER_V0.md`](VEG_READER_V0.md)
- Кратко: [`../docs/VEG_OPEN.md`](../docs/VEG_OPEN.md)
- Открытие: File → Open, Welcome, CLI `OpenVegas.exe file.veg`
- v0: header + media paths + labels + эвристический timeline  
- Не v0: точные in/out, fades/CF, groups, rate, запись `.veg`

### Plugins
- `PluginScanner` — stub OFX path (Preferences); реального host нет

---

## Сборка

### CMake
- Минимум 3.16, C++17, Qt6 Widgets + Svg
- `AUTOUIC` / `AUTOMOC` / `AUTORCC`
- Опция `OPENVGAS_COPY_VEGAS_RUNTIME` → только в `build/vegas-runtime/` (gitignore)

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
| [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md) | Баги, отключённое, планы (создавать/обновлять) |
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

## Настройки (QSettings)

- Organization: `OpenVegas`
- Application: `OpenVegas`
- Примеры ключей: `welcome/showOnStartup`, `plugins/ofxPath`

Не хранить абсолютные пути Vegas Program Files как обязательные; только опциональные подсказки.

---

## Тестирование

- Пока нет полноценного `tests/` — при добавлении: всё тестовое → `tests/`
- Ручная проверка Open `.veg`:
  ```text
  build\OpenVegas.exe SAMPLES\example_project_with_video_and_audio.veg
  ```
- Регресс UI: сравнить с `SAMPLES/pages/*_static.html` и `SAMPLES/screenshots/`
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
- OFX/плагины Vegas — чужая IP; только путь сканирования stub
- Валидировать пути при Open; missing media — warning, не crash
- Не выполнять произвольный код из `.veg` (файл data-only)

---

## Производительность timeline

- Рисовать только видимую область по мере роста сцен
- `pixelsPerSecond` clamp (сейчас ~8…400)
- Тяжёлый decode медиа / waveform / preview — асинхронно (когда появится FFmpeg/Qt Multimedia)
- Не парсить весь `.veg` повторно без нужды

---

## Обязательные действия при разработке

1. Эталон UI → `SAMPLES/` (static HTML / screenshot) перед полировкой chrome  
2. Новый `.cpp/.h` → **CMakeLists.txt и OpenVegas.pro**  
3. Баг / stub / план → `ISSUES_AND_PLANS.md`  
4. Существенная фича → `MARKDOWN/` (развёрнуто) + при необходимости `docs/` (кратко) + ссылка в README  
5. Open `.veg` меняется → обновить `VEG_READER_V0.md` и `docs/VEG_OPEN.md`  
6. Изменение правил → синхронизировать **`MARKDOWN/INIT.MD`** и **`.cursor/commands/init.md`**  
7. Не трогать Cursor plan-файлы вне репозитория, если пользователь не просил  

---

## Архитектурные модули (текущий MVP)

| Модуль | Ответственность |
|--------|-----------------|
| `MainWindow` | Shell, toolbars, open/demo, wiring диалогов |
| `TimelineView` | Ruler, tracks, events, playhead, context menus |
| `ProjectModel` | Состояние проекта |
| `VegReader` | Импорт `.veg` v0 |
| `WelcomeDialog` | Старт / recent samples |
| `IconFactory` | SVG icons |
| `MenuBuilder` | Меню File/Edit/View/… |
| `PluginScanner` | Stub OFX list |

Детали порта и фазы: [`QT68_PORT_AND_VEG_OPEN.md`](QT68_PORT_AND_VEG_OPEN.md).

---

*Правила обязательны для консистентности OpenVegas (C++/Qt / Vegas-like NLE). При конфликте с устаревшим текстом приоритет у актуального кода и эталонов в `SAMPLES/`.*
