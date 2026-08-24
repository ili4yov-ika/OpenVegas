# Аудит интерфейсных пустышек

**Дата:** 2026-08-24 (обновлено после подключения мастер-фейдера)
**Область:** `src/`, `ui/*.ui`
**Инструмент:** `python tools/audit_ui_stubs.py` (счётчики) / `--json` (машинный вывод для диффа)

Отчёт перечисляет элементы интерфейса, которые **видны и выглядят рабочими, но ничего не
делают**. Полностью рабочие элементы сюда не попадают.

Инструмент даёт **кандидатов, а не приговор**: каждая строка ниже прочитана в коде. Формы
ложных срабатываний, которые он уже отсеивает, и те, что отсеять нельзя, — в §7.

Связанные документы: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md) — стратегический backlog;
здесь — построчный список с `file:line`.

---

## Статусы

| Статус | Значение |
|--------|----------|
| **DEAD** | Кликается, выглядит обычным, при клике не происходит ничего. Худший случай: пользователь считает, что действие выполнено. |
| **DISABLED** | Намеренно `setEnabled(false)`. На экране честно: видно, что пока нельзя. |
| **NO SINK** | Поле/ползунок/флажок, чьё значение никто не читает. Вводить можно, значение никуда не уходит. |
| **DISCARDED** | Кнопка создана прямо в вызове layout, указатель не сохранён. Связать её нечем в принципе. |
| **ORPHAN** | `.ui`-файл собирается, но ни один исходник его не подключает. |

---

## Сводка

| Категория | Кол-во | Где |
|-----------|-------:|-----|
| Пункты строки меню, DEAD | 79 | `MenuBuilder.cpp` |
| Пункты строки меню, DISABLED | 13 | `MenuBuilder.cpp` |
| Пункты прочих меню, DEAD | 14 | Mixing Console, Trimmer, контекстные |
| Пункты прочих меню, DISABLED | 6 | Explorer, Trimmer, контекстные |
| Кнопки без обработчика | 16 | MainWindow и диалоги |
| Кнопки, созданные «на выброс» | 24 | MainWindow (22), ExplorerPane (2) |
| Поля/ползунки без потребителя | 30 | Project Properties и др. |
| Осиротевшие `.ui` | 2 | `ProjectPropertiesDialog.ui`, `TrimmerWindow.ui` |

Из 91 просмотренной кнопки **40 не делает ничего** (16 без обработчика + 24 «на выброс»).
Из 166 полей ввода — **30** без потребителя.

---

## 1. Главные находки

Три вещи стоит починить раньше остального — не потому, что их много, а потому, что они
обманывают сильнее всего.

### 1.1 ~~Мастер-фейдер громкости не подключён~~ — исправлено

Был `QSlider` с подсказкой «Master volume», созданный, положенный в layout и установленный
в 32; ни `connect`, ни чтения. Теперь ведёт в `ProjectModel::setMasterVolumeDb()` по той же
шкале, что и микшер (70 = 0 дБ), толкает изменение в живой микс на каждом шаге и пишет одну
запись отмены на всё перетаскивание. Начальное положение берётся из модели, а не из
случайного 32 (это примерно −33 дБ — выглядело неправильно даже до подключения). Кнопка
Lock Fader рядом тоже ожила: блокирует ползунок, не трогая саму громкость.

### 1.2 Project Properties собирает данные и выбрасывает их

`src/ui/ProjectPropertiesDialog.cpp`. Диалог **выглядит полностью рабочим**: каждое поле
связано с лямбдой, помечающей диалог изменённым, поэтому кнопка Apply загорается. Но
`applyToModel()` (строка 741) пишет в модель ровно четыре значения — размер кадра, частоту
кадров, частоту дискретизации и темп. Всё остальное не доходит никуда:

| Строка | Поле | Вкладка |
|-------:|------|---------|
| 293 | `m_adjustSource` Adjust source media to better match project | Video |
| 340 | `m_startAllVideo` Start all new projects with these settings | Video |
| 354 | `m_stereoBusses` Number of stereo busses | Audio |
| 409 | `m_startAllAudio` | Audio |
| 427 | `m_rulerStart` Ruler start time | Ruler |
| 442 | `m_beatsPerMeasure` | Ruler |
| 453 | `m_startAllRuler` | Ruler |
| 465–468 | `m_title`, `m_artist`, `m_engineer`, `m_copyright` | Summary |
| 481 | `m_startAllSummary` | Summary |
| 493 | `m_upc` UPC/EAN | Audio CD |
| 496 | `m_firstTrack` First track number | Audio CD |
| 526 | `m_360` 360 Output | Advanced |
| 532 | `m_swapLR` | Advanced |
| 550 | `m_includeCancel` | Advanced |
| 569 | `m_startAllAdvanced` | Advanced |

Отдельно: `m_overridePrerender`, `m_lfeFilter` и ползунок `m_crosstalk` **связаны и делают
видимую работу** — включают соседние контролы и обновляют подпись, — но их собственное
значение в модель тоже не уходит. Полурабочие, а не мёртвые.

Кнопка `customize` (строка 517) — без обработчика.

### 1.3 Панель инструментов Media/Preview: 24 кнопки «на выброс»

Идиома `layout->addWidget(IconFactory::toolButton(this, tr("…"), …))` создаёт кнопку и
сразу теряет указатель. Подключиться к ней нельзя ничем: фабрика ставит всем один
`objectName("iconBtn")`, так что и `findChild` не поможет.

`src/app/MainWindow.cpp` (22): Auto Preview (834), Capture Video (835), Get Media from the
Web (841), Remove Selected Media (843), Media Properties (844), Start Preview (853), Stop
Preview (854), Open in Audio Editor (855), Views (857), Search Media (859), Filter Media
(860), Preview on External Monitor (874), Copy Snapshot to Clipboard (970), Save Snapshot to
File (971), Record into Track (1404), Trim (1458), Heal (1468), Lock (1469), Enable Snapping
(1488), Auto Ripple (1496), Lock Envelopes (1497), Video Output Color Grading (1509).

`src/ui/ExplorerPane.cpp` (2): Views (709), Search (710).

---

## 2. Кнопки без обработчика

| Файл:строка | Кнопка | Подпись/назначение |
|---|---|---|
| `src/app/MainWindow.cpp:845` | `fx` | Apply Non-Real-Time Event FX |
| `src/app/MainWindow.cpp:876` | `fx` | второй такой же на другой панели |
| `src/app/MainWindow.cpp:973` | `btn360` | 360° Video |
| `src/app/MainWindow.cpp:982` | `btnHdr` | HDR |
| `src/app/MainWindow.cpp:1169` | `autoWrite` | Automation Settings |
| `src/app/MainWindow.cpp:1259` | `maxBtn` | Maximize панели |
| `src/app/MainWindow.cpp:1266` | `closeBtn` | Close панели |
| `src/ui/ColorGradingEditor.cpp:406` | `help` | «?» в заголовке |
| `src/ui/CustomizeKeyboardDialog.cpp:57` | `saveAs` | Save As… |
| `src/ui/CustomizeKeyboardDialog.cpp:61` | `m_deleteMapBtn` | Delete |
| `src/ui/FindMissingFileDialog.cpp:141` | `customBtn` | Custom… |
| `src/ui/FindMissingFileDialog.cpp:143` | `aboutBtn` | About… |
| `src/ui/MatchMediaVideoSettingsDialog.cpp:165` | `customBtn` | Custom… |
| `src/ui/MatchMediaVideoSettingsDialog.cpp:167` | `aboutBtn` | About… |
| `src/ui/MediaPropertiesDialog.cpp:132` | `snapshotBtn` | снимок кадра |
| `src/ui/ProjectPropertiesDialog.cpp:517` | `customize` | Customize… |

---

## 3. Поля и ползунки без потребителя

Помимо перечисленных в §1.2:

| Файл:строка | Контрол | Тип |
|---|---|---|
| `src/ui/ColorGradingEditor.cpp:596` | `channel` | QComboBox |
| `src/ui/CustomizeKeyboardDialog.cpp:53` | `m_mapCombo` | QComboBox |
| `src/ui/FindMissingFileDialog.cpp:136` | `seq` | QCheckBox |
| `src/ui/MatchMediaVideoSettingsDialog.cpp:152` | `m_openSequence` | QCheckBox |
| `src/ui/MatchMediaVideoSettingsDialog.cpp:156` | `m_firstImage` | QLineEdit |
| `src/ui/MatchMediaVideoSettingsDialog.cpp:158` | `m_lastImage` | QLineEdit |
| `src/ui/MediaPropertiesDialog.cpp:120` | `m_timecodeFormatCombo` | QComboBox |
| `src/ui/MediaPropertiesDialog.cpp:129` | `m_streamCombo` | QComboBox |
| `src/ui/MixingConsoleWindow.cpp:178` | `c` | QComboBox |
| `src/ui/TitlesTextKeyframePane.cpp:525` | `m_timecodeEdit` | QLineEdit |
| `src/ui/TrackMotionDialog.cpp:510` | `m_preset` | QComboBox |
| `src/ui/TrackMotionDialog.cpp:767` | `cb` | QCheckBox |
| `src/ui/VideoEventFxDialog.cpp:184` | `s` | QDoubleSpinBox |
| `src/ui/VideoEventFxDialogExact.cpp:1654` | `sel` | QComboBox |
| `src/ui/AudioEventFxDialog.cpp:128` | `s` | QSlider |

---

## 4. Строка меню (`src/ui/MenuBuilder.cpp`)

Файл сам помечает свои недоделки хелперами `addStub()` (строка 19) и `addStubDisabled()`
(строка 30) — поэтому счёт здесь точный: **79 DEAD, 13 DISABLED** из ~136 пунктов.

| Меню | DEAD | DISABLED | Примеры |
|------|-----:|---------:|---------|
| Edit | 4 | 10 | Undo/Redo (disabled), Quantize to Frames, Close Gaps |
| Options → Enable/Quantize/Metronome | 7 | 0 | Enable Snapping, Metronome, Customize Toolbar… |
| Event switches | 7 | 0 | Mute, Lock, Loop, Invert Phase, Normalize |
| Group | 6 | 0 | Group, Ungroup, Clear Group, Select Group |
| Edit tool | 5 | 0 | Normal/Envelope/Selection/Zoom Edit Tool |
| Channels | 5 | 0 | Both, Left Only, Right Only, Combine, Swap |
| View | 5 | 0 | Show Bus Tracks, Zoom In/Out Time |
| Insert | 4 | 0 | Audio/Video Bus Track, Empty Event, Text Media… |
| File | 3 | 0 | Incremental Save, Real-Time Render…, Capture… |
| Ripple / Select / Layouts | по 3 | 0 | Affected Tracks, Select Event Start, Save Layout… |
| Navigate / Stream / Dock / Scripting / Tools / Audio / Video / Help | по 2 | 0 | Go to Next Marker, Run Script…, Video Scopes, Contents and Index |
| Toolbars | 1 | 0 | Main Toolbar |
| Extensions / Tags / Ext | 0 | 3 | целиком disabled |

---

## 5. Прочие меню

| Файл:строка | Статус | Пункт |
|---|---|---|
| `ContextMenuBuilder.cpp:1336` | DEAD | Delete All in &Selection |
| `ContextMenuBuilder.cpp:1432` | DISABLED | &Selectively Prerender Video… |
| `ContextMenuBuilder.cpp:1435` | DEAD | &Clean Up Prerendered Video… |
| `ExplorerPane.cpp:1118,1121,1124,1127` | DISABLED | Rename, Cut, Copy, Remove |
| `MixingConsoleWindow.cpp:682` | DEAD | Audio Properties… |
| `MixingConsoleWindow.cpp:699–704` | DEAD | Show Channels: Audio Tracks, Audio Busses, Input Busses, Assignable FX Busses, Master Bus, Preview Bus |
| `MixingConsoleWindow.cpp:714` | DEAD | Label Control Regions |
| `MixingConsoleWindow.cpp:762` | DEAD | Reset Meter Clip |
| `TrimmerWindow.cpp:1245` | DISABLED | Add to Timeline up to Cursor |
| `TrimmerWindow.cpp:1249` | DEAD | Create Subclip… |
| `TrimmerWindow.cpp:1274` | DEAD | Detect Scenes and Add to Timeline from Cursor |
| `TrimmerWindow.cpp:1281` | DEAD | Edit Visible Button Set… |

Контекстные меню за прошедший год почти доделаны: из 77 `addAction` в
`ContextMenuBuilder.cpp` подключено 57, три не подключены, остальные — добавление уже
существующих действий в группу.

---

## 6. Осиротевшие `.ui`

| Файл | Виджетов | Состояние |
|---|---:|---|
| `ui/ProjectPropertiesDialog.ui` | 21 | Диалог целиком написан руками в `.cpp` (758 строк); `.ui` собирается AUTOUIC, но `ui_ProjectPropertiesDialog.h` никто не подключает |
| `ui/TrimmerWindow.ui` | 7 | То же: `TrimmerWindow.cpp` строит окно сам |

Оба перечислены и в `CMakeLists.txt`, и в `OpenVegas.pro`. На экране они не видны — это не
пустышки интерфейса, а мусор сборки, но найдены тем же проходом и держатся здесь, чтобы не
искать заново.

---

## 7. Что инструмент отсеивает и чего он не видит

Отсеивается автоматически (эти формы **не** являются пустышками):

- `addAction(текст, receiver, slot)` и вариант с лямбдой — перегрузка связывает сразу;
- `group->addAction(существующее)` — не создание;
- действие, сравниваемое с результатом `menu.exec()` (контекстное меню Pan/Crop);
- кнопка с `setMenu()` — работу делают пункты меню (кнопка Overlays в превью);
- кнопка или контрол, возвращаемые из фабрики (`IconFactory::toolButton`, лямбда
  `makeFader`) — связывает вызывающая сторона;
- `buttonBox` в диалогах — Designer связывает его в секции `<connections>` самого `.ui`.

**Слепое пятно, которое закрыть нечем:** контрол, связанный только с «пометить изменённым».
Для инструмента он неотличим от рабочего — есть `connect`, значит жив. Именно так выглядит
весь §1.2, и найден он чтением `applyToModel()`, а не проходом. Поэтому при правках диалогов
с кнопкой Apply проверять надо не наличие `connect`, а то, доходит ли значение до модели.

Локальные имена ищутся **в своём файле**: раньше поиск шёл по всем сразу, и `closeBtn`,
подключённый в другом диалоге, поручался за мёртвого однофамильца в `MainWindow`. Так одна
находка и пряталась.

---

## Как обновлять

```text
python tools/audit_ui_stubs.py           # счётчики
python tools/audit_ui_stubs.py --json    # полный список, удобно диффать
```

Правило проекта — в [`INIT.MD`](INIT.MD) («Пустышки интерфейса»): при появлении нового
неподключённого элемента или при подключении существующего этот файл правится в том же
коммите.
