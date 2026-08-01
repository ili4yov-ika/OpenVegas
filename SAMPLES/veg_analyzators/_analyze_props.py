import struct, math, re, json, hashlib
from pathlib import Path

DIR = Path(r"d:\Devs\C++\OpenVegas\sample_ui")
OUT = DIR / "docs_veg"
files = sorted(DIR.glob("*.veg"))

def f64(b,o): return struct.unpack_from("<d", b, o)[0]
def u16(b,o): return struct.unpack_from("<H", b, o)[0]
def u32(b,o): return struct.unpack_from("<I", b, o)[0]
def u64(b,o): return struct.unpack_from("<Q", b, o)[0]
def i32(b,o): return struct.unpack_from("<i", b, o)[0]
def guid(b,o):
    d1,d2,d3 = struct.unpack_from("<IHH", b, o)
    d4 = b[o+8:o+16]
    return f"{d1:08X}-{d2:04X}-{d3:04X}-{d4[0]:02X}{d4[1]:02X}-{d4[2:8].hex().upper()}"

def read_utf16z(data, off, maxlen=1024):
    chars=[]
    i=off
    while i+1 < len(data) and len(chars)<maxlen:
        c = data[i] | (data[i+1]<<8)
        if c==0: return "".join(chars), i+2
        if c < 32 or c > 0x10FFFF: break
        chars.append(chr(c)); i+=2
    return "".join(chars), i

def utf16_all(data, minlen=4):
    out=[]; i=0
    while i+3 < len(data):
        if data[i+1]==0 and 32<=data[i]<=126:
            j=i; s=[]
            while j+1 < len(data) and data[j+1]==0 and 32<=data[j]<=126:
                s.append(chr(data[j])); j+=2
            if len(s)>=minlen: out.append((i,"".join(s))); i=j; continue
        i+=1
    return out

# Interpret project properties block at 0x40
# From hex:
# 40: B8 00 00 00 = 184?
# 44: 00 00 16 00 = version major/minor? 0, 22
# 48: 05 00 00 00 = 5
# 4C: 80 BB 00 00 = 48000 sample rate
# 50: double ?
# 58: double 120.0

for fp in files[:1]:
    d=fp.read_bytes()
    print(fp.name)
    print("u32@40", u32(d,0x40))
    print("u16@44", u16(d,0x44), "u16@46", u16(d,0x46))
    print("u32@48", u32(d,0x48))
    print("u32@4C", u32(d,0x4C), "<- likely sample rate")
    print("f64@50", f64(d,0x50))
    print("f64@58", f64(d,0x58))
    print("bytes 60-F0:", d[0x60:0xF0].hex())

# Compare property fields across all
print("\n=== PROP TABLE ===")
print(f"{'file':55} size  f56  ver?  sr    d50           d58")
for fp in files:
    d=fp.read_bytes()
    name=fp.name
    if len(name)>52: name=name[:49]+"..."
    print(f"{name:55} {len(d):5} {u64(d,56):5} {u16(d,0x46):4} {u32(d,0x4C):5} {f64(d,0x50):12.6f} {f64(d,0x58):10.4f}")

# Locate where project path starts - seems 0xF8 area from hex showing D:\ 
print("\n=== PATH AT ~0xF8 ===")
for fp in files:
    d=fp.read_bytes()
    # find first utf16 path-like
    for off,s in utf16_all(d):
        if s.startswith("D:\\") or s.startswith("C:\\"):
            print(f"{fp.name[:50]:50} @0x{off:04X} {s[:90]}")
            break

# Event name counts
print("\n=== EVENT/LABEL COUNTS ===")
for fp in files:
    d=fp.read_bytes()
    labels=[s for o,s in utf16_all(d) if s.startswith("sample_for_project")]
    media=[s for o,s in utf16_all(d) if re.search(r"(?i)\.(mp4|wav)$", s)]
    fx=[s for o,s in utf16_all(d) if s.startswith("VEGAS ")]
    print(f"{fp.name[:48]:48} labels={len(labels)} media_paths={len(media)} veg_fx={len(fx)} size={len(d)}")
    for s in media: print("   ", s)
    for s in sorted(set(labels)): print("   L", s)
    for s in sorted(set(fx)): print("   FX", s)

# Binary-diff regions between video_and_audio and crossfade (similar size)
a=(DIR/"example_project_with_video_and_audio.veg").read_bytes()
b=(DIR/"example_project_with_video_and_audio_with_crossfade.veg").read_bytes()
# find differing ranges
diffs=[]
i=0
while i < min(len(a),len(b)):
    if a[i]!=b[i]:
        j=i
        while j < min(len(a),len(b)) and a[j]!=b[j]: j+=1
        diffs.append((i,j,j-i))
        i=j
    else:
        i+=1
print("\n=== DIFF video_and_audio vs crossfade ===")
print("len", len(a), len(b), "diff_regions", len(diffs))
for start,end,ln in diffs[:25]:
    print(f"  0x{start:04X}-0x{end:04X} ({ln} bytes)")

# only_video vs video_and_audio
c=(DIR/"example_project_with_only_video.veg").read_bytes()
print("\nsize only_video", len(c), "video_audio", len(a), "only_audio", len((DIR/"example_project_with_only_audio.veg").read_bytes()), "trimmers", len((DIR/"example_project_with_video_and_audio_trimmers.veg").read_bytes()))

# Look for 48000 / 1920 / 1080 / 59.94 as numbers
print("\n=== MAGIC NUMBER SEARCH in video_and_audio ===")
needles = {
  "48000_u32": struct.pack("<I", 48000),
  "44100_u32": struct.pack("<I", 44100),
  "1920_u32": struct.pack("<I", 1920),
  "1080_u32": struct.pack("<I", 1080),
  "1280_u32": struct.pack("<I", 1280),
  "720_u32": struct.pack("<I", 720),
  "30_f64": struct.pack("<d", 30.0),
  "29.97_f64": struct.pack("<d", 29.97),
  "59.94_f64": struct.pack("<d", 59.94),
  "23.976_f64": struct.pack("<d", 23.976),
  "120_f64": struct.pack("<d", 120.0),
}
for name, pat in needles.items():
    offs=[]
    start=0
    while True:
        p=a.find(pat, start)
        if p<0: break
        offs.append(p); start=p+1
    if offs:
        print(name, [hex(o) for o in offs[:12]])

# Serialize notes blob size estimate: ProjectNotes between ~0xC00 and media section
