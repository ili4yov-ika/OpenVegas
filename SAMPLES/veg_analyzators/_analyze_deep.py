import struct, re, json
from pathlib import Path
from collections import defaultdict

DIR = Path(r"d:\Devs\C++\OpenVegas\sample_ui")
OUT = DIR / "docs_veg"
files = sorted(DIR.glob("*.veg"))

def u32(b,o): return struct.unpack_from("<I", b, o)[0]
def u64(b,o): return struct.unpack_from("<Q", b, o)[0]
def guid_str(b,o):
    d1,d2,d3 = struct.unpack_from("<IHH", b, o)
    d4 = b[o+8:o+16]
    return f"{d1:08X}-{d2:04X}-{d3:04X}-{d4[0]:02X}{d4[1]:02X}-{d4[2:8].hex().upper()}"

def utf16_strings(data, minlen=4):
    out=[]; i=0
    while i+3 < len(data):
        if data[i+1]==0 and 32<=data[i]<=126:
            j=i; s=[]
            while j+1 < len(data) and data[j+1]==0 and (32<=data[j]<=126 or data[j] in (9,10,13)):
                if data[j] in (9,10,13): break
                s.append(chr(data[j])); j+=2
            if len(s)>=minlen:
                out.append((i,"".join(s))); i=j; continue
        i+=1
    return out

def ascii_strings(data, minlen=5):
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

# Deep header / early structure for one file, then compare
reports = []
for fp in files:
    data = fp.read_bytes()
    n = len(data)
    # Parse suspected outer header
    # 0:4 magic, 4:16 ?fixed, 16:8 filesize, 24:16 guidA, 40:16 guidB, 56:8 first_payload_size?
    first_payload = u64(data,56)
    # After header 64 bytes, content until ?
    # Check if first_payload points to end of a region starting at 64
    region_end = 64 + first_payload
    hdr = {
        "file": fp.name,
        "size": n,
        "magic": data[:4].decode("ascii", errors="replace"),
        "fixed_4_16": data[4:16].hex(),
        "file_size_field": u64(data,16),
        "guid_root": guid_str(data,24),
        "guid_type": guid_str(data,40),
        "field_56": first_payload,
        "64_plus_field56": region_end,
        "region_end_in_file": region_end <= n,
        "bytes_64_96": data[64:96].hex(),
        "u32_list_at_64": [u32(data,64+i*4) for i in range(8)],
    }

    # Find all occurrences of media extensions as utf16
    u16s = utf16_strings(data)
    media = []
    for off,s in u16s:
        if re.search(r"(?i)\.(mp4|mov|avi|wav|mpg|m2ts|mxf|aif|flac|png|jpg|jpeg|veg)$", s) or re.search(r"(?i)sample_for_project", s):
            media.append({"off":off, "s":s})
        elif ":\\" in s and len(s)>8:
            if any(k in s.lower() for k in ("video","audio","sample","openvegas","documents","videos","music",".mp",".wav")):
                media.append({"off":off, "s":s})

    # Plugin / FX names
    fx = [ {"off":o,"s":s} for o,s in u16s if re.search(r"(?i)(VEGAS |OFX|Compressor|Noise Gate|EQ|Track |Sony |MAGIX |Glow|Color)", s) ]

    # Type names / .NET-ish
    asci = ascii_strings(data)
    types = [ {"off":o,"s":s} for o,s in asci if "ScriptPortal" in s or "Vegas." in s or s.startswith("/Script") ]

    # Compare structure: look for repeating 16-byte GUIDs with Sonic Foundry suffix 00C04F8EDB8A
    guids=[]
    i=0
    suf=bytes.fromhex("00C04F8EDB8A")
    while i+16<=n:
        if data[i+8:i+16]==suf:
            guids.append((i, guid_str(data,i)))
            i+=16
        else:
            i+=1

    # Find float-looking clusters near media? skip
    # Diff vs related files later

    reports.append({
        "hdr": hdr,
        "media": media,
        "fx": fx[:40],
        "types": types,
        "guid_count": len(guids),
        "unique_guids": sorted(set(g for _,g in guids)),
        "guid_offsets_sample": guids[:30],
        "utf16_count": len(u16s),
        "notable_utf16": [ {"off":o,"s":s} for o,s in u16s if len(s)>=6 and (
            re.search(r"(?i)(track|audio|video|event|media|frame|rate|timecode|project|bus|master|pan|level|volume|fade|cross)", s)
            or s.endswith((".mp4",".wav",".veg",".mov"))
            or ":\\" in s
        )][:80]
    })

# Cross-file: common prefix length
pairs=[]
named={r["hdr"]["file"]: DIR.joinpath(r["hdr"]["file"]).read_bytes() for r in reports}
# fix: names with mojibake - use Path objects from files list
named={fp.name: fp.read_bytes() for fp in files}

def common_prefix(a,b):
    m=min(len(a),len(b)); i=0
    while i<m and a[i]==b[i]: i+=1
    return i

keys=list(named.keys())
for i in range(len(keys)):
    for j in range(i+1,len(keys)):
        a,b=named[keys[i]],named[keys[j]]
        cp=common_prefix(a,b)
        pairs.append({"a":keys[i],"b":keys[j],"common_prefix":cp,"size_a":len(a),"size_b":len(b),"size_diff":abs(len(a)-len(b))})

# Hex dump first 256 of video_and_audio
va = (DIR/"example_project_with_video_and_audio.veg").read_bytes()
print("=== FIRST 256 video_and_audio ===")
for row in range(0,256,16):
    chunk=va[row:row+16]
    hx=" ".join(f"{x:02X}" for x in chunk)
    asc="".join(chr(x) if 32<=x<=126 else "." for x in chunk)
    print(f"{row:04X}  {hx}  {asc}")

print("\n=== SUMMARY JSON ===")
out = {
  "header_model": {
    "0_4": "magic 'riff' (lowercase)",
    "4_16": "constant 2E91CF11A5D628DB04C10000 across all samples",
    "16_24": "u64 total file size",
    "24_40": "GUID root 46C429EF-904A-11D2-8722-00C04F8EDB8A",
    "40_56": "GUID type B28F2D5A-230F-11D2-86AF-00C04F8EDB8A",
    "56_64": "u64 size of following header/blob region (variable)",
  },
  "files": reports,
  "common_prefixes": sorted(pairs, key=lambda x: -x["common_prefix"])[:20],
}
(OUT/"_deep_analysis.json").write_text(json.dumps(out, indent=2, ensure_ascii=False), encoding="utf-8")
print(json.dumps({
  "header_model": out["header_model"],
  "files_brief": [{
    "file": r["hdr"]["file"],
    "size": r["hdr"]["size"],
    "field_56": r["hdr"]["field_56"],
    "media": r["media"],
    "types": r["types"],
    "fx_n": len(r["fx"]),
    "guid_n": r["guid_count"],
    "unique_guids": r["unique_guids"],
  } for r in reports],
  "top_common_prefixes": out["common_prefixes"][:10],
}, indent=2, ensure_ascii=False))
