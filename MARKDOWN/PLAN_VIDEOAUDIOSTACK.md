# Video / Audio Stack — план

Поэтапный roadmap поверх MVP. Kdenlive (`thirdparty/kdenlive`, gitignore) — **референс** тайминга/архитектуры, не форк. Vegas Pro runtime (`SAMPLES/VEGAS-PRO-22-PROGRAM-FILES`) — **справочник** OFX/иконок для отладки, не LoadLibrary proprietary hosts.

См. также: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md), [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md) (VST/OFX/Builtin), `SAMPLES/veg_project/README.md` (эталоны VEG + interchange).

**Обновлено:** 2026-08-03 (transport clock, interchange, plugins MVP + VST3 IPlugView / Event FX UX / VEG Glint).

---

## Цель

Единый **MediaEngine**-фасад: playback (часы = audio), offline render, пути плагинов (VST / OFX / Vegas folder), unit-тесты. Плагины: Builtin Delay/Reverb, VST3 lean host, OFX process + video FX chain — см. [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md).

## Non-goals

- Порт MLT/Kdenlive без зависимости
- Загрузка DLL внутренних FX Vegas как «своих»
- Полный realtime 4K multi-layer GPU compositor в фазах 1–6
- Native `.prproj` write (Premiere/AE) — пока stub; импорт путей best-effort

---

## Текущее состояние (инвентаризация)

| Область | Статус | Где / заметки |
|---------|--------|---------------|
| Audio playback / graph / mixer | **Done** | `src/audio/*`; Track→Bus→Master; meters |
| Builtin audio DSP | **Done** | Gate / EQ / Comp / Chorus / **Delay / Reverb** |
| VST2 / VST1 process + editor | **Done** | VeSTige + HWND editor + stretch (child HWND fill) |
| VST3 | **Done lean + IPlugView** | SDK `thirdparty/vst3sdk`; process/state/editor; CI `.vst3` fixture backlog |
| OFX | **Done MVP** | load+process + emulated Soften/Invert/Sepia; `Gain.ofx` fixture |
| Video preview compositor | **Done** | soft CPU: Pan/Crop, Track Motion, Color + `applyVideoFxChain` |
| Continuous video decode | **Done** | `FFmpegStreamDecoder` raw pipe; optional linked libav |
| A/V transport clock | **Done** | AudioEngine master; `wireTransportButtons` после device; stop at timeline end; click-seek + `m_seekEpoch` |
| Event Gain / Level live | **Done** | `applyLiveMixer` + `liveAudioParamsChanged` (раньше только на rebuild) |
| Video Color Corrector / Grading | **Done (MVP)** | `ColorCorrectorApply` в `VideoCompositor` |
| Render Wave PCM | **Done** | `AudioEngine::renderToWav` |
| Render AAC/MP4/… | **Done** | `MediaEngine` + FFmpeg CLI; HW prefer |
| HW decode/encode | **Done** | `-hwaccel auto`; NVENC/QSV/AMF → libx264 |
| Reverse / Loop SubClip (EDL) | **Done** | `sourceTimeSec` + `AudioGraph` + waveform notches; см. samples README |
| NLE interchange import | **Done (MVP)** | Vegas CSV EDL, FCP7/Resolve XML, FCPX, Premiere path scrape; `applyInterchangeEvents` |
| NLE interchange export | **Done (MVP)** | Vegas CSV, FCP7+pathurl/fades, FCPXML assets; CMX `.edl`; Premiere write — stub |
| Render progress UI | **Done** | `RenderingProgressDialog` (elapsed / ETA / cancel) |
| Timeline ruler zoom UX | **Done** | Fit / Zoom In-Out; Ctrl+колёсико; sync `m_pxPerSec` |
| Build: CMake MinGW path | **Done** | `build/Windows_MinGW-x64` (preset `windows-mingw-debug`) |
| Build: Qt Creator qmake | **Done** | `OpenVegas.pro` синхронизирован с CMake; `user32` / `winmm` |
| Shadow / Glow, blend modes, mask interpolate | **Not started** | video polish |
| VEG VST/OFX full state restore | **Partial** | `CcnK` → chunk; Event FX: Glint XML + skip Magix AutoFrame; полный blob backlog |
| Premiere `.prproj` write | **Not started** | UI stub → использовать FCP7 XML / EDL |

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
- Interchange: `ProjectInterchange` ↔ File→Import/Export; полный mapping клипов через `ProjectModel::applyInterchangeEvents` (не только `addMediaAt`).

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
| `openvegas_media_tests` | FFmpegEncoder smoke, PluginScanner, OfxHost, Vst3Host, stream decoder, **`[interchange]`** |

---

## Фазы 7+ — что ещё нужно

| Фаза | Содержание | Приоритет | Статус |
|------|------------|-----------|--------|
| **7a** | Reverse / Loop media model (SubClip EDL + waveform + AudioGraph) | High | **Done** (2026-08) |
| **7b** | Render progress + cancel + ETA | High | **Done** |
| **7c** | Timeline zoom / Fit / ruler sync | Med | **Done** |
| **7d** | Tooling: единый Windows MinGW build dir + qmake `.pro` sync | Med | **Done** |
| **7e** | Transport clock polish (seek race, click-seek, stop at end, live event gain) | High | **Done** (2026-08-03) |
| **7f** | NLE interchange MVP (Vegas CSV / FCP7 / FCPX / Premiere scrape + export) | High | **Done** (2026-08-03) |
| **8** | Real **VST3** (Steinberg SDK): instantiate, process, state, `IPlugView` editor | High | **Done** (CI fixture backlog) |
| **9** | Real **OFX** host: LoadLibrary/dlopen, process frame, params UI | High | **Done MVP** (+ emulated Soften/Invert) |
| **10** | Video polish: Soften/Shadow/Glow, blend modes, mask interpolate | Med | Soften emulated; rest **Not started** |
| **11** | VEG: восстановление VST/OFX state chunks; Event FX chain truth | Med | **Partial** (`CcnK` + Glint/AutoFrame recovery) |
| **12** | Linked encode path / ProRes quality / GPU compositor (опционально) | Low | **Not started** |
| **13** | Native Premiere `.prproj` write (опционально) | Low | **Not started** |

### Фаза 7a — детали (сделано)

1. EDL reverse: `StreamStart` = in-point на **reversed** item; `mediaLengthSec` = META Length (не StreamLength).
2. `ClipEvent::sourceTimeSec` / `AudioGraph` — одна формула reverse SubClip + loop.
3. Waveform + loop notches: первый notch при reverse = `cycle - mediaStart`.
4. Samples: `project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg` (looped) и `…-fx1.veg` (split); sidecars EDL/FCPX/interchange; см. `SAMPLES/veg_project/README.md`.

### Фаза 7e — transport / live mix (сделано)

1. `wireTransportButtons` **после** создания `AudioEngine` (иначе нет `positionChanged` → залипание playhead/video).
2. Stop на `timelineEndSec` (кроме loop region); UI `playingChanged(false)`.
3. Click-seek во время Play: не эмитить `playheadChanged` из play-timer при external clock; `m_seekEpoch` в `processBlock`.
4. Event Level/Gain + fades: `AudioGraph::applyLiveMixer` копирует clip gain/fades; Timeline `liveAudioParamsChanged` → `syncMixerLive`.

### Фаза 7f — NLE interchange (сделано)

| Формат | Import | Export |
|--------|--------|--------|
| Vegas EDL Text (`;` CSV) | File→Import EDL автодетект + Open `.veg` sidecar; `applyInterchangeEvents` | `.txt` по умолчанию (`exportVegasCsvEdl`) |
| CMX3600 EDL | `importEdl` fallback | `.edl` |
| FCP7 / Resolve xmeml | sequence clipitems, transitions→fades, `pathurl` | clipitems + `pathurl` + fade transitions |
| FCPX | `<video>`/`<audio>`, offset/mediaStart, timeMap reverse, fades; не ломать kind audio на mp4 | assets + video/audio (не gap) |
| Premiere `.prproj` | path scrape (в т.ч. пробелы в имени); без timeline | stub → FCP7/EDL |

Эталоны: `SAMPLES/veg_project/{edl-text-file,final-cut-pro-7_davinci-resolve,final-cut-pro-x,premiere_after-effect}/`.  
Тесты: `tests/test_project_interchange.cpp` (`[interchange]`).

**Ограничения MVP:** Mixing Console / Track Motion / Pan-Crop KF / Color Grading / Event FX из interchange **не** восстанавливаются (как в Vegas export logs) — канон: Open `.veg`.

### Фаза 8 — VST3 real (сделано)

1. CMake / auto `thirdparty/vst3sdk` → `OPENVGAS_HAS_VST3_SDK` lean host.
2. Instantiate / `process` / state ↔ `FxSlot.state` / chunk.
3. Embed editor: `IPlugView` → HWND Qt parent; stretch при `canResize()`.
4. Backlog: open-source `.vst3` CI fixture; полный `Module`/`PlugProvider` stack.

### Фаза 9 — OFX real (сделано MVP)

1. `OfxHost` load + process + emulated Soften/Invert/Sepia.
2. `applyVideoFxChain` в `VideoCompositor`.
3. Params UI (generic) + VEG `{Svfx:}` / XML Glint recovery.
4. Fixture `Gain.ofx` в tests.

### Фаза 10–13 — polish / optional

- Mask interpolate; Track Motion Shadow/Glow; blend modes.
- Полный VEG OFX/VST3 proprietary blob reverse.
- Encode quality / GPU compositor / native `.prproj` write — по необходимости.

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

**Базовый video/audio stack (фазы 0–6) готов:** playback, builtins (+Delay/Reverb), VST2, VST3+IPlugView, OFX process, continuous decode, HW, MediaEngine render, Color Corrector + video FX chain.

**Сделано поверх плана:** reverse/loop SubClip; render progress; timeline zoom; MinGW path + qmake; **A/V clock + click-seek + live event gain**; **NLE interchange MVP**; **plugins MVP** (Delay/Reverb, VST3 editor, OFX chain, VEG Glint/Event FX UX) — см. `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`.

**Дальше по смыслу:** CI `.vst3` fixture → video polish (Shadow/Glow) → полный VEG blob reverse → (опц.) Premiere write.
