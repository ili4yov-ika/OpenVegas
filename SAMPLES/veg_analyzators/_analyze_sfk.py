#!/usr/bin/env python3
"""Analyze Vegas/Sound Forge .sfk (SFPK) peak sidecars."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def analyze(path: Path) -> None:
    data = path.read_bytes()
    if data[:4] != b"SFPK":
        print(path.name, "NOT SFPK, magic=", data[:4])
        return
    ver = u32(data, 4)
    hdr = u32(data, 8)
    ch = u32(data, 0x14)
    spb = u32(data, 0x18)
    frames = u32(data, 0x1C)
    body = data[hdr:]
    bins = frames // spb if spb else 0
    expect = bins * ch * 2 * 2
    print(f"{path.name}")
    print(f"  size={len(data)} ver={ver} header={hdr}")
    print(f"  channels={ch} samplesPerBin={spb} sourceFrames={frames}")
    print(f"  hash0=0x{u32(data,0x0C):08X} hash1=0x{u32(data,0x24):08X}")
    print(f"  bins={bins} body={len(body)} expect={expect} ok={len(body)==expect}")
    for sr in (48000, 44100, 192000):
        print(f"  duration@{sr}Hz = {frames/sr:.3f}s")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="*", type=Path)
    args = ap.parse_args()
    paths = args.paths
    if not paths:
        root = Path(__file__).resolve().parents[1]
        paths = list(root.glob("**/*.sfk"))
    for p in paths:
        analyze(p)


if __name__ == "__main__":
    main()
