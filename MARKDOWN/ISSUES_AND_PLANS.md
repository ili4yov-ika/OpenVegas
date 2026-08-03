# Issues and plans — OpenVegas

Журнал багов, stub-фич и планов. Обновлять при каждой заметной находке (см. [`INIT.MD`](INIT.MD)).

---

## Известные баги

| ID | Описание | Статус | План |
|----|----------|--------|------|
| — | _(пусто)_ | | |

---

## Video Preview pipeline (2026-08-01)

| Фаза | Статус | Содержание |
|------|--------|------------|
| V0 Decode | Done | `VideoFrameCache` + `FFmpegStreamDecoder` continuous raw pipe (optional libav); stills `QImageReader` |
| V1 Compositor | Done | `VideoCompositor` bottom-up SourceOver; opacity × fades (`fadeCurveAmplitude`); mute/solo video |
| V2 KF eval | Done | `VideoKeyframeEval` lerp Pan/Crop + Track Motion (Hold/Linear/Smooth/Sharp/Slow/Fast) |
| V3 Transforms | Done | `PanCropApply` (crop/rotate/flip + mask hold); `TrackMotionApply` (height-normalized) |
| V4 UI wire | Done | `MainWindow::refreshPreviewFrame` + audio-clock frame ticks + last-frame hold + prefetch |
| V4b A/V sync | Done | Audio master clock (как MLT consumer); soft nearest-frame; burst prefetch вперёд |
| V5 Tests | Done | Catch2 `openvegas_video_tests` + `tests/fixtures/video/` |
| V6+ | Partial | continuous decode **Done**; HW encode/decode **Done**; inPoint/outPoint; mask path interpolate; Shadow/Glow; blend modes; OFX process |

**Стек:** software CPU (`QImage`/`QPainter`); **часы = AudioEngine** (как Kdenlive/MLT consumer); видео показывает кадр по `positionChanged` + quantize to fps. Не realtime 4K multi-layer.

---

## Render As (2026-08-01)

| Фаза | Статус | Содержание |
|------|--------|------------|
| UI Vegas parity | Done | Formats/Templates, search, filters, favorites, Folder/Name, free space, Render Options, About/Help |
| Catalog | Done | `RenderTemplateCatalog` (Vegas-style names; AAC LC/HE-AAC templates) |
| Wave export | Done | `Wave (Microsoft)` → `AudioEngine::renderToWav` (+ optional loop region) |
| FFmpeg AAC/MP4/ProRes | Done | MediaEngine + CLI; HW prefer (NVENC/QSV/AMF → libx264); см. [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md) |

---

## Video / Audio Stack roadmap

Полный поэтапный план (MediaEngine, OFX, VST3, HW): [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md).

## Audio roadmap phases (2026-08-01)

| Фаза | Статус | Содержание |
|------|--------|------------|
| P0 Scaffold | Done | `src/audio/`, miniaudio (`thirdparty/miniaudio.h`), `AudioPluginHost` API (`prepare`/`reset`/params/state), CMake `OPENVGAS_AUDIO` |
| P1 Playback | Done | `AudioDecodeCache` (WAV + ffmpeg CLI), `AudioGraph` + `AudioEngine`, transport sync (`TimelineView` external clock) |
| P2 Fades/CF | Done | `FadeCurves` shared UI/DSP; clip fade in/out; overlap via per-clip fades; micro equal-power edges |
| P3 Mixer | Done | Fader/pan/mute/solo → model; **Track→Bus→Master**; live mixer sync; meters |
| P4 Builtin DSP | Done | Gate / EQ / Comp / Chorus in `BuiltinDsp`; Event→Track→Bus→Assignable→Master |
| P5 VST3 | Partial | VST3 stub (SDK path); **VST2/VST1 LoadLibrary + process** via VeSTige (`thirdparty/vestige/aeffectx.h`) |
| P6 Automation | Done | Lanes + menus + runtime; FX slot `fx:N:gainDb` |
| P7 Bounce | Done | File → Bounce Audio Mixdown (offline WAV via same graph) |
| P8 Tests | Done | Catch2 incl. graph mute/fade/live mixer |

**Стек:** decode WAV / ffmpeg CLI (optional linked FFmpeg via pkg-config); device **miniaudio**; graph свой; VST3 SDK optional.

---

## Поддержка плагинов Vegas / VST / OFX — статус (2026-08-01)

**Вердикт:** audio engine + builtin DSP + composite host есть. VST3 process/editor — stub до Steinberg SDK. OFX host — нет. **Video Preview compositor (MVP soft CPU)** — decode + Pan/Crop/Motion/opacity в program monitor.

### Архитектура

| Слой | Файлы | Роль |
|------|-------|------|
| Audio engine | `src/audio/*` | Decode cache, graph, miniaudio device, transport clock |
| Типы / цепочка | `src/plugins/AudioPluginTypes.h` | `PluginFormat` (Builtin/Vst1/Vst2/Vst3/Ofx), `FxSlot` |
| Builtin-каталог | `BuiltinAudioCatalog.*` | Имена Vegas/MAGIX + default track FX |
| Builtin DSP | `src/audio/BuiltinDsp.*` | Gate / EQ / Comp / Chorus process |
| VST scan | `AudioPluginScanner.*` | VST1/VST2 `*.dll`, VST3 `*.vst3` |
| Registry | `AudioPluginRegistry.*` | Builtin + scanned VST |
| Host | `CompositePluginHost` / `Vst3Host` / `NullAudioPluginHost` | Builtin process; VST3 stub (+ `OPENVGAS_VST3_SDK_PATH`) |
| OFX list | `PluginScanner.*` / `OfxHost` | Discover + stub load |
| VEG | `VegReader.*` | Имена `{Svfx:}`, `OFX:`, `(VST2/3)`; бинарь Pan/Crop / Track Motion |
| UI | `PluginChooserDialog`, `AudioEventFxDialog`, … | Chooser + редакторы |

### Внутренние плагины Vegas

| Плагин | Статус |
|--------|--------|
| Event Pan/Crop (+ Mask) | Редактор + VEG; **preview: Pan/Crop KF + mask hold** (`VideoCompositor`) |
| Track Motion | Редактор + VEG; **preview: motion KF** (Shadow/Glow FX — не в v1) |
| Audio builtins (Chorus, Track EQ/Gate/Comp, …) | Каталог + UI; **DSP в playback** (Gate/EQ/Comp/Chorus) |
| Color Corrector / Brightness and Contrast | **preview + UI MVP** (`ColorCorrectorApply`) |
| Color Grading (track) | UI + **preview** (lift/gamma/gain/offset) |
| Прочие video FX (Titles, Blur, …) | Имена / stubs — **косметика** |

### VST1 / VST2 / VST3

| Возможность | Статус |
|-------------|--------|
| Скан (Preferences: отдельные корни VST1 / VST2 / VST3) | Да |
| Instantiation / `LoadLibrary` | **VST2/VST1:** да (VeSTige); VST3: stub до SDK |
| Process / state chunk | **VST2:** `processReplacing` + chunk; Builtin DSP; VST3 pass-through |
| Native editor UI | **VST2:** `effEditOpen` → HWND; VST3 placeholder |
| VEG: имя + формат `(VST\|VST1\|VST2\|VST3, 64 Bit)` | Да; state **не** восстанавливается |

### OFX / Vegas video plug-ins

| Возможность | Статус |
|-------------|--------|
| Список папок `OFX Video Plug-Ins` (+ CMake runtime copy) | Частично |
| Host / process / UI | Нет |
| VEG `{Svfx:}` / `OFX:` → `FxSlot` | Имена в цепочку |
| Video Track FX | OFX chooser-stub |

### Порядок до реального hosting

1. Audio graph → `AudioPluginHost::process` по `fxChain`
2. Реальный **VST3** host (Steinberg SDK): instantiate, SR/blocksize, state ↔ `FxSlot.state`
3. Embed editor (`IPlugView` → Qt)
4. Сопоставление VEG-имён со сканом + state
5. VST2 / VST1 (legacy), затем отдельно OFX (**не** грузить чужие Vegas DLL как «свои»)
6. Параллельно: builtin DSP (EQ/gate/comp) и video compositor для Pan/Crop / Track Motion

---

## Неработающие / отключённые / stub

| Тема | Почему | План |
|------|--------|------|
| Точный timeline из `.veg` | Частично: v1 читает start/length/rate; in/out/markers ещё эвристика | v2: media in/out, markers |
| Запись `.veg` / `.ovp` | Не реализовано | После стабильного reader |
| OFX host | Только `PluginScanner` stub | Отдельная фаза (см. статус плагинов выше) |
| Audio VST3/VST2/VST1 process/editor | Composite host + stubs; SDK optional | Полный VST3 process + IPlugView |
| Video Pan/Crop / Track Motion в preview | MVP soft compositor есть; Shadow/Glow/blend modes/OFX — нет | Video pipeline P1+ |
| Customize Keyboard | Диалог + Vegas-ish catalog (TrackView/List Transport, JKL scrub) | Named maps Save As; добить остальные вкладки |
| Реальный video preview / decode | `VideoFrameCache` + filmstrip; нет GPU / linked decode | P1 realtime / GPU |
| Automation lane UI (draw envelopes) | Model + menus + runtime; нет KF drawing на клипе | KeyframeLane reuse |
| Explorer / Video FX / Generators / Transitions / Notes panels | Placeholder labels / косметический catalog | По static HTML макетам |
| Relink dialog | Silent path search + missing-media warning | UI Relink как в Vegas |

---

## Планы на реализацию

| Приоритет | Задача | Заметки |
|-----------|--------|---------|
| P1 | VegReader v2 — media in/out, markers | `trimmers_markers`, controlled diffs |
| P1 | ISSUES держать в синхроне с TODO в коде | INIT |
| P2 | Fades / crossfades / event groups | Макеты `project-*-crossfade*` |
| P2 | Trimmer + Event Properties на реальные media | |
| P3 | Preview decode + transport sync | **MVP done** — см. Video Preview pipeline ниже |
| P3 | Undo stack | |
| P3 | Audio graph + builtin DSP (gate/EQ/comp) | Нужен для слышимых Track FX |
| P4 | VST3 host (process → state → editor), затем VST2/VST1 | Scaffold scan done; не тащить IP Vegas DLL |
| P4 | OFX host | Отдельно от VST; только открытый OFX API |

---

## Исправлено

| Дата | Что | Как |
|------|-----|-----|
| 2026-08-01 | Video Preview compositor MVP | `src/video/*` + wire preview; см. Video Preview pipeline |
| 2026-08-01 | Статус плагинов Vegas/VST/OFX зафиксирован | Секция выше; Pan/Crop Mask VEG `MSKL/ANCP` |
| 2026-08-01 | Audio plugins scaffold (VST2/VST3 scan, builtin catalog, FxSlot, UI) | Registry + NullAudioPluginHost; Preferences VST Effects |
| 2026-07-30 | VegReader v1 — start/length/rate + open UX | Binary ticks/1e7, A/V pairing, missing-media dialog, zoom-to-fit |
| 2026-07-29 | `Qt::UniqueConnection` + lambda crash | Убран UniqueConnection |
| 2026-07-29 | Welcome / empty project / Project Media / Preview chrome | По static HTML + screenshots |
| 2026-07-29 | Open `.veg` v0 | `VegReader` + `applyVegImport` |

---

*При сомнении — сверяться с `SAMPLES/veg_project/`, скриншотами Vegas и `SAMPLES/screenshots/` (если есть).*
