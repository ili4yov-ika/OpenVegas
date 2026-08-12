# OpenVegas

[![CI](https://github.com/ili4yov-ika/OpenVegas/actions/workflows/ci.yml/badge.svg)](https://github.com/ili4yov-ika/OpenVegas/actions/workflows/ci.yml)

Открытый кроссплатформенный видеоредактор — аналог **VEGAS Pro 22** на **C++ / Qt 6.8** с лицензией **GNU GPL**.

Цель — привычный workspace Vegas (таймлайн, events, fades/crossfades, media bin, Trimmer, Properties, Event FX, Media Generators, Transitions) и открытие проектов `.veg`.

Сейчас: **рабочий MVP** — playback A/V, compositor, Builtin/VST/OFX (настоящие OFX-плагины, включая бандлы VEGAS, реально рендерят), Render As, interchange, импорт `.veg` (v1 + Event FX + Titles & Text + переходы), редактирование на таймлайне и сохранение проекта в собственный формат.

---

## Скриншот

![Main Interface](docs/main_ui.png)

---

## Что уже работает

| Область | Статус |
|---------|--------|
| UI shell / timeline / media bin | Done |
| Open `.veg` (timings, fades, media pool) | Done (v1) |
| Video Event FX из `.veg` (Glint, Chroma Blur, …) | Done (best-effort) |
| Video preview / compositor (Pan/Crop, Color, FX chain) | Done MVP |
| Audio engine + mixer + Builtin DSP (Delay/Reverb, …) | Done |
| VST1 / VST2 process + editor | Done |
| VST3 process + `IPlugView` editor | Done (нужен SDK) |
| OFX host — загрузка и рендер реальных `.ofx` (включая бандлы VEGAS) | Done (хост) |
| **Видеоплагины из Vegas Pro — пока НЕ работают**³ | Не работает |
| OFX кроссплатформенно — стандартные корни (`OFX_PLUGIN_PATH`, `/usr/OFX/Plugins`, `/Library/OFX/Plugins`) + ABI-гейт | Done |
| Event FX UI (Video / Audio, немодальные) | Done |
| Video FX — Drag'n'Drop эффекта/пресета на видеоклип | Done |
| Render As (FFmpeg) + progress UI | Done |
| NLE interchange (Vegas CSV / FCP7 / FCPX / Premiere scrape) | Done MVP¹ |
| Media Generator — VEGAS Titles & Text (анимации, hover-preview, Drag'n'Drop) | Done MVP |
| Titles & Text — окно редактора, Media Properties, кейфреймы параметров | Done MVP |
| Transitions — 3D Blinds (4 пресета, DnD на фейд/кроссфейд, окно свойств) | Done для 1 группы² |
| Переходы из `.veg` (3D Blinds: пресет + параметры + привязка к событию) | Done |
| Правка на таймлайне: multi-select (Shift/Ctrl), групповое перетаскивание, Split (`S`), Automatic Crossfades, magnet snap | Done |
| Save / Save As — родной **OpenVegas Project Archive** | Done |

¹ Переходы не переносит **ни один** формат обмена — это предел самих форматов, а не наш дефект: Vegas в своих же экспортах вырождает 3D Blinds в `Cross Dissolve`. Что именно выживает в каждом формате — таблица в [`MARKDOWN/ISSUES_AND_PLANS.md`](MARKDOWN/ISSUES_AND_PLANS.md) (раздел «Render As / interchange»).
² Остальные ~24 группы каталога (3D Cascade, Barn Door, Iris, …) — намеренно нерабочие плейсхолдеры: тайлы не перетаскиваются, чтобы нельзя было поставить переход, который ничего не делает.
³ **Видеоплагины из Vegas Pro сейчас не открываются и не рендерят.** Цепочка Event FX из `.veg` восстанавливается, плагины видны в каталоге и в окне Event FX, но картинки не дают. Сам хост исправен — настоящий `Vfx1.ofx` проходит `Load → Describe → DescribeInContext → CreateInstance → Render` со статусом 0; не работает **перенос значений из проекта**: OFX-блоб параметров в `.veg` не разобран, слот приезжает пустым, и плагин честно рендерит с радиусом `0`.

Собственные подмены (box blur вместо Chroma Blur, sepia-матрица вместо Sepia, gain на любой упавший рендер) **намеренно отключены** — `OPENVEGAS_EMULATED_VIDEO_FX = 0` в [`src/plugins/OfxHost.h`](src/plugins/OfxHost.h). Раньше они создавали впечатление, что приложение крутит эффекты VEGAS, тогда как оно крутило их приближения. Теперь эффект, который нельзя отрендерить по-настоящему, не рендерится вовсе. **Писать новые такие реализации не следует** — путь вперёд один: разобрать настоящие параметры. Замеры и план — [`MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md`](MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md).

Подробности и backlog: [`MARKDOWN/ISSUES_AND_PLANS.md`](MARKDOWN/ISSUES_AND_PLANS.md), [`MARKDOWN/PLAN_VIDEOAUDIOSTACK.md`](MARKDOWN/PLAN_VIDEOAUDIOSTACK.md), [`MARKDOWN/PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](MARKDOWN/PLAN_VIDEO-AUDIO-PLUGINS-STACK.md).

---

## Стек

| Слой | Выбор |
|------|--------|
| Язык / UI | C++17+, Qt 6.8 (Widgets), `.ui` в `ui/` |
| Сборка | CMake 3.16+ (предпочтительно) и `OpenVegas.pro` |
| Компиляторы | MSVC 2022, GCC 9+, Clang 12+ |
| Аудио | miniaudio graph, Builtin DSP, VST1–3 |
| Видео | software compositor + FFmpeg decode/encode |
| Плагины | VST3 SDK (опц.), OpenFX host MVP |
| Лицензия | GNU GPL v3 or later ([LICENSE](LICENSE)) |

---

## Сборка и запуск

Требуется **Qt 6.8+** (Widgets + Svg). Перед rebuild на Windows закройте `OpenVegas.exe`, если линковка падает из‑за занятого файла.

### Qt Creator (qmake)

Откройте [`OpenVegas.pro`](OpenVegas.pro), kit **Qt 6.8+ Widgets** (MSVC / MinGW / Clang) → Build.

### CMake

```bash
# Windows MinGW (см. также cmake --preset windows-mingw-debug)
cmake -S . -B build/Windows_MinGW-x64 -DCMAKE_PREFIX_PATH=<путь-к-Qt6.8>
cmake --build build/Windows_MinGW-x64
```

Запуск:

```text
build\Windows_MinGW-x64\OpenVegas.exe
build\Windows_MinGW-x64\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny.veg
```

Эталонные проекты для проверки:

| Что смотреть | Файл в `SAMPLES/veg_project/` |
| ------------ | ----------------------------- |
| Video Event FX | `project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg` |
| Переходы 3D Blinds на фейдах и кроссфейдах | `project_transitions_3d-blinds.veg` |
| Titles & Text | `project_titles-and-text.veg` |

Опционально скопировать поддеревья Vegas в **игнорируемый** `build/.../vegas-runtime/`:

```bash
cmake -S . -B build/Windows_MinGW-x64 -DCMAKE_PREFIX_PATH=<Qt6.8> -DOPENVGAS_COPY_VEGAS_RUNTIME=ON
```

**Не коммитьте** `build/`, `vegas-runtime/` и бинарники Vegas — они в [`.gitignore`](.gitignore).

### VST3 SDK (опционально)

Для реального VST3 host нужен [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (лицензия Steinberg отдельно от GPL; Usage Guidelines).

Shallow-clone в `thirdparty/vst3sdk` (submodules `pluginterfaces`, `base`, `public.sdk`). CMake выставляет `OPENVGAS_VST3_SDK_PATH`, если заголовки на месте; иначе stub pass-through:

```bash
cd thirdparty/vst3sdk && git submodule update --init pluginterfaces base public.sdk
cmake -S . -B build/Windows_MinGW-x64 -DCMAKE_PREFIX_PATH=<Qt6.8>
```

### OFX-видеоплагины

Ищутся в путях установки VEGAS Pro и в стандартных корнях OpenFX:

| Платформа | Корни |
|-----------|-------|
| Все | `OFX_PLUGIN_PATH` (`;` на Windows, `:` иначе) |
| Windows | `%CommonProgramFiles%\OFX\Plugins` |
| macOS | `/Library/OFX/Plugins`, `~/Library/OFX/Plugins` |
| Linux | `/usr/OFX/Plugins`, `/usr/local/OFX/Plugins`, `~/OFX/Plugins` |

Бандлы VEGAS содержат только `Contents/Win64`, поэтому на Linux и macOS они видны в
каталоге, но не загружаются — рендер идёт через встроенную эмуляцию по имени эффекта.
Сторонние OFX-плагины, собранные для вашей ОС, работают полноценно.

Если плагин не грузится:

```bash
OPENVEGAS_OFX_TRACE=/tmp/ofx.log ./OpenVegas   # лог всех host-колбэков
OPENVEGAS_OFX_THREADS=1 ./OpenVegas            # однопоточный рендер OFX
```

Подробности — [`MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md`](MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md).

### Тесты

```bash
cmake --build build/Windows_MinGW-x64 --target openvegas_audio_tests openvegas_video_tests openvegas_media_tests
ctest --test-dir build/Windows_MinGW-x64 --output-on-failure
```

### CI (GitHub Actions)

На каждый push / PR в `main` workflow [`.github/workflows/ci.yml`](.github/workflows/ci.yml) собирает Release на **Ubuntu 22.04**, **Windows 2022** и **macOS 14** (Qt **6.8.3**) и запускает `ctest`.

---

## Структура репозитория

```
OpenVegas/
├── CMakeLists.txt / OpenVegas.pro / CMakePresets.json
├── src/
│   ├── app/ ui/ timeline/ model/ io/
│   ├── audio/ video/ media/ plugins/
├── ui/                   # Qt Designer .ui
├── resources/            # QSS, icons
├── tests/                # Catch2
├── tools/                # установщики, утилиты
├── MARKDOWN/             # INIT, ISSUES, CHECKLIST, PLAN_*, VEG_READER, UI_STUBS_AUDIT
├── docs/                 # краткие гайды
├── thirdparty/           # vst3sdk / openfx (локально, по необходимости)
└── SAMPLES/              # veg_project, veg_analyzators, screenshots, runtime Vegas
```

| Путь | Назначение |
|------|------------|
| [`src/`](src/) | MainWindow, timeline, model, VegReader, audio/video/media/plugins |
| [`tests/`](tests/) | Catch2: audio / video / media / plugin state |
| [`tools/`](tools/README.md) | NSIS/deb/rpm, macOS/WSL, `svg_to_ico` |
| [`MARKDOWN/`](MARKDOWN/INIT.MD) | Правила, планы, ISSUES, чеклист, аудит заглушек |
| [`SAMPLES/`](SAMPLES/README.md) | Эталонные `.veg`, медиа, Vegas Pro 22 |

---

## Эталоны Vegas

1. Проекты и медиа — [`SAMPLES/veg_project/README.md`](SAMPLES/veg_project/README.md)
2. Разбор `.veg` и скрипты-анализаторы — [`SAMPLES/veg_analyzators/README.md`](SAMPLES/veg_analyzators/README.md)
3. Runtime Vegas Pro 22 — [`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md`](SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md)

---

## Документация

1. **Правила разработки** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
2. **Баги и планы** — [`MARKDOWN/ISSUES_AND_PLANS.md`](MARKDOWN/ISSUES_AND_PLANS.md)
3. **Video / Audio stack** — [`MARKDOWN/PLAN_VIDEOAUDIOSTACK.md`](MARKDOWN/PLAN_VIDEOAUDIOSTACK.md)
4. **Plugins stack** — [`MARKDOWN/PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](MARKDOWN/PLAN_VIDEO-AUDIO-PLUGINS-STACK.md)
5. **OFX / видеоплагины Vegas + кроссплатформенность** — [`MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md`](MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md)
6. **Открытие `.veg` (кратко)** — [`docs/VEG_OPEN.md`](docs/VEG_OPEN.md)
7. **Открытие `.veg` (развёрнуто)** — [`MARKDOWN/VEG_READER_V0.md`](MARKDOWN/VEG_READER_V0.md)
8. **Перенос на Qt + парсер** — [`MARKDOWN/QT68_PORT_AND_VEG_OPEN.md`](MARKDOWN/QT68_PORT_AND_VEG_OPEN.md)
9. **Поддерживаемые файлы** — [`docs/support_files.md`](docs/support_files.md)
10. **Формат `.veg`** — [`SAMPLES/veg_analyzators/00_format_overview.md`](SAMPLES/veg_analyzators/00_format_overview.md)
11. **Родной формат проекта** — [`MARKDOWN/PROJECT_ARCHIVE_FORMAT.md`](MARKDOWN/PROJECT_ARCHIVE_FORMAT.md)
12. **Сайдкары `.sfk` / `.sfl`** — [`MARKDOWN/SFK_SFL_SIDECAR_FILES.md`](MARKDOWN/SFK_SFL_SIDECAR_FILES.md)
13. **Что в UI ещё заглушка** — [`MARKDOWN/UI_STUBS_AUDIT.md`](MARKDOWN/UI_STUBS_AUDIT.md)
14. **Текущий чеклист задач** — [`MARKDOWN/CHECKLIST.md`](MARKDOWN/CHECKLIST.md)
15. **Аппаратные ускорители (VLD, NVENC/QSV/AMF)** — [`MARKDOWN/HARDWARE_ACCELERATOR.md`](MARKDOWN/HARDWARE_ACCELERATOR.md)

---

## Лицензия

Код OpenVegas — **GNU GPL v3 or later**. Полный текст: [`LICENSE`](LICENSE).

VEGAS Pro и материалы в `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/` принадлежат MAGIX/VEGAS и **не** покрываются GPL; только справочные эталоны. Копии runtime (`build/vegas-runtime/`) не публиковать в git. VST3 SDK — лицензия Steinberg, отдельно от GPL приложения.
