# Поддерживаемые файлы

Сводка форматов OpenVegas: проект, медиа, interchange, рендер, служебные.  
Статусы: **done** — работает; **partial** — best-effort / упрощённо; **stub** — пункт UI или план, без полной реализации.

Канонический список расширений медиа: [`src/io/MediaMime.cpp`](../src/io/MediaMime.cpp).  
Открытие `.veg`: [`docs/VEG_OPEN.md`](VEG_OPEN.md).

---

## Проекты

| Формат | Расширение | Статус | Примечание |
|--------|------------|--------|------------|
| VEGAS Pro project | `.veg` | **done** (open) | VegReader v1; запись `.veg` нет |
| OpenVegas native | `.ovp` | **stub** | Есть в фильтре Open; reader/writer нет |
| Save / Save As | — | **stub** | Меню пока stub |
| Project Archive | папка + `project.json` | **done** (export) | `OpenVegasArchive` v1 + `media_list.txt` + опц. `Media/` + sidecar `.edl`; open archive нет |
| Vegas backup / proxy | `.veg.bak`, `.sfap0` | игнор | Не открываются как проект |

При открытии `.veg`, если рядом есть sidecar Vegas CSV EDL  
`edl-text-file/<basename>.txt` — таймлайн (fades, channels) берётся из него.

---

## Медиа (импорт / DnD / Explorer)

| Kind | Расширения |
|------|------------|
| Video | `.mp4` `.mov` `.mkv` `.avi` `.m4v` `.wmv` `.webm` `.mpg` `.mpeg` `.mts` `.m2ts` `.mxf` |
| Audio | `.mp3` `.wav` `.flac` `.aif` `.aiff` `.ogg` `.m4a` `.bwf` `.wma` |
| Still | `.jpg` `.jpeg` `.png` `.tga` `.bmp` `.tif` `.tiff` `.gif` `.webp` |

- DnD / Project Media / Explorer — полный список выше (`MediaMime::isMediaFile`).
- File → Import Media… — укороченный фильтр диалога: `mp4 mov mkv wav mp3 aif` (+ All).
- Relink / Find Missing — расширенные фильтры (в т.ч. `.ts` `.wave` и т.п. в UI).

Декод: WAV (+ BWF) нативно; прочее видео/аудио — через внешний ffmpeg CLI (если доступен в PATH). Превью still — Qt image IO.

---

## Interchange (File → Import / Export)

| Операция | Файлы | Статус |
|----------|-------|--------|
| Import media from `.veg` | `.veg` | **done** — пути в media pool |
| Import Premiere / AE | `.prproj` | **partial** — scrape путей |
| Export Premiere / AE | `.prproj` | **stub** |
| Import FCP7 / Resolve | `.xml` | **partial** — пути + простой timeline |
| Export FCP7 / Resolve | `.xml` | **done** (упрощённый) |
| Import FCPX | `.fcpxml` | **partial** |
| Export FCPX | `.fcpxml` | **done** (упрощённый) |
| Import EDL | `.edl` `.txt` | **partial** — CMX3600-ish (не Vegas CSV) |
| Export EDL | `.edl` `.txt` | **done** — CMX-style |
| Vegas CSV EDL (sidecar) | `.txt` | **done** — при open `.veg` |
| Import Broadcast Wave | `.wav` `.bwf` | **done** → media pool |
| Import Closed Captions | `.srt` `.vtt` `.scc` | **partial** — cues → markers |
| AAF / OMF | — | нет |

Эталонные экспорты Vegas: [`SAMPLES/veg_project/`](../SAMPLES/veg_project/README.md)  
(`edl-text-file/`, `final-cut-pro-7_davinci-resolve/`, `final-cut-pro-x/`, `premiere_after-effect/`).

---

## Render / Bounce

Каталог шаблонов (Vegas-style имена): [`src/io/RenderTemplateCatalog.cpp`](../src/io/RenderTemplateCatalog.cpp).  
В UI перечислены AAC, ProRes, MP4/HEVC/AV1, MXF, FLAC, Image Sequence и др.

| Что реально кодируется | Файл | Код |
|------------------------|------|-----|
| Wave (Microsoft) PCM | `.wav` | `AudioEngine::renderToWav` |
| Bounce Audio Mixdown | `.wav` | то же |

Остальные форматы Render As — **UI only** до FFmpeg-encode (см. [`MARKDOWN/ISSUES_AND_PLANS.md`](../MARKDOWN/ISSUES_AND_PLANS.md)).

---

## Служебные и прочее

| Тип | Файлы | Статус |
|-----|-------|--------|
| Peak cache (Vegas) | `.sfk` (`SFPK`) | **done** (read) — waveform; запись нет |
| Captions | `.srt` `.vtt` `.scc` | **partial** → markers |
| Extract Audio from CD | выход `.wav` | **done** (Windows CDDA) |
| Иконки / ресурсы | `.svg` `.png` `.ico` `.qss` | приложение |

Кандидаты peaks: `file.ext.sfk` и `basename.sfk` рядом с медиа  
([`MediaWaveformCache`](../src/io/MediaWaveformCache.cpp)).

---

## Что не поддерживается (осознанно)

- Запись родного `.veg` / `.ovp`
- Полный round-trip Premiere / AAF / OMF
- Encode видео/аудио кроме WAV
- Открытие `.veg.bak` / `.sfap0` как проектов

---

## Код (точки входа)

| Область | Файлы |
|---------|--------|
| Медиа allow-list | `src/io/MediaMime.*` |
| Open `.veg` | `src/io/VegReader.*`, `ProjectModel::applyVegImport` |
| Interchange | `src/io/ProjectInterchange.*` |
| Диалоги фильтров | `src/app/MainWindow.cpp` |
| Render templates | `src/io/RenderTemplateCatalog.*`, `src/ui/RenderAsDialog.*` |
| Peaks | `src/io/MediaWaveformCache.*` |
