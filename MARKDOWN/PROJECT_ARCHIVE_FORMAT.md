# OpenVegas Project Archive — родной формат сохранения (`project.json`)

Документ описывает **реализованный** (2026-08-09) round-trip формат сохранения проекта — то, что стоит за
File → Save / Save As / Open, когда открывается не `.veg`, а папка-архив OpenVegas. Раньше `Save`/`Save As…` были
пустыми стабами (см. [`UI_STUBS_AUDIT.md`](UI_STUBS_AUDIT.md)) — Ctrl+S ничего не делал, и путь «сохранить и
вернуться к проекту» отсутствовал вообще.

`.veg` — **закрытый бинарный** формат Vegas Pro; писать его нельзя (риск дать несовместимый/повреждённый файл,
который сам Vegas Pro не откроет). Вместо этого используется **собственный** формат — обычная папка с JSON внутри,
которую сам OpenVegas умеет и писать, и читать обратно. Явно НЕ представляется как настоящий `.veg`.

---

## 1. Зачем и что изменилось

`ProjectInterchange::exportProjectArchive()` существовал и раньше (File → Export → «VEGAS Project Archive»), но писал
только тайминг клипов — **без `fxChain`**. Это значит: сохранить проект с Titles & Text / Media Generator / любым FX,
переоткрыть — и всё содержимое генераторов и эффектов **исчезало** (клипы оставались пустыми). Плюс не было парной
функции импорта вообще — «сохранённое» нельзя было открыть обратно ни в каком виде.

**v2** (`src/io/ProjectInterchange.cpp`) добавляет:
- `fxChain` на треке и на событии — весь список `FxSlot` (opaque `state`-блоб плагина сохраняется как есть, byte-for-byte,
  через base64; сама функция его не разбирает и не обязана понимать конкретный плагин).
- `mediaPath`, `mediaKind`, `mediaStartSec`/`mediaLengthSec`, `looped`/`reversed`, fade-кривые, `opacity`, `gainDb`,
  `firstChannel`/`channelCount` на событии.
- `height`/`muted`/`solo`/`volumeDb`/`pan`/`busId`/`automationMode`/`displayColor` на треке.
- Event Pan/Crop **позиционные** keyframes (`positionKeyframes`).
- Маркеры таймлайна проекта (`ProjectModel::markers()`).
- Парная функция чтения `ProjectInterchange::importProjectArchive()` — раньше отсутствовала вообще.

---

## 2. Формат папки

```
<Имя проекта>/
  project.json        — всё состояние проекта (см. §3)
  media_list.txt       — человекочитаемый дубль медиапула (kind\tname\tpath), не парсится обратно
  <Имя проекта>.edl     — побочный экспорт для интероп (Vegas EDL CSV)
  <Имя проекта>.txt      — то же в generic EDL
  Media/                — копии медиафайлов, только если при экспорте выбрано «Copy media»
```

`project.json` — единственный файл, который читает `importProjectArchive()`; остальные — для удобства/интеропа с
другими программами, не требуются для повторного открытия в OpenVegas.

### 3. Схема `project.json` (v2)

```jsonc
{
  "format": "OpenVegasArchive", "version": 2,
  "title": "...", "sourceProject": "путь к .veg, из которого начинался проект (если был)",
  "frameRate": 29.97, "sampleRate": 48000, "tempoBpm": 120.0, "width": 1920, "height": 1080,
  "markers": [ {"number": 1, "timeSec": 3.5, "label": "..."} ],
  "media": [ {"path": "...", "displayName": "...", "kind": "video|audio|still", "missing": false,
              "archivedPath": "Media/имя (только если Copy media)"} ],
  "tracks": [
    {
      "id": 1, "name": "...", "kind": "video|audio",
      "height": 96, "muted": false, "solo": false, "volumeDb": 0.0, "pan": 0.0, "busId": -1,
      "automationMode": 1,                       // AutomationWriteMode enum int
      "displayColor": "#AARRGGBB",                // опущено, если Track::displayColor невалиден
      "fxChain": [ {"pluginId": "...", "displayName": "...", "format": 0,
                    "bypass": false, "state": "<base64>", "hostKey": "..."} ],
      "events": [
        {
          "id": 1, "name": "...", "mediaPath": "...",
          "startSec": 0.0, "lengthSec": 5.0, "mediaStartSec": 0.0, "mediaLengthSec": 0.0,
          "looped": true, "reversed": false,
          "fadeInSec": 0.0, "fadeOutSec": 0.0, "fadeInCurve": 3, "fadeOutCurve": 3,
          "opacity": 1.0, "gainDb": 0.0,
          "mediaKind": 2,                          // EventMediaKind enum int (2 = Title)
          "groupId": 0, "firstChannel": 0, "channelCount": 0,
          "fxChain": [ /* как у трека */ ],
          "panCrop": { "positionKeyframes": [ {"timeSec":0.0,"width":1920.0,"height":1080.0,
                        "xCenter":960.0,"yCenter":540.0,"angleDeg":0.0,
                        "rotationXCenter":960.0,"rotationYCenter":540.0,
                        "smoothness":0.0,"type":0} ],
                        "maintainAspectRatio": true, "stretchToFillFrame": true }
        }
      ]
    }
  ]
}
```

`fxChain[].state` — **непрозрачный** блоб конкретного плагина (для builtin — обычно JSON-параметры через
`saveParams`/`loadParams`, для VST/OFX — состояние хоста); архив его не интерпретирует, только переносит байты через
base64 туда и обратно. Именно поэтому Titles & Text / Media Generator / Pan-Crop-FX-slot и любой builtin-плагин
переживают round-trip не нуждаясь в отдельном коде на каждый плагин.

---

## 4. Что **не** входит в v2 (сознательно, задокументированный пробел)

Не идёт следствием лени — это реальная граница объёма работы за один заход; ниже — что нужно доделать, если
понадобится:

| Не сохраняется | Где живёт в `ProjectModel` | Почему пропущено |
|---|---|---|
| Track Motion (keyframes позиции/тени/свечения трека) | `Track::motion` (`TrackMotionState`) | Отдельная керфрейм-структура (`TrackMotionKeyframe`/`TrackFXKeyframe`) сравнимого объёма с Pan/Crop — не влезло в этот заход |
| Mixing Console (Assignable FX bus, Mixer Bus, Input Bus) | `ProjectModel::assignableFxBuses()`/`mixerBuses()`/`mixerInputBuses()` (см. `hasMixerExtras()`) | Отдельная под-система, есть свой UI (`MixingConsoleWindow`) — требует отдельного захода |
| Automation Lanes (track- и event-level) | `Track::automationLanes` / `TrackEvent::automationLanes` | Керфрейм-массивы с `AutomationPoint`, аналогично Track Motion |
| Event Pan/Crop **mask** keyframes | `EventPanCropState::maskKeyframes` | Только позиционные keyframes вошли в v2; маска — отдельный `MaskKeyframe` тип |
| Event Media Markers на `MediaItem` (Trimmer) | `MediaItem::markers` | Тайм-лайн маркеры (`ProjectModel::markers()`) сохраняются; per-media — нет |

**Практическое следствие**: если в проекте использован Track Motion / Mixing Console / Automation — Save/Open вернёт
клипы, треки и FX корректно, но эти три вида state придётся настроить заново после переоткрытия. Ничего не падает и
не портится тихо — просто эти поля не пишутся и не читаются (не «теряются» непредсказуемо, а последовательно
отсутствуют).

---

## 5. UI: что изменилось

| Действие | Было | Стало |
|---|---|---|
| File → Save (Ctrl+S) / панель инструментов | `addStub` — ничего не делает | Если проект уже сохранялся/открыт как архив в этой сессии — тихо перезаписывает туда же. Иначе — как Save As |
| File → Save As… (Ctrl+Shift+S) | `addStub` | Диалог: папка-назначение → имя папки проекта → `exportProjectArchive(..., copyMedia=false)` |
| File → Open… | Открывал только `.veg` | Диалог принимает и `.veg`, и `project.json` внутри архива (или саму папку архива) — `ProjectInterchange::isProjectArchive()` определяет по полю `"format"` |
| File → Incremental Save | `addStub` | Остаётся стабом — авто-нумерация имени папки при каждом сохранении не реализована в этом заходе |

`MainWindow::m_currentArchivePath` — новое поле сессии: путь последнего Save/Save As/открытого архива. Пусто, пока
не сохранялись ни разу или пока открыт `.veg` (у него другой, read-only смысл — `ProjectModel::projectPath()`) —
тогда Save ведёт себя как Save As.

---

## 6. Файлы

| Путь | Роль |
|------|------|
| `src/io/ProjectInterchange.h/.cpp` | `exportProjectArchive()` / `importProjectArchive()` / `isProjectArchive()`, JSON-хелперы `fxSlotToJson`/`fxSlotFromJson`/`eventPanCropToJson`/`eventPanCropFromJson` |
| `src/model/ProjectModel.h` | `setProjectPath()` — новый public setter, нужен читателю архива (Track/MediaItem/markers уже были public-mutable) |
| `src/app/MainWindow.{h,cpp}` | `onSaveProject()`/`onSaveProjectAs()`, `m_currentArchivePath`, ветка детекции архива в `openProjectPath()` |
| `src/ui/MenuBuilder.cpp` | Save/Save As… больше не `addStub` |
| `tests/test_project_interchange.cpp` | «OpenVegas project archive round-trips full state» — Titles & Text `fxChain` (через `titlesTextFromSlot` после round-trip), Pan/Crop keyframe, маркер, цвет/mute трека, gain события — все проверены byte-exact после export→import |
