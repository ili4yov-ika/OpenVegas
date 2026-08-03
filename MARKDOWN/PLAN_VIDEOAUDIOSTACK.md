# Video / Audio Stack — план

Поэтапный roadmap поверх MVP. Kdenlive (`thirdparty/kdenlive`, gitignore) — **референс** тайминга/архитектуры, не форк. Vegas Pro runtime (`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES`) — **справочник** OFX/иконок для отладки, не LoadLibrary proprietary hosts.

См. также: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md).

**Обновлено:** 2026-08-03.

---

## Цель

Единый **MediaEngine**-фасад: playback (часы = audio), offline render, пути плагинов (VST / OFX / Vegas folder), unit-тесты. Дальше — OFX process host, полный VST3 (Steinberg SDK), расширенные video FX, polish timeline/render.

## Non-goals

- Порт MLT/Kdenlive без зависимости
- Загрузка DLL внутренних FX Vegas как «своих»
- Полный realtime 4K multi-layer GPU compositor в фазах 1–6

---

## Текущее состояние (инвентаризация)

| Область | Статус | Где / заметки |
|---------|--------|---------------|
| Audio playback / graph / mixer | **Done** | `src/audio/*`; Track→Bus→Master; meters |
| Builtin audio DSP | **Done** | Gate / EQ / Comp / Chorus |
| VST2 / VST1 process + editor | **Done (Partial UI)** | VeSTige `LoadLibrary` + `processReplacing` + HWND editor |
| VST3 | **Stub** | `Vst3Host`: pass-through без Steinberg SDK; scan/tests есть |
| Video preview compositor | **Done** | soft CPU: Pan/Crop, Track Motion KF, opacity/fades, Color Corrector |
| Continuous video decode | **Done** | `FFmpegStreamDecoder` raw pipe; optional linked libav |
| OFX | **Discover + stub host** | `PluginScanner` + `OfxHost` (без LoadLibrary / process) |
| Video Color Corrector / Grading | **Done (MVP)** | `ColorCorrectorApply` в `VideoCompositor` |
| Render Wave PCM | **Done** | `AudioEngine::renderToWav` |
| Render AAC/MP4/… | **Done** | `MediaEngine` + FFmpeg CLI; HW prefer |
| HW decode/encode | **Done** | `-hwaccel auto`; NVENC/QSV/AMF → libx264 |
| Reverse / Loop SubClip (EDL) | **Done** | `sourceTimeSec` + `AudioGraph` + waveform notches; см. samples README |
| Render progress UI | **Done** | `RenderingProgressDialog` (elapsed / ETA / cancel) |
| Timeline ruler zoom UX | **Done** | Fit / Zoom In-Out; Ctrl+колёсико; sync `m_pxPerSec` |
| Build: CMake MinGW path | **Done** | `build/Windows_MinGW-x64` (preset `windows-mingw-debug`) |
| Build: Qt Creator qmake | **Done** | `OpenVegas.pro` синхронизирован с CMake; `user32` / `winmm` |
| OFX process / GPU compositor | **Not started** | следующий крупный блок |
| Shadow / Glow, blend modes, mask interpolate | **Not started** | video polish |
| VEG VST state restore | **Not started** | имена/формат есть, chunk не восстанавливается |

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

- Playback: AudioEngine clock → UI quantize → compositor.
- Offline render: temp WAV (+ optional PNG sequence) → ffmpeg → output; UI — `RenderingProgressDialog`.
- Audio FX graph ≠ video FX graph (OFX отдельно).
- Reverse SubClip: `source = cycle - fmod(StreamStart + local, cycle)` при `reversed`; `mediaLengthSec` = META Length; loop notches: первый = `cycle - mediaStart`.

---

## Фазы 0–6 (базовый стек) — **закрыты**

| Фаза | Содержание | DoD | Статус |
|------|------------|-----|--------|
| **0** | Этот документ + sync с ISSUES | md актуален | **Done** |
| **1** | MediaEngine + FFmpeg Render As + Preferences `vegasProPath` + tests | Wave + AAC/MP3/FLAC + MP4 H.264; scan paths | **Done** |
| **2** | OFX discovery hardening + stub host API (load/describe, без process) | scan sample OFX; unit tests | **Done** |
| **3** | VST3 host API + scan tests; real process если Steinberg SDK | stub pass-through / SDK path | **Done** (stub; SDK optional) |
| **4** | Video FX builtins (Color Corrector MVP в compositor) | preview + test | **Done** |
| **5** | Linked FFmpeg / continuous decode | без CLI-seek на каждый кадр | **Done** |
| **6** | HW path (`-hwaccel`, `h264_nvenc` / QSV) | fallback soft | **Done** |

### Пути плагинов (Preferences) — реализовано

| Key | Назначение |
|-----|------------|
| `plugins/vegasProPath` | Корень Vegas Pro Program Files |
| `plugins/ofxPath` | Явная папка OFX (или Vegas root) |
| `plugins/vst1Paths` / `vst2Paths` / `vst3Paths` | Скан VST |
| Fallback | `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES`, `vegas-runtime`, Steam/Program Files guesses |

Приоритет OFX roots: `vegasProPath` → preferred/ofxPath → settings ofxPath → app `vegas-runtime` → sample folder → platform guesses.

### Тест-матрица

| Target | Покрытие |
|--------|----------|
| `openvegas_audio_tests` | DSP / fades / graph / mute / mixer |
| `openvegas_video_tests` | compositor / Color Corrector / fixtures |
| `openvegas_media_tests` | FFmpegEncoder smoke, PluginScanner, OfxHost, Vst3Host, stream decoder |

---

## Фазы 7+ — что ещё нужно

| Фаза | Содержание | Приоритет | Статус |
|------|------------|-----------|--------|
| **7a** | Reverse / Loop media model (SubClip EDL + waveform + AudioGraph) | High | **Done** (2026-08) |
| **7b** | Render progress + cancel + ETA | High | **Done** |
| **7c** | Timeline zoom / Fit / ruler sync | Med | **Done** |
| **7d** | Tooling: единый Windows MinGW build dir + qmake `.pro` sync | Med | **Done** |
| **8** | Real **VST3** (Steinberg SDK): instantiate, process, state, `IPlugView` editor | High | **Not started** (API stub есть) |
| **9** | Real **OFX** host: LoadLibrary/dlopen, process frame, params UI | High | **Not started** (discover only) |
| **10** | Video polish: mask path interpolate; Shadow/Glow; blend modes; in/out point edge cases | Med | **Not started** |
| **11** | VEG: восстановление VST/OFX state chunks; полный round-trip FX | Med | **Not started** |
| **12** | Linked encode path / ProRes quality / GPU compositor (опционально) | Low | **Not started** |

### Фаза 7a — детали (сделано)

1. EDL reverse: `StreamStart` = in-point на **reversed** item; `mediaLengthSec` = META Length (не StreamLength).
2. `ClipEvent::sourceTimeSec` / `AudioGraph` — одна формула reverse SubClip + loop.
3. Waveform + loop notches: первый notch при reverse = `cycle - mediaStart`.
4. Samples: `project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg` (looped) и `…-fx1.veg` (split); sidecars EDL/FCPX/interchange; см. `SAMPLES/veg_project/README.md`.

### Фаза 8 — VST3 real (нужно)

1. CMake `OPENVGAS_VST3_SDK_PATH` → полная сборка host (сейчас только `#ifdef` stub path).
2. Instantiate / `process` / state ↔ `FxSlot.state`.
3. Embed editor (`IPlugView` → Qt widget / HWND).
4. Сопоставление VEG `(VST3, 64 Bit)` + восстановление chunk.

### Фаза 9 — OFX real (нужно)

1. Заменить stub `OfxHost::load` на реальный host (не proprietary Vegas internals).
2. Process в `VideoCompositor` pipeline (после builtins / вместо stub chain).
3. Params UI + VEG `{Svfx:}` / `OFX:` → живой slot.
4. Тесты на open-source OFX sample (не Vegas DLL).

### Фаза 10–12 — polish / optional

- Mask interpolate между KF; Track Motion Shadow/Glow; blend modes.
- VEG state для VST2/3 и OFX.
- Дальнейший encode quality / linked mux / GPU — по необходимости, не блокер MVP.

---

## Фазы 1–6 — детали реализации (архив)

### Фаза 1

1. Preferences: **Vegas Pro Program Files** + QSettings `plugins/vegasProPath`
2. `PluginScanner` — приоритет путей
3. `src/io/FFmpegEncoder` — `findFfmpeg`, `QProcess`, audio + MP4
4. `src/media/MediaEngine` — `renderProject(...)`: WAV → (frames) → ffmpeg
5. `MainWindow::onRenderAs` — не Wave → MediaEngine
6. Catch2 `openvegas_media_tests`

### Фаза 2

1. `src/plugins/OfxHost` — discover/describe `.ofx.bundle`
2. `PluginScanner::scanDirectory` → `OfxHost::discoverInRoot`
3. Тесты `tests/test_ofx_host.cpp`
4. FFmpeg: fallback рядом с приложением (Windows portable)

### Фаза 3

1. `Vst3Host::hasSdk()` + create/process/state stub
2. CMake `OPENVGAS_VST3_SDK_PATH` → `OPENVGAS_HAS_VST3_SDK`
3. Тесты `tests/test_vst3_host.cpp`
4. Реальный instantiate/process/editor — **фаза 8**

### Фаза 4

1. `ColorCorrectorApply` — brightness/contrast/saturation/gamma + Color Grading
2. `VideoCompositor::compose` после Pan/Crop
3. UI: Color Corrector в `VideoEventFxDialogExact`
4. `videoFxSlotFromName` / VEG `colorcorrector` → Builtin
5. Тесты в `openvegas_video_tests`

### Фаза 5

1. `FFmpegStreamDecoder` — continuous raw RGB
2. `VideoFrameCache` через stream decoder
3. Optional `OPENVGAS_FFMPEG` (libav)
4. Тесты `tests/test_ffmpeg_stream_decoder.cpp`

### Фаза 6

1. Decode: `-hwaccel auto` (`media/hwAccel`, `OPENVGAS_HWACCEL`); retry без HW
2. Encode: probe → `h264_nvenc` / `h264_qsv` / `h264_amf`, иначе `libx264`
3. Encode runtime fallback на soft

---

## Краткий вердикт

**Базовый video/audio stack (фазы 0–6) готов:** playback, builtins, VST2, continuous decode, HW, MediaEngine render, Color Corrector MVP, OFX/VST3 stubs.

**Сделано поверх плана:** reverse/loop SubClip, render progress UI, timeline zoom, Windows MinGW path + qmake sync.

**Дальше по смыслу:** VST3 SDK host → OFX process → video polish / VEG state. Не путать discover/stub с реальным hosting.
