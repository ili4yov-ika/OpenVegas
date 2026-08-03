# Video / Audio Plugins Stack — план реализации

План **хостинга и обработки** аудио- и видеоплагинов поверх готового MediaEngine / playback стека.

Родительский roadmap: [`PLAN_VIDEOAUDIOSTACK.md`](PLAN_VIDEOAUDIOSTACK.md) (фазы **8–11**).  
См. также: [`ISSUES_AND_PLANS.md`](ISSUES_AND_PLANS.md), `SAMPLES/veg_project/README.md`.

**Обновлено:** 2026-08-03 (P1–P6 + VST3 `IPlugView` editor, Event FX UX, VEG Glint/AutoFrame fix, editor stretch).

---

## Цель

Сделать FX **реально звучащими и видимыми** в playback и offline render — и **не останавливаться**, пока не закрыт Plugins MVP (см. ниже) и зелёный CI по плагинам.

1. **Audio:** Builtin DSP + VST2 E2E + **VST3 lean host + `IPlugView`** + state round-trip из VEG.
2. **Video:** Builtin chain в compositor + **OFX process** + params UI + VEG `{Svfx:}` / XML roots (Glint).
3. Единый контракт `FxSlot` ↔ host ↔ UI; **unit-тесты на каждый шаг**; open-source fixtures (не proprietary Vegas DLL).
4. Укладываться в **бюджеты производительности** preview (audio callback + video frame) без деградации базового таймлайна.

## Non-goals

- LoadLibrary / dlopen **внутренних** FX Vegas Pro как «своих» hosts.
- Полный GPU realtime compositor на старте (CPU OFX → позже GPU/OpenCL опционально).
- Порт MLT effect stack / Kdenlive plugins.
- Реализация всех пунктов каталога Builtin audio сразу (Delay/Reverb — Done; остальное backlog).

---

## Режим непрерывного исполнения

План исполняется **итерациями до готовности**, без «остановились на полпути».

### Правило цикла

```text
пока Plugins MVP не закрыт:
  1. взять следующую незакрытую фазу (P1…P6 по порядку ниже)
  2. реализовать минимальный инкремент к DoD фазы
  3. добавить/обновить unit-тесты (обязательно, см. «Тест-гейт»)
  4. прогнать: openvegas_audio_tests | openvegas_video_tests | openvegas_media_tests
  5. проверить perf smoke (см. «Производительность»)
  6. если DoD фазы не выполнен — вернуться к п.2 той же фазы
  7. отметить фазу Done в таблице статусов; перейти к следующей
  8. после P6 — прогнать полный MVP checklist; баги → hotfix-итерации, не новый scope
```

### Порядок фаз (жёсткий)

**P1 → P2 → P3 → P4 → P5 → P6** (последовательно).  
Параллелить можно только подготовку фикстур/доков.  
Если P2 блокирован отсутствием Steinberg SDK: stub + `SKIP`; MVP без real VST3 **не считается закрытым**.

### Definition of Done итерации

- [ ] Код компилируется (`OpenVegas` + три test target).
- [ ] Новые/изменённые unit-тесты зелёные.
- [ ] Нет регрессии существующих plugin-related тестов.
- [ ] Обновлён статус фазы в этом файле.
- [ ] Если трогали realtime path — записан результат perf smoke (или «N/A»).

### Когда останавливаться

Только когда выполнен **«Критерии готовности Plugins MVP»** и все фазы P1–P6 либо **Done**, либо **Blocked** с write-up.

---

## Тест-гейт (unit tests — обязательно)

Без новых/обновлённых unit-тестов инкремент **не принимается**.

### Принципы

1. **Сначала тест или вместе с кодом** — для чистой логики (DSP, state pack, chain order, host process).
2. Host с DLL: фикстуры из `tests/fixtures/plugins/`; нет DLL → `SKIP` с причиной.
3. Manual — доп. проверка после зелёного CI.
4. Теги Catch2: `[plugins]`, `[vst3]`, `[ofx]`, `[builtin-dsp]`, `[perf]`, `[video-fx]`, `[state]`.

### Команды (MinGW)

```text
cmake --build build/Windows_MinGW-x64 --target openvegas_audio_tests openvegas_video_tests openvegas_media_tests -j
ctest --test-dir build/Windows_MinGW-x64 -R "openvegas_(audio|video|media)_tests" --output-on-failure
build/Windows_MinGW-x64/openvegas_media_tests.exe "[plugins]"
build/Windows_MinGW-x64/openvegas_media_tests.exe "[video-fx]"
```

### Фикстуры

- `tests/fixtures/plugins/` — open-source only; **запрещено** коммитить proprietary Vegas OFX/VST.

---

## Производительность

| Путь | Бюджет | Как мерить |
|------|--------|------------|
| Audio `processBlock` (без FX / builtin) | ≤ ~50% buffer duration | `[perf]` |
| + 1–2 Builtin FX | + ≤ 1–2 ms | `[perf]` |
| + 1 VST2/VST3 (лёгкий) | + ≤ 3–5 ms | ручной + optional `[perf]` |
| Video preview + builtins | ≤ ~16–33 ms/frame | compositor |
| + 1 CPU OFX на preview res | ≤ +8–15 ms | compositor |

Правила: instance cache; OFX на preview res; audio process только в audio thread; UI create/editor на GUI.

### Perf smoke checklist

- [ ] Play 10 s без FX — без xruns.
- [ ] + Builtin EQ / Delay — то же.
- [ ] + VST2/VST3 (если DLL есть) — то же + native editor открывается.
- [ ] Preview с Color Corrector / Chroma Blur path — UI отзывчив.

---

## Текущее состояние (база)

| Слой | Статус | Где |
|------|--------|-----|
| `FxSlot` / `PluginFormat` / VEG name map | **Done** | `AudioPluginTypes.h` (`hostKey`, Glint/Chroma Blur/…) |
| Paths Preferences | **Done** | `PluginScanner`, QSettings |
| Builtin audio: Gate / EQ / Comp / Chorus / Delay / Reverb | **Done E2E** | `BuiltinDsp` + editors (без бренда «VEGAS ») |
| VST1 / VST2 process + HWND editor + stretch | **Done E2E** | `Vst2Host`; child HWND fill on resize |
| VST3 lean host + **`IPlugView` editor** + stretch | **Done** | `Vst3Host` (`canResize`→`onSize`); CI fixture `.vst3` backlog |
| OFX | **Done MVP** | `OfxHost` + emulated Soften/Invert/Sepia; `Gain.ofx` |
| Video FX chain | **Done** | `applyVideoFxChain` |
| VEG video Event FX recovery | **Done** | `recoverVideoEventFxNames`: без Magix AutoFrame; Glint из `<Glint>` XML |
| VEG VST `CcnK` chunks | **Partial** | `state["chunk"]` + setState on create |
| Mixing Console Track FX | **Done** | strip `fx` → dialog + sync |
| Event FX UX | **Done** | Video→`VideoEventFxDialogExact`; Audio→немодальный `AudioEventFxDialog`; пустая цепочка → chooser после окна |
| Composite host | **Done** | `CompositePluginHost` + `hostKey` instance map |
| Unit tests | **Done MVP** | `[builtin-dsp]` `[ofx]` `[vst3]` `[state]` `[video-fx]` `[perf]` |

```text
ProjectModel (TrackEvent/Track/Bus .fxChain + panCrop/motion)
        │
        ├─► AudioEngine / AudioGraph
        │      BuiltinDsp | Vst2Host | Vst3Host(+IPlugView) | CompositePluginHost
        │
        └─► VideoCompositor
               PanCrop | ColorCorrector/Grading | OfxHost::process / emulated
```

---

## Принципы реализации

1. **Audio ≠ Video host.** Общий только `FxSlot` + scanner paths.
2. **Open-source fixtures + unit-тесты** раньше proprietary Vegas samples.
3. Preview и Render As — один process path.
4. `FxSlot.state` / `state["chunk"]` — единый store; instances keyed by **`hostKey`** (не raw `FxSlot*`).
5. `#ifdef OPENVGAS_HAS_VST3_SDK` — без ломки VST2/Builtin.
6. Magix AI (`de.magix:autoframe` и пр.) **не** Video Event FX chain.
7. VelvetMatter OFX без `{Svfx:…}` — recover по ASCII XML roots (`<Glint>`, …).

---

## Фазы плагинов (P0–P6)

| Фаза | Содержание | Приоритет | Статус | Связь |
|------|------------|-----------|--------|-------|
| **P0** | Инвентаризация + правила теста/perf | High | **Done** | — |
| **P1** | Builtin Delay + Reverb | Med | **Done** | polish |
| **P2** | Real VST3 + `IPlugView` | High | **Done** (CI `.vst3` fixture backlog) | фаза **8** |
| **P3** | Audio FX UX / console / editor lifetime | Med | **Done** (soft bypass fade backlog) | — |
| **P4** | Real OFX host | High | **Done** MVP | фаза **9** |
| **P5** | Video FX chain в compositor | High | **Done** | фаза **9–10** |
| **P6** | VEG state + Event FX chain truth | Med | **Partial** — CcnK + Glint/AutoFrame fix; полный OFX blob backlog | фаза **11** |

---

## P1 — Builtin Delay + Reverb — **Done**

- [x] DSP + UI + unit/perf.

---

## P2 — Real VST3 host — **Done** (fixture backlog)

### DoD

- [x] CMake `OPENVGAS_VST3_SDK_PATH` / auto `thirdparty/vst3sdk` → `OPENVGAS_HAS_VST3_SDK`.
- [x] Без SDK — stub + `SKIP`.
- [x] `IComponent` + `IAudioProcessor` + `IEditController` (lean LoadLibrary).
- [x] `process` stereo; state get/set ↔ `FxSlot.state` / chunk.
- [x] **Editor:** `IPlugView` → HWND parent Qt; `IPlugFrame` resize; `IComponentHandler` → pending params → `process`.
- [x] Stretch: `canResize()` → `onSize` под размер панели.
- [x] IID: `commoniids.cpp` (IPlugView / IPlugFrame).
- [ ] Open-source `.vst3` fixture в CI (process≠identity).
- [ ] Полный `public.sdk` `Module`/`PlugProvider` stack (опционально).

### Gaps

- Нет CI fixture `.vst3`.
- Часть плагинов может требовать больше host interfaces (`IMessage`, …).

---

## P3 — Audio FX UX — **Done** (bypass fade backlog)

### DoD

- [x] Mixing Console Track FX → `AudioEventFxDialog` + sync.
- [x] Event FX: **немодальное** окно; клик FX на клипе → Audio Event FX (не сразу Chooser).
- [x] Пустая цепочка: после открытия Event FX → Plug-In Chooser.
- [x] Video клик FX → `VideoEventFxDialogExact` (не Chooser).
- [x] VST2/VST3 editor stretch в панели.
- [ ] Soft bypass fade (щелчок) — backlog.
- [x] `hostKey` — instances переживают copy graph.

---

## P4 — Real OFX — **Done** MVP

- [x] Load/process + emulated Soften/Blur/Invert/Sepia/Gain.
- [x] Fixture `Gain.ofx`; unit tests.
- [ ] Status-bar warning UI при crash/plugin error — backlog.

---

## P5 — Video plugin chain — **Done**

- [x] `applyVideoFxChain` в preview path.
- [x] Unit `test_video_fx_chain.cpp`.
- Backlog: Shadow/Glow/blend/mask interpolate — фаза 10 родителя.

---

## P6 — VEG state / Event FX truth — **Partial**

### DoD

- [x] VST2 `CcnK`/`FPCh`/`FxCk` → `state["chunk"]` + apply on create.
- [x] **Video Event FX:** `recoverVideoEventFxNames`:
  - skip `de.magix:*` (Auto Frame / AI);
  - skip `{Svfx:…:sepia}` если следом `<Softlight>` (orphan Soft Contrast);
  - inject `{Svfx:…:glintvelvetmatter}` из `<Glint>` XML;
  - порядок по byte offset после `CountEventFXs`.
- [x] Unit `[video-fx]` на `…-reverse-fades-fx.veg` → chromablur + glint.
- [ ] Полный reverse VEG OFX/VST3 proprietary blobs.
- [ ] Matched scanned DLL parity на всех FX samples.

Эталон UI Vegas для FX sample: **Pan/Crop + Chroma Blur + Glint (Мерцание / Sparkle)** — не Auto Frame, не Sepia.

---

## Тест-матрица

| Target / tag | Покрытие |
|--------------|----------|
| `openvegas_audio_tests` `[builtin-dsp]` `[perf]` | Gate/EQ/Comp/Chorus/Delay/Reverb |
| `openvegas_media_tests` `[plugins]` `[vst3]` `[ofx]` `[state]` `[video-fx]` | Host, VEG FX recovery, chunks |
| `openvegas_video_tests` | Color/PanCrop + FX chain |
| Manual | Play + native VST editors + VEG FX sample |

---

## Критерии готовности «Plugins MVP»

1. Unit P1–P6 зелёные (или SKIP на внешний блокер).
2. VST3 + SDK: insert → process → **IPlugView** → state в сессии.
3. OFX / emulated видно в Preview.
4. Builtin Delay+Reverb слышны.
5. VEG: CcnK **или** write-up; Event FX chain совпадает с Vegas UI на FX sample.
6. Регрессии VST2 + Color Corrector + Pan/Crop зелёные.
7. Perf smoke без стабильных xruns / UI freeze.

**Статус MVP (2026-08-03):** закрыт по функционалу; остаётся CI `.vst3` fixture + полный VEG blob reverse + soft bypass fade.

---

## Краткий вердикт

**Работает:** Builtin Delay/Reverb; VST2 E2E + stretch; VST3 process/state/**IPlugView**/stretch; OFX+emulated+video chain; Mixing Console FX; VEG Glint/Chroma Blur (без лишнего AutoFrame); немодальные Event FX окна.

**Backlog:** open-source `.vst3` в CI; soft bypass fade; полный reverse VEG OFX/VST3 blobs; Shadow/Glow/blend; status-bar OFX errors.
