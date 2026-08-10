# Аудит незавершённого интерфейса — пустые пункты меню и недоделанные UI-элементы

**Дата:** 2026-08-07
**Область:** `src/ui/`, `src/app/MainWindow.cpp`, `src/plugins/`, `src/io/`.
**Метод:** построчный разбор `MenuBuilder.cpp`, `ContextMenuBuilder.cpp` (каждый `addAction`/`QAction` проверен на наличие рабочего `connect()`), плюс grep по всему `src/` на маркеры незавершённости (`not implemented`, `coming soon`, `planned`, `stub`, `placeholder`, `no-op`, перманентный `setEnabled(false)`).

См. также: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md) — стратегический backlog; этот файл — конкретный построчный список с `file:line`.

---

## Легенда статусов

| Статус | Значение |
|--------|----------|
| **DEAD** | Пункт меню создан, но не имеет `connect()` вообще — клик не делает ничего (и не выглядит disabled, что хуже: пользователь думает, что что-то произошло). |
| **DISABLED** | Пункт создан перманентно disabled (`setEnabled(false)`), в коде нет пути, который бы его включил. |
| **STUB** | Обработчик есть, но внутри — заглушка: `QMessageBox`/`statusBar()` с текстом «not implemented yet» / «coming soon», либо hardcoded/no-op логика. |

Полностью рабочие пункты (OK) в списки ниже **не включены** — см. итоговую сводку.

---

## Сводка

| Область | DEAD | DISABLED | STUB | Всего проверено |
|---------|------|----------|------|------------------|
| `MenuBuilder.cpp` (строка меню) | ~60 | 8 | 2 | ~130 actions |
| `ContextMenuBuilder.cpp` (контекстные меню) | ~70 | ~25 | 5 целых меню | весь файл (1658 строк) |
| Диалоги/панели (см. §3) | — | 4+ | 8+ | точечно |

Из ~130 пунктов в главном меню примерно **половина** — мёртвые заглушки. В контекстных меню картина хуже: несколько **целых подменю** (Time Display, Split Screen, Preview, Take, показ «…» на событии) не имеют ни одного рабочего действия.

---

## 1. Главное меню (`src/ui/MenuBuilder.cpp`)

Файл использует два внутренних хелпера — `addStub()` (строки 19-28, добавляет action без `connect()`) и `addStubDisabled()` (строки 30-38, добавляет disabled action). Их наличие само по себе является сигналом: авторы явно помечали недоделанные пункты, но не убрали/не спрятали их из UI.

### File

| Пункт | Место | Статус | Заметка |
|-------|-------|--------|---------|
| Save | `MenuBuilder.cpp:58` | ✅ Implemented (2026-08-09) | `MainWindow::onSaveProject()`. `.veg` по-прежнему нельзя писать (закрытый бинарный формат) — сохраняет в собственный round-trip формат «OpenVegas Project Archive» (`project.json`, теперь с полным `fxChain`/Pan-Crop/маркерами — v1 хранил только тайминг). Первый Save в сессии ведёт себя как Save As; далее — тихая перезапись. См. [`PROJECT_ARCHIVE_FORMAT.md`](PROJECT_ARCHIVE_FORMAT.md). |
| Save As… | `MenuBuilder.cpp:60` | ✅ Implemented (2026-08-09) | `MainWindow::onSaveProjectAs()` — диалог папки-назначения + имя проекта. |
| Incremental Save | `MenuBuilder.cpp:62` | DEAD | Всё ещё стаб — авто-нумерация имени при каждом Save не реализована. |
| Close | `MenuBuilder.cpp:55-56` | STUB | Подключено к `MainWindow::onNewProject` — по факту это «New», а не «Close»: нет запроса на сохранение несохранённых изменений. |
| Real-Time Render… | `MenuBuilder.cpp:67` | DEAD | — |
| Capture… | `MenuBuilder.cpp:99` | DEAD | Соответствует отсутствию pipeline захвата (см. §2). |
| Export → Premiere/After Effects (*.prproj)… | `MenuBuilder.cpp:91` → `MainWindow::onExportPremiere` (`MainWindow.cpp:2966-2973`) | STUB | Показывает `QMessageBox::information("Native .prproj export is not available yet…")`. Файл не пишется. |

### Edit

| Пункт | Место | Статус |
|-------|-------|--------|
| Paste Repeat…, Paste Insert, Paste Event Attributes, Selectively Paste Event Attributes | `155-158` | DISABLED |
| Trim | `161` | DISABLED |
| Smart Split | `168` | DISABLED |
| Quantize to Frames, Close Gaps, Freeze selected/all Adjustment Events to Project | `169-172` | DEAD |
| Navigate → Go to Previous/Next Marker | `180-181` | DEAD |
| Post-Edit Ripple (все 3 варианта) | `184-186` | DEAD |
| Select → Select Event Start/End, Select Events to End | `190-192` | DEAD |
| Editing Tool → Normal/Envelope/Selection/Zoom Edit Tool, Delete | `195-199` | DEAD | Vegas-стандартный toolbox переключения инструментов вообще не реализован. |
| Extensions → «(none)» | `202` | DISABLED | Плейсхолдер; ничто в репозитории не сканирует и не заполняет это меню. |
| Switches (Mute/Lock/Loop/Invert Phase/Normalize/Maintain Aspect Ratio/Reduce Interlace Flicker) | `207-213` | DEAD |
| Tags → «(none)» | `216` | DISABLED | Тот же паттерн, что Extensions. |
| Group (Group/Ungroup/Ignore Event Grouping/Clear Group/Select Group/Create New Group) | `219-224` | DEAD | «Ignore Event Grouping» здесь — **дубликат** рабочего тумблера на панели инструментов (`MainWindow.cpp:1224-1229`), но эта копия в меню не синхронизирована с `m_project.ignoreEventGrouping()`. |
| Stream → Stream 0/1 | `227-228` | DEAD |
| Channels (Both/Left Only/Right Only/Combine/Swap) | `231-235` | DEAD |
| Undo All | `238` | DISABLED |
| Clear Edit History… | `239` | DISABLED |

### View

| Пункт | Место | Статус |
|-------|-------|--------|
| Show Bus Tracks, Active Take Information | `242-243` | DEAD |
| Zoom In/Out Time, Zoom Time to Selection | `245-247` | DEAD |
| Window Layouts (Default/Save/Load Layout…) | `251-253` | DEAD |
| Toolbar → Main Toolbar | `257` | DEAD | Чекбокс есть и предустановлен, но переключение ничего не показывает/не прячет. |
| Docking Layouts (Default/Save Docking Layout…) | `263-264` | DEAD |

### Insert / Tools / Options / Help

| Пункт | Место | Статус |
|-------|-------|--------|
| Audio/Video Envelope (Volume, Pan, Composite Level, Fade to Color) | `299-305` | DEAD |
| Audio/Video Bus Track, Empty Event, Text Media… | `308-312` | DEAD |
| Scripting (Run Script…, Rescan Script Menu Folder) | `317-318` | DEAD | Скриптинга нет вообще. |
| External Tools → «(none)» | `322` | DISABLED |
| Build/Prune Dynamic RAM Preview | `325-326` | DEAD |
| Audio Mixer, Rebuild Audio Peaks | `330-331` | DEAD |
| Video Scopes, Apply Non-Real-Time Event FX… | `335-336` | DEAD |
| Enable Snapping | `343` | DEAD | Чекбокс не связан с `m_project.snappingEnabled()` — реальный snap живёт только в отдельной кнопке на панели инструментов. |
| Quantize to Frames | `348` | DEAD |
| Enable Ripple Editing (все 3 варианта) | `355-357` | DEAD |
| Metronome | `360` | DEAD |
| Ignore Event Grouping | `370` | DEAD | Тот же дубликат, что в Edit (см. выше). |
| Customize Toolbar…, Customize Timeline Toolbar…, Export/Import Preferences… | `374-378` | DEAD | (Соседний «Customize Keyboard…» на `376` — рабочий.) |
| Contents and Index, OpenVegas Interactive Tutorials | `382-383` | DEAD | Справочной системы нет. |

---

## 2. Контекстные меню (`src/ui/ContextMenuBuilder.cpp`)

Здесь ситуация хуже — есть **целые функции**, где ни один пункт не подключён:

- **`showEventMoreMenu`** (`939-984`, кнопка «…» на событии) — Active Take Information, Playback Rate, Event Headers, Event Length, Color Grading, Media FX, Event Handles, Motion Tracking, Detect Scenes and Split, Normalize, Auto Normalize, Edit Visible Button Set… — всё DEAD/DISABLED, ни одного рабочего пункта.
- **`showTimeDisplayMenu`** (`1468-1491`) — весь Time Format submenu и MIDI-опции — DEAD.
- **`showSplitScreenMenu`** (`1494-1507`) — целиком DEAD.
- **`showZoomMenu`** (`1540-1559`) — «+»/«−»/Original resolution/Bypass Zoom без `connect()`; проценты 50–800% только переписывают текст на чипе тулбара, реального зума превью не меняют.
- **`showPreviewMenu`** (`1604-1655`) — Video Output Color Grading…, Background, Preview Quality, Display Frame Rate, Copy/Save Frame — всё DEAD.

Прочие находки по областям:

| Область | Место | Статус | Заметка |
|---------|-------|--------|---------|
| Video/Audio event → Switches submenu | `51-86`, `93-102` | DEAD | Mute/Lock/Loop/Hold Last Frame/Trim to include all frames/Invert Phase/Normalize/Auto Normalize и т.д. |
| Group → Cut All / Copy All | `139-140` | DEAD | Соседний «Delete All» рабочий. |
| Take submenu (Rename/Choose/Delete Active, Delete…) | `153-171` | DEAD/DISABLED | Весь subMenu декоративный. |
| Video track → Compositing Mode (15 blend-режимов) | `483-508` | DEAD | Ни Add/Subtract/Multiply/Screen/Overlay и т.д. не подключены. |
| Make Compositing Parent/Child | `510-511` | DISABLED |
| Track Group submenu (Rename/Group/Ungroup/Collapse/Expand/Mute/Solo) | `405-417` | DISABLED | Весь submenu недоступен. |
| Sound Mapper / Input device submenu | `450-465`, `625` | DEAD |
| Pan Type submenu | `640-651` | DEAD |
| Track-empty-area меню: видео-ветка «Track FX…» | `1163` | DEAD | **Несогласованность**: у аудио-ветки соседний пункт (`1167-1169`) рабочий (`window->onTrackFx`), у видео — нет. Видеотрек нельзя открыть Track FX через клик по пустой области. |
| Markers/Regions submenu (Region, Command Marker, CD Track/Index Marker, Delete All in Selection) | `1322-1325`, `1334` | DISABLED | Работают только обычные «Marker» и «Delete All». |
| Selectively Prerender Video… / Clean Up Prerendered Video… | `1430-1433` | DISABLED/DEAD |
| Overlays → Closed Captioning CC1-4, channel-view radios | `1578-1599` | DEAD | Соседние Grid/Safe Areas рабочие. |
| Ruler → Audio CD Time формат | `1241` | DISABLED |
| Ruler → Set Time at Cursor…, Set Project Tempo… | `1243-1244` | DEAD |

Полный построчный список (~70 DEAD + ~25 DISABLED пунктов) см. в истории ревью; здесь приведены наиболее показательные/пользователь-заметные группы.

---

## 3. Диалоги и панели

### Color Grading Editor (`src/ui/ColorGradingEditor.cpp`)
Хелпер `makeStubTab()` (`357-369`) рисует по центру серую надпись вместо реальных контролов. Из 8 вкладок **6 — заглушки**:
`Input LUT` (`422`), `HL Color Wheels` (`425`), `Utilities` (`427`), `HSL Curves` (`434`), `HSL` (`435`), `Look LUT` (`436`) — везде текст вида «…coming soon.» Реально работают только `Color Wheels` и `Color Curves`.

### Video Event FX (Pan/Crop) — `src/ui/VideoEventFxDialog.cpp`
`syncUiFromChain()` (`251-260`): комментарий в коде прямо говорит, что параметры — «display-only placeholders»; width/height/center/angle всегда хардкод 1920×1080/960/540/0, не читаются и не пишутся в событие. Правка значений в UI ни на что не влияет.
*(Замечание: это отдельный, более старый диалог `VideoEventFxDialog`, не путать с рабочим `VideoEventFxDialogExact`, который используется в актуальном flow — см. `MainWindow::onVideoEventFx`.)*

### Render As (`src/ui/RenderAsDialog.cpp`)
- «Customize Template» — перманентно disabled (`36`, `143-150`), обработчик клика (на случай если когда-то включат) показывает «Template customization is not implemented yet.»
- «Help» — всегда показывает «No help is currently available for this subject.» независимо от контекста.
- Apple ProRes в списке шаблонов выбирается как обычный формат, но `RenderTemplateCatalog.cpp:373-376` содержит `note`: «ProRes templates are provided for interchange. Encoding requires a future FFmpeg/ProRes pipeline» — этот текст **нигде не показывается пользователю** в `RenderAsDialog`, то есть выбор ProRes визуально ничем не отличается от рабочего формата.

### Customize Keyboard (`src/ui/CustomizeKeyboardDialog.cpp`)
- «Save As…» (именованная раскладка) — disabled, tooltip: «Custom named maps — planned» (`57-59`).
- «Delete» (раскладки) — disabled, нигде не включается (`61-63`).
(Add/Replace/Remove/Locate — рабочие, для контраста.)

### Trimmer (`src/ui/TrimmerWindow.cpp`)
- «Add to Timeline up to Cursor» (Shift+A) — disabled (`1244-1247`).
- «Create Subclip…» — DEAD, нет `connect()` (`1249`).
- «Detect Scenes and Add to Timeline from Cursor» — DEAD (`1274`).
- «Edit Visible Button Set…» — DEAD (`1281`).

---

## 4. Функциональные дыры за пределами меню

### Transport / Capture
- `MainWindow.cpp:3413-3414` — команда `Transport.Record`: `statusBar()->showMessage(tr("Record — not implemented yet"))`. Кнопка/шорткат записи ничего не делает.
- `MainWindow.cpp:1346-1349` — индикатор «Record Time» на статус-баре — хардкод-строка `"Record Time (2 channels): 41:06:27:05"`, комментарий подтверждает: placeholder до появления capture I/O. Никогда не меняется.
- Соответствует File → Capture… (DEAD, см. выше) — pipeline захвата отсутствует полностью.

### Плагины: редактор GUI для VST1/2/3
`src/plugins/AudioPluginHost.cpp:47-56` (`NullAudioPluginHost::openEditor`) и `src/audio/Vst3Host.cpp:1709-1712` — попытка открыть нативный editor плагина показывает: «Plug-in editor host is not implemented yet (VST1/VST2/VST3 SDK + audio engine — next stage).» Обработка звука через builtin DSP fallback работает, но нативный GUI — нет.
*(Пересекается с [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md), где для видео OFX / VST3 `IPlugView` уже реализован — этот пробел специфичен для более старого audio-host пути.)*

### VEGAS Shared audio-FX — часть каталога без реального DSP
`src/plugins/VegasSharedAudioCatalog.h:11-19` документирует статусы `Implemented` / `CatalogOnly` / `Unmapped`. Из ~24 записей в `catalog()` (`VegasSharedAudioCatalog.cpp:42-111`) только 12 — `Implemented`; 16 — `CatalogOnly`, и `chooserDescriptors()` (`220-243`) фильтрует только `Unmapped`, то есть **`CatalogOnly` эффекты попадают в реальный Plug-In Chooser** как обычные пункты: Wave Hammer, Pitch Shift, Flange, Distortion, Vibrato, Amplitude Modulation, Smooth/Enhance, ExpressFX Distortion, ExpressFX EQ, Volume. При добавлении в цепочку они падают в identity pass-through host (`AudioPluginHost.cpp:31-45`) — звук не меняется, и UI никак это не показывает.
Также `BuiltinAudioCatalog.cpp:141-155` — «Third Party» (Auto-Key, GClip, GGate, GMulti, GNormal, GSnap) и «5.1 FX» (Surround Panner) существуют только чтобы список чузера выглядел полным — без DSP.

### Transitions / Media Generator / Video FX панели — активация косметическая
**Обновление 2026-08-08:** для `MediaGeneratorPane` эта находка закрыта — см. `ISSUES_AND_PLANS.md` («Исправлено»). Double-click создаёт настоящий timeline-event для Titles & Text, и добавлен Drag'n'Drop (плагин-строка + пресет-тайлы → `mimeData`/`startDrag` в `GeneratorDragListWidget`, `dragEnterEvent`/`dropEvent` уже были в `TimelineView` для media-дропов). **Обновление 2026-08-08 (2):** Drag'n'Drop теперь работает для всех Media Generator плагинов, не только Titles & Text — Checkerboard, Color Gradient, Credit Roll, Noise Texture, Solid Color и Test Pattern получили реальный (паттерн-based) рендер-бэкенд, `video/MediaGeneratorApply.h`; double-click на них по-прежнему показывает toast (не переведён на insert-по-клику, только DnD).

**Поправка 2026-08-10:** предыдущая запись была преждевременной — Drag'n'Drop пресет-тайлов на самом деле НЕ запускался (пользователь подтвердил вживую: работал только double-click). Причина была не в `mimeData()`/payload (та часть цепочки была и остаётся корректной), а в том, что `QAbstractItemView`'s собственный механизм автозапуска drag (`dragEnabled()` + виртуальный `startDrag()`) просто не срабатывал для этой конфигурации виджета (`IconMode` + `SingleSelection` + свежий, ранее не выделенный тайл) — headless-проба (`QApplication` с `-platform offscreen`, синтетические `QMouseEvent`press/move) подтвердила: `startDrag()` не вызывался ни при какой комбинации (пресёлект/не пресёлект, `SingleSelection`/`ExtendedSelection`, второй клик по уже выделенному, движение на 100px). `GeneratorDragListWidget` переписан на явное отслеживание press→move→launch в собственных `mousePressEvent`/`mouseMoveEvent` (тот же паттерн, что уже проверенно работает в `TimelineView`'s внутреннем drag клипов) — та же headless-проба подтверждает, что новый путь действительно доходит до запуска `QDrag`. Живое подтверждение от пользователя всё ещё не получено (см. `ISSUES_AND_PLANS.md`). Обе другие панели (`TransitionsPane`, `VideoFxPane`) — по-прежнему как описано ниже (косметическая активация, drag-and-drop не реализован).

**Обновление 2026-08-11 (`TransitionsPane`):** для группы **3D Blinds** находка закрыта — у неё появился реальный рендерер (`video/TransitionApply.h`), тайлы пресетов показывают настоящий переход и играют демо-анимацию при наведении, перетаскиваются на фейд/кроссфейд таймлайна (`TransitionDragListWidget`, ручное отслеживание жеста — тот же паттерн, что у Media Generator), на таймлайне рисуется полоска перехода с кнопкой свойств, а `TransitionPropertiesDialog` даёт слайдеры/пресеты/Animate. Остальные ~24 группы каталога (3D Cascade, Barn Door, Iris, …) по-прежнему без рендерера: их тайлы остаются нарисованными плейсхолдерами и намеренно НЕ перетаскиваются (`pluginId` пуст), чтобы не создавать переход, который ничего не делает.

`MainWindow.cpp:1553-1601` — во всех трёх панелях активация элемента (даблклик/Enter) подключена одинаково:
```cpp
connect(m_transitions, &TransitionsPane::transitionActivated, this, [this](const QString &name) {
    statusBar()->showMessage(tr("Transition: %1").arg(name), 2500);
});
```
То же для `VideoFxPane` (кроме добавления через кнопку/двойной клик именно в Video FX пейне на клипе, см. `VideoFxPane::pluginActivated` — тот путь, что реально работает, отличается от этого) и для `TransitionsPane`. Drag-and-drop не реализован ни там, ни там (`grep` по `mimeData`/`startDrag`/`dropEvent` в `TransitionsPane.cpp` — пусто). Визуально полностью готовые панели (превью, категории, поиск) не вставляют переход на таймлайн через double-click или DnD.

### CD Audio
`src/io/CdAudioReader.h:23` — «Windows CDDA: TOC + raw sector rip to WAV. Stub elsewhere.» — File → Extract Audio from CD… работает только на Windows, на остальных платформах — заглушка.

---

## Не считать находками (для контраста / уже отслежено в ISSUES_AND_PLANS.md)

- `ExplorerPane.cpp` — Rename/Cut/Copy/Remove в контекстном меню пустой области disabled — это ожидаемо (нет выделения), не баг.
- `MediaThumbCache` — плейсхолдеры превью — легитимный паттерн асинхронной подгрузки.
- Explorer/Transitions панели как таковые уже отмечены в `ISSUES_AND_PLANS.md` («Неработающие / backlog») как «Placeholders» — этот файл уточняет: панели **визуально не заглушки** (полноценный UI), проблема именно в отсутствии insert-действия. (Media Generator из этого списка исключён — 2026-08-08, Drag'n'Drop insert-действие есть для всех плагинов; double-click остаётся toast для всех, кроме Titles & Text.)
- Shadow/Glow/blend/mask interpolate, soft bypass fade, `.prproj`/`.veg` write — уже в roadmap `PLAN_VIDEOAUDIOSTACK.md`/`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`.

---

## Приоритеты (предложение)

1. ~~**File → Save/Save As**~~ — реализовано 2026-08-09 (родной round-trip формат «OpenVegas Project Archive», не `.veg` — см. [`PROJECT_ARCHIVE_FORMAT.md`](PROJECT_ARCHIVE_FORMAT.md)). Track Motion / Mixing Console / Automation Lanes всё ещё не входят в архив — задокументированный, не тихий пробел.
2. **Transitions/Video FX panes** — insert-по-двойному-клику или drag&drop на таймлайн; сейчас полностью готовый браузер контента ни на что не годен, кроме просмотра превью. (Media Generator для Titles & Text уже сделан — 2026-08-08.)
3. **VEGAS Shared `CatalogOnly` эффекты** — либо пометить в UI чузера (например суффиксом «(no DSP)»), либо скрыть из списка до реализации.
4. **Video Event FX (`VideoEventFxDialog`, старый)** — проверить, не является ли он мёртвым кодом, если весь flow уже идёт через `VideoEventFxDialogExact`; если мёртв — удалить, а не оставлять диалог с фейковыми параметрами.
5. Остальное (Editing Tool submenu, Group/Switches/Channels в Edit, Compositing Mode blend-режимы, Time Display/Split Screen/Preview context-меню) — низкий приоритет, это Vegas-паритет «для галочки», а не блокер базового редактирования.
