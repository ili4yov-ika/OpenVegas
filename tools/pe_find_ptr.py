#!/usr/bin/env python3
"""
Find 64-bit pointers to given virtual addresses inside a PE image.

Unlike a naive scan, this maps section headers so a match is reported with the
*virtual* address of the slot holding the pointer — which is what a vtable lookup
needs. Comparing raw file offsets against pointer values (as an earlier helper did)
can never match in a PE, because the image stores VAs.

Usage: pe_find_ptr.py <file.ofx> <hexVA> [hexVA ...]
"""
import struct
import sys


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[pe:pe + 4] == b'PE\0\0', 'not a PE'
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    optsz = struct.unpack_from('<H', data, pe + 20)[0]
    magic = struct.unpack_from('<H', data, pe + 24)[0]
    base = struct.unpack_from('<Q', data, pe + 24 + 24)[0] if magic == 0x20b \
        else struct.unpack_from('<I', data, pe + 24 + 28)[0]
    tbl = pe + 24 + optsz
    out = []
    for i in range(nsec):
        o = tbl + i * 40
        name = data[o:o + 8].rstrip(b'\0').decode('ascii', 'replace')
        vsize, va, rawsz, raw = struct.unpack_from('<IIII', data, o + 8)
        out.append((name, va, vsize, raw, rawsz))
    return base, out


def off_to_va(base, secs, off):
    for name, va, vsize, raw, rawsz in secs:
        if raw <= off < raw + rawsz:
            return base + va + (off - raw), name
    return None, None


def main():
    path, targets = sys.argv[1], [int(a, 16) for a in sys.argv[2:]]
    data = open(path, 'rb').read()
    base, secs = sections(data)
    print(f'image base 0x{base:x}, {len(secs)} sections')
    for t in targets:
        needle = struct.pack('<Q', t)
        hits, start = [], 0
        while True:
            p = data.find(needle, start)
            if p < 0:
                break
            hits.append(p)
            start = p + 1
        print(f'\npointer to 0x{t:x}: {len(hits)} slot(s)')
        for p in hits:
            va, sec = off_to_va(base, secs, p)
            print(f'  file 0x{p:x} -> VA 0x{va:x} in {sec}' if va else f'  file 0x{p:x} (outside sections)')


main()
