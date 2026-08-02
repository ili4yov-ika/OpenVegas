# `veg_project/` — эталонные проекты VEGAS Pro 22

Каталог **реальных** проектов и interchange-экспортов для OpenVegas: разбор `.veg`, QA импорта/экспорта (EDL / FCP XML / FCPXML / Premiere), сверка fades / trims / crossfades / markers.

Создано в **VEGAS Pro 22**. Файлы `.veg` хранят edit decisions (треки, events, fades, пути к медиа) и **не встраивают** медиаданные.

Подробный реверс бинарного формата (по более ранним сэмплам `example_project_*`): [`../veg_analyzators/`](../veg_analyzators/README.md).

---

## Быстрый старт

```text
# из корня OpenVegas (имя файла тоже резолвится через SAMPLES/veg_project)
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_mix-console.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_mix-console-2.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_track-motion.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_pan-crop.veg
build\OpenVegas.exe sample_for_project_pictures.veg
build\OpenVegas.exe SAMPLES\veg_project\project_sample_for_project_audio.veg
build\OpenVegas.exe project_sample_for_project_audio_trims-and-crossfade.veg
build\OpenVegas.exe project_big--buck-bunny_opacity-gain.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_4x3-preview-and-fades.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_4x3-preview-and-fades_color-grading.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg
build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_576x1024-preview-and-fades.veg
```

Медиа должно лежать рядом с `.veg` (или по абсолютным путям внутри файла). Relink в OpenVegas ищет файлы по имени рядом с проектом, в `SAMPLES/veg_project`, `assets` и export-подпапках (в т.ч. суффиксы `-1` / `- 1`).

При открытии `.veg`, если рядом есть `edl-text-file/<basename>.txt`, таймлайн (треки, fades, CurveIn/Out) берётся из Vegas EDL CSV — точнее, чем бинарный разбор. **Audio Event FX** и Mixing Console buses читаются из UTF-16 строк внутри `.veg` (в EDL их нет).

### Mixing Console samples

Оба проекта — **BBB full length** (~634.57 s), **60 fps**, **48 kHz**; таймлайн как у базового BBB (1× VIDEO + **4** AUDIO lanes из 5.1: `FirstChannel`/`Channels`). При открытии OpenVegas поднимает **Mixing Console**, если в UTF-16 есть Bus / Input / Assignable FX.

| Файл | Каналы микшера (UTF-16) | Назначение |
|------|-------------------------|------------|
| `project_big--buck-bunny_mix-console.veg` | **FX 1**=Chorus, **FX 2**=Volume (без Bus/Input) | Эталон **Assignable FX**; порядок: tracks → FX1 → Master → FX2… |
| `project_big--buck-bunny_mix-console-2.veg` | **Bus A/B**, **Input A–D** (без Assignable FX) | Эталон **Audio Bus** + **Input Bus**; порядок после tracks, до/после Master |

Правило раскладки в OpenVegas: audio tracks только слева; Bus / Input / FX — только после них (можно до и после Master). Interchange (EDL / FCPXML / XML / prproj) тащит только timeline A/V — шины микшера из экспорта не восстанавливаются.

### Track Motion sample

| Файл | Содержание |
|------|------------|
| `project_big--buck-bunny_track-motion.veg` | Короткий клип BBB (~59.8 s, 3840×2160): **Track Motion** на видеодорожке — 8 Position KF + Shadow/Glow каналы |

Открыть `.veg` → ПКМ по заголовку видеодорожки → **Track Motion…**. EDL/FCP/Premiere **не** переносят Track Motion (в логах Vegas — ignored); данные читаются из бинарного списка в `.veg` (GUID pair + записи по 220 / 216 байт). Координаты height-normalized: `x/y/w/h × frameHeight` → пиксели UI.

### Pan/Crop sample

| Файл | Содержание |
|------|------------|
| `project_big--buck-bunny_pan-crop.veg` | Клип BBB **~60.017 s**, **3840×2160**, 60 fps: **Event Pan/Crop** — **12** Position KF (`POSL`/`POSK`) |
| `project_big--buck-bunny_pan-crop_mask.veg` | Тот же клип: **1** Position KF + **Mask** — **7** KF (`MSKL`/`MSKK`/`PATH`/`ANCP`), freehand + rect + oval |

Открыть → клик по иконке Pan/Crop на клипе (или Video Event FX). Interchange **частично** экспортирует motion (FCPX `adjust-transform` / FCP7 Basic Motion), но полный pan/zoom/flip — из бинарного `EPCP`→`POSK` в `.veg`. Поля KF (пиксели кадра): Width / Height / X Center / Y Center / Angle / rotation centers; отрицательные W/H = зеркало. Маска: `EPCP`→`MSKL`→`MSKK`/`MSKB` (время, тики `/1e7`) → `PTHL`/`PATH`/`ANCP` (якоря: x/y + in/out absolute → relative tangents). EDL таймлайн даёт только длину клипа — без Pan/Crop.

### Color Grading sample

| Файл | Содержание |
|------|------------|
| `project_big--buck-bunny_4x3-preview-and-fades_color-grading.veg` | Как **4x3 + fades**, плюс **Video Track FX → Color Grading** (`{Svfx:com.vegascreativesoftware:colorgrading}`) |

Открыть `.veg` → ПКМ по видеодорожке → **Color Grading** / **Track FX**. Параметры в этом сэмпле — **identity** (Lift/Offset 0, Gamma/Gain 1, curves 2 точки 0→1); эталон наличия плагина и импорта в `FxSlot`. EDL/FCP/Premiere Color Grading **не** переносят (в FCP7 логе: `Effects for track … ignored`).

### Reverse + Event FX sample

| Файл | Содержание |
|------|------------|
| `project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg` | **640×480**, BBB ~**232.8 s** **reversed** + fades; Video Event FX (Auto Frame / Chroma Blur / Sepia); wav clip + Audio Event FX (Pro-C 2 / Fresh Air / Auto-Key) |

Открыть → видео играет **задом наперёд** (первые ~232.8 s источника). Reverse: `META:\SubClip\{…} (1)[0][…][1]` + label `(reversed)`; в EDL `PlayRate=1`, `StreamStart≈401.78 s` (quirk полного reverse-subclip). FCPX: `timeMap` 401.77→634.55 ⇒ value 232.77→0. Interchange **игнорирует** event/track FX (Premiere log: `Effects for event/track … ignored`).

---

## Структура

```
veg_project/
├── README.md
│
├── big-buck-bunny_video-60fps-4k.mp4   # ~726 MB, 60 fps, 4K (основное видео)
├── big-buck-bunny_video-60fps-4k.mp4.sfk
├── sample_for_project_audio.wav         # короткий wav (~10.3 s)
├── sample_for_project_audio.sfk
├── sample_for_project_picture_1.png     # still 3840×2160 (~6.1 MB)
├── sample_for_project_picture_2.png     # still 3840×2160 (~6.4 MB)
│
├── project_big--buck-bunny.veg
├── project_big--buck-bunny_fades.veg
├── project_big--buck-bunny_markers.veg
├── project_big--buck-bunny_trims-and-crossfade.veg
├── project_big--buck-bunny_opacity-gain.veg   # event opacity % + audio gain
├── project_big--buck-bunny_mix-console.veg    # Mixing Console: Assignable FX (Chorus, Volume)
├── project_big--buck-bunny_mix-console-2.veg  # Mixing Console: Bus A/B + Input A–D
├── project_big--buck-bunny_track-motion.veg   # Track Motion: Position/Shadow/Glow KF
├── project_big--buck-bunny_pan-crop.veg       # Event Pan/Crop: 12 Position KF
├── project_big--buck-bunny_pan-crop_mask.veg  # Event Pan/Crop Mask: 7 Mask KF
├── project_big--buck-bunny_4x3-preview-and-fades.veg  # 640×480 4:3 + event fades
├── project_big--buck-bunny_4x3-preview-and-fades_color-grading.veg  # 4:3 + fades + Color Grading
├── project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg  # 4:3 reverse + fades + event FX
├── project_big--buck-bunny_576x1024-preview-and-fades.veg  # 576×1024 portrait + fades
├── project_sample_for_project_audio.veg
├── project_sample_for_project_audio_trims-and-crossfade.veg
├── sample_for_project_pictures.veg      # два still по 5 s
│
├── edl-text-file/                       # Export → EDL Text File (*.txt) — CSV Vegas
├── final-cut-pro-7_davinci-resolve/     # Export → FCP7 / Resolve (*.xml) + копии медиа
├── final-cut-pro-x/                     # Export → FCPX (*.fcpxml) + копии медиа
└── premiere_after-effect/               # Export → Premiere/AE (*.prproj) + копии медиа
```

В подпапках экспорта рядом с XML/FCPXML/prproj лежат **копии/дубликаты медиа** (с суффиксами `-1`, `-2` и т.п.) и `*.log` от Vegas — это артефакты экспорта, не отдельные проекты.

---

## Медиа

| Файл | Назначение | Примечание |
|------|------------|-----------|
| `big-buck-bunny_video-60fps-4k.mp4` | Видео Big Buck Bunny | ~726 MB; 60 fps; длительность источника ≈ **634.57 s** |
| `sample_for_project_audio.wav` | Короткий аудиоклип | ≈ **10.285 s**; 48 kHz |
| `sample_for_project_picture_1.png` | Still #1 | **3840×2160**; ~6.1 MB |
| `sample_for_project_picture_2.png` | Still #2 | **3840×2160**; ~6.4 MB |
| `*.sfk` | Peak-кэш Vegas | Не звук; см. [`../veg_analyzators/02_sfk_peak_files.md`](../veg_analyzators/02_sfk_peak_files.md) |

Тот же wav/mp4 для UI-макетов продублирован в [`../assets/`](../assets/) и иногда в корне `SAMPLES/` — для `.veg` канонические пути указывают сюда, в `veg_project/`. PNG stills канонически лежат рядом с `.veg` (и копируются в export-подпапки).

---

## Проекты `.veg`

Все сэмплы: **VEGAS 22**, sample rate **48000**.  
Big Buck Bunny / pictures — **60.000 fps**; audio-only — **≈29.970 fps**.

| Файл | Size | fps | Состав (ориентир) | Смысл |
|------|------|-----|-------------------|--------|
| `project_big--buck-bunny.veg` | ~20 KB | 60 | 1× video + paired audio, ~634.6 s | Базовый A/V на полном ролике |
| `project_big--buck-bunny_fades.veg` | ~28 KB | 60 | Несколько video-дорожек одного клипа (~232.8 s) с разными **fade curves** | Эталон форм фейда (Fast/Linear/Slow/Smooth/Sharp); fade-in ≈37.1 s, fade-out ≈41.1 s |
| `project_big--buck-bunny_markers.veg` | ~23 KB | 60 | Клип ~118.7 s + маркеры на линейке | Markers / ruler |
| `project_big--buck-bunny_trims-and-crossfade.veg` | ~34 KB | 60 | **6** video + **6** audio сегментов | Trims + цепочка; есть overlap (CF) между поздними клипами |
| `project_big--buck-bunny_opacity-gain.veg` | ~24.6 KB | 60 | **4** video + **4** audio (5.1 lanes), ~**89.87 s** | Эталон **event opacity** (video) и **event gain** (audio) |
| `project_big--buck-bunny_mix-console.veg` | ~24.0 KB | 60 | BBB full (~634.6 s) + **Assignable FX** Chorus/Volume | Эталон FX-каналов Mixing Console |
| `project_big--buck-bunny_mix-console-2.veg` | ~27.7 KB | 60 | BBB full (~634.6 s) + **Bus A/B** + **Input A–D** | Эталон Bus / Input Bus в Mixing Console |
| `project_big--buck-bunny_track-motion.veg` | ~12.8 KB | 60 | Клип ~59.8 s + **Track Motion** KF | Эталон Track Motion (Position/Shadow/Glow) |
| `project_big--buck-bunny_pan-crop.veg` | ~12.3 KB | 60 | Клип ~60.0 s + **Event Pan/Crop** 12 KF | Эталон Event Pan/Crop (`POSK`) |
| `project_big--buck-bunny_pan-crop_mask.veg` | ~22.5 KB | 60 | Клип ~60.0 s + Pan/Crop **Mask** 7 KF | Эталон Mask (`MSKK`/`ANCP`) |
| `project_big--buck-bunny_4x3-preview-and-fades.veg` | ~14.0 KB | **29.97** | BBB ~**232.8 s** в проекте **640×480 (4:3)** + fades | Эталон **4:3 preview** + event fades на A/V |
| `project_big--buck-bunny_4x3-preview-and-fades_color-grading.veg` | ~20.1 KB | **29.97** | Тот же 4:3/fades + **track Color Grading** | Эталон Video Track FX Color Grading |
| `project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg` | ~40.0 KB | **29.97** | 4:3 + **reverse** BBB + fades + video/audio **Event FX** + wav | Эталон reverse SubClip + event FX chains |
| `project_big--buck-bunny_576x1024-preview-and-fades.veg` | ~14.0 KB | **29.97** | BBB ~**232.8 s** в проекте **576×1024** (portrait) + fades | Эталон **вертикальный preview** + Match Output Aspect + fades |
| `project_sample_for_project_audio.veg` | ~17.1 KB | 29.97 | 1 audio ~10.3 s + **Audio Event FX** | Эталон **Audio Event FX** на клипе |
| `project_sample_for_project_audio_trims-and-crossfade.veg` | ~13 KB | 29.97 | **5** audio-сегментов | Audio trims + overlaps/fades |
| `sample_for_project_pictures.veg` | ~12.5 KB | 60 | **2× still** PNG на video track, по **5.0 s**, без audio | Эталон still / image events (слайдшоу) |

### Раскладка `project_big--buck-bunny_4x3-preview-and-fades`

Проект **640×480** (aspect **4:3**), **≈29.970 fps**, sample rate **48000**, VEGAS **22**.  
Размер `.veg` ≈ **13.96 KB** (`md5 fd52e414…`). Источник — 4K/60 fps `big-buck-bunny_video-60fps-4k.mp4`; **frame size проекта** SD 4:3 (не 3840×2160). Preview letterbox’ит 16:9 → 4:3.

В бинарнике frame size **640×480 @ 0x242** (шаг скана 2 байта; иначе ложный fallback 1920×1080). Также встречаются 1920×1080 / 3840×2160 (медиа/пресеты) — не проектный кадр.

| # | MediaType | Length | Fade in / out | CurveIn / Out |
|---|-----------|--------|---------------|---------------|
| 1 | VIDEO | **232.7833 s** (тики @ `0x14D8`…) | **22.7561** / **28.6127** s (`0x1548` / `0x1550`) | **2** / **−2** |
| 2 | AUDIO | то же (`0x1D80`…) | **36.5952** / **36.9098** s (`0x1DF0` / `0x1DF8`) | **2** / **−2** |

Audio: `FirstChannel`/`Channels` = **0 / 2**. Track FX (UTF-16): Noise Gate / EQ / Compressor. Также строки: `Preview`, `Master`, `Main Timeline`, `Microsoft Sound Mapper`, `Empty Graph`.

Sidecar: **`edl-text-file/…_4x3-preview-and-fades.txt`** (нужен для fades — из бинарника OpenVegas пока не читает FadeTime*).

Interchange (переэкспорт 2026-08-01 ≈23:44):

| Формат | Файл | Заметки |
|--------|------|---------|
| Vegas EDL | `edl-text-file/…_4x3-preview-and-fades.txt` | VIDEO+AUDIO, fades/curves как выше |
| FCP7 / Resolve | `final-cut-pro-7_davinci-resolve/…xml` | sequence **640×480**, media **3840×2160**; media-копия `… - 12.mp4` |
| FCPX | `final-cut-pro-x/….fcpxml` | format `640x480p2997`; fades в `adjust-blend` / `adjust-volume`; **`adjust-transform` scale=`1.333333 1`** (вписывание 16:9 → 4:3) |
| Premiere / AE | `premiere_after-effect/….prproj` | + `.log` |

Открыть: `build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_4x3-preview-and-fades.veg` → **640×480**, **29,97 fps**; длинные fade-in/out на A/V.

### Раскладка `project_big--buck-bunny_4x3-preview-and-fades_color-grading`

База — **`…_4x3-preview-and-fades`** (640×480 @ **29.97**, BBB ~**232.8 s**, те же fades).  
Размер `.veg` ≈ **20.08 KB** (`md5 642b85c0…`); дельта ≈ **+6.1 KB** — blob OFX Color Grading.

В UTF-16: `{Svfx:com.vegascreativesoftware:colorgrading}`, пресет `(Default)`, параметры `Lift_*` / `Gamma_*` / `Gain_*` / `LogWheel_*` / `Curve_*` / `LUT*`. В этом файле колёса и кривые — **identity** (после имени параметра double: Lift/Offset **0**, Gamma/Gain **1**; `Curve_Y` = `2:0.0:0.0:…:1.0:1.0:…`). OpenVegas кладёт слот **Color Grading** в FX-цепочку **видеодорожки** и читает `lift|gamma|gain|offset.{r,g,b,y}` + `curve.rgb`.

| # | MediaType | Length | Fade in / out | CurveIn / Out |
|---|-----------|--------|---------------|---------------|
| 1 | VIDEO | **232.7833 s** | **22.7561** / **28.6127** s | **2** / **−2** |
| 2 | AUDIO | то же | **36.5952** / **36.9098** s | **2** / **−2** |

Sidecar: **`edl-text-file/…_color-grading.txt`** (таймлайн/fades; grading в EDL нет).

Interchange (экспорт 2026-08-02 ≈03:48):

| Формат | Файл | Заметки |
|--------|------|---------|
| Vegas EDL | `edl-text-file/…_color-grading.txt` | VIDEO+AUDIO, fades; **без** grading |
| FCP7 / Resolve | `final-cut-pro-7_davinci-resolve/….xml` | `Effects for track … ignored` |
| FCPX | `final-cut-pro-x/….fcpxml` | timeline only |
| Premiere / AE | `premiere_after-effect/….prproj` | + `.log` |

Открыть: `build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_4x3-preview-and-fades_color-grading.veg` → **640×480** + Color Grading на video track.

### Раскладка `project_big--buck-bunny_4x3-preview-reverse-fades-fx`

Проект **640×480** @ **≈29.970 fps**, sample rate **48000**, VEGAS **22**.  
Размер `.veg` ≈ **40.05 KB** (`md5 67a4bee8…`). Frame size **640×480 @ 0x250**.

| # | MediaType | File | Start | Length | StreamStart | Fade in / out | Примечание |
|---|-----------|------|-------|--------|-------------|---------------|------------|
| 1 | VIDEO | BBB mp4 | 0 | **232.7833 s** | **401.7833 s** | **22.756** / **28.613** s | **reversed** (первые 232.8 s источника задом) |
| 2 | AUDIO | BBB mp4 | 0 | **232.7833 s** | 0 | **36.595** / **0.010** s | paired audio (fade-out короткий) |
| 3 | AUDIO | `sample_for_project_audio.wav` | **230.838 s** | **24.100 s** | **8.191 s** | **1.945** / **0.010** s | **reversed** wav + Audio Event FX |

UTF-16 Video Event FX: `{Svfx:de.magix:autoframe}`, `{Svfx:com.vegascreativesoftware:chromablur}`, `{Svfx:com.vegascreativesoftware:sepia}` (+ preset «Soft Moderate Contrast»).  
Audio Event FX (на wav): FabFilter Pro-C 2 (VST2), Fresh Air (VST2), Auto-Key (VST3).  
Track FX (audio): VEGAS Track Noise Gate / EQ / Compressor.

OpenVegas: `TrackEvent::reversed` + `sourceTimeSec()`; video event FX → цепочка первого video event; audio event FX → только audio-only файлы (wav).

Sidecar: **`edl-text-file/…_reverse-fades-fx.txt`**.

Interchange (экспорт 2026-08-02 ≈04:40–04:46):

| Формат | Файл | Заметки |
|--------|------|---------|
| Vegas EDL | `edl-text-file/…_reverse-fades-fx.txt` | StreamStart/fades; PlayRate=1; FX нет |
| FCP7 / Resolve | `final-cut-pro-7_davinci-resolve/….xml` | (+ `.log`) |
| FCPX | `final-cut-pro-x/….fcpxml` | **`timeMap`** reverse на video + wav |
| Premiere / AE | `premiere_after-effect/….prproj` | FX ignored; media-копии `-16.mp4` / `-3.wav` |

Открыть: `build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg`.

### Раскладка `project_big--buck-bunny_576x1024-preview-and-fades`

Проект **576×1024** (portrait ≈ **9:16**), **≈29.970 fps**, sample rate **48000**, VEGAS **22**.  
Размер `.veg` ≈ **14.6 KB** (`md5 d2b0221d…`). Клип BBB ~**232.8 s** + fades; кадр проекта вертикальный.

В бинарнике frame size **576×1024 @ 0x24C** (whitelist `scanFrameSize`). Event Pan/Crop — **Match Output Aspect** в media space: каждый Position KF = **1215×2160** @ center **1920×1080** (полный 4K source).

**Эталон интерполяции Position KF** (`POSK` payload `+0x18` u32; Vegas codes: **0** Linear, **1** Fast, **2** Slow, **3** Smooth, **4** Sharp, **5** Hold):

| # | time (s) | type code | Interpolation |
|---|----------|-----------|---------------|
| 1 | 0.000 | 0 | Linear |
| 2 | 23.600 | 3 | Smooth |
| 3 | 38.167 | 2 | Slow |
| 4 | 68.233 | 4 | Sharp |
| 5 | 90.867 | 5 | Hold |
| 6 | 123.167 | 1 | Fast |
| 7 | 155.500 | 0 | Linear |
| 8 | 185.567 | 3 | Smooth |
| 9 | 220.833 | 2 | Slow |

| # | MediaType | Length | Fade in / out | CurveIn / Out |
|---|-----------|--------|---------------|---------------|
| 1 | VIDEO | **232.7833 s** | **22.7561** / **28.6127** s | **2** / **−2** |
| 2 | AUDIO | то же | **36.5952** / **36.9098** s | **2** / **−2** |

Sidecar: **`edl-text-file/…_576x1024-preview-and-fades.txt`**.

Interchange (экспорт 2026-08-02):

| Формат | Файл | Заметки |
|--------|------|---------|
| Vegas EDL | `edl-text-file/…_576x1024-preview-and-fades.txt` | VIDEO+AUDIO, fades как выше |
| FCP7 / Resolve | `final-cut-pro-7_davinci-resolve/….xml` | sequence **576×1024** |
| FCPX | `final-cut-pro-x/….fcpxml` | format `576×1024`; `adjust-transform` scale ≈ `1.333333` / `3.160494` |
| Premiere / AE | `premiere_after-effect/….prproj` | + `.log` |

Открыть: `build\OpenVegas.exe SAMPLES\veg_project\project_big--buck-bunny_576x1024-preview-and-fades.veg` → **576×1024**, **9** Position KF с разными кривыми; ПКМ по diamond → Cut/Copy/Paste/Delete + тип интерполяции.

### Раскладка `sample_for_project_pictures`

Проект **3840×2160**, **60 fps**, только video track.

| # | Файл | start (s) | length (s) | end (s) |
|---|------|-----------|------------|---------|
| 1 | `sample_for_project_picture_1.png` | 0.000 | 5.000 | 5.000 |
| 2 | `sample_for_project_picture_2.png` | 5.000 | 5.000 | 10.000 |

Итого timeline ≈ **10 s**. В Vegas EDL: `MediaType=VIDEO`, `Looped=TRUE`, `CurveIn/Out=4` (Smooth), fades 0. Stills в Vegas лежат на video-дорожке как VIDEO-events — OpenVegas определяет **still** по расширению `.png`.

### Раскладка `…_trims-and-crossfade` (BBB, video track)

| # | start (s) | length (s) | end (s) | Заметка |
|---|-----------|------------|---------|---------|
| 1 | 0.000 | 23.450 | 23.450 | |
| 2 | 23.450 | 41.183 | 64.633 | стык |
| 3 | 64.633 | 20.017 | 84.650 | |
| 4 | 84.650 | 34.033 | 118.683 | |
| 5 | 118.683 | 53.183 | 171.866 | |
| 6 | 152.700 | 65.500 | 218.200 | **overlap** с #5 → crossfade |

Audio-сегменты зеркалят video (paired A/V).

### Раскладка `project_big--buck-bunny_opacity-gain`

Проект **3840×2160**, **60 fps**, sample rate **48000**. Один источник `big-buck-bunny_video-60fps-4k.mp4`, длина событий ≈ **89.867 s** (`Length` EDL = **89866.6666 ms** ≈ **5392** frames @ 60).

Структура как у базового BBB с 5.1: **4 video tracks** + **4 audio tracks** (каналы 1/2, 3, 4, 5/6). Fades 0 (у audio в EDL крошечный `FadeTimeOut=10 ms`). Track FX в `.veg`: стандартные `VEGAS Track Compressor` / `Noise Gate` / `EQ`.

Ключевое поле Vegas EDL: **`SustainGain`** (линейный множитель 0…1 на «полке» события, не envelope fade).

| # | MediaType | Vegas Track | FirstChannel / Channels | SustainGain | Смысл в UI Vegas |
|---|-----------|-------------|-------------------------|-------------|------------------|
| 1 | VIDEO | 1 | — | **0.786885** | Opacity ≈ **78.7 %** |
| 2 | VIDEO | 2 | — | **0.278689** | Opacity ≈ **27.9 %** |
| 3 | VIDEO | 3 | — | **1.000000** | Opacity **100 %** |
| 4 | VIDEO | 4 | — | **0.000000** | Opacity **0 %** (невидим) |
| 5 | AUDIO | 0 | 0 / 2 → `[Channels 1/2]` | **0.796875** | Event gain ≈ **−2.0 dB** (`20·log10`) |
| 6 | AUDIO | 1 | 2 / 1 → `[Channel 3]` | **0.234375** | ≈ **−12.6 dB** |
| 7 | AUDIO | 2 | 3 / 1 → `[Channel 4]` | **1.000000** | **0.0 dB** |
| 8 | AUDIO | 3 | 4 / 2 → `[Channels 5/6]` | **0.000000** | mute / −∞ (как Level 0) |

Сверка экспортов:

| Формат | Файл | Что внутри |
|--------|------|------------|
| Vegas EDL CSV | `edl-text-file/project_big--buck-bunny_opacity-gain.txt` | 4× VIDEO + 4× AUDIO; `SustainGain` как выше |
| FCP7 / Resolve | `final-cut-pro-7_davinci-resolve/project_big--buck-bunny_opacity-gain.xml` | `opacity` **78.69 / 27.87 / 100 / 0**; `audiolevels`/`level` **0.797 / 0.234 / 1 / 0** |
| FCPX | `final-cut-pro-x/project_big--buck-bunny_opacity-gain.fcpxml` | 4 video + 4 audio lanes, duration `539200/6000s`; opacity/gain в XML почти не отражены |
| Premiere / AE | `premiere_after-effect/project_big--buck-bunny_opacity-gain.prproj` | + `.log`; медиа-копии |

Для OpenVegas: video `SustainGain` → `TrackEvent::opacity`; audio `SustainGain` → линейный множитель → `gainDb = 20·log10(g)` (при `g≈0` — mute / −∞).

### Раскладка `project_sample_for_project_audio`

Проект **≈29.970 fps**, sample rate **48000**, один audio event:

| Поле | Значение |
|------|----------|
| Медиа | `sample_for_project_audio.wav` |
| Start / Length (EDL) | **0** / **10285.3333 ms** ≈ **10.285 s** |
| Track FX (дорожка) | `VEGAS Track Compressor` / `Noise Gate` / `EQ` (стандарт Vegas) |

**Audio Event FX** (цепочка на клипе, порядок из UTF-16 `.veg`):

| # | Plug-in | Формат |
|---|---------|--------|
| 1 | Fresh Air | VST2, 64 Bit |
| 2 | Chorus | Builtin (VEGAS) |
| 3 | Auto-Key | VST3, 64 Bit |
| 4 | FabFilter Pro-Q 4 | VST2, 64 Bit |

OpenVegas при Open читает имена из UTF-16 (пары `Name` + `(VSTn, 64 Bit)` и известные builtin вроде Chorus) → `TrackEvent::fxChain`. Окно **Audio Event FX** показывает цепочку; UI builtin Chorus редактируется, VST — placeholder до host SDK.

Сверка экспортов:

| Формат | Файл | Примечание |
|--------|------|------------|
| Vegas EDL CSV | `edl-text-file/project_sample_for_project_audio.txt` | 1× AUDIO; **FX chain в EDL нет** — только `.veg` UTF-16 |
| FCP7 / Resolve | `final-cut-pro-7_davinci-resolve/project_sample_for_project_audio.xml` | timeline; плагины Vegas/VST обычно не переносятся |
| FCPX | `final-cut-pro-x/project_sample_for_project_audio.fcpxml` | то же |
| Premiere / AE | `premiere_after-effect/project_sample_for_project_audio.prproj` | то же |

### Раскладка `project_sample_for_project_audio_trims-and-crossfade`

| # | start (s) | length (s) |
|---|-----------|------------|
| 1 | 0.000 | 2.736 |
| 2 | 2.736 | 2.402 |
| 3 | 5.138 | 5.147 |
| 4 | 10.277 | 4.605 |
| 5 | ~12.737 | ~5.681 |

---

## Экспорты Vegas (File → Export)

Для **каждого** основного `.veg` (кроме отдельно отмеченных) в подпапках лежат одноимённые экспорты. Это эталоны для OpenVegas **Import/Export**.

| Папка | Формат Vegas | Расширение | Примечание |
|-------|--------------|------------|------------|
| [`edl-text-file/`](edl-text-file/) | EDL Text File | `.txt` | **CSV с `;`**, не CMX3600. Времена в **миллисекундах**. Есть `FadeTimeIn/Out`, `CurveIn/Out` |
| [`final-cut-pro-7_davinci-resolve/`](final-cut-pro-7_davinci-resolve/) | FCP7 / Resolve | `.xml` | xmeml + копии медиа + `.log` |
| [`final-cut-pro-x/`](final-cut-pro-x/) | Final Cut Pro X | `.fcpxml` | + иногда `.xml`; копии медиа + `.log` |
| [`premiere_after-effect/`](premiere_after-effect/) | Premiere / AE | `.prproj` | + `.log`; медиа-копии |

### Поля Vegas EDL CSV (важно для парсера)

Заголовок (фрагмент):

`ID; Track; StartTime; Length; PlayRate; …; MediaType; FileName; …; FadeTimeIn; FadeTimeOut; …; CurveIn; …; CurveOut; …`

- `StartTime` / `Length` / `FadeTime*` — **мс** (делить на 1000 → секунды).
- `CurveIn` / `CurveOut` — код формы фейда Vegas (в `…_fades.veg` встречаются `2`, `1`, `-2`, `4`, `-4` — пять форм кривой).
- `MediaType` — `VIDEO` / `AUDIO`.
- `SustainGain` — уровень «полки» события (0…1): для video ≈ **opacity**, для audio ≈ **линейный gain** (см. `…_opacity-gain`).
- Несколько строк на один визуальный клип возможны (многоканальный audio, несколько video tracks).

Пример: `project_big--buck-bunny_fades.txt` — 5 video-tracks одного источника с разными `CurveIn`/`CurveOut` и длинными fade in/out.

### Экспорты `sample_for_project_pictures` (сверка)

| Формат | Файл | Что внутри |
|--------|------|------------|
| Vegas EDL CSV | `edl-text-file/sample_for_project_pictures.txt` | 2× VIDEO, StartTime 0 / 5000 ms, Length 5000 ms; пути к PNG в `veg_project/` |
| FCP7 / Resolve | `final-cut-pro-7_davinci-resolve/sample_for_project_pictures.xml` | xmeml; sequence **60 fps**, duration **600** frames (=10 s); clipitems start/end **0–300**, **300–600**; копии PNG рядом |
| FCPX | `final-cut-pro-x/sample_for_project_pictures.fcpxml` | sequence `60000/6000s`; два `<video>` по `30000/6000s` (=5 s); assets 3840×2160 |
| Premiere / AE | `premiere_after-effect/sample_for_project_pictures.prproj` | бинарный/zip `.prproj` + `.log`; медиа-копии |

---

## Связь с OpenVegas

| Задача | Что брать отсюда |
|--------|------------------|
| File → Open `.veg` | Любой `project_*.veg` / `sample_for_project*.veg` — старт диалога: `veg_project/` |
| Timeline при Open | Если есть `edl-text-file/<basename>.txt` — таймлайн/fades/curves из Vegas CSV EDL; иначе бинарный `.veg` |
| Stills / pictures | `sample_for_project_pictures.veg` (+ EDL / FCP XML / FCPXML / prproj) |
| File → Import → Media from Project | `.veg` → media pool |
| File → Import → EDL / XML / FCPXML / Premiere | Файлы из подпапок экспорта |
| File → Export → … | Сверять с соответствующими эталонами |
| Fade curve popup | `project_big--buck-bunny_fades.veg` + EDL `CurveIn/Out` |
| Event opacity / audio gain | `project_big--buck-bunny_opacity-gain.veg` + EDL `SustainGain` (+ FCP7 opacity/level) |
| Audio Event FX (клип) | `project_sample_for_project_audio.veg` — UTF-16: Fresh Air / Chorus / Auto-Key / Pro-Q 4 |
| Trims / CF | `…_trims-and-crossfade.veg` (пока без sidecar EDL — бинарный разбор) |
| Markers | `…_markers.veg` |

Реверс бинарных блоков (ticks / 1e7, rate): [`../veg_analyzators/`](../veg_analyzators/README.md).  
Старые карточки `example_project_*` (ещё в analyzators) — предыдущее поколение сэмплов; актуальные проекты для QA — **этот** каталог.

---

## Размер и git

- `big-buck-bunny_video-60fps-4k.mp4` (~726 MB) и копии в export-папках сильно раздувают дерево.
- `VEGAS-PRO-22-PROGRAM-FILES/` уже в [`../.gitignore`](../.gitignore).
- Имеет смысл **не коммитить** 4K-ролик и дубликаты в export-папках, либо хранить LFS / внешнюю ссылку — по политике репозитория.

---

## Лицензия / IP

- Формат `.veg`, экспорты и runtime Vegas — собственность MAGIX / VEGAS; в OpenVegas только как **справочные эталоны**.
- Big Buck Bunny — отдельная лицензия исходного ролика (обычно CC); соблюдать условия распространения медиа.
- Код OpenVegas — GPL-3.0-or-later; не смешивать бинарники Vegas в GPL-дистрибутив.
