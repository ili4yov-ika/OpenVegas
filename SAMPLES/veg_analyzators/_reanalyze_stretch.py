# -*- coding: utf-8 -*-
"""One-shot deep compare: compressed vs stretched .veg samples."""
from pathlib import Path
import hashlib, struct, json

DIR = Path(r"d:\Devs\C++\OpenVegas\sample_ui")
OUT = DIR / "docs_veg"
FILES = [
    "example_project_with_2_videos-one_compressed.veg",
    "example_project_with_2_videos-one_stretched.veg",
    "example_project_with_2_videos.veg",
]


def ok(c):
    return (32 <= c <= 126) or (0x0400 <= c <= 0x04FF)


def utf16(data, minlen=4):
    out = []
    i = 0
    while i + 3 < len(data):
        c0 = data[i] | (data[i + 1] << 8)
        if ok(c0):
            j = i
            s = []
            while j + 1 < len(data):
                c = data[j] | (data[j + 1] << 8)
                if not ok(c):
                    break
                s.append(chr(c))
                j += 2
            if len(s) >= minlen:
                out.append((i, "".join(s)))
                i = j
                continue
        i += 1
    return out


def ascii_runs(data, minlen=5):
    out = []
    cur = []
    start = 0
    for i, x in enumerate(data):
        if 32 <= x <= 126:
            if not cur:
                start = i
            cur.append(chr(x))
        else:
            if len(cur) >= minlen:
                out.append((start, "".join(cur)))
            cur = []
    if len(cur) >= minlen:
        out.append((start, "".join(cur)))
    return out


def find_rates(data):
    hits = []
    for off in range(0, len(data) - 16, 8):
        rate = struct.unpack_from("<d", data, off)[0]
        if not (0.05 < rate < 10.0) or abs(rate - 1.0) < 1e-6:
            continue
        if off < 16:
            continue
        if struct.unpack_from("<Q", data, off - 8)[0] != 0:
            continue
        if struct.unpack_from("<Q", data, off + 8)[0] != 0:
            continue
        length = struct.unpack_from("<Q", data, off - 16)[0]
        if length < 1000 or length > 10**12:
            continue
        media_like = struct.unpack_from("<Q", data, off - 24)[0] if off >= 24 else 0
        hits.append(
            {
                "off": off,
                "rate": rate,
                "pct": rate * 100.0,
                "len_ticks": length,
                "len_sec": length / 1e7,
                "prev_u64": media_like,
                "prev_sec": media_like / 1e7 if media_like else None,
                "source_used_sec": (length / 1e7) * rate,
            }
        )
    return hits


def guid(b, o):
    d1, d2, d3 = struct.unpack_from("<IHH", b, o)
    d4 = b[o + 8 : o + 16]
    return f"{d1:08X}-{d2:04X}-{d3:04X}-{d4[0]:02X}{d4[1]:02X}-{d4[2:8].hex().upper()}"


def hexdump(data, start, length, width=16):
    lines = []
    end = min(len(data), start + length)
    for row in range(start, end, width):
        chunk = data[row : row + width]
        hx = " ".join(f"{x:02X}" for x in chunk)
        asc = "".join(chr(x) if 32 <= x <= 126 else "." for x in chunk)
        lines.append(f"{row:06X}  {hx:<{width*3}}  {asc}")
    return "\n".join(lines)


def analyze(name):
    p = DIR / name
    d = p.read_bytes()
    u = utf16(d)
    a = ascii_runs(d)
    media = [s for o, s in u if s.lower().endswith((".mp4", ".wav", ".mov", ".avi"))]
    paths = [s for o, s in u if ":\\" in s]
    fx = [s for o, s in u if s.startswith("VEGAS ")]
    svfx = sorted({s for o, s in u if s.startswith("{Svfx:")})
    labels = [s for o, s in u if s.startswith("sample_for_project")]
    proj = next((s for o, s in u if o >= 0xF0 and ".veg" in s and ":\\" in s), "?")
    rates = find_rates(d)
    return {
        "name": name,
        "size": len(d),
        "md5": hashlib.md5(d).hexdigest(),
        "mtime": p.stat().st_mtime,
        "ver": struct.unpack_from("<H", d, 0x46)[0],
        "sr": struct.unpack_from("<I", d, 0x4C)[0],
        "fps": struct.unpack_from("<d", d, 0x50)[0],
        "tempo": struct.unpack_from("<d", d, 0x58)[0],
        "filesize_field": struct.unpack_from("<Q", d, 0x10)[0],
        "field56": struct.unpack_from("<Q", d, 0x38)[0],
        "guid_root": guid(d, 0x18),
        "guid_type": guid(d, 0x28),
        "const4_16": d[4:16].hex(),
        "proj_path": proj,
        "media": media,
        "paths": list(dict.fromkeys(paths)),
        "fx_set": sorted(set(fx)),
        "fx_count": len(fx),
        "svfx": svfx,
        "labels": labels,
        "rates": rates,
        "u16s": u,
        "asci": a,
        "data": d,
    }


def write_card(info, role):
    rates = info["rates"]
    if rates:
        rows = "\n".join(
            f"| `0x{r['off']:04X}` | {r['rate']:.12f} | **{r['pct']:.1f}%** | {r['len_sec']:.6f} s | {r['source_used_sec']:.6f} s |"
            for r in rates
        )
        rates_md = f"""| Offset | Rate (f64) | % | Длина на таймлайне | ≈ source used (len×rate) |
|--------|------------|---|--------------------|--------------------------|
{rows}

Паттерн: `u64 ?`, `u64 length_ticks`, `u64 0`, **`f64 rate`**, `u64 0`.  
Единица времени ≈ **10 000 000 ticks/сек**. UI VEGAS: `rate × 100` → **{rates[0]['pct']:.1f}%** (с запятой в локали)."""
    else:
        rates_md = "_Нестандартных playback rate не найдено._"

    media_md = "\n".join(f"- `{m}`" for m in info["media"]) or "- _(нет)_"
    paths_md = "\n".join(f"- `{p}`" for p in info["paths"])
    fx_md = "\n".join(f"- `{x}`" for x in info["fx_set"]) or "- —"
    svfx_md = "\n".join(f"- `{x}`" for x in info["svfx"]) or "- _(нет)_"
    notable = []
    for o, s in info["u16s"]:
        if (
            s.endswith((".mp4", ".wav", ".veg"))
            or s.startswith("VEGAS ")
            or s.startswith("{Svfx:")
            or "untitled" in s.lower()
            or "сжат" in s
            or "растянут" in s
            or "crossfade" in s.lower()
        ):
            notable.append(s)
    notable = list(dict.fromkeys(notable))[:30]
    notable_md = "\n".join(f"- `{s}`" for s in notable) or "- —"

    extra = []
    if rates:
        extra.append(
            f"- Time-stretch: **{rates[0]['pct']:.1f}%** ×{len(rates)} (video+audio pair)."
        )
        if rates[0]["rate"] > 1:
            extra.append("- Rate > 1 → **сжатие / ускорение** (velocity accordion плотнее, badge >100%).")
        else:
            extra.append("- Rate < 1 → **растяжение / замедление** (accordion реже, badge <100%).")
        src = rates[0]["source_used_sec"]
        extra.append(
            f"- Один и тот же кусок медиа (~**{src:.2f} s**) укладывается в событие **{rates[0]['len_sec']:.3f} s** на таймлайне."
        )
    disk = info["name"]
    emb = Path(info["proj_path"]).name if info["proj_path"] != "?" else "?"
    if disk.lower() != emb.lower():
        extra.append(
            f"- Имя на диске (`{disk}`) ≠ embedded (`{emb}`) — rename/Save As без обновления пути."
        )
    extra_md = ("\n".join(extra) + "\n") if extra else ""

    body = f"""# `{info["name"]}`

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

Доп. ссылка на nested project (UTF-16 `.veg`):
{chr(10).join(f'- `{p}`' for p in info['paths'] if p.lower().endswith('.veg') and 'example_project_with_video_and_audio_with_crossfade' in p) or '- —'}

## Playback rate / time-stretch

{rates_md}

## Track FX (уникальные)

Вхождений FX-строк: **{info["fx_count"]}**

{fx_md}

## Event FX (`{{Svfx:...}}`)

{svfx_md}

## Прочие UTF-16 пути

{paths_md}

## Заметные строки

{notable_md}

## Hex dump (первые 128 байт)

```
{hexdump(info["data"], 0, 128)}
```

## Рассуждения

- Ранний блоб (~{info["field56"]} B) — props + путь + ProjectNotes; далее media pool и timeline.
{extra_md}- Для OpenVegas: читать **f64 playback rate** на событии + длину в ticks/1e7; UI — гармошка на video, процент на audio.
"""
    out = OUT / "files" / (Path(info["name"]).stem + ".md")
    out.write_text(body, encoding="utf-8")
    return out


def write_strings(info):
    lines = [
        f"# Strings: {info['name']}\n",
        f"size={info['size']} md5={info['md5']}\n",
        "\n## UTF-16LE\n",
    ]
    for o, s in info["u16s"]:
        if len(s) >= 4 and (
            ":\\" in s
            or s.startswith("VEGAS ")
            or s.startswith("{Svfx:")
            or s.endswith((".mp4", ".wav", ".veg"))
            or "untitled" in s.lower()
            or "Main Timeline" in s
            or "Sound Mapper" in s
            or "crossfade" in s.lower()
            or "сжат" in s
            or "растянут" in s
        ):
            lines.append(f"0x{o:06X}  {s}\n")
    lines.append("\n## ASCII (ProjectNotes / types)\n")
    for o, s in info["asci"]:
        if "ScriptPortal" in s or "ProjectNotes" in s:
            lines.append(f"0x{o:06X}  {s}\n")
    out = OUT / (Path(info["name"]).stem + "_strings.md")
    out.write_text("".join(lines), encoding="utf-8")
    return out


def diff_pair(a, b):
    """Byte-diff summary compressed vs stretched."""
    da, db = a["data"], b["data"]
    m = min(len(da), len(db))
    diffs = [i for i in range(m) if da[i] != db[i]]
    # also length delta
    regions = []
    if diffs:
        start = prev = diffs[0]
        for x in diffs[1:]:
            if x <= prev + 8:
                prev = x
            else:
                regions.append((start, prev))
                start = prev = x
        regions.append((start, prev))

    # key semantic diffs = rate fields
    lines = [
        f"# Diff: `{a['name']}` vs `{b['name']}`\n",
        "\n",
        f"| | compressed | stretched |\n",
        f"|---|------------|-----------|\n",
        f"| Size | {a['size']} | {b['size']} |\n",
        f"| MD5 | `{a['md5']}` | `{b['md5']}` |\n",
        f"| Δ size | | {b['size'] - a['size']:+d} B |\n",
        f"| Differing bytes (aligned prefix) | | **{len(diffs)}** / {m} |\n",
        f"| Diff regions (~clustered) | | **{len(regions)}** |\n",
        "\n## Playback rates\n\n",
        "| | compressed | stretched |\n",
        "|---|------------|-----------|\n",
    ]
    if a["rates"] and b["rates"]:
        ra, rb = a["rates"][0], b["rates"][0]
        lines.append(f"| Rate | **{ra['pct']:.1f}%** | **{rb['pct']:.1f}%** |\n")
        lines.append(f"| Timeline len | {ra['len_sec']:.6f} s | {rb['len_sec']:.6f} s |\n")
        lines.append(
            f"| Source used (len×rate) | {ra['source_used_sec']:.6f} s | {rb['source_used_sec']:.6f} s |\n"
        )
        lines.append(
            f"| Product check | same media slice ≈ **{ra['source_used_sec']:.2f} s** |\n"
        )

    lines.append("\n## Semantic takeaway\n\n")
    lines.append(
        "- Оба файла — один проект (2 клипа: long crossfade-render + Untitled), отличаются **playback rate** Untitled video/audio pair.\n"
    )
    lines.append(
        "- compressed: rate≈**2.456** (245,6%), короткое событие (~2.45 s).\n"
    )
    lines.append(
        "- stretched: rate≈**0.363** (36,3%), длинное событие (~16.58 s).\n"
    )
    lines.append(
        "- `source_used ≈ timeline_len × rate` почти одинаков → один и тот же media in/out, разный time-stretch.\n"
    )
    lines.append(
        "- Размер файлов почти равен (+8 B у stretched); rate/длины — компактные поля, не новый media pool.\n"
    )

    out = OUT / "files" / "COMPARE_compressed_vs_stretched.md"
    out.write_text("".join(lines), encoding="utf-8")
    return out


def main():
    infos = [analyze(n) for n in FILES]
    roles = {
        "example_project_with_2_videos-one_compressed.veg": (
            "Два видеоклипа: Untitled **сжат/ускорен** (playback rate > 100%)"
        ),
        "example_project_with_2_videos-one_stretched.veg": (
            "Два видеоклипа: Untitled **растянут/замедлен** (playback rate < 100%)"
        ),
    }
    for info in infos[:2]:
        write_card(info, roles[info["name"]])
        write_strings(info)
        print("OK", info["name"], "md5", info["md5"][:12], "rates", [round(r["pct"], 2) for r in info["rates"]])

    diff_pair(infos[0], infos[1])

    # patch summary.json entries for these two
    summary_path = OUT / "summary.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8")) if summary_path.exists() else []
    by_name = {x["name"]: x for x in summary}
    for info in infos[:2]:
        by_name[info["name"]] = {
            "name": info["name"],
            "size": info["size"],
            "md5": info["md5"],
            "ver": info["ver"],
            "sr": info["sr"],
            "fps": info["fps"],
            "labels": len(info["labels"]),
            "media": info["media"],
            "fx": info["fx_set"],
            "svfx": info["svfx"],
            "proj_path": info["proj_path"],
            "playback_rates": [
                {
                    "off": f"0x{r['off']:X}",
                    "rate": r["rate"],
                    "pct": round(r["pct"], 2),
                    "len_sec": round(r["len_sec"], 6),
                    "source_used_sec": round(r["source_used_sec"], 6),
                }
                for r in info["rates"]
            ],
        }
    # keep stable order from FILES then others
    ordered = []
    seen = set()
    for n in FILES:
        if n in by_name:
            ordered.append(by_name[n])
            seen.add(n)
    for n, v in by_name.items():
        if n not in seen:
            ordered.append(v)
    summary_path.write_text(json.dumps(ordered, indent=2, ensure_ascii=False), encoding="utf-8")

    # update files/README rate column for these two (regen light)
    readme = OUT / "files" / "README.md"
    if readme.exists():
        text = readme.read_text(encoding="utf-8")
        for info in infos[:2]:
            pct = f"{info['rates'][0]['pct']:.1f}%" if info["rates"] else "—"
            # replace size/rate cells loosely via line rewrite
        lines = []
        for line in text.splitlines(True):
            changed = False
            for info in infos[:2]:
                if f"`{info['name']}`" in line:
                    pct = f"{info['rates'][0]['pct']:.1f}%" if info["rates"] else "—"
                    stem = Path(info["name"]).stem + ".md"
                    line = (
                        f"| `{info['name']}` | {info['size']} | {len(info['labels'])} | "
                        f"{len(info['media'])} | {pct} | [{stem}]({stem}) |\n"
                    )
                    changed = True
                    break
            lines.append(line)
        readme.write_text("".join(lines), encoding="utf-8")

    print("Wrote compare + cards + strings + summary")


if __name__ == "__main__":
    main()
