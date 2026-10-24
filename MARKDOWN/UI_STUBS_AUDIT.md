# Аудит интерфейсных заглушек

**Дата:** 2026-10-04
**Область:** `src/`, зарегистрированные формы `ui/`, пути от UI до модели/рендера/аудиографа.
**Повторение:** `python tools/audit_ui_stubs.py` и `--json`.
**Связанный backlog:** [ISSUES_AND_PLANS.md](ISSUES_AND_PLANS.md).

Подключены команды, для которых в проекте есть исполнение. Необрабатываемые режимы
явно отключены. **DISABLED остаётся незавершённой функцией**: ноль кликабельных
кандидатов в автоматическом сканере не означает готовность всех функций VEGAS.

## Статусы и метод

| Статус | Значение |
|---|---|
| DEAD | Активная кнопка/команда без обработчика |
| NO SINK / NO EFFECT | Ввод принимается, но не доходит до модели либо обработки |
| DISABLED | Функция пока не реализована; управление отключено |
| DISPLAY | Read-only индикатор, не поле настройки |
| DISCARDED | Кнопка передана в layout без сохранения указателя |
| ORPHAN | Зарегистрированная Designer-форма не используется кодом |

Сканер выдаёт кандидатов. Он различает отключённые элементы и индикаторы, проверяет
`addItem` в контекстных меню и исключает контролы, возвращаемые фабрикой через
out-параметр. Чтение значения или `connect` само по себе не доказывает применение:
отдельно проверены Project Properties, Media Properties, микшер, Customize Keyboard
и Color Grading.

## Сводка текущего сканирования

| Категория | Активных кандидатов | Остаток / область подсчёта |
|---|---:|---|
| MenuBuilder helper declarations | 0 DEAD | 54 DISABLED |
| ContextMenuBuilder literal addItem declarations | 0 | 161 DISABLED |
| Другие addAction без обработчика | 0 DEAD | 13 DISABLED |
| new QPushButton/QToolButton без обработчика | 0 DEAD | 13 DISABLED из 97 деклараций |
| new input без reader/подключения | 0 NO SINK | 15 DISABLED + 2 DISPLAY из 173 деклараций |
| Кнопки IconFactory прямо в addWidget | 0 | Указатели сохранены и обработаны |
| Неиспользуемые .ui | 0 | Две формы удалены из дерева и CMake/qmake |

Это количества **деклараций**, не уникальных возможностей и не runtime-виджетов.
54 MenuBuilder-декларации включают условные fallback Undo/Redo и `(none)`;
рабочие Undo/Redo по-прежнему предоставляются QUndoStack. Контекстные циклы с
динамическими подписями и фабричные кнопки не входят в число literal declarations.
Категории нельзя складывать в общий процент готовности.

Прежний отчёт от 2026-08-25 завышал число активных заглушек: Custom/About,
HDR/360, Save As keyboard map и другие элементы уже были отключены.
Одновременно он пропускал многочисленные `addItem` и фабрики микшера.
Поэтому старые и новые общие числа напрямую несопоставимы.

## Сверка с vegas220.exe в Ghidra

Эталон: `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/vegas220.exe`, 52 279 120 байт.
SHA-256: `2db22a41e9972b5987761c2f6f28a2186e2e18e69ec0a9563e857162f0ee39b5`.

В загруженной программе прочитаны строки, таблицы указателей, xrefs и дизассемблирование
регистрации настроек. Изменений/переименований в программе Ghidra не делалось.

| Доказательство | VA | Вывод для аудита |
|---|---|---|
| `File.Save`, `File.SaveAs`, `File.SaveInc` | `140d45638`, `140d45650`, `140d45668` | Incremental Save — отдельная команда |
| Указатель на SaveInc в таблице | `140fd0268` | Запись содержит command id `0x2b3d` |
| `Options.EnableSnapping`, `Options.QuantizeToFrames` | `140d45cb8`, `140d45e40` | Переключатели нужно связывать с реальным состоянием редактора |
| `Edit.Group.Clear/CreateNew/RemoveFrom/SelectAll` | `140d44be0`, `140d44c08`, `140d44c38`, `140d44c68` | Группировка — несколько самостоятельных команд |
| `Edit.Group.CopyAll/CutAll/DeleteAll` | `140d48800`, `140d48828`, `140d48850` | Операции группы нельзя считать подключёнными по одному Select All |
| `Default Ruler Offset` | `140d629c0`; xref `14026a14d` | Смещение линейки — сохраняемая настройка |
| `Current Keyboard Mapping File`, `keyboard.ini` | `140d61438`, `140dd9070` | Именованные карты требуют отдельного хранения; пока DISABLED |
| `szSnapshotPath`, `Snapshot Save Path` | `140d5e840`, `140d5e860`; xref `14025f5e8` | Снимок и путь сохранения — реальная операция |
| `fApplyAspectToSnapshot`, `fSaveSnapshotAsPNG` | `140d72548`, `140d725d0` | Снимок имеет собственные параметры; полного parity пока нет |
| `fUseProjectNameAsSummaryTitle` | `140d6e2c0` | Summary отличается от фиктивного ввода с одним markDirty |

Xrefs настроек ведут в `FUN_14024eec0`. В дизассемблировании после загрузки адресов
`140d629c0`, `140d61438`, `140d5e840` видны вызовы регистратора `140034030`.
Полная декомпиляция этой большой функции превысила тайм-аут моста.

Эти данные подтверждают наличие и раздельность команд/настроек, **не доказывают всю
логику обработчиков**. Схема имён incremental-файлов, формат ввода offset в секундах,
набор сохраняемых панелей и RGB-интерполяция ниже — контракт OpenVegas.
Полное совпадение с VEGAS по этим деталям не заявляется. Проприетарные исходники,
ресурсы и бинарники в реализацию не копировались.

## Результат исправлений

Project Properties теперь применяет Summary (Title/Artist/Engineer/Copyright/Comments),
формат/смещение линейки, размер такта и длительность доли. Они сохраняются в
`.ovp`/`.ozp`/`project.json` и включены в ProjectSnapshot для Undo и отката
Apply при Cancel. Флаги новых проектов для Video/Audio/Ruler/Summary сохраняют defaults
после OK. Видео-шаблоны сохраняются/выбираются/удаляются. Нестандартные fps и sample rate
перестали округляться при простом открытии/применении диалога.

Меню исполняют Incremental Save (новый свободный `_NNN` путь), Trim выделенных событий
по loop region, quantize границ, переходы по маркерам, выбор границ событий,
масштабирование/fit selection, Group/Ungroup/Clear/Select Group, Loop события,
Maintain Aspect Ratio, Both/Left/Right audio, Undo All/Clear History, раскладки,
показ toolbar, Text Media и Help. Вставка дорожек использует модель и Undo.
Split/Group/Clear Group/Mixing Console и Undo/Redo получают клавиши через единый
QAction binding без конкурирующего QShortcut. Snapping/Ignore Grouping/Loop Playback
синхронизируются между моделью, меню и панелями.

Project Media и Explorer получили Views/Search; Project Media также Filter,
Remove/Properties, Start/Stop/Auto Preview через существующий Trimmer.
Snapshot создаёт изображение в размере проекта, ждёт декодирование с тайм-аутом
и записывает PNG/JPEG либо clipboard. Используется существующий preview compositor:
качество исходного decode ограничено VideoFrameCache, это не новый полноразмерный decoder.

Микшер применяет Show Channels/Control Regions, открывает Audio Properties, сбрасывает
зафиксированный clip-индикатор, использует реальные output buses дорожек. Фейдеры
захватывают исходный snapshot до live edit; два Master-фейдера связаны с одним
стереоуровнем модели. Панели Master можно закрыть/развернуть и восстановить раскладкой.
Контекстные команды используют существующие Event FX/Pan-Crop, групповые Cut/Copy,
Loop/Aspect/Channels и Mute/Solo дорожки.

RGB-кривая Color Grading перестала только сохраняться: существующий renderer читает
`curve.rgb` и применяет её к каналам, сохраняя alpha. Это piecewise-linear RGB-кривая
OpenVegas; отдельные R/G/B и HSL остаются недоступны.
Timecode в Titles & Text keyframe pane принимает секунды или `HH:MM:SS,cc`.

## Sound Forge / VEGAS Shared: применение параметров — 2026-10-04

Подтверждён дефект на **ExpressFX Graphic EQ**: нативный фейдер показывал −1 дБ,
IPersistStream-блоб менялся, но DSP оставался на прежних настройках. Отношение RMS
выхода к входу было около 1. После исправления — **0.89125** (−1 дБ). Это измерение
сигнала, а не вывод из наличия окна или обработчика.

`OnStatusChange` теперь помечает правку для отложенного Apply. Хост применяет её на
GUI-потоке под mutex обработки через `Stop → Apply/Load → Run`, чтобы фильтр обновил
DSP. Страницы остаются привязаны к тому же объекту. При смене параметров перезапускается
поток самого фильтра; сохранение хвостов delay/reverb при таком изменении не заявляется.

Окно FX публикует изменения состояния и bypass во время работы; добавление, удаление
и перестановка сразу перестраивают аудиограф. Состояние нативного плагина попадает в
модель до закрытия окна и в Undo. Восстановление документа применяет блоб к существующему
`hostKey`, а неизменённый документный блоб не затирает ещё не сохранённую правку страницы.
Редактор использует заданный аудиоформат вместо безусловных 48 кГц.

Подключены загрузка FX микшерных шин, live-bypass Bus/Assignable FX, захват состояния
шин при сохранении и reset внешних FX при seek. Ошибки запуска DirectShow/allocator/Load
сохраняются как диагностика; окно не сообщает об успешной обработке после отказа.
Цель редактора разрешается по id, а смена документа очищает открытый FX target.
Общие параметрические контролы находят слот по стабильному `hostKey`; повторная
правка после копирования цепочки или перестройки графа сохраняется в текущей модели.
Опрос сохраняет только изменённый внешний state и сравнивает его с независимой копией.

Проверка `[soundforge]`: **12 cases / 1014 assertions**, включая все **39**
зарегистрированных процессоров, настоящий нативный фейдер EQ, непустой state,
восстановление настроек и смену sample rate. Прогон всего audio suite с исключением
отдельного sandbox-зависимого `[policy]`: **37 cases / 1301 assertions — passed**.

Интеграционная проверка `python tools/run_ui_stubs_smoke.py --build build-msvc --suite audio-fx`
использует настоящий DirectShow-плагин, его HWND и штатный MainWindow/AudioEngine graph.
**146 checks — passed**: нативные редакторы открылись для всех **39** эффектов,
все заявленные страницы активировались. Проверены влияние правки на микс, bypass,
Undo/Redo, удаление FX, FX шин и повторные правки встроенного Track EQ после копирования
цепочки и перестройки графа. Проверка вывода использует одинаковый маршрут
и целое число периодов тона в блоке, чтобы pan law/фаза EQ не искажали A/B RMS.
OFX-каталог в этом runner пустой; оригинальные Shared audio DLL не подменяются.
Предыдущие общие CTest-отказы ниже остаются отдельным ограничением.

## Остаток: главное меню — DISABLED

| Место | Элемент | Статус |
|---|---|---|
| [src/ui/MenuBuilder.cpp:63](../src/ui/MenuBuilder.cpp#L63) | Real-Time Render… | DISABLED |
| [src/ui/MenuBuilder.cpp:139](../src/ui/MenuBuilder.cpp#L139) | Undo | DISABLED |
| [src/ui/MenuBuilder.cpp:144](../src/ui/MenuBuilder.cpp#L144) | Redo | DISABLED |
| [src/ui/MenuBuilder.cpp:151](../src/ui/MenuBuilder.cpp#L151) | Paste Repeat… | DISABLED |
| [src/ui/MenuBuilder.cpp:152](../src/ui/MenuBuilder.cpp#L152) | Paste Insert | DISABLED |
| [src/ui/MenuBuilder.cpp:153](../src/ui/MenuBuilder.cpp#L153) | Paste Event Attributes | DISABLED |
| [src/ui/MenuBuilder.cpp:154](../src/ui/MenuBuilder.cpp#L154) | Selectively Paste Event Attributes | DISABLED |
| [src/ui/MenuBuilder.cpp:164](../src/ui/MenuBuilder.cpp#L164) | Smart Split | DISABLED |
| [src/ui/MenuBuilder.cpp:179](../src/ui/MenuBuilder.cpp#L179) | Close Gaps | DISABLED |
| [src/ui/MenuBuilder.cpp:180](../src/ui/MenuBuilder.cpp#L180) | Freeze selected Adjustment Events to Project | DISABLED |
| [src/ui/MenuBuilder.cpp:181](../src/ui/MenuBuilder.cpp#L181) | Freeze all Adjustment Events to Project | DISABLED |
| [src/ui/MenuBuilder.cpp:193](../src/ui/MenuBuilder.cpp#L193) | Affected Tracks | DISABLED |
| [src/ui/MenuBuilder.cpp:194](../src/ui/MenuBuilder.cpp#L194) | Affected Tracks, Bus Tracks, Markers, and Regions | DISABLED |
| [src/ui/MenuBuilder.cpp:195](../src/ui/MenuBuilder.cpp#L195) | All Tracks, Markers, and Regions | DISABLED |
| [src/ui/MenuBuilder.cpp:220](../src/ui/MenuBuilder.cpp#L220) | Normal Edit Tool | DISABLED |
| [src/ui/MenuBuilder.cpp:221](../src/ui/MenuBuilder.cpp#L221) | Envelope Edit Tool | DISABLED |
| [src/ui/MenuBuilder.cpp:222](../src/ui/MenuBuilder.cpp#L222) | Selection Edit Tool | DISABLED |
| [src/ui/MenuBuilder.cpp:223](../src/ui/MenuBuilder.cpp#L223) | Zoom Edit Tool | DISABLED |
| [src/ui/MenuBuilder.cpp:227](../src/ui/MenuBuilder.cpp#L227) | (none) | DISABLED |
| [src/ui/MenuBuilder.cpp:232](../src/ui/MenuBuilder.cpp#L232) | Mute | DISABLED |
| [src/ui/MenuBuilder.cpp:233](../src/ui/MenuBuilder.cpp#L233) | Lock | DISABLED |
| [src/ui/MenuBuilder.cpp:250](../src/ui/MenuBuilder.cpp#L250) | Invert Phase | DISABLED |
| [src/ui/MenuBuilder.cpp:251](../src/ui/MenuBuilder.cpp#L251) | Normalize | DISABLED |
| [src/ui/MenuBuilder.cpp:264](../src/ui/MenuBuilder.cpp#L264) | Reduce Interlace Flicker | DISABLED |
| [src/ui/MenuBuilder.cpp:267](../src/ui/MenuBuilder.cpp#L267) | (none) | DISABLED |
| [src/ui/MenuBuilder.cpp:304](../src/ui/MenuBuilder.cpp#L304) | Stream 0 | DISABLED |
| [src/ui/MenuBuilder.cpp:305](../src/ui/MenuBuilder.cpp#L305) | Stream 1 | DISABLED |
| [src/ui/MenuBuilder.cpp:319](../src/ui/MenuBuilder.cpp#L319) | Combine | DISABLED |
| [src/ui/MenuBuilder.cpp:320](../src/ui/MenuBuilder.cpp#L320) | Swap Channels | DISABLED |
| [src/ui/MenuBuilder.cpp:327](../src/ui/MenuBuilder.cpp#L327) | Show Bus Tracks | DISABLED |
| [src/ui/MenuBuilder.cpp:328](../src/ui/MenuBuilder.cpp#L328) | Active Take Information | DISABLED |
| [src/ui/MenuBuilder.cpp:370](../src/ui/MenuBuilder.cpp#L370) | Volume | DISABLED |
| [src/ui/MenuBuilder.cpp:371](../src/ui/MenuBuilder.cpp#L371) | Pan | DISABLED |
| [src/ui/MenuBuilder.cpp:375](../src/ui/MenuBuilder.cpp#L375) | Composite Level | DISABLED |
| [src/ui/MenuBuilder.cpp:376](../src/ui/MenuBuilder.cpp#L376) | Fade to Color | DISABLED |
| [src/ui/MenuBuilder.cpp:379](../src/ui/MenuBuilder.cpp#L379) | Audio Bus Track | DISABLED |
| [src/ui/MenuBuilder.cpp:380](../src/ui/MenuBuilder.cpp#L380) | Video Bus Track | DISABLED |
| [src/ui/MenuBuilder.cpp:382](../src/ui/MenuBuilder.cpp#L382) | Empty Event | DISABLED |
| [src/ui/MenuBuilder.cpp:388](../src/ui/MenuBuilder.cpp#L388) | Run Script… | DISABLED |
| [src/ui/MenuBuilder.cpp:389](../src/ui/MenuBuilder.cpp#L389) | Rescan Script Menu Folder | DISABLED |
| [src/ui/MenuBuilder.cpp:393](../src/ui/MenuBuilder.cpp#L393) | (none) | DISABLED |
| [src/ui/MenuBuilder.cpp:396](../src/ui/MenuBuilder.cpp#L396) | Build Dynamic RAM Preview | DISABLED |
| [src/ui/MenuBuilder.cpp:397](../src/ui/MenuBuilder.cpp#L397) | Selectively Prune Dynamic RAM Preview | DISABLED |
| [src/ui/MenuBuilder.cpp:402](../src/ui/MenuBuilder.cpp#L402) | Rebuild Audio Peaks | DISABLED |
| [src/ui/MenuBuilder.cpp:406](../src/ui/MenuBuilder.cpp#L406) | Video Scopes | DISABLED |
| [src/ui/MenuBuilder.cpp:407](../src/ui/MenuBuilder.cpp#L407) | Apply Non-Real-Time Event FX… | DISABLED |
| [src/ui/MenuBuilder.cpp:430](../src/ui/MenuBuilder.cpp#L430) | Affected Tracks | DISABLED |
| [src/ui/MenuBuilder.cpp:431](../src/ui/MenuBuilder.cpp#L431) | Affected Tracks, Bus Tracks, Markers, and Regions | DISABLED |
| [src/ui/MenuBuilder.cpp:432](../src/ui/MenuBuilder.cpp#L432) | All Tracks, Markers, and Regions | DISABLED |
| [src/ui/MenuBuilder.cpp:435](../src/ui/MenuBuilder.cpp#L435) | Metronome | DISABLED |
| [src/ui/MenuBuilder.cpp:449](../src/ui/MenuBuilder.cpp#L449) | Customize Toolbar… | DISABLED |
| [src/ui/MenuBuilder.cpp:450](../src/ui/MenuBuilder.cpp#L450) | Customize Timeline Toolbar… | DISABLED |
| [src/ui/MenuBuilder.cpp:453](../src/ui/MenuBuilder.cpp#L453) | Export/Import Preferences… | DISABLED |
| [src/ui/MenuBuilder.cpp:462](../src/ui/MenuBuilder.cpp#L462) | OpenVegas Interactive Tutorials | DISABLED |

## Остаток: прочие прямые действия — DISABLED

| Место | Элемент | Статус |
|---|---|---|
| [src/ui/ContextMenuBuilder.cpp:1444](../src/ui/ContextMenuBuilder.cpp#L1444) | Delete All in &Selection | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1546](../src/ui/ContextMenuBuilder.cpp#L1546) | &Selectively Prerender Video... | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1549](../src/ui/ContextMenuBuilder.cpp#L1549) | &Clean Up Prerendered Video... | DISABLED |
| [src/ui/ExplorerPane.cpp:1137](../src/ui/ExplorerPane.cpp#L1137) | Rename | DISABLED |
| [src/ui/ExplorerPane.cpp:1140](../src/ui/ExplorerPane.cpp#L1140) | Cut | DISABLED |
| [src/ui/ExplorerPane.cpp:1143](../src/ui/ExplorerPane.cpp#L1143) | Copy | DISABLED |
| [src/ui/ExplorerPane.cpp:1146](../src/ui/ExplorerPane.cpp#L1146) | Remove | DISABLED |
| [src/ui/MixingConsoleWindow.cpp:783](../src/ui/MixingConsoleWindow.cpp#L783) | Preview Bus | DISABLED |
| [src/ui/MixingConsoleWindow.cpp:796](../src/ui/MixingConsoleWindow.cpp#L796) | Label Control Regions | DISABLED |
| [src/ui/TrimmerWindow.cpp:1245](../src/ui/TrimmerWindow.cpp#L1245) | Add to Timeline up to Cursor | DISABLED |
| [src/ui/TrimmerWindow.cpp:1249](../src/ui/TrimmerWindow.cpp#L1249) | Create Subclip… | DISABLED |
| [src/ui/TrimmerWindow.cpp:1274](../src/ui/TrimmerWindow.cpp#L1274) | Detect Scenes and Add to Timeline from Cursor | DISABLED |
| [src/ui/TrimmerWindow.cpp:1281](../src/ui/TrimmerWindow.cpp#L1281) | Edit Visible Button Set… | DISABLED |

## Остаток: кнопки без handler и поля без reader

DISPLAY не является заглушкой. DISABLED остаётся backlog.

| Место | Элемент | Статус |
|---|---|---|
| [src/app/MainWindow.cpp:882](../src/app/MainWindow.cpp#L882) | fx | DISABLED |
| [src/app/MainWindow.cpp:951](../src/app/MainWindow.cpp#L951) | fx | DISABLED |
| [src/app/MainWindow.cpp:1052](../src/app/MainWindow.cpp#L1052) | btn360 | DISABLED |
| [src/app/MainWindow.cpp:1061](../src/app/MainWindow.cpp#L1061) | btnHdr | DISABLED |
| [src/app/MainWindow.cpp:1249](../src/app/MainWindow.cpp#L1249) | autoWrite | DISABLED |
| [src/ui/CustomizeKeyboardDialog.cpp:62](../src/ui/CustomizeKeyboardDialog.cpp#L62) | saveAs | DISABLED |
| [src/ui/CustomizeKeyboardDialog.cpp:66](../src/ui/CustomizeKeyboardDialog.cpp#L66) | m_deleteMapBtn | DISABLED |
| [src/ui/FindMissingFileDialog.cpp:141](../src/ui/FindMissingFileDialog.cpp#L141) | customBtn | DISABLED |
| [src/ui/FindMissingFileDialog.cpp:143](../src/ui/FindMissingFileDialog.cpp#L143) | aboutBtn | DISABLED |
| [src/ui/MatchMediaVideoSettingsDialog.cpp:167](../src/ui/MatchMediaVideoSettingsDialog.cpp#L167) | customBtn | DISABLED |
| [src/ui/MatchMediaVideoSettingsDialog.cpp:169](../src/ui/MatchMediaVideoSettingsDialog.cpp#L169) | aboutBtn | DISABLED |
| [src/ui/MediaPropertiesDialog.cpp:134](../src/ui/MediaPropertiesDialog.cpp#L134) | snapshotBtn | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:584](../src/ui/ProjectPropertiesDialog.cpp#L584) | customize | DISABLED |

| Место | Элемент | Статус |
|---|---|---|
| [src/ui/ColorGradingEditor.cpp:605](../src/ui/ColorGradingEditor.cpp#L605) | channel | DISABLED |
| [src/ui/CustomizeKeyboardDialog.cpp:56](../src/ui/CustomizeKeyboardDialog.cpp#L56) | m_mapCombo | DISABLED |
| [src/ui/FindMissingFileDialog.cpp:136](../src/ui/FindMissingFileDialog.cpp#L136) | seq | DISABLED |
| [src/ui/MatchMediaVideoSettingsDialog.cpp:152](../src/ui/MatchMediaVideoSettingsDialog.cpp#L152) | m_openSequence | DISABLED |
| [src/ui/MatchMediaVideoSettingsDialog.cpp:158](../src/ui/MatchMediaVideoSettingsDialog.cpp#L158) | m_firstImage | DISPLAY |
| [src/ui/MatchMediaVideoSettingsDialog.cpp:160](../src/ui/MatchMediaVideoSettingsDialog.cpp#L160) | m_lastImage | DISPLAY |
| [src/ui/MediaPropertiesDialog.cpp:120](../src/ui/MediaPropertiesDialog.cpp#L120) | m_timecodeFormatCombo | DISABLED |
| [src/ui/MediaPropertiesDialog.cpp:130](../src/ui/MediaPropertiesDialog.cpp#L130) | m_streamCombo | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:349](../src/ui/ProjectPropertiesDialog.cpp#L349) | m_adjustSource | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:412](../src/ui/ProjectPropertiesDialog.cpp#L412) | m_stereoBusses | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:560](../src/ui/ProjectPropertiesDialog.cpp#L560) | m_upc | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:563](../src/ui/ProjectPropertiesDialog.cpp#L563) | m_firstTrack | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:593](../src/ui/ProjectPropertiesDialog.cpp#L593) | m_360 | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:599](../src/ui/ProjectPropertiesDialog.cpp#L599) | m_swapLR | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:617](../src/ui/ProjectPropertiesDialog.cpp#L617) | m_includeCancel | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:636](../src/ui/ProjectPropertiesDialog.cpp#L636) | m_startAllAdvanced | DISABLED |
| [src/ui/VideoEventFxDialogExact.cpp:1654](../src/ui/VideoEventFxDialogExact.cpp#L1654) | sel | DISABLED |

## Семантический остаток, который connect/reader не обнаруживает

### Project Properties: неисполняемые настройки обработки

Все перечисленные поля отключены: Apply не принимает параметры без processing path.
Фактические Width/Height/FPS/Sample Rate/Tempo/Ruler/Summary и поддержанные defaults
не входят в этот список.

| Место | Элемент | Статус |
|---|---|---|
| [src/ui/ProjectPropertiesDialog.cpp:289](../src/ui/ProjectPropertiesDialog.cpp#L289) | m_hdr | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:295](../src/ui/ProjectPropertiesDialog.cpp#L295) | m_fieldOrder | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:298](../src/ui/ProjectPropertiesDialog.cpp#L298) | m_pixelAspect | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:300](../src/ui/ProjectPropertiesDialog.cpp#L300) | m_rotation | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:320](../src/ui/ProjectPropertiesDialog.cpp#L320) | m_pixelFormat | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:336](../src/ui/ProjectPropertiesDialog.cpp#L336) | m_renderQuality | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:338](../src/ui/ProjectPropertiesDialog.cpp#L338) | m_motionBlur | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:340](../src/ui/ProjectPropertiesDialog.cpp#L340) | m_deinterlace | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:343](../src/ui/ProjectPropertiesDialog.cpp#L343) | m_resample | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:349](../src/ui/ProjectPropertiesDialog.cpp#L349) | m_adjustSource | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:353](../src/ui/ProjectPropertiesDialog.cpp#L353) | m_overridePrerender | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:376](../src/ui/ProjectPropertiesDialog.cpp#L376) | m_prerenderFolder | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:410](../src/ui/ProjectPropertiesDialog.cpp#L410) | m_masterBus | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:412](../src/ui/ProjectPropertiesDialog.cpp#L412) | m_stereoBusses | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:424](../src/ui/ProjectPropertiesDialog.cpp#L424) | m_bitDepth | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:426](../src/ui/ProjectPropertiesDialog.cpp#L426) | m_audioQuality | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:430](../src/ui/ProjectPropertiesDialog.cpp#L430) | m_lfeFilter | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:434](../src/ui/ProjectPropertiesDialog.cpp#L434) | m_lfeCutoff | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:437](../src/ui/ProjectPropertiesDialog.cpp#L437) | m_lfeQuality | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:451](../src/ui/ProjectPropertiesDialog.cpp#L451) | m_recordFolder | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:560](../src/ui/ProjectPropertiesDialog.cpp#L560) | m_upc | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:563](../src/ui/ProjectPropertiesDialog.cpp#L563) | m_firstTrack | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:593](../src/ui/ProjectPropertiesDialog.cpp#L593) | m_360 | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:597](../src/ui/ProjectPropertiesDialog.cpp#L597) | m_stereo3d | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:599](../src/ui/ProjectPropertiesDialog.cpp#L599) | m_swapLR | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:607](../src/ui/ProjectPropertiesDialog.cpp#L607) | m_crosstalk | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:617](../src/ui/ProjectPropertiesDialog.cpp#L617) | m_includeCancel | DISABLED |
| [src/ui/ProjectPropertiesDialog.cpp:636](../src/ui/ProjectPropertiesDialog.cpp#L636) | m_startAllAdvanced | DISABLED |

### Фабрики, генераторы и клавиатурные карты

| Место | Остаток | Статус |
|---|---|---|
| [MainWindow.cpp](../src/app/MainWindow.cpp) | Web media, external audio editor/monitor, record into track, heal/lock/ripple/lock envelopes, master video FX/CG, master mute/solo/automation write | DISABLED |
| [MixingConsoleWindow.cpp](../src/ui/MixingConsoleWindow.cpp) | Device I/O, automation modes, phase/record/monitor, send/FX routing, Bus/Master FX controls, Input/Assignable scalar controls без соответствующей модели | DISABLED |
| [MixingConsoleWindow.cpp](../src/ui/MixingConsoleWindow.cpp) | Meter ranges/labels/peak-valley hold, Preview Bus, region headers | DISABLED |
| [MediaPropertiesDialog.cpp](../src/ui/MediaPropertiesDialog.cpp) | Размер/field order/PAR/alpha/rotation/background генератора сохраняются в структуре, но renderer их не обрабатывает | DISABLED; существующие данные сохранены |
| [CustomizeKeyboardDialog.cpp](../src/ui/CustomizeKeyboardDialog.cpp) | Именованные карты; неподключённые контексты и Global-команды | DISABLED; редактируются только исполняемые bindings |
| [ColorGradingEditor.cpp](../src/ui/ColorGradingEditor.cpp) | Отдельные R/G/B curves, HSL curves | DISABLED / информационная вкладка |
| [VideoEventFxDialogExact.cpp](../src/ui/VideoEventFxDialogExact.cpp) | Неподключённый preset selector | DISABLED; выбор существующих preset tiles остаётся отдельным механизмом |
| [ContextMenuBuilder.cpp](../src/ui/ContextMenuBuilder.cpp) | Feet/Frames, SMPTE и Audio CD форматы линейки без реализации formatter | DISABLED |
| [TrimmerWindow.cpp](../src/ui/TrimmerWindow.cpp) | Subclip, scene detection, editable visible button set | DISABLED |

### Контекстные меню

`addItem` создаёт недоступный пункт. После подключения настоящего callback
`enableItem` разрешает действие с учётом переданного context-enabled флага.
Это предотвращает появление активной заглушки при добавлении новой подписи.
Ниже literal declarations, оставшиеся отключёнными; динамические radio-списки
(resample/pan/compositing/preview presets и др.) проверяются отдельно и не входят
в этот счётчик.

| Место | Элемент | Статус |
|---|---|---|
| [src/ui/ContextMenuBuilder.cpp:61](../src/ui/ContextMenuBuilder.cpp#L61) | Mute | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:62](../src/ui/ContextMenuBuilder.cpp#L62) | Lock | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:64](../src/ui/ContextMenuBuilder.cpp#L64) | Hold last frame | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:80](../src/ui/ContextMenuBuilder.cpp#L80) | Trim event to include all frames | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:97](../src/ui/ContextMenuBuilder.cpp#L97) | Reduce Interlace Flicker | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:119](../src/ui/ContextMenuBuilder.cpp#L119) | Mute | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:120](../src/ui/ContextMenuBuilder.cpp#L120) | Lock | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:134](../src/ui/ContextMenuBuilder.cpp#L134) | Invert Phase | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:135](../src/ui/ContextMenuBuilder.cpp#L135) | Normalize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:136](../src/ui/ContextMenuBuilder.cpp#L136) | Auto Normalize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:204](../src/ui/ContextMenuBuilder.cpp#L204) | Rename Active | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:205](../src/ui/ContextMenuBuilder.cpp#L205) | Choose Active… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:207](../src/ui/ContextMenuBuilder.cpp#L207) | Next Take | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:208](../src/ui/ContextMenuBuilder.cpp#L208) | Previous Take | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:210](../src/ui/ContextMenuBuilder.cpp#L210) | Delete Active | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:211](../src/ui/ContextMenuBuilder.cpp#L211) | Delete… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:228](../src/ui/ContextMenuBuilder.cpp#L228) | Velocity | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:229](../src/ui/ContextMenuBuilder.cpp#L229) | Freeze Frame at Cursor | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:230](../src/ui/ContextMenuBuilder.cpp#L230) | Transition Progress | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:238](../src/ui/ContextMenuBuilder.cpp#L238) | Stream 1 | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:268](../src/ui/ContextMenuBuilder.cpp#L268) | Combine | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:269](../src/ui/ContextMenuBuilder.cpp#L269) | Swap | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:276](../src/ui/ContextMenuBuilder.cpp#L276) | By Moving | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:277](../src/ui/ContextMenuBuilder.cpp#L277) | By Slipping | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:279](../src/ui/ContextMenuBuilder.cpp#L279) | Create Sync Link with Selected Events | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:282](../src/ui/ContextMenuBuilder.cpp#L282) | Unlink | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:283](../src/ui/ContextMenuBuilder.cpp#L283) | Select All | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:409](../src/ui/ContextMenuBuilder.cpp#L409) | Show Automation Controls | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:469](../src/ui/ContextMenuBuilder.cpp#L469) | Rename Track Group | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:471](../src/ui/ContextMenuBuilder.cpp#L471) | Group Selected Tracks | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:472](../src/ui/ContextMenuBuilder.cpp#L472) | Ungroup Selected Tracks | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:473](../src/ui/ContextMenuBuilder.cpp#L473) | Collapse | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:474](../src/ui/ContextMenuBuilder.cpp#L474) | Expand | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:476](../src/ui/ContextMenuBuilder.cpp#L476) | Mute Track Group | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:477](../src/ui/ContextMenuBuilder.cpp#L477) | Solo Track Group | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:532](../src/ui/ContextMenuBuilder.cpp#L532) | Bypass Motion Blur | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:539](../src/ui/ContextMenuBuilder.cpp#L539) | Activate Automation Controls | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:556](../src/ui/ContextMenuBuilder.cpp#L556) | Custom… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:572](../src/ui/ContextMenuBuilder.cpp#L572) | Make Compositing Parent | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:573](../src/ui/ContextMenuBuilder.cpp#L573) | Make Compositing Child | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:574](../src/ui/ContextMenuBuilder.cpp#L574) | Minimize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:575](../src/ui/ContextMenuBuilder.cpp#L575) | Maximize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:580](../src/ui/ContextMenuBuilder.cpp#L580) | Edit Visible Button Set… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:607](../src/ui/ContextMenuBuilder.cpp#L607) | Pair as Stereoscopic 3D Subclips | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:612](../src/ui/ContextMenuBuilder.cpp#L612) | Mute | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:613](../src/ui/ContextMenuBuilder.cpp#L613) | Composite Level | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:614](../src/ui/ContextMenuBuilder.cpp#L614) | Fade to Color | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:638](../src/ui/ContextMenuBuilder.cpp#L638) | Top… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:639](../src/ui/ContextMenuBuilder.cpp#L639) | Bottom… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:642](../src/ui/ContextMenuBuilder.cpp#L642) | Expand Track Layers | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:647](../src/ui/ContextMenuBuilder.cpp#L647) | Set Default Track Properties… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:655](../src/ui/ContextMenuBuilder.cpp#L655) | Arm For Record | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:656](../src/ui/ContextMenuBuilder.cpp#L656) | Invert Track Phase | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:657](../src/ui/ContextMenuBuilder.cpp#L657) | Activate Automation Controls | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:667](../src/ui/ContextMenuBuilder.cpp#L667) | Master (Microsoft Sound Mapper) | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:672](../src/ui/ContextMenuBuilder.cpp#L672) | Minimize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:673](../src/ui/ContextMenuBuilder.cpp#L673) | Maximize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:674](../src/ui/ContextMenuBuilder.cpp#L674) | Auto Ducking Controller | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:676](../src/ui/ContextMenuBuilder.cpp#L676) | Edit Visible Button Set… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:699](../src/ui/ContextMenuBuilder.cpp#L699) | Arm for Record | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:703](../src/ui/ContextMenuBuilder.cpp#L703) | Master (Microsoft Sound Mapper) | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:732](../src/ui/ContextMenuBuilder.cpp#L732) | Mute | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:801](../src/ui/ContextMenuBuilder.cpp#L801) | Invert Phase | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:804](../src/ui/ContextMenuBuilder.cpp#L804) | Set Default Track Properties… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:811](../src/ui/ContextMenuBuilder.cpp#L811) | Use Vertical Meters | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:814](../src/ui/ContextMenuBuilder.cpp#L814) | Show Output Meters | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:842](../src/ui/ContextMenuBuilder.cpp#L842) | Open Parent Media in Trimmer | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:843](../src/ui/ContextMenuBuilder.cpp#L843) | Select in Project Media list | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:844](../src/ui/ContextMenuBuilder.cpp#L844) | Edit Source Project | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:846](../src/ui/ContextMenuBuilder.cpp#L846) | Create Nested Timeline | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:847](../src/ui/ContextMenuBuilder.cpp#L847) | Open Nested Timeline | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:850](../src/ui/ContextMenuBuilder.cpp#L850) | Media FX… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:854](../src/ui/ContextMenuBuilder.cpp#L854) | Motion Tracking | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:858](../src/ui/ContextMenuBuilder.cpp#L858) | Paste Event Attributes | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:859](../src/ui/ContextMenuBuilder.cpp#L859) | Selectively Paste Event Attributes | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:873](../src/ui/ContextMenuBuilder.cpp#L873) | Detect Scenes and Split | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:879](../src/ui/ContextMenuBuilder.cpp#L879) | Smart Split | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:880](../src/ui/ContextMenuBuilder.cpp#L880) | Event Heal | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:881](../src/ui/ContextMenuBuilder.cpp#L881) | Close Gaps | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:883](../src/ui/ContextMenuBuilder.cpp#L883) | Resize Adjustment Event to Project | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:885](../src/ui/ContextMenuBuilder.cpp#L885) | Add Missing Stream for Selected Event | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:886](../src/ui/ContextMenuBuilder.cpp#L886) | Create Subclip | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:915](../src/ui/ContextMenuBuilder.cpp#L915) | Pair as Stereoscopic 3D Subclip | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:917](../src/ui/ContextMenuBuilder.cpp#L917) | Select Events to End | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:918](../src/ui/ContextMenuBuilder.cpp#L918) | Select All After Cursor | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:935](../src/ui/ContextMenuBuilder.cpp#L935) | Open Parent Media in Trimmer | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:936](../src/ui/ContextMenuBuilder.cpp#L936) | Open in Audio Editor | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:937](../src/ui/ContextMenuBuilder.cpp#L937) | Open Copy in Audio Editor | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:938](../src/ui/ContextMenuBuilder.cpp#L938) | Edit Source Project | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:939](../src/ui/ContextMenuBuilder.cpp#L939) | Create Nested Timeline | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:940](../src/ui/ContextMenuBuilder.cpp#L940) | Open Nested Timeline | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:941](../src/ui/ContextMenuBuilder.cpp#L941) | Select in Project Media list | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:943](../src/ui/ContextMenuBuilder.cpp#L943) | Beat Detection | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:944](../src/ui/ContextMenuBuilder.cpp#L944) | Tempo Detection | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:947](../src/ui/ContextMenuBuilder.cpp#L947) | Apply Non-Real-Time Event FX… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:951](../src/ui/ContextMenuBuilder.cpp#L951) | Paste Event Attributes | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:952](../src/ui/ContextMenuBuilder.cpp#L952) | Selectively Paste Event Attributes | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:971](../src/ui/ContextMenuBuilder.cpp#L971) | Event Heal | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:972](../src/ui/ContextMenuBuilder.cpp#L972) | Close Gaps | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:974](../src/ui/ContextMenuBuilder.cpp#L974) | Add Missing Stream for Selected Event | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:975](../src/ui/ContextMenuBuilder.cpp#L975) | Create Subclip | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1005](../src/ui/ContextMenuBuilder.cpp#L1005) | Select Events to End | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1006](../src/ui/ContextMenuBuilder.cpp#L1006) | Select All After Cursor | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1025](../src/ui/ContextMenuBuilder.cpp#L1025) | FX Automation… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1056](../src/ui/ContextMenuBuilder.cpp#L1056) | Playback Rate | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1057](../src/ui/ContextMenuBuilder.cpp#L1057) | Freeze Frame at Cursor | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1058](../src/ui/ContextMenuBuilder.cpp#L1058) | Selectively Paste Event Attributes | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1061](../src/ui/ContextMenuBuilder.cpp#L1061) | Color Grading | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1062](../src/ui/ContextMenuBuilder.cpp#L1062) | Media FX | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1064](../src/ui/ContextMenuBuilder.cpp#L1064) | Motion Tracking | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1065](../src/ui/ContextMenuBuilder.cpp#L1065) | Detect Scenes and Split | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1067](../src/ui/ContextMenuBuilder.cpp#L1067) | Edit Visible Button Set… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1073](../src/ui/ContextMenuBuilder.cpp#L1073) | Normalize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1074](../src/ui/ContextMenuBuilder.cpp#L1074) | Auto Normalize | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1076](../src/ui/ContextMenuBuilder.cpp#L1076) | Edit Visible Button Set… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1253](../src/ui/ContextMenuBuilder.cpp#L1253) | Insert Empty Event | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1255](../src/ui/ContextMenuBuilder.cpp#L1255) | Insert Text Media… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1260](../src/ui/ContextMenuBuilder.cpp#L1260) | Select All on Track | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1265](../src/ui/ContextMenuBuilder.cpp#L1265) | Insert/Remove Envelope | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1267](../src/ui/ContextMenuBuilder.cpp#L1267) | Insert/Remove Envelope | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1271](../src/ui/ContextMenuBuilder.cpp#L1271) | Record Input… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1347](../src/ui/ContextMenuBuilder.cpp#L1347) | Set Time &at Cursor... | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1348](../src/ui/ContextMenuBuilder.cpp#L1348) | Set Project Tempo... | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1431](../src/ui/ContextMenuBuilder.cpp#L1431) | &Region | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1432](../src/ui/ContextMenuBuilder.cpp#L1432) | &Command Marker | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1433](../src/ui/ContextMenuBuilder.cpp#L1433) | CD &Track Region | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1434](../src/ui/ContextMenuBuilder.cpp#L1434) | CD &Index Marker | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1590](../src/ui/ContextMenuBuilder.cpp#L1590) | Time at Cursor | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1593](../src/ui/ContextMenuBuilder.cpp#L1593) | MIDI Timecode In | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1594](../src/ui/ContextMenuBuilder.cpp#L1594) | MIDI Timecode Out | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1595](../src/ui/ContextMenuBuilder.cpp#L1595) | MIDI Clock Out | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1616](../src/ui/ContextMenuBuilder.cpp#L1616) | FX Bypassed | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1619](../src/ui/ContextMenuBuilder.cpp#L1619) | Clipboard | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1621](../src/ui/ContextMenuBuilder.cpp#L1621) | Select Left Half | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1622](../src/ui/ContextMenuBuilder.cpp#L1622) | Select Right Half | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1623](../src/ui/ContextMenuBuilder.cpp#L1623) | Select All | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1625](../src/ui/ContextMenuBuilder.cpp#L1625) | Retain Split Screen Setting | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1660](../src/ui/ContextMenuBuilder.cpp#L1660) | + | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1661](../src/ui/ContextMenuBuilder.cpp#L1661) | − | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1663](../src/ui/ContextMenuBuilder.cpp#L1663) | Original resolution | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1679](../src/ui/ContextMenuBuilder.cpp#L1679) | Bypass Zoom | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1701](../src/ui/ContextMenuBuilder.cpp#L1701) | Closed Captioning CC1 (Primary) | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1702](../src/ui/ContextMenuBuilder.cpp#L1702) | Closed Captioning CC2 | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1703](../src/ui/ContextMenuBuilder.cpp#L1703) | Closed Captioning CC3 (Secondary) | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1704](../src/ui/ContextMenuBuilder.cpp#L1704) | Closed Captioning CC4 | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1730](../src/ui/ContextMenuBuilder.cpp#L1730) | Video Output Color Grading… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1732](../src/ui/ContextMenuBuilder.cpp#L1732) | Default Background | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1735](../src/ui/ContextMenuBuilder.cpp#L1735) | Black Background | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1736](../src/ui/ContextMenuBuilder.cpp#L1736) | White Background | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1751](../src/ui/ContextMenuBuilder.cpp#L1751) | Video Preview Preferences… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1752](../src/ui/ContextMenuBuilder.cpp#L1752) | Preview Device Preferences… | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1756](../src/ui/ContextMenuBuilder.cpp#L1756) | Draft | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1757](../src/ui/ContextMenuBuilder.cpp#L1757) | Preview | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1758](../src/ui/ContextMenuBuilder.cpp#L1758) | Good | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1760](../src/ui/ContextMenuBuilder.cpp#L1760) | Auto | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1763](../src/ui/ContextMenuBuilder.cpp#L1763) | Full | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1764](../src/ui/ContextMenuBuilder.cpp#L1764) | Half | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1765](../src/ui/ContextMenuBuilder.cpp#L1765) | Quarter | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1769](../src/ui/ContextMenuBuilder.cpp#L1769) | Full | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1772](../src/ui/ContextMenuBuilder.cpp#L1772) | Half | DISABLED |
| [src/ui/ContextMenuBuilder.cpp:1773](../src/ui/ContextMenuBuilder.cpp#L1773) | Quarter | DISABLED |

## Проверка и ограничения

- CMake/MinGW Debug: приложение и три test executables собираются.
- `openvegas_media_tests "[project-file],[trim],[ruler],[split],[selection]"`:
  **15 test cases, 76 assertions — passed**.
- `openvegas_video_tests "[grading]"`: **1 case, 4 assertions — passed**.
- UI smoke: **24 проверки — passed, exit 0** (включая ожидание фоновых preview jobs).
- Аудитатор выполнен; активных кандидатов в его категориях нет.
- `git diff --check`: без ошибок после финального обновления документа.

Полный `ctest --output-on-failure -j 2 --timeout 120` **не прошёл**:
audio — policy-тест в `test_vegas_shared_catalog.cpp:157` не прочитал записанное
native QSettings-значение; video и media — тайм-аут на оригинальных OFX.
В media дополнительно отмечены failures Chroma Blur process / effectIndexMap.
Это не зелёный полный regression-прогон; реальный хост сторонних плагинов требует
отдельной проверки вне ограничений этой sandbox-среды.

UI smoke использует штатные UI/model/compositor/mixer объекты приложения и пустую
тестовую реализацию PluginScanner; он не проверяет оригинальные OFX.
Проверяемые настройки MainWindow/Project Properties используют выбранный формат
QSettings; тест направляет их в временный INI-каталог. Preview jobs должны завершиться
до teardown. Runner для Windows MinGW/Ninja: `python tools/run_ui_stubs_smoke.py --build build-msvc`.

Не заявляются полная реализация отключённых функций, полное восстановление/запись
родного `.veg`, точное совпадение настроек snapshot/keyboard maps с VEGAS и pixel/DSP parity.
Автоматический сканер не видит все фабрики и не доказывает sink по одному connect;
его нулевые активные счётчики нужно сохранять вместе с ручным семантическим аудитом.
