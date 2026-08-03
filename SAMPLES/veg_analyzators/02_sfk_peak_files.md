# Peak-файлы `.sfk` (SFPK) и соседние кэши Vegas

Разбор sidecar-файлов рядом с медиа в `SAMPLES/screenshots/` и связанных кэшей проекта.

| Файл | Размер | Роль |
|------|--------|------|
| `screenshots/sample_for_project_audio.sfk` | 61 776 B | peaks для `sample_for_project_audio.wav` |
| `screenshots/sample_for_project_video.mp4.sfk` | 92 480 B | peaks для **аудиодорожки** внутри `sample_for_project_video.mp4` |
| `*.veg.sfk` | ~100–120 KB | peaks, привязанные к проекту (кэш) |
| `*.veg.sfap0` | десятки MB | **audio proxy** проекта (RIFF/WAVE), не peaks |

Скрипт повторного разбора: [`_analyze_sfk.py`](_analyze_sfk.py).

---

## 1. Зачем программе нужны `.sfk`

Официально (MAGIX / Sony Catalyst / help Vegas 21+):

- **SFK = Sound Forge / Sonic Foundry Peak file** — кэш **графических пиков waveform**, не звук.
- Vegas (и Sound Forge) создаёт их **автоматически** при добавлении аудио на таймлайн, в том числе аудио **внутри видеофайла**.
- Цель: быстро рисовать waveform и meters **без повторного полного скана** медиа.
- Имя: рядом с медиа, обычно `имя.wav` → `имя.sfk`, для видео часто `имя.mp4.sfk`.
- Можно безопасно удалить — при следующем открытии Vegas **пересоздаст** (дольше первый раз).
- Preferences → General (Vegas 22):  
  - *Hide new .sfk files*  
  - *Do not create .sfk files* (peaks в RAM, не на диск)

**Для OpenVegas:** при отрисовке audio-events можно (а) читать `.sfk` если есть, (б) строить свой peak-кэш, (в) игнорировать и рисовать simplified wave. Файл **не нужен** для открытия `.veg` / playback.

---

## 2. Формат SFPK (по нашим сэмплам, ver=1)

Magic ASCII: **`SFPK`**. Заголовок **64 байта**, далее тело пиков.

### Header

| Offset | Тип | Смысл (наблюдение) |
|--------|-----|---------------------|
| 0x00 | `char[4]` | `"SFPK"` |
| 0x04 | `u32` | версия = **1** |
| 0x08 | `u32` | размер header = **64** |
| 0x0C | `u32` | fingerprint / hash медиа (меняется с файлом) |
| 0x10 | `u32` | `16` (служебное / размер подзаголовка?) |
| 0x14 | `u32` | **число каналов** (у сэмплов = 2) |
| 0x18 | `u32` | **samples per bin** = **256** |
| 0x1C | `u32` | **число PCM-фреймов** исходного аудио |
| 0x20 | `u32` | 0 |
| 0x24 | `u32` | второй fingerprint (связан с 0x0C) |
| 0x28…0x3F | | нули в сэмплах |

### Тело

На каждый bin, на каждый канал: **`i16 min`, `i16 max`** (LE).

```
bins          = sourceFrames / samplesPerBin
bodyBytes     = bins * channels * 2 * sizeof(i16)
```

Проверка на сэмплах — **точное совпадение**:

| Файл | frames | bins | channels | body | expect |
|------|--------|------|----------|------|--------|
| `…_audio.sfk` | 1 974 784 | 7 714 | 2 | 61 712 | 61 712 |
| `…_video.mp4.sfk` | 2 957 312 | 11 552 | 2 | 92 416 | 92 416 |

### Сверка с медиа (ffprobe)

**`sample_for_project_audio.wav`**

- PCM 24-bit LE, **192 000 Hz**, stereo  
- duration ≈ **10.285 s**  
- frames = 192000 × 10.285 ≈ **1 974 784** = поле `0x1C`  
- duration из SFK при 192 kHz: **10.285 s** ✓

**`sample_for_project_video.mp4`**

- video H.264 ~67 s; audio **AAC 44100 Hz stereo**, duration ≈ **67.059 s**  
- frames = 44100 × 67.059 ≈ **2 957 312** = поле `0x1C`  
- SFK относится к **аудиопотоку MP4**, не к видеокадрам ✓

Итого: `.mp4.sfk` — это peaks **звука ролика**, чтобы на audio-track рисовать wave от embedded audio.

---

## 3. Соседние кэши (чтобы не путать)

| Расширение | Magic / вид | Назначение |
|------------|-------------|------------|
| `.sfk` | `SFPK` | peaks waveform (маленькие) |
| `.veg.sfk` | `SFPK` | peaks на уровне проекта |
| `.veg.sfap0` | `riff`… + `WAVE` | **прокси/кэш аудио** проекта (крупный PCM-подобный блоб) |
| `.veg.bak` | как `.veg` | бэкап проекта |
| `.sfap0` у медиа | — | audio proxy для тяжёлого файла (если включено) |

`.sfap0` **не** замена медиа и **не** peaks: это ускорение scrub/preview аудио. Для разбора формата `.veg` обычно не нужен.

---

## 4. Эскиз чтения в C++/Qt

```cpp
struct SfkInfo {
  quint32 version = 0;
  quint32 channels = 0;
  quint32 samplesPerBin = 0;
  quint32 sourceFrames = 0;
  QVector<qint16> minMax; // [bin][ch][min,max] flattened
};

bool readSfk(const QString& path, SfkInfo* out);
// Отрисовка: для видимого time-range выбрать bins и рисовать vertical min/max.
```

Не хранить SFK в git как обязательный артефакт продукта — только как эталон кэша Vegas.

---

## 5. VEGAS Pro 22 — дополнительная информация о продукте

Сводка по установке в репозитории + публичные источники (Steam / MAGIX readme / update history).

### Идентификация нашей сборки

| Поле | Значение |
|------|----------|
| Продукт | **VEGAS Pro 22 Steam Edition** |
| Build в `SAMPLES` | **22.0.250** (`vegas220.exe`, `install.cfg`, ScriptPortal) |
| Steam AppID | **3068400** |
| Издатель | MAGIX Software GmbH |
| Релиз Steam | ~**10–11 Sep 2024** |
| Путь (из install.cfg) | `…\Steam\steamapps\common\VEGAS Pro 22 Steam Edition\…` |

### Архитектура (из Program Files)

- Native host: `vegas220.exe` + `sharedk.dll`
- .NET / scripting: `ScriptPortal.Vegas.*` (док: `ScriptPortal.Vegas.xml`)
- UI kit MAGIX Protein; Qt 5.15 UPS; VEGASCapture (Electron-like)
- FileIO plugs, OFX (Filters, Titles, Stabilize, MagixAiFx, MagixCVFx…)
- AI/ONNX models (SAM, depth, colorization, upscale…) + OpenVINO / DirectML / OpenCV 4.9
- OpenColorIO / ACES
- Hub / WebView2 / MagixOFA

Подробная карта каталогов: [`../VEGAS-PRO-22-PROGRAM-FILES/README.md`](../VEGAS-PRO-22-PROGRAM-FILES/README.md).

### Возможности, заявленные для Pro 22 (релиз / апдейты)

Из Steam «What's new» и `readme/Vegas_readme.htm` / update history:

- **AI Auto Reframe** (1:1, 9:16 и т.п.)
- **Beat & Tempo Detection**, сетка под музыку
- **Audio Sync** / Multicam Audio Sync (переработан)
- **Text-based Editing** (полная версия)
- **AI Smart Mask 2.0**, AI DeHaze / Sharpen / Smoothen / Upscale (модели в `models\`)
- **AV1** GPU decode/encode (где есть железо)
- Новый / переработанный **Explorer**, Welcome screen
- **Auto Normalize**, **Auto Ducking** (build 194+)
- HEIC/HEIF, улучшенный BRAW / HEVC / MP3 / timecode metadata
- OFX + **VST3** экосистема
- Preferences для управления **`.sfk`** (build 93+), оптимизация peak building

Ветки билдов (ориентир): 93 → 122 → 194 → 239 → **248** (Steam news) → у нас в снимке **250**.

### Файлы и данные пользователя

| Тип | Путь / заметка |
|-----|----------------|
| Проект | `.veg` (edit decisions, не медиа) |
| Peaks | `.sfk` рядом с медиа |
| Audio proxy | `.sfap0` |
| User content | `ProgramData\VEGAS\…`, `Documents\VEGAS\…` (см. `install.cfg`) |
| Layouts | `Standard Layouts\*.VegasWindowLayout` |
| Скрипты | `Script Menu\*.cs` |

### Для OpenVegas

1. UI/поведение — `SAMPLES/veg_project/` + этот runtime как справочник.  
2. Импорт `.veg` — `docs_veg/` (не зависит от `.sfk`).  
3. Waveform — опционально читать SFPK v1 или строить свой кэш.  
4. Не тащить в репозиторий `models\` / полные FileIO бинарники как runtime зависимости.

---

## 6. Ссылки

- Sony: [What are SFK files?](https://www.sony.com/electronics/support/articles/CCCT99004)  
- FileInfo: [.sfk — Sound Forge Peak](https://fileinfo.com/extension/sfk)  
- MAGIX help (Vegas 21, актуально по смыслу): [.sfk files](https://help.magix-hub.com/video/vegas/21/en/content/topics/insertaudiotrack.htm), [Preferences General](https://help.magix-hub.com/video/vegas/21/en/content/topics/12-preferences/prefgeneraltab.htm)  
- Steam: [VEGAS Pro 22 Steam Edition](https://store.steampowered.com/app/3068400/VEGAS_Pro_22_Steam_Edition/)  
- Локальный readme: `VEGAS-PRO-22-PROGRAM-FILES/readme/Vegas_readme.htm`
