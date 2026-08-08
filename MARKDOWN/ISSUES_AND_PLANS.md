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
| OFX | Done MVP | discover/describe/process + emulated fallback; `kOfxActionLoad`/`kOfxActionDescribe` теперь проходят для настоящего Vegas `.ofx` (`OfxMultiThreadSuite` + недостающие host-свойства — было `kOfxStatErrMissingHostFeature`); `kOfxImageEffectActionDescribeInContext` всё ещё останавливается на одном `Source`-клипе (без `Output`/params) — точная причина не найдена даже после Ghidra RE, рендер по-прежнему через emulated fallback, см. `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md` |
| Event FX UX | Done | Video/Audio немодальные окна; пустой audio chain → chooser после FX |
| VEGAS Shared → builtins | Done | `VegasSharedAudioCatalog` (map + discovery; no LoadLibrary) |
| VEG Event FX | Done (sample) | Glint из `<Glint>`; без Magix AutoFrame; unit `[video-fx]` |
| VEG Track FX (video) | Done (sample) | Sepia + Soft Contrast (`<Softlight>` XML, ранее считался «мусором») из `recoverVideoTrackFxNames`; кладётся на первую видеодорожку |
| VEG chunks | Partial | `CcnK` → `state["chunk"]` |

### Архитектура (актуально)

| Слой | Файлы | Роль |
|------|-------|------|
| Audio engine | `src/audio/*` | Graph, miniaudio, BuiltinDsp, Vst2/Vst3Host |
| Типы | `AudioPluginTypes.h` | `FxSlot`, `hostKey`, VEG name map |
| Host | `CompositePluginHost` | Builtin / VST1–3 routing |
| OFX | `OfxHost` | Discover + process + emulated |
| VEG | `VegReader` | UTF-16 + `recoverVideoEventFxNames`/`recoverVideoTrackFxNames` + chunks |
| UI | `AudioEventFxDialog` (audio Track FX), `VideoEventFxDialogExact` (video Event FX), `VideoTrackFxDialog` (video Track FX), Chooser | Event/Track FX; общие `KeyframeLaneWidgets.h` + `VegasVideoPluginCatalog::paramsInfoForSlot` |

### Внутренние / video FX

| Плагин | Статус |
|--------|--------|
| Pan/Crop + Mask | Preview + VEG + editor |
| Track Motion | Preview KF (Shadow/Glow — backlog) |
| Audio builtins | DSP + UI (без бренда «VEGAS ») |
| Color Corrector / Grading | Preview + UI |
| Chroma Blur / Glint / Sepia / Soft Contrast | VEG map + emulated/OFX path; редактор параметров теперь читает реальные params плагина (`OfxHost::paramsForSlot`) с fallback на эвристику; сам рендер через настоящий `.ofx` пока не проходит (см. backlog) |
| Video Track FX (Sepia + Soft Contrast) | `VideoTrackFxDialog` — цепочка + реальные/приблизительные параметры + keyframe lanes (Lanes/Curves), проверено вживую на `reverse-fades-fx.veg` |

---

## Неработающие / backlog

| Тема | Почему | План |
|------|--------|------|
| Реальный рендер через настоящие Vegas OFX бинарники | `Load`/`Describe` теперь проходят (см. «Исправлено»); `DescribeInContext` объявляет только `Source`-клип и возвращает `kOfxStatErrMissingHostFeature` — ни `Output`, ни params, ни один из перебираемых контекстов (Filter/General/Generator) не помогает (см. `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md` P4) | Глубокий RE (gdb + objdump + Ghidra с восстановлением RTTI, найден класс `chromablurPlugin`) не нашёл точное условие; вероятен license/version gate внутри бинарника. Отложено — приоритет смещён на точность emulated fallback |
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
| 2026-08-08 | `project_big--buck-bunny.veg` (4K60 AV1 источник) в Vegas Pro играет плавно, а в OpenVegas картинка почти замирала — счётчик `Frame:` полз вперёд, но видимая картинка не менялась по несколько секунд подряд | `VideoFrameCache::BurstDecodeJob` вставляет кадры в кэш только целиком, ПОСЛЕ завершения всего burst (до 45 кадров/процесс ffmpeg); но `prefetch()` дёргается почти на каждый тик аудио-часов (30–60 раз/сек during playback), а старая проверка «пропустить burst, если диапазон уже закэширован» смотрит только на уже ГОТОВЫЕ кадры — пока первый burst ещё декодирует, каждый следующий тик видел тот же «пустой» диапазон и запускал ЕЩЁ ОДИН перекрывающийся процесс ffmpeg, так что `kMaxInflight=6` слотов забивались КОНКУРИРУЮЩИМИ (не параллельно полезными) процессами, декодирующими один и тот же 4K/AV1 диапазон и деря друг у друга CPU — реальный кадр для текущей позиции никогда не успевал доехать. Исправлено: `VideoFrameCache` теперь помнит per-path диапазон уже АКТИВНОГО (ещё не завершённого) burst и не запускает новый, пока старый не закроет этот диапазон; индивидуальные «near-center» запросы кадров тоже больше не дублируют то, что уже покрывает burst. Проверено вживую до/после (скриншоты, `project_big--buck-bunny.veg`): до фикса — картинка застывшая при ползущем `Frame:` счётчике на 200+ кадров; после — картинка меняется каждый кадр, воспроизведение с реальной скоростью |
| 2026-08-08 | Preview `reverse-fades-fx.veg` показывал видео повёрнутым (~57°), хотя реальный Vegas Pro показывает его без поворота | `VegReader::parseTrackMotion` читал сырой `rotationZ`/`orientationZ` как радианы напрямую, но на диске это **обороты** (`1.0` = один полный оборот 360° — визуально неотличимо от отсутствия поворота, что и показывает настоящий Vegas); наш код применял `1.0` как `1.0` радиан (~57°) — заметный, ошибочный спин. Исправлено умножением на `2π` при парсинге, чтобы совпасть с радианной конвенцией, которую остальной код (`TrackMotionDialog` `kRadToDeg`/`kDegToRad`, `TrackMotionApply::rotate`) уже использует; проверено вживую (скриншот превью) + unit `VegReader converts Track Motion rotationZ from turns to radians`. Ранее вообще не было тестов на `parseTrackMotion`/`rotationZ` |
| 2026-08-07 | Glint fallback-параметры (`VegasVideoPluginCatalog::paramsInfoForSlot`) — всего 3 (`Threshold`/`Boost`/`Gain`), причём `Gain` не является реальным параметром плагина вовсе | Заменено на полный набор из `<Glint>` VEG XML-схемы (11 параметров: `Threshold`, `Boost`, `HorizontalRadius`, `VerticalRadius`, `Hue`, `HueSweep`, `Saturation`, `Rotation`, `Streaks`, `ReduceFlicker`, `EffectOnly`) — совпадает с реальным диалогом Vegas (вкладка Effect; Mask вне охвата этой плоской таблицы); unit `Glint fallback params match the real com.vegascreativesoftware:glintvelvetmatter set` |
| 2026-08-07 | Настоящие Vegas OFX плагины не проходили `kOfxActionLoad` (`kOfxStatErrMissingHostFeature`) — минимальный host не реализовывал `OfxMultiThreadSuite` и не объявлял часть OFX 1.4 host-свойств (`kOfxPropVersion`, `kOfxImageEffectPropSupportsOverlays`, `kOfxImageEffectInstancePropSequentialRender`, animation-support флаги параметров) | Реализован `OfxMultiThreadSuiteV1` (`std::thread`-backed) + добавлены недостающие host-свойства в `initHostProps()`; `kOfxActionLoad` и `kOfxActionDescribe` теперь успешно проходят для реального `Vfx1.ofx` (Chroma Blur) — впервые в проекте. Также: `DescribeInContext` перебирает `Filter → General → Generator` вместо одного жёстко заданного контекста. Regression-тест `OfxHost::load Load+Describe succeed for a real Vegas OFX plug-in`. `DescribeInContext` всё ещё не проходит до конца — см. backlog и `PLAN_VIDEO-AUDIO-PLUGINS-STACK.md` P4 |
| 2026-08-07 | На видеодорожке `reverse-fades-fx.veg` были эффекты (Sepia + Soft Contrast), но OpenVegas показывал пустую цепочку — `{Svfx:…:sepia}` + `<Softlight>` XML считались «мусором» и просто отбрасывались, никуда не попадая | `VegReader::recoverVideoTrackFxNames` + `ProjectModel::applyVideoTrackFxFromVeg` — распознают ту же пару как **Video Track FX** (эффект `com.vegascreativesoftware:softcontrastvelvetmatter`, пресет «Soft Moderate Contrast», сверено с XML каталога) и кладут на первую видеодорожку; unit + сверено вживую скриншотом |
| 2026-08-07 | Video Track FX открывал тот же диалог, что и аудио Track FX (`AudioEventFxDialog`) — без параметров/кейфреймов для видео-OFX | Новый `VideoTrackFxDialog` (цепочка + реальные/fallback параметры + keyframe lanes), общий `ui/KeyframeLaneWidgets.h` (вынесен из `VideoEventFxDialogExact`) и `VegasVideoPluginCatalog::paramsInfoForSlot()` как единый источник для Event/Track FX; `MainWindow::onTrackFx` ветвится по `TrackKind::Video`; починен мёртвый пункт «Track FX…» в контекстном меню пустой видеодорожки |
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
