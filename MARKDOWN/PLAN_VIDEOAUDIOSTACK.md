# Video / Audio Stack — план

Поэтапный roadmap поверх существующего MVP. Kdenlive (`thirdparty/kdenlive`, gitignore) — **референс** тайминга/архитектуры, не форк. Vegas Pro runtime (`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES`) — **справочник** OFX/иконок для отладки, не LoadLibrary proprietary hosts.

См. также: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md).

---

## Цель

Единый **MediaEngine**-фасад: playback (часы = audio), offline render, пути плагинов (VST / OFX / Vegas folder), unit-тесты. Дальше — OFX/VST3 host, builtins video FX, linked FFmpeg, HW encode.

## Non-goals

- Порт MLT/Kdenlive без зависимости
- Загрузка DLL внутренних FX Vegas как «своих»
- Полный realtime 4K multi-layer GPU compositor в фазах 1–4

---

## Текущее состояние (инвентаризация)

| Область | Статус | Где |
|---------|--------|-----|
| Audio playback / graph / mixer | **Done** | `src/audio/*` |
| Builtin audio DSP | **Done** | Gate/EQ/Comp/Chorus |
| VST2 process + editor | **Partial** | VeSTige |
| VST3 | **Stub** | до Steinberg SDK |
| Video preview compositor | **Done** | soft CPU |
| OFX | **Discover + stub host** | `PluginScanner` + `OfxHost` (без process) |
| VST3 | **Stub host** (+ SDK path) | `Vst3Host`; scan/tests; process с SDK |
| Video Color Corrector | **Done (MVP)** | `ColorCorrectorApply` в `VideoCompositor` |
| Render Wave PCM | **Done** | `AudioEngine::renderToWav` |
| Render AAC/MP4/… | **Done** | FFmpeg CLI + HW prefer |
| Continuous video decode | **Done** | `FFmpegStreamDecoder` raw pipe / optional libav |
| HW decode/encode | **Done** | `-hwaccel auto`; NVENC/QSV/AMF → libx264 |

---

## Архитектура целевого MediaEngine

```text
Timeline / ProjectModel
        │
        ▼
   MediaEngine  ──► Preferences paths (vegasPro / OFX / VST)
        │
        ├── AudioEngine (master clock, graph, renderToWav)
        ├── VideoCompositor + VideoFrameCache (slave frames)
        └── FFmpegEncoder (offline mux/encode via CLI)
```

- Playback: как сейчас — AudioEngine clock → UI quantize → compositor.
- Offline render: temp WAV (+ optional PNG sequence) → ffmpeg → output.
- Audio FX graph ≠ video FX graph (OFX отдельно).

---

## Фазы

| Фаза | Содержание | DoD | Статус |
|------|------------|-----|--------|
| **0** | Этот документ + sync с ISSUES | md актуален | **Done** |
| **1** | MediaEngine + FFmpeg Render As + Preferences `vegasProPath` + tests | Wave + AAC/MP3/FLAC + MP4 H.264; scan paths | **Done** |
| **2** | OFX discovery hardening + stub host API (load/describe, без process) | scan sample OFX; unit tests | **Done** |
| **3** | VST3 host API + scan tests; real process если Steinberg SDK | stub pass-through / SDK path | **Done** (stub; SDK optional) |
| **4** | Video FX builtins (Color Corrector MVP в compositor) | preview + test | **Done** |
| **5** | Linked FFmpeg / continuous decode | без CLI-seek на каждый кадр | **Done** |
| **6** | HW path (`-hwaccel`, `h264_nvenc` / QSV) | fallback soft | **Done** |

### Пути плагинов (Preferences)

| Key | Назначение |
|-----|------------|
| `plugins/vegasProPath` | Корень Vegas Pro Program Files |
| `plugins/ofxPath` | Явная папка OFX (или Vegas root) |
| `plugins/vst1Paths` / `vst2Paths` / `vst3Paths` | Скан VST |
| Fallback | `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES`, `vegas-runtime`, Steam/Program Files guesses |

Приоритет OFX roots: `vegasProPath` → preferred/ofxPath → settings ofxPath → app `vegas-runtime` → sample folder → platform guesses.

### Тест-матрица

| Target | Фазы |
|--------|------|
| `openvegas_audio_tests` | audio DSP / fades (уже есть) |
| `openvegas_video_tests` | compositor (уже есть) |
| `openvegas_media_tests` | FFmpegEncoder smoke, PluginScanner paths (фаза 1+) |

---

## Фаза 1 — детали реализации

1. Preferences: поле **Vegas Pro Program Files** + QSettings `plugins/vegasProPath`
2. `PluginScanner` — приоритет путей (см. выше)
3. `src/io/FFmpegEncoder` — `findFfmpeg` (через `MediaFilmstripCache`), `QProcess`, audio + MP4
4. `src/media/MediaEngine` — `renderProject(...)`: WAV → (frames) → ffmpeg
5. `MainWindow::onRenderAs` — не Wave → MediaEngine
6. Catch2 `openvegas_media_tests`

**Не в фазе 1:** NVENC, ProRes full quality, OFX process, linked libav.

## Фаза 2 — детали

1. `src/plugins/OfxHost` — discover/describe `.ofx.bundle` (Win64/…), `load()` stub
2. `PluginScanner::scanDirectory` использует `OfxHost::discoverInRoot`
3. Тесты `tests/test_ofx_host.cpp`
4. FFmpeg: fallback рядом с приложением (Windows portable)

## Фаза 3 — детали

1. `Vst3Host::hasSdk()` + create/process/state stub (pass-through без SDK)
2. CMake `OPENVGAS_VST3_SDK_PATH` → `OPENVGAS_HAS_VST3_SDK` (иначе STATUS stub)
3. Тесты `tests/test_vst3_host.cpp` (scan `*.vst3`, round-trip state)
4. Реальный instantiate/process/editor — только при наличии Steinberg SDK

## Фаза 4 — детали

1. `src/video/ColorCorrectorApply` — brightness/contrast/saturation/gamma + Color Grading lift/gamma/gain
2. `VideoCompositor::compose` применяет event + track color FX после Pan/Crop
3. UI: страница Color Corrector в `VideoEventFxDialogExact`
4. `videoFxSlotFromName` / VEG `colorcorrector` → Builtin
5. Тесты в `openvegas_video_tests`

## Фаза 5 — детали

1. `src/io/FFmpegStreamDecoder` — один процесс → raw RGB sequence (continuous)
2. `VideoFrameCache` DecodeJob / BurstDecodeJob через stream decoder (без PNG seek-per-frame)
3. Optional `OPENVGAS_FFMPEG` (libavformat/codec/swscale) — linked decode, иначе CLI pipe
4. Тесты `tests/test_ffmpeg_stream_decoder.cpp`

## Фаза 6 — детали

1. Decode: `-hwaccel auto` (QSettings `media/hwAccel`, env `OPENVGAS_HWACCEL`); retry без HW
2. Encode: probe `ffmpeg -encoders` → `h264_nvenc` / `h264_qsv` / `h264_amf`, иначе `libx264`
3. Encode runtime fallback на soft, если HW encoder упал
