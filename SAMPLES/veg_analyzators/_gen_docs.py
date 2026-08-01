# -*- coding: utf-8 -*-
import struct, re, hashlib, json
from pathlib import Path

DIR = Path(r"d:\Devs\C++\OpenVegas\sample_ui")
OUT = DIR / "docs_veg"
OUT.mkdir(exist_ok=True)
files = sorted(DIR.glob("*.veg"))

def u16(b,o): return struct.unpack_from("<H", b, o)[0]
def u32(b,o): return struct.unpack_from("<I", b, o)[0]
def u64(b,o): return struct.unpack_from("<Q", b, o)[0]
def f64(b,o): return struct.unpack_from("<d", b, o)[0]
def guid(b,o):
    d1,d2,d3 = struct.unpack_from("<IHH", b, o)
    d4 = b[o+8:o+16]
    return f"{d1:08X}-{d2:04X}-{d3:04X}-{d4[0]:02X}{d4[1]:02X}-{d4[2:8].hex().upper()}"

def _u16_char_ok(c):
    """ASCII printable + Cyrillic + common path punctuation."""
    if 32 <= c <= 126:
        return True
    if 0x0400 <= c <= 0x04FF:  # Cyrillic
        return True
    return False

def utf16_all(data, minlen=4):
    """Extract UTF-16LE runs (ASCII + Cyrillic) for paths/labels."""
    out=[]; i=0
    while i+3 < len(data):
        c0 = data[i] | (data[i+1] << 8)
        if _u16_char_ok(c0):
            j=i; s=[]
            while j+1 < len(data):
                c = data[j] | (data[j+1] << 8)
                if not _u16_char_ok(c):
                    break
                s.append(chr(c)); j+=2
            if len(s)>=minlen:
                out.append((i,"".join(s))); i=j; continue
        i+=1
    return out

def find_playback_rates(data):
    """Heuristic: f64 rate flanked by zero u64s; nearby u64 ≈ timeline length in 1e7 ticks/sec."""
    hits=[]
    for off in range(0, len(data)-16, 8):
        rate = f64(data, off)
        if not (0.05 < rate < 10.0) or abs(rate - 1.0) < 1e-6:
            continue
        if off < 16:
            continue
        if u64(data, off-8) != 0 or u64(data, off+8) != 0:
            continue
        length = u64(data, off-16)
        if length < 1000 or length > 10**12:
            continue
        hits.append({
            "off": off,
            "rate": rate,
            "pct": rate * 100.0,
            "len_ticks": length,
            "len_sec": length / 1e7,
        })
    return hits

def ascii_all(data, minlen=5):
    out=[]; cur=[]; start=0
    for i,x in enumerate(data):
        if 32<=x<=126:
            if not cur: start=i
            cur.append(chr(x))
        else:
            if len(cur)>=minlen: out.append((start,"".join(cur)))
            cur=[]
    if len(cur)>=minlen: out.append((start,"".join(cur)))
    return out

def hexdump(data, start, length, width=16):
    lines=[]
    end=min(len(data), start+length)
    for row in range(start, end, width):
        chunk=data[row:row+width]
        hx=" ".join(f"{x:02X}" for x in chunk)
        asc="".join(chr(x) if 32<=x<=126 else "." for x in chunk)
        lines.append(f"{row:06X}  {hx:<{width*3}}  {asc}")
    return "\n".join(lines)

infos=[]
for fp in files:
    d=fp.read_bytes()
    u16s=utf16_all(d)
    asci=ascii_all(d)
    media=[s for o,s in u16s if re.search(r"(?i)\.(mp4|wav|mov|avi|mpg)$", s)]
    labels=[s for o,s in u16s if s.startswith("sample_for_project")]
    fx=[s for o,s in u16s if s.startswith("VEGAS ")]
    paths=[s for o,s in u16s if ":\\" in s]
    types=[s for o,s in asci if "ScriptPortal" in s or "ProjectNotes" in s]
    proj_path=next((s for o,s in u16s if o>=0xF0 and ".veg" in s and (":\\" in s or s.startswith("D:") or s.startswith("C:"))), "?")
    if proj_path == "?":
        proj_path=next((s for o,s in u16s if o>=0xF0 and (".veg" in s or s.startswith("D:\\") or s.startswith("C:\\"))), "?")
    rates = find_playback_rates(d)
    # Event FX plug-in ids (Svfx:...)
    svfx = sorted({s for o,s in u16s if s.startswith("{Svfx:")})
    info={
        "path": fp,
        "name": fp.name,
        "size": len(d),
        "md5": hashlib.md5(d).hexdigest(),
        "ver": u16(d,0x46),
        "sr": u32(d,0x4C),
        "fps": f64(d,0x50),
        "tempo": f64(d,0x58),
        "field56": u64(d,0x38),
        "filesize_field": u64(d,0x10),
        "guid_root": guid(d,0x18),
        "guid_type": guid(d,0x28),
        "const4_16": d[4:16].hex(),
        "media": media,
        "labels": labels,
        "label_set": sorted(set(labels)),
        "fx_set": sorted(set(fx)),
        "fx_count": len(fx),
        "svfx": svfx,
        "paths": paths,
        "types": types,
        "proj_path": proj_path,
        "rates": rates,
        "data": d,
        "u16s": u16s,
    }
    infos.append(info)

# ---------- Overview ----------
overview = f'''# Формат `.veg` (VEGAS Pro) — обзор по сэмплам OpenVegas

Документация собрана **реверс-инжинирингом бинарных файлов** из `sample_ui/*.veg`
(созданы в **VEGAS Pro 22**). Формат **проприетарный**, полной публичной спецификации нет.
Ниже — рабочая модель, достаточная для OpenVegas (чтение метаданных, путей медиа, оценка состава таймлайна).

## Что такое `.veg`

- Файл **проекта** NLE (Sonic Foundry → Sony → MAGIX VEGAS Pro).
- Хранит **edit decisions**: треки, события (клипы), тримы, фейды/crossfade, FX, настройки проекта.
- **Не содержит** сами медиаданные — только **ссылки** (абсолютные пути) и параметры.
- Контейнер — **кастомный RIFF-подобный** поток: magic `riff` (lowercase), далее GUID-ы Sonic Foundry и бинарные блоки.
- Типичный размер сэмплов: **12–20 KB** (без встроенного видео).

## Общая схема файла

```
+-------------------+
| Outer header 64B  |  magic + const + size + 2x GUID + size_blob
+-------------------+
| Project props     |  version, sample rate, fps, tempo, флаги...
+-------------------+
| Identity / paths  |  UTF-16LE: путь к .veg, Documents, AppData\\VEGAS Pro\\22.0\\
+-------------------+
| ProjectNotes blob |  .NET BinaryFormatter-подобные имена типов ScriptPortal.Vegas.*
+-------------------+
| Media pool        |  UTF-16LE пути к .mp4 / .wav (+ размеры кадра 1920x1080 и т.п.)
+-------------------+
| Timeline / events |  события, лейблы клипов, тримы; растёт с числом events
+-------------------+
| Track FX defaults |  "VEGAS Track Compressor/Noise Gate/EQ" (часто на каждый аудио-контекст)
+-------------------+
| Trailer / dirs    |  снова AppData / Documents (служебные пути сессии)
+-------------------+
```

## Outer header (offset 0x00, 64 байта)

| Offset | Тип | Наблюдение |
|--------|-----|------------|
| 0x00 | `char[4]` | `"riff"` (не классический `RIFF`) |
| 0x04 | 12 bytes | **Константа** во всех сэмплах: `2E 91 CF 11 A5 D6 28 DB 04 C1 00 00` |
| 0x10 | `u64` LE | **Размер всего файла** (равен `filesize`) |
| 0x18 | GUID | `46C429EF-904A-11D2-8722-00C04F8EDB8A` (корневой/продуктовый) |
| 0x28 | GUID | `B28F2D5A-230F-11D2-86AF-00C04F8EDB8A` (тип документа) |
| 0x38 | `u64` LE | Размер «раннего» блоба (~3800–3850): props + пути + ProjectNotes до медиа-пула |

Суффикс GUID `00C04F8EDB8A` характерен для старых CLSID Sonic Foundry.

## Project properties (с ~0x40)

Во всех 8 файлах одинаково:

| Offset | Значение | Смысл |
|--------|----------|--------|
| 0x46 | `u16 = 22` | Версия VEGAS Pro (**22**) |
| 0x4C | `u32 = 48000` | Sample rate проекта |
| 0x50 | `f64 ≈ 59.94006` | Frame rate (59.94p) |
| 0x58 | `f64 = 120.0` | Tempo (BPM), дефолт Vegas |

Далее — флаги/счётчики (не полностью расшифрованы), затем около **0xF8** начинается UTF-16LE путь к самому `.veg`.

Также в файлах встречаются `u32` **1920** и **1080** (несколько раз) — размеры кадра проекта/медиа.

## Строки и кодировки

- Пути и имена клипов: **UTF-16LE**.
- Имена .NET-типов Project Notes: **ASCII** (`ScriptPortal.Vegas.ProjectNotes.*`, `Version=22.0.0.250`).
- Служебные каталоги: `…\\AppData\\Local\\VEGAS Pro\\22.0\\`, `…\\Documents`.

## Что растёт с «сложностью» проекта

| Файл (роль) | Size | Лейблы events* | Медиа-пути | Примечание |
|-------------|------|----------------|------------|------------|
| only_audio | 12200 | 1 | wav+mp4 в пуле | Самый маленький таймлайн |
| only_video | 13600 | 2 | wav+mp4 | Видео-события без аудио на TL |
| video_and_audio | 14256 | 3 | wav+mp4 | Базовый A/V |
| +crossfade | 14288 | 3 | wav+mp4 | +~32 байта / мелкие правки vs base |
| 2_videos | ~17.8K | 0 sample_* | `1untitled.mp4` | Другой медиа-набор |
| +trimmers | 19448 | **12** | wav+mp4 | Много сегментов (тримы) |

\\*Подсчёт вхождений строк `sample_for_project_*` (не обязательно = число UI-клипов 1:1, но коррелирует).

**Вывод:** основной «вес» `.veg` после фиксированного префикса (~3.8 KB notes+props) дают **записи событий/медиа**, а не сырое видео.

## Общие рассуждения для OpenVegas

1. **Парсер v0** может ограничиться: проверка magic/`riff`, чтение fps/sr/version, извлечение UTF-16 путей `.mp4`/`.wav`, подсчёт event-лейблов.
2. **Полный таймлайн** (in/out, track index, crossfade curves) — плотные бинарные структуры без FourCC; нужны дополнительные сэмплы с контролируемыми изменениями (один клип сдвинули на N кадров → diff).
3. **ProjectNotes** сериализованы в стиле .NET — можно вырезать/игнорировать при импорте.
4. **FX-строки** `VEGAS Track *` часто присутствуют как дефолтные цепочки даже в простых проектах — не всегда значат, что пользователь явно повесил FX на клип.
5. Файлы **не portable** без медиа: абсолютные пути Windows. Для OpenVegas нужен relink / media search.
6. Два файла с «кракозябрами» в имени — копии `2_videos` с чуть другой длиной пути (field56/size отличаются на длину имени).

## Индекс документов

- [00_format_overview.md](00_format_overview.md) — этот файл
- [01_header_and_props.md](01_header_and_props.md) — детальный разбор заголовка
- [files/](files/) — карточка по каждому `.veg`
- `_deep_analysis.json`, `_analysis_raw.json` — сырой вывод скриптов
- `_analyze_*.py` — воспроизводимые анализаторы

## Источники / ограничения

- Публично: «VEG = Vegas project, RIFF-related, references not media» (filext, file-extensions.com и др.).
- Детали полей — **только из наших сэмплов**; имена полей гипотетические, помечены как наблюдения.
'''

(OUT/"00_format_overview.md").write_text(overview, encoding="utf-8")

header_doc = '''# Заголовок и свойства проекта `.veg`

## Hex dump начала (пример: `example_project_with_video_and_audio.veg`)

```
000000  72 69 66 66 2E 91 CF 11 A5 D6 28 DB 04 C1 00 00  riff......(.....
000010  B0 37 00 00 00 00 00 00 EF 29 C4 46 4A 90 D2 11  .7.......).FJ...
000020  87 22 00 C0 4F 8E DB 8A 5A 2D 8F B2 0F 23 D2 11  ."..O...Z-...#..
000030  86 AF 00 C0 4F 8E DB 8A E8 0E 00 00 00 00 00 00  ....O...........
000040  B8 00 00 00 00 00 16 00 05 00 00 00 80 BB 00 00  ................
000050  28 6B 55 E2 53 F8 4D 40 00 00 00 00 00 00 5E 40  (kU.S.M@......^@
```

Интерпретация:

- `0x10`: `B0 37 00 00 00 00 00 00` → `0x37B0` = **14256** = размер файла
- `0x18`…`0x27`: GUID `46C429EF-904A-11D2-8722-00C04F8EDB8A`
- `0x28`…`0x37`: GUID `B28F2D5A-230F-11D2-86AF-00C04F8EDB8A`
- `0x38`: `E8 0E 00 00…` → **3816** — длина раннего блоба
- `0x46`: `16 00` → **22** (Pro 22)
- `0x4C`: `80 BB 00 00` → **48000** Hz
- `0x50`: double **≈59.94006**
- `0x58`: double **120.0** BPM

## Ранний блоб (field @0x38)

Во всех сэмплах ≈ **3800–3850 байт**. Внутри:

1. Числовые props / GUID сессии
2. UTF-16 путь к `.veg` (старт обычно **0xF8**)
3. `C:\\Users\\…\\Documents`
4. `C:\\Users\\…\\AppData\\Local\\VEGAS Pro\\22.0\\`
5. ASCII/.NET кусок **ProjectNotes** (`ScriptPortal.Vegas.ProjectNotes.ProjectNotesList`, `ProjectNoteItem`, `Version=22.0.0.250`)

Длина блоба **слегка растёт** с длиной пути к файлу проекта (видно на копиях `2_videos-*`).

## Медиа-пул (после notes)

Абсолютные UTF-16 пути:

- `…\\screenshots\\sample_for_project_audio.wav`
- `…\\screenshots\\sample_for_project_video.mp4`
- или `C:\\Users\\Admin\\Videos\\1untitled.mp4` (в проектах с 2 videos)

Рядом бинарно встречаются **1920/1080** (размер кадра).

## Таймлайн / события

Плотный бинарный хвост. Ориентиры:

- Повторяющиеся UTF-16 лейблы `sample_for_project_video` / `_audio`
- В trimmers-проекте лейблов **намного больше** при тех же 2 медиа-файлах → сегментация/тримы
- Crossfade vs base: почти тот же размер (+32 B), много мелких byte-diff (параметры фейдов/позиций), не новый медиафайл

## Track FX strings

Почти везде:

- `VEGAS Track Compressor`
- `VEGAS Track Noise Gate`
- `VEGAS Track EQ`

Повторы связаны с числом аудио-контекстов/треков, не обязательно с ручной расстановкой FX пользователем.

## Сравнение размеров (интуиция)

```
only_audio < only_video < video+audio ≈ crossfade < 2_videos≈compressed/stretched < trimmers < 2_videos(+FX)
   12.2K       13.6K         14.3K         14.3K              ~17.8K                    19.4K     18.5K
```

Сжатие/растяжение клипа (**playback rate**) почти не меняет размер файла относительно sibling `2_videos` без FX — в бинарнике это в основном `f64` rate + длины событий, а не новые медиа.
'''
(OUT/"01_header_and_props.md").write_text(header_doc, encoding="utf-8")

# per-file docs
files_dir = OUT / "files"
files_dir.mkdir(exist_ok=True)

index_lines = ["# Карточки файлов `.veg`\n", "\n| Файл | Size | Events labels | Media | Rate | Doc |\n|------|------|---------------|-------|------|-----|\n"]

for info in infos:
    stem = info["name"]
    # safe filename for doc
    safe = re.sub(r'[<>:"/\\\\|?*]', "_", stem)
    if not safe.endswith(".md"):
        doc_name = safe + ".md"
    else:
        doc_name = safe
    # nicer: replace .veg with .md
    doc_name = safe[:-4] + ".md" if safe.lower().endswith(".veg") else safe + ".md"

    role = "unknown"
    n = info["name"].lower()
    embedded = (info["proj_path"] or "").lower()
    if "trimmers" in n: role = "Видео+аудио с несколькими тримами/сегментами"
    elif "crossfade" in n: role = "Видео+аудио с crossfade"
    elif "only_audio" in n: role = "Только аудио на таймлайне"
    elif "only_video" in n: role = "Только видео на таймлайне"
    elif "one_compressed" in n or "сжато" in embedded or "сжато" in n:
        role = "Два видеоклипа: одно событие сжато (ускорено, playback rate > 100%)"
    elif "one_stretched" in n or "растянут" in embedded or "растянут" in n:
        role = "Два видеоклипа: одно событие растянуто (замедленно, playback rate < 100%)"
    elif "2_videos" in n: role = "Проект с двумя видео / иным медиа-набором"
    elif "video_and_audio" in n: role = "Базовый проект video+audio"

    dump = hexdump(info["data"], 0, 128)
    media_md = "\n".join(f"- `{m}`" for m in info["media"]) or "- _(нет путей .mp4/.wav в UTF-16)_"
    labels_md = "\n".join(f"- `{l}` (×{info['labels'].count(l)})" for l in info["label_set"]) or "- _(нет sample_for_project_*)_"
    fx_md = "\n".join(f"- `{x}`" for x in info["fx_set"]) or "- —"
    svfx_md = "\n".join(f"- `{x}`" for x in info["svfx"]) or "- _(нет `{Svfx:...}`)_"
    paths_md = "\n".join(f"- `{p}`" for p in dict.fromkeys(info["paths"]))  # unique preserve order
    types_md = "\n".join(f"- `{t[:120]}`" for t in info["types"][:8]) or "- —"

    rates = info["rates"]
    if rates:
        rate_rows = "\n".join(
            f"| `0x{r['off']:04X}` | {r['rate']:.12f} | **{r['pct']:.1f}%** | {r['len_sec']:.6f} s |"
            for r in rates
        )
        rates_md = f"""| Offset | Rate (f64) | % | Длина события (u64/1e7) |
|--------|------------|---|-------------------------|
{rate_rows}

Паттерн вокруг rate: `u64 length_ticks`, `u64 0`, **`f64 rate`**, `u64 0`. Единица времени ≈ **10 000 000 ticks/сек** (100 ns).
В UI VEGAS процент ≈ `rate × 100` (для сжатого клипа **245,6%**)."""
    else:
        rates_md = "_Нестандартных playback rate (≠ 1.0) не найдено._"

    notable = [s for o,s in info["u16s"] if re.search(r"(?i)(sample_for|VEGAS |1untitled|OpenVegas|crossfade|Svfx)", s) or s.endswith((".mp4",".wav",".veg")) or "сжат" in s or "растянут" in s]
    notable = list(dict.fromkeys(notable))[:40]
    notable_md = "\n".join(f"- `{s}`" for s in notable) or "- —"

    extra_notes = []
    if rates:
        uniq = sorted({round(r["rate"], 9) for r in rates})
        pcts = ", ".join(f"{r*100:.1f}%" for r in uniq)
        extra_notes.append(f"- Обнаружен time-stretch / velocity: **{pcts}** ({len(rates)} вхожд.; обычно video+audio pair).")
        if any(r["rate"] > 1.0 for r in rates):
            extra_notes.append("- Rate **> 1** → клип **сжат/ускорен** (на видео — «гармошка» velocity, на аудио — процент в углу события).")
        if any(r["rate"] < 1.0 for r in rates):
            extra_notes.append("- Rate **< 1** → клип **растянут/замедлен**.")
    if info["name"].lower() != Path(info["proj_path"]).name.lower() and info["proj_path"] != "?":
        extra_notes.append(f"- Имя файла на диске (`{info['name']}`) **отличается** от embedded-пути проекта — при Save As / rename путь внутри `.veg` может оставаться старым (кириллица).")
    extra_md = ("\n".join(extra_notes) + "\n") if extra_notes else ""

    body = f'''# `{info["name"]}`

## Роль сэмпла

{role}

## Идентификация

| Поле | Значение |
|------|----------|
| Size | {info["size"]} bytes |
| MD5 | `{info["md5"]}` |
| Filesize field @0x10 | {info["filesize_field"]} |
| Early blob @0x38 | {info["field56"]} |
| VEGAS version @0x46 | {info["ver"]} |
| Sample rate @0x4C | {info["sr"]} |
| FPS @0x50 | {info["fps"]:.6f} |
| Tempo @0x58 | {info["tempo"]} |
| GUID root | `{info["guid_root"]}` |
| GUID type | `{info["guid_type"]}` |
| Const @0x04 | `{info["const4_16"]}` |

## Путь проекта (UTF-16 внутри файла)

`{info["proj_path"]}`

## Медиа-ссылки

{media_md}

## Playback rate / time-stretch

{rates_md}

## Лейблы событий (`sample_for_project_*`)

Число вхождений: **{len(info["labels"])}**

{labels_md}

## Track FX (уникальные строки)

Вхождений FX-строк всего: **{info["fx_count"]}**

{fx_md}

## Event FX (`{{Svfx:...}}`)

{svfx_md}

## Прочие UTF-16 пути

{paths_md}

## ProjectNotes / .NET type names (ASCII)

{types_md}

## Заметные строки

{notable_md}

## Hex dump (первые 128 байт)

```
{dump}
```

## Рассуждения

- Ранний блоб (~{info["field56"]} B) содержит props + путь проекта + ProjectNotes; дальше — медиа-пул и таймлайн.
- Размер **{info["size"]}** согласуется с ролью «{role}»: больше событий/сегментов → больше хвост файла.
{extra_md}- Для импорта в OpenVegas: медиа-пути + **playback rate** на событиях (velocity); точные in/out — через diff с sibling-сэмплами.
'''
    (files_dir / doc_name).write_text(body, encoding="utf-8")
    rate_note = f"{rates[0]['pct']:.1f}%" if rates else "—"
    index_lines.append(f"| `{info['name']}` | {info['size']} | {len(info['labels'])} | {len(info['media'])} | {rate_note} | [{doc_name}]({doc_name}) |\n")

(files_dir / "README.md").write_text("".join(index_lines), encoding="utf-8")

# README root
readme = '''# docs_veg

Разбор строения файлов **`.veg`** (VEGAS Pro 22) из каталога `sample_ui`.

## С чего начать

1. [00_format_overview.md](00_format_overview.md) — общая модель формата и выводы для OpenVegas  
2. [01_header_and_props.md](01_header_and_props.md) — заголовок, fps/sr, notes  
3. [files/README.md](files/README.md) — карточка по каждому сэмплу  

## Сырые данные / скрипты

| Файл | Назначение |
|------|------------|
| `_analyze_veg.py` | строки, GUID, краткий JSON |
| `_analyze_deep.py` | глубокий разбор + common prefix |
| `_analyze_props.py` | props/fps/sr, counts, diffs |
| `_deep_analysis.json` | машинный дамп |
| `_analysis_raw.json` | машинный дамп |
| `*_strings.md` | извлечённые строки по файлам |

Перезапуск:

```bash
python docs_veg/_analyze_veg.py
python docs_veg/_analyze_deep.py
python docs_veg/_analyze_props.py
```
'''
(OUT / "README.md").write_text(readme, encoding="utf-8")

# summary table json for convenience
summary = [{
  "name": i["name"], "size": i["size"], "md5": i["md5"],
  "ver": i["ver"], "sr": i["sr"], "fps": i["fps"],
  "labels": len(i["labels"]), "media": i["media"], "fx": i["fx_set"],
  "svfx": i["svfx"],
  "proj_path": i["proj_path"],
  "playback_rates": [{"off": f"0x{r['off']:X}", "rate": r["rate"], "pct": round(r["pct"], 2), "len_sec": round(r["len_sec"], 6)} for r in i["rates"]],
} for i in infos]
(OUT / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")

print("Wrote docs to", OUT)
print("files:", len(list(files_dir.glob('*.md'))))
