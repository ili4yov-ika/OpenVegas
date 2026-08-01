import os, struct, re, json, hashlib
from collections import Counter, defaultdict
from pathlib import Path

DIR = Path(r"d:\Devs\C++\OpenVegas\sample_ui")
OUT = DIR / "docs_veg"
files = sorted(DIR.glob("*.veg"))

def u16(b,o): return struct.unpack_from("<H", b, o)[0]
def u32(b,o): return struct.unpack_from("<I", b, o)[0]
def u64(b,o): return struct.unpack_from("<Q", b, o)[0]
def guid(b,o):
    d1,d2,d3 = struct.unpack_from("<IHH", b, o)
    d4 = b[o+8:o+16]
    return f"{d1:08X}-{d2:04X}-{d3:04X}-{d4[0]:02X}{d4[1]:02X}-{d4[2:8].hex().upper()}"

def ascii_runs(data, minlen=4):
    out=[]
    cur=[]
    start=0
    for i,x in enumerate(data):
        if 32 <= x <= 126:
            if not cur: start=i
            cur.append(chr(x))
        else:
            if len(cur)>=minlen:
                out.append((start, "".join(cur)))
            cur=[]
    if len(cur)>=minlen:
        out.append((start, "".join(cur)))
    return out

def utf16le_runs(data, minlen=4):
    def ok(c):
        return (32 <= c <= 126) or (0x0400 <= c <= 0x04FF)
    out=[]
    i=0
    while i+3 < len(data):
        c0 = data[i] | (data[i+1] << 8)
        if ok(c0):
            j=i; s=[]
            while j+1 < len(data):
                c = data[j] | (data[j+1] << 8)
                if not ok(c):
                    break
                s.append(chr(c)); j+=2
            if len(s)>=minlen:
                out.append((i, "".join(s))); i=j
            else:
                i+=1
        else:
            i+=1
    return out

# Known Vegas-ish GUIDs from header
ROOT_SIG = bytes.fromhex("72696666")  # riff
HDR_GUID = bytes.fromhex("2E91CF11A5D628DB04C10000")

summaries = []
all_guids = Counter()
all_ext = Counter()

for fp in files:
    data = fp.read_bytes()
    size = len(data)
    info = {
        "file": fp.name,
        "size": size,
        "md5": hashlib.md5(data).hexdigest(),
        "header": {},
        "strings_ascii": [],
        "strings_utf16": [],
        "paths": [],
        "media_hints": [],
        "chunk_like": [],
        "guid_hits": [],
    }
    # header parse
    if data[:4]==b"riff":
        info["header"]["magic"]="riff"
        info["header"]["guid_a"]=guid(data,4) if size>=20 else None
        # bytes 4..15 look like GUID in mixed endian; try CLSID layout
        info["header"]["raw16_4_19"]=data[4:20].hex()
        if size>=24:
            info["header"]["size_at_16"]=u64(data,16)
            info["header"]["size_matches_file"]= (u64(data,16)==size) or (u32(data,16)==size) or (u32(data,16)+8==size) or (u32(data,16)+24==size)
            info["header"]["u32_16"]=u32(data,16)
            info["header"]["u32_20"]=u32(data,20)
        if size>=40:
            info["header"]["guid_24"]=guid(data,24)
            all_guids[info["header"]["guid_24"]] += 1
        if size>=56:
            info["header"]["guid_40"]=guid(data,40)
            all_guids[info["header"]["guid_40"]] += 1
        if size>=64:
            info["header"]["u64_56"]=u64(data,56)

    # walk for GUID-like (MS CLSID pattern: many end with 00C04F8EDB8A)
    i=0
    while i+16 <= size:
        if data[i+8:i+16] == bytes.fromhex("00C04F8EDB8A") or data[i+10:i+16]==bytes.fromhex("C04F8EDB8A"):
            g = guid(data,i)
            all_guids[g]+=1
            if len(info["guid_hits"])<80:
                info["guid_hits"].append({"off":i, "guid":g})
            i += 16
        else:
            i += 1

    # Also scan every 16-aligned? skip - expensive enough

    asciis = ascii_runs(data, 5)
    u16s = utf16le_runs(data, 4)
    # filter interesting
    interesting_re = re.compile(r"(?i)(\.mp4|\.mov|\.avi|\.wav|\.mpg|\.mxf|\.png|\.jpg|\\\\|/|[A-Za-z]:\\|vegas|sony|magix|ofx|plugin|track|audio|video|sample)")
    for off,s in asciis:
        if interesting_re.search(s) or len(s)>=12:
            if len(info["strings_ascii"])<120:
                info["strings_ascii"].append({"off":off,"s":s})
        if re.search(r"(?i)\.(mp4|mov|avi|wav|mpg|mxf|aif|flac|png|jpg|jpeg)$", s) or "\\" in s or "/" in s and "." in s:
            info["paths"].append({"off":off,"s":s,"enc":"ascii"})
        extm = re.findall(r"\.(mp4|mov|avi|wav|mpg|mxf|aif|flac|png|jpg|jpeg|veg|sfap0)", s, re.I)
        for e in extm: all_ext[e.lower()]+=1

    for off,s in u16s:
        if interesting_re.search(s) or len(s)>=8:
            if len(info["strings_utf16"])<150:
                info["strings_utf16"].append({"off":off,"s":s})
        if re.search(r"(?i)\.(mp4|mov|avi|wav|mpg|mxf|aif|flac|png|jpg|jpeg)", s) or ":\\" in s or s.startswith("\\\\"):
            info["paths"].append({"off":off,"s":s,"enc":"utf16le"})
        if re.search(r"(?i)sample_for_project|OpenVegas|VEGAS|Sony|MAGIX", s):
            info["media_hints"].append({"off":off,"s":s})

    # naive chunk scan: look for 4cc ascii + size
    for off in range(0, min(size-8, size), 1):
        four = data[off:off+4]
        if all(32<=b<=126 for b in four) and four.isascii():
            tag = four.decode("ascii")
            if re.fullmatch(r"[A-Za-z0-9 _\-]{4}", tag):
                sz = u32(data, off+4)
                if 8 <= sz <= size-off and sz < size:
                    # likely only if followed by plausible data
                    if tag.lower() in ("riff","list","fmt ","data","veg ","PROJ","TRAK","EVNT") or (sz>16 and sz<size//2):
                        if len(info["chunk_like"])<40:
                            info["chunk_like"].append({"off":off,"tag":tag,"size":sz})

    summaries.append(info)
    # write per-file dump of strings
    dump = []
    dump.append(f"# Strings: {fp.name}\n")
    dump.append(f"size={size} md5={info['md5']}\n")
    dump.append("\n## UTF-16LE\n")
    for x in info["strings_utf16"]:
        dump.append(f"0x{x['off']:06X}  {x['s']}\n")
    dump.append("\n## ASCII\n")
    for x in info["strings_ascii"][:80]:
        dump.append(f"0x{x['off']:06X}  {x['s']}\n")
    (OUT / (fp.stem + "_strings.md")).write_text("".join(dump), encoding="utf-8")

# pairwise diff sizes
print(json.dumps({
  "count": len(summaries),
  "guids_top": all_guids.most_common(30),
  "exts": all_ext.most_common(),
  "files": [{
     "file":s["file"], "size":s["size"], "md5":s["md5"],
     "hdr":s["header"],
     "paths":s["paths"][:20],
     "media_hints":s["media_hints"][:20],
     "guid_hits_n": len(s["guid_hits"]),
     "guid_unique": sorted(set(g["guid"] for g in s["guid_hits"]))[:40],
  } for s in summaries]
}, indent=2, ensure_ascii=False))

(OUT / "_analysis_raw.json").write_text(json.dumps(summaries, indent=2, ensure_ascii=False), encoding="utf-8")
