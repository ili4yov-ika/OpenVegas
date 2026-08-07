# Issues and plans — OpenVegas

Журнал багов, stub-фич и планов. Обновлять при каждой заметной находке (см. [`INIT.MD`](INIT.MD)).

Связанные планы: [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md), [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md), [`UI_STUBS_AUDIT.md`](UI_STUBS_AUDIT.md) — построчный аудит пустых пунктов меню / недоделанных UI-элементов, [`VEGAS_SHARED_PLUGINS_REVERSE.md`](VEGAS_SHARED_PLUGINS_REVERSE.md) — разбор бинарников реального VEGAS Shared Plug-Ins пакета (PE-экспорты, COM-структура, план реверса).

---

## Известные баги

| ID | Описание | Статус | План |
|----|----------|--------|------|
| — | _(пусто)_ | | |

---

## Video Preview pipeline (2026-08-01)

| Фаза | Статус | Содержание |
|------|--------|------------|
| V0–V5 | Done | Decode, compositor, KF, transforms, UI wire, A/V sync, tests |
| V6+ | Partial | continuous decode / HW **Done**; mask interpolate; Shadow/Glow; blend modes |

**Стек:** software CPU; часы = AudioEngine; не realtime 4K multi-layer.

---

## Render As / interchange

| Тема | Статус |
|------|--------|
| FFmpeg AAC/MP4/ProRes + HW prefer | Done |
| Render progress UI | Done |
| NLE interchange MVP (Vegas CSV / FCP7 / FCPX / Premiere scrape) | Done |
| Native `.prproj` write | Stub → FCP7/EDL |

---

## Video / Audio Stack roadmap

Полный план: [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md).  
Плагины P1–P6: [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md).

## Audio / plugins status (2026-08-03)

| Фаза | Статус | Содержание |
|------|--------|------------|
| P0–P4 Builtin / Playback / Mixer / DSP | Done | + Delay / Reverb |
| P5 VST3 | **Done** lean + `IPlugView` | SDK `thirdparty/vst3sdk`; CI `.vst3` fixture backlog |
| VST2/VST1 | Done E2E | process + HWND editor + stretch |
| OFX | Done MVP | discover/describe/process + emulated fallback; реальный Vegas `.ofx` каталогизируется и индексируется верно, но `Describe` для него не проходит (`kOfxStatErrMissingHostFeature`) — рендер всегда через emulated fallback, см. `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md` |
| Event FX UX | Done | Video/Audio немодальные окна; пустой audio chain → chooser после FX |
| VEGAS Shared → builtins | Done | `VegasSharedAudioCatalog` (map + discovery; no LoadLibrary) |
| VEG Event FX | Done (sample) | Glint из `<Glint>`; без Magix AutoFrame; unit `[video-fx]` |
| VEG chunks | Partial | `CcnK` → `state["chunk"]` |

### Архитектура (актуально)

| Слой | Файлы | Роль |
|------|-------|------|
| Audio engine | `src/audio/*` | Graph, miniaudio, BuiltinDsp, Vst2/Vst3Host |
| Типы | `AudioPluginTypes.h` | `FxSlot`, `hostKey`, VEG name map |
| Host | `CompositePluginHost` | Builtin / VST1–3 routing |
| OFX | `OfxHost` | Discover + process + emulated |
| VEG | `VegReader` | UTF-16 + `recoverVideoEventFxNames` + chunks |
| UI | `AudioEventFxDialog`, `VideoEventFxDialogExact`, Chooser | Event/Track FX |

### Внутренние / video FX

| Плагин | Статус |
|--------|--------|
| Pan/Crop + Mask | Preview + VEG + editor |
| Track Motion | Preview KF (Shadow/Glow — backlog) |
| Audio builtins | DSP + UI (без бренда «VEGAS ») |
| Color Corrector / Grading | Preview + UI |
| Chroma Blur / Glint / Sepia | VEG map + emulated/OFX path; редактор параметров теперь читает реальные params плагина (`OfxHost::paramsForSlot`) с fallback на эвристику; сам рендер через настоящий `.ofx` пока не проходит (см. backlog) |

---

## Неработающие / backlog

| Тема | Почему | План |
|------|--------|------|
| Реальный рендер через настоящие Vegas OFX бинарники | `kOfxActionLoad` → `kOfxStatErrMissingHostFeature` (см. `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md` P4) | Нужен дизасм плагина, чтобы понять недостающий host suite/свойство |
| Запись `.veg` / `.ovp` | Не реализовано | После стабильного reader |
| Soft bypass fade | Skip есть; fade нет | Plugins P3 backlog |
| CI open-source `.vst3` fixture | Нет DLL в дереве | Добавить fixture / SKIP documented |
| Golden compare vs Vegas Shared DSP | Каркас есть | `tests/fixtures/vegas_shared/` + bounce из Vegas |
| Полный VEG OFX/VST3 blob | Proprietary layout | Best-effort write-up; P6 |
| Shadow / Glow / blend / mask interpolate | Не в MVP compositor | Фаза 10 |
| Status-bar OFX crash warnings | Fail soft есть | Wiring UI |
| Customize Keyboard named maps | Catalog partial | Save As maps |
| Explorer / Generators / Transitions panels | Placeholders | По макетам |
| Relink dialog UI | Silent search | Vegas-style Relink |

---

## Планы на реализацию

| Приоритет | Задача | Заметки |
|-----------|--------|---------|
| P1 | CI `.vst3` fixture + process≠identity | Plugins P2 gap |
| P1 | ISSUES синхрон с кодом | INIT |
| P2 | Soft bypass fade | Audio UX |
| P2 | Video Shadow/Glow / blend / mask interpolate | Фаза 10 |
| P3 | Полный VEG FX blob reverse | P6 |
| P3 | Undo stack polish / named keyboard maps | |
| P4 | Native `.prproj` write (опц.) | Поверх FCP7 |

---

## Исправлено

| Дата | Что | Как |
|------|-----|-----|
| 2026-08-07 | Video Event FX: один общий слайдер «Radius» вместо реальных параметров плагина (напр. Chroma Blur — нет Vertical pixels) | `OfxHost::paramsForSlot()` (реальные params из OFX `Describe`) + `VideoEventFxDialogExact::paramsInfoForSlot()` — один источник правды для редактора и для строк кейфреймов |
| 2026-08-07 | `effectIndexMap`/`ensureModule` не находили реальные Vegas OFX бинарники (`Vfx1.ofx` и др.) — «module not found», `pluginIndex` откатывался на 0 (не тот эффект) | `ScopedOfxDllDirectory` + `ofxInstallRootForBinary` — добавляют корень установки VEGAS в DLL search path на время `LoadLibrary` (зависимости `sharedk.dll`/`OpenColorIO_2_0.dll` лежат в корне, не рядом с `.ofx`) |
| 2026-08-07 | Найден (не исправлен) более глубокий барьер: настоящие Vegas OFX плагины отвечают на `kOfxActionLoad` статусом `kOfxStatErrMissingHostFeature` → `Describe` никогда не проходит → обработка кадра всегда тихо уходит в CPU-эмуляцию | Задокументировано в `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md` (P4); нужен дизасм, чтобы понять какой host-feature не хватает |
| 2026-08-07 | Video/OFX Plug-In Chooser показывал пустой список / «Source: (none)» | `PluginChooserDialog` создавался с `scanner=nullptr` в `ContextMenuBuilder`/`VideoEventFxDialogExact`; добавлены `MainWindow::pluginScanner()` и `VideoEventFxDialogExact::setPluginScanner()` |
| 2026-08-07 | Видеоплагины VEGAS Pro (OFX) не каталогизировались из реальной установки/SAMPLES | `VegasVideoPluginCatalog` — парсинг XML-ресурсов бандлов, resolve plugin index, маппинг на `FxSlot`; `PluginScanner`/`VideoFxPane` переведены на него |
| 2026-08-07 | Рефакторинг плагинной подсистемы (дублирование normalize/root-guessing/builtin-name предикатов, мёртвый код, лишние per-frame lookups в `OfxHost::processSlot`) | Общие хелперы в `AudioPluginTypes.h`, `VegasVideoPluginCatalog::discoverUsingScanner()`, убран redundant re-lookup/`effectIndexMap` recompute на каждый кадр |
| 2026-08-03 | VEGAS Shared Plug-Ins → builtin substitutes | `VegasSharedAudioCatalog` + registry category + `[vegas-shared]` tests |
| 2026-08-03 | Video Event FX: лишний Auto Frame / Sepia вместо Glint | `recoverVideoEventFxNames` (skip Magix; Glint XML; sepia+Softlight) |
| 2026-08-03 | FX на видеоклипе → Chooser вместо Video Event FX | `onVideoEventFx` |
| 2026-08-03 | FX на аудиоклипе → Chooser / модальное окно | немодальный `AudioEventFxDialog`; chooser только если цепочка пуста (после окна) |
| 2026-08-03 | VST3 modal «IPlugView not wired» | `IPlugView` + `IPlugFrame` + stretch |
| 2026-08-03 | VST editor не растягивался | VST2 child HWND fill; VST3 `canResize`→`onSize` |
| 2026-08-03 | VST instances ломались после graph copy | `FxSlot::hostKey` |
| 2026-08-03 | VEGAS brand на Track Gate/EQ/Comp | `builtinFxDisplayName` / normalize |
| 2026-08-03 | Plugins P1–P6 MVP (Delay/Reverb, OFX, video chain, CcnK) | см. PLAN_VIDEO-AUDIO-PLUGINS-STACK |
| 2026-08-03 | Transport clock / live gain / interchange | PLAN_VIDEOAUDIOSTACK 7e/7f |
| 2026-08-01 | Video Preview compositor MVP | `src/video/*` |
| 2026-07-30 | VegReader v1 start/length/rate | Binary ticks |
| 2026-07-29 | Open `.veg` v0 + Welcome | `VegReader` + `applyVegImport` |

---

*При сомнении — сверяться с `SAMPLES/veg_project/`, скриншотами Vegas и актуальными PLAN_*.md.*
