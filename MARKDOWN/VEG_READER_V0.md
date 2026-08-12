# Открытие проектов `.veg` в OpenVegas (VegReader)

Документ описывает **реализованный** импортёр проектов VEGAS Pro 22: архитектуру, формат, API, UX и ограничения.  
Уровни: **v0** (header/media/labels) → **v1** (start/length/rate, fades) → **Event FX recovery** (2026-08-03).  
Для сырого реверса бинарника см. также [`SAMPLES/docs_veg/`](../SAMPLES/docs_veg/README.md) и обзорный план [`QT68_PORT_AND_VEG_OPEN.md`](QT68_PORT_AND_VEG_OPEN.md).  
Плагины / FX chain: [`PLAN_VIDEO-AUDIO-PLUGINS-STACK.md`](PLAN_VIDEO-AUDIO-PLUGINS-STACK.md).

Краткая шпаргалка: [`docs/VEG_OPEN.md`](../docs/VEG_OPEN.md).

---

## 1. Цель v0

Дать пользователю рабочее **File → Open** / Welcome / CLI:

1. Проверить, что файл — `.veg` Vegas Pro (magic + header).
2. Прочитать свойства проекта (version, sample rate, fps, tempo, кадр).
3. Извлечь **media pool** (абсолютные UTF-16 пути к `.mp4` / `.wav` / …).
4. Собрать **лейблы событий** (например `sample_for_project_*`).
5. Заполнить UI: Project Media, эвристический timeline, preview meta, статус-бар.
6. Попытаться **relink** медиа, если абсолютный путь с другой машины битый.

**Не цель v0:** точные `start` / `length` / fades / crossfades / track index / playback rate из плотного бинарного хвоста (это **v1/v2** — частично **Done**).

---

## 2. Файлы в репозитории

| Путь | Роль |
|------|------|
| `src/io/VegReader.h` / `.cpp` | Парсер + timings + `recoverVideoEventFxNames` + chunks |
| `src/model/ProjectModel.{h,cpp}` | `applyVegImport()`, media pool, tracks, FX slots |
| `src/plugins/AudioPluginTypes.h` | VEG display name → `FxSlot` map |
| `src/app/MainWindow.cpp` | Open / Welcome / CLI / Event FX dialogs |
| `src/ui/WelcomeDialog.cpp` | Список `SAMPLES/example_project_*.veg`, double-click → open |
| `src/app/main.cpp` | CLI: `OpenVegas.exe path\to\file.veg` |
| `tests/test_plugin_state.cpp` | `[video-fx]` Glint / AutoFrame regression |
| `SAMPLES/example_project_*.veg` | Эталонные проекты Vegas Pro 22 |
| `SAMPLES/veg_project/` | FX / reverse / fades samples |
| `SAMPLES/docs_veg/` | Реверс-документация и скрипты анализа |

Сборка: цель CMake / qmake включает `src/io/VegReader.cpp`.

---

## 3. Формат `.veg` (кратко)

Проприетарный файл **edit decisions** (не контейнер медиа):

```
[0x00] Outer header 64 B   magic "riff" + const + size + 2×GUID + early blob size
[0x40] Project properties  version @0x46, sampleRate @0x4C, fps @0x50, tempo @0x58
[~0xF8] UTF-16LE paths     свой .veg, Documents, AppData\VEGAS Pro\22.0\
[…]    ProjectNotes        .NET-подобные ASCII type names (игнорируем)
[…]    Media pool          UTF-16 пути .mp4/.wav + маркеры 1920/1080
[…]    Timeline / events   плотный бинарь + UTF-16 лейблы клипов
[…]    Track FX strings    "VEGAS Track Compressor/EQ/…" (часто дефолт)
```

Обязательная проверка header:

| Offset | Ожидание |
|--------|----------|
| `0x00` | `"riff"` (lowercase) |
| `0x04` | 12 байт `2E 91 CF 11 A5 D6 28 DB 04 C1 00 00` |
| `0x10` | `u64` LE = размер файла |
| `0x18` / `0x28` | GUID Sonic Foundry (`…00C04F8EDB8A`) |
| `0x46` | `u16` версия (22) |
| `0x4C` | `u32` sample rate (48000) |
| `0x50` | `f64` frame rate (~59.94) |
| `0x58` | `f64` tempo BPM (120) |

Подробности: [`SAMPLES/docs_veg/00_format_overview.md`](../SAMPLES/docs_veg/00_format_overview.md), [`01_header_and_props.md`](../SAMPLES/docs_veg/01_header_and_props.md).

---

## 4. API `VegReader`

```cpp
struct VegHeaderInfo {
  quint64 fileSize = 0;
  quint16 vegasVersion = 0;   // 22
  quint32 sampleRate = 0;     // 48000
  double frameRate = 0;       // 59.94…
  double tempoBpm = 0;        // 120
  int width = 0;              // 1920 (скан / fallback)
  int height = 0;             // 1080
};

struct VegOpenResult {
  VegHeaderInfo header;
  QStringList mediaPaths;     // UTF-16 пути медиа
  QStringList eventLabels;    // sample_for_project_* и т.п.
  QString projectPathHint;    // путь к .veg изнутри файла
  QStringList warnings;
  QString sourcePath;         // фактически открытый путь
};

class VegReader {
public:
  static bool looksLikeVeg(const QByteArray& data);
  static VegOpenResult open(const QString& path, QString* error = nullptr);
};
```

### Алгоритм `open()`

1. Прочитать файл целиком в `QByteArray`.
2. `looksLikeVeg` → иначе `error` и пустой результат.
3. Прочитать поля header; при `filesize` mismatch / странном fps/sr — `warnings`, с разумными fallback.
4. Скан UTF-16LE printable-строк (≥4 символов).
5. Классификация строк:
   - `*.veg` → `projectPathHint`
   - пути с медиа-расширением (не AppData/Documents) → `mediaPaths` (уникальные)
   - `sample_for_project_*` / `*untitled*` без `\` `/` → `eventLabels`
   - если лейблов нет — basename медиа-файлов
6. Вернуть `VegOpenResult` (даже с warnings).

Медиа-расширения: `.mp4 .mov .avi .mkv .webm .mpg .mpeg .mxf .wav .mp3 .aif .aiff .flac .png .jpg .jpeg .tga .tif .tiff`.

---

## 5. Заполнение `ProjectModel`

`ProjectModel::applyVegImport(const VegOpenResult&, const QString& openedPath)`:

1. `loadEmptyProject()`, затем выставить fps / sample rate / tempo / размер кадра из header.
2. Для каждого media path:
   - `resolveMediaPath()` — если файла нет, искать по имени рядом с `.veg`, в `screenshots/`, `assets/`, родительских каталогах.
   - Добавить `MediaItem { path, displayName, kind, missing }`.
3. Разнести video / audio / still.
4. Построить эвристический timeline:
   - Video track из video-лейблов (или video media names).
   - Audio track из audio-лейблов (или audio media names).
   - События с дефолтной длиной ~8 с, при нескольких клипах — лёгкий overlap по времени (заготовка под будущие CF).

Это **не** точная EDL Vegas — визуальный каркас для QA и дальнейшей разработки парсера.

---

## 6. UX

| Действие | Поведение |
|----------|-----------|
| **File → Open…** | Диалог, старт в `SAMPLES/` если есть; фильтр `*.veg` |
| **Welcome → Browse…** | Тот же `onOpenProject` |
| **Welcome → recent** | Список `SAMPLES/example_project_*.veg`; double-click → `openProjectPath` |
| **CLI** | `OpenVegas.exe D:\path\project.veg` — без Welcome |
| После открытия | Заголовок окна = имя файла; Project Media карточки; timeline; preview Project/Preview fps; status bar 12 с |

Статус-бар пример:

```text
VEGAS Pro 22 · 48000 Hz · 59.940 fps · 1920×1080  |  Media pool: 2 file(s)  |  Event labels: …  |  N media missing…
```

Missing media не блокирует открытие: пути показываются в meta карточек (`Missing: …`).

---

## 7. Эталонные проекты

| Файл | Ожидание v0 |
|------|-------------|
| `example_project_with_only_audio.veg` | Audio ~10.3 s из бинарных timings |
| `example_project_with_only_video.veg` | Video ~67 s (+ paired take audio) |
| `example_project_with_video_and_audio.veg` | A/V group ~67 s + orphan wav |
| `…_crossfade.veg` / fades / trimmers / 2_videos | Реальные start/length/rate; overlap→fade |

Карточки: [`SAMPLES/docs_veg/files/`](../SAMPLES/docs_veg/files/README.md).

Повторный анализ:

```bash
python SAMPLES/docs_veg/_analyze_veg.py
python SAMPLES/docs_veg/_analyze_deep.py
python SAMPLES/docs_veg/_analyze_props.py
```

---

## 8. Ограничения и известные пробелы

1. **Media in/out** отдельно от timeline length, markers, полные CF-кривые — ещё не разобраны (v2).
2. Файл **не portable** без медиа; relink эвристический по имени + warning dialog.
3. Строки `VEGAS Track Compressor/EQ/Noise Gate` часто дефолт Vegas, не пользовательский FX.
4. Число UTF-16 лейблов ≠ числу UI-клипов 1:1 — счётчик событий берётся из timing-блоков.
5. ProjectNotes (.NET) игнорируются.
6. `.veg.bak`, `.sfk`, `.veg.sfap0` не читаются и не нужны для open.
7. Запись `.veg` / `.ovp` отсутствует.
8. **OFX parameter blob — не decoded**, и это не косметика: слот приезжает с пустой картой
   параметров, инстанс берёт объявленные плагином дефолты (у Chroma Blur `HorizontalPixels`/
   `VerticalPixels` = `0`), и эффект VEGAS в превью **визуально не работает**, хотя хост его
   честно грузит и рендерит. Значения в файле лежат читаемо (UTF-16 имя + double'ы + кейфреймы) —
   это работа по парсеру. Замеры: [`PLAN_OFX_VIDEO_PLUGINS.md`](PLAN_OFX_VIDEO_PLUGINS.md).
   VST3 blob — тоже не decoded. Legacy-эффекты VEGAS, хранящие состояние XML-ом, читаются
   полностью — см. ниже. Имена Event FX — best-effort.

**Сделано в v1:** паттерн `start/length/rate` (ticks÷1e7), A/V pairing, overlap fades, zoom-to-fit, last open dir.

**Сделано в Event FX recovery (2026-08-03):** `recoverVideoEventFxNames` — не брать Magix AutoFrame; inject Glint из ASCII `<Glint>`; не путать Softlight-adjacent sepia с Event FX. Sample: `project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg`. Unit: `tests/test_plugin_state.cpp` `[video-fx]`.

### Состояние legacy-плагинов VEGAS (2026-08-12)

`parseLegacyVideoFxStates` → `VegOpenResult::legacyFxStates`.

Не всякий видеоэффект VEGAS — OFX. Перечисление `OfxGetPlugin` по всем установленным бандлам
показывает 78 эффектов в `Vfx1.ofx` против 102 в его манифесте: **Glint («Мерцание») и
Soft Contrast среди экспортируемых отсутствуют**. Это pre-OFX плагины, и их состояние лежит в
`.veg` не блобом OFX-параметров, а обычным XML (`<Glint>`, `<Softlight>`). Раньше из него бралось
только имя эффекта, и все параметры в UI показывали выдуманные дефолты.

Раскладка (восстановлена по `project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg`):

```text
[имя пресета, UTF-16]  [<?xml … <Root>…</Root>]        ← текущее значение плагина
[u32 0x1c] [u64 время, тики 1e7] [16 нулей] [u32 size] [u32 size] [<?xml …]   ← кейфрейм
… повторяется на каждый кейфрейм
```

То есть тег `0x1c` лежит за 32 байта до `<?xml`, время — за 28. Блоб **без** такого разбега —
не кейфрейм, а текущее значение; блоб с неразобранным разбегом пропускается, а не превращается
в кейфрейм на нуле (иначе анимация выдумывалась бы на ровном месте).

Значения приводятся к единицам, которые показывает диалог VEGAS: доли → проценты, углы остаются
градусами, `true`/`false` → 1/0. Вложенные `<Mask>` / `<VignetteEffect>` намеренно не разбираются —
их одноимённые элементы (`Enabled`, `Strength`) затирали бы значения самого эффекта.

Кейфреймы `ProjectModel` кладёт в `automationLanes` события — лейн заводится только для параметров,
которые реально меняются (VEGAS пишет в каждый блоб все). Разбор: [`PLAN_OFX_VIDEO_PLUGINS.md`](PLAN_OFX_VIDEO_PLUGINS.md).
Unit: `tests/test_plugin_state.cpp` `[plugins][state][veg][video-fx]`.

---

## 9. Дорожная карта парсера

| Уровень | Scope |
|---------|--------|
| **v0** | Header + media pool + labels + UI apply + relink + CLI |
| **v1** | Binary `start/length/rate` (ticks/1e7); A/V pairing; overlap fades; open UX |
| **Event FX (сейчас)** | `recoverVideoEventFxNames`; CcnK chunks → `state["chunk"]`; legacy XML-состояние (Glint / Soft Contrast) — значения, кейфреймы, пресет; полный OFX/VST3 blob — backlog |
| **v2** | Media in/out, markers, track index, полные CF curves |
| **Writer** | Сохранение OpenVegas-native (например `.ovp`) и/или экспорт subset `.veg` |

Связанные UI-задачи: Trimmer на media из pool, диалог Relink; Video/Audio Event FX — немодальные окна.

---

## 10. Быстрая проверка

```text
cmake --build build
build\OpenVegas.exe SAMPLES\example_project_with_video_and_audio.veg
```

Ожидание:

- Окно `example_project_with_video_and_audio.veg - OpenVegas`
- В Project Media — карточки по найденным путям (или Missing)
- На timeline — Video 1 / Audio 1 с длительностями из `.veg` (не фиксированные 8 с)
- Preview footer: fps из header (~59,940p)
- Status bar: `Timeline: N event block(s) from .veg`

---

## 11. Лицензия / IP

Парсер и документация OpenVegas — **GPL-3.0-or-later**.  
Формат `.veg`, runtime Vegas и материалы в `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/` принадлежат MAGIX/VEGAS и **не** покрываются GPL; используются только как справочные эталоны. Не коммитить `vegas-runtime/` и бинарники Vegas.
