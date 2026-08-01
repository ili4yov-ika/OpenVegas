# Открытие `.veg` (кратко)

Импортёр проектов **VEGAS Pro 22** — **VegReader v1** (тайминги из бинарных блоков + fallback v0).

Развёрнуто: [`MARKDOWN/VEG_READER_V0.md`](../MARKDOWN/VEG_READER_V0.md).  
Реверс формата: [`SAMPLES/docs_veg/`](../SAMPLES/docs_veg/README.md).

## Как открыть

| Способ | Действие |
|--------|----------|
| Меню | File → Open… (`*.veg`) |
| Welcome | Browse… или double-click по `SAMPLES/example_project_*.veg` |
| CLI | `OpenVegas.exe path\to\project.veg` |

## Что читается (v1)

- Magic `riff` + header (version, sample rate, fps, tempo, frame size)
- UTF-16 пути медиа (`.mp4`, `.wav`, …) + расширенный relink рядом с `.veg` / `SAMPLES`
- Лейблы событий, Track FX / Event FX строки
- **Timeline:** `start` / `length` / `playback rate` из паттерна ticks÷1e7 (см. `docs_veg`)
- Совпадающие video+audio блоки → A/V groups; overlap → оценка fades
- UI: Project Media, timeline zoom-to-fit, предупреждение о missing media

## Код

- `src/io/VegReader.*` — парсер  
- `ProjectModel::applyVegImport()` — модель + tracks  
- `MainWindow::openProjectPath()` — UX  

## Ещё не точно

Media in/out отдельно от timeline length, markers, полные CF-кривые, запись `.veg`.

## Проверка

```text
build\OpenVegas.exe SAMPLES\example_project_with_video_and_audio.veg
build\OpenVegas.exe SAMPLES\example_project_with_fades_and_crossfades.veg
```
