# -*- coding: utf-8 -*-
"""Remove obsolete <message> with translation type=vanished from .ts (shrinks file, drops mojibake junk)."""
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def clean(path: Path) -> int:
    tree = ET.parse(path)
    root = tree.getroot()
    removed = 0
    for ctx in list(root):
        if ctx.tag != "context":
            continue
        to_drop = []
        for msg in list(ctx):
            if msg.tag != "message":
                continue
            tr = msg.find("translation")
            if tr is not None and tr.get("type") == "vanished":
                to_drop.append(msg)
        for msg in to_drop:
            ctx.remove(msg)
            removed += 1
    tree.write(path, encoding="utf-8", xml_declaration=True)
    return removed


def main():
    for name in ("ru_RU.ts", "en_US.ts"):
        p = ROOT / "translations" / name
        n = clean(p)
        print(p.name, "removed vanished messages:", n)


if __name__ == "__main__":
    main()
