# OpenVegas

[![CI](https://github.com/ili4yov-ika/OpenVegas/actions/workflows/ci.yml/badge.svg)](https://github.com/ili4yov-ika/OpenVegas/actions/workflows/ci.yml)

Открытый кроссплатформенный видеоредактор — аналог **VEGAS Pro 22** на **C++ / Qt 6.8** с лицензией **GNU GPL**.

Цель — привычный workspace Vegas (таймлайн, events, fades/crossfades, media bin, Trimmer, Properties, Event FX) и открытие проектов `.veg`.

Сейчас: **рабочий MVP** — playback A/V, compositor, Builtin/VST/OFX, Render As, interchange, импорт `.veg` (v1 + Event FX).

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
| OFX process + emulated Soften/Invert/Sepia | Done MVP |
| Event FX UI (Video / Audio, немодальные) | Done |
| Render As (FFmpeg) + progress UI | Done |
| NLE interchange (Vegas CSV / FCP7 / FCPX / Premiere scrape) | Done MVP |
| Media Generator — VEGAS Titles & Text (анимации, hover-preview, Drag'n'Drop) | Done MVP |

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
build\Windows_MinGW-x64\OpenVegas.exe SAMPLES\example_project_with_video_and_audio.veg
```

FX sample: `SAMPLES/veg_project/project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg`.

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
├── MARKDOWN/             # INIT, ISSUES, PLAN_*, VEG_READER
├── docs/                 # краткие гайды
├── thirdparty/           # vst3sdk / openfx (локально, по необходимости)
└── SAMPLES/              # .veg, veg_project, docs_veg, runtime Vegas
```

| Путь | Назначение |
|------|------------|
| [`src/`](src/) | MainWindow, timeline, model, VegReader, audio/video/media/plugins |
| [`tests/`](tests/) | Catch2: audio / video / media / plugin state |
| [`tools/`](tools/README.md) | NSIS/deb/rpm, macOS/WSL, `svg_to_ico` |
| [`MARKDOWN/`](MARKDOWN/INIT.MD) | Правила, планы, ISSUES |
| [`SAMPLES/`](SAMPLES/README.md) | Эталонные `.veg`, медиа, Vegas Pro 22 |

---

## Эталоны Vegas

1. Проекты и медиа — [`SAMPLES/veg_project/README.md`](SAMPLES/veg_project/README.md)
2. Разбор `.veg` — [`SAMPLES/docs_veg/`](SAMPLES/docs_veg/README.md), [`SAMPLES/veg_analyzators/README.md`](SAMPLES/veg_analyzators/README.md)
3. Runtime Vegas Pro 22 — [`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md`](SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/README.md)

---

## Документация

1. **Правила разработки** — [`MARKDOWN/INIT.MD`](MARKDOWN/INIT.MD)
2. **Баги и планы** — [`MARKDOWN/ISSUES_AND_PLANS.md`](MARKDOWN/ISSUES_AND_PLANS.md)
3. **Video / Audio stack** — [`MARKDOWN/PLAN_VIDEOAUDIOSTACK.md`](MARKDOWN/PLAN_VIDEOAUDIOSTACK.md)
4. **Plugins stack** — [`MARKDOWN/PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](MARKDOWN/PLAN_VIDEO-AUDIO-PLUGINS-STACK.md)
5. **Открытие `.veg` (кратко)** — [`docs/VEG_OPEN.md`](docs/VEG_OPEN.md)
6. **Открытие `.veg` (развёрнуто)** — [`MARKDOWN/VEG_READER_V0.md`](MARKDOWN/VEG_READER_V0.md)
7. **Перенос на Qt + парсер** — [`MARKDOWN/QT68_PORT_AND_VEG_OPEN.md`](MARKDOWN/QT68_PORT_AND_VEG_OPEN.md)
8. **Поддерживаемые файлы** — [`docs/support_files.md`](docs/support_files.md)
9. **Формат `.veg`** — [`SAMPLES/docs_veg/00_format_overview.md`](SAMPLES/docs_veg/00_format_overview.md)

---

## Лицензия

Код OpenVegas — **GNU GPL v3 or later**. Полный текст: [`LICENSE`](LICENSE).

VEGAS Pro и материалы в `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/` принадлежат MAGIX/VEGAS и **не** покрываются GPL; только справочные эталоны. Копии runtime (`build/vegas-runtime/`) не публиковать в git. VST3 SDK — лицензия Steinberg, отдельно от GPL приложения.
