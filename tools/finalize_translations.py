# -*- coding: utf-8 -*-
"""Finalize ru_RU and en_US .ts: fill missing strings, clear unfinished markers."""
from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path

from apply_remaining_en import EN

ROOT = Path(__file__).resolve().parents[1]


def message_source(msg: ET.Element) -> str:
    src_el = msg.find("source")
    if src_el is None:
        return ""
    return "".join(src_el.itertext())


def message_translation(msg: ET.Element) -> str:
    tr_el = msg.find("translation")
    if tr_el is None:
        return ""
    return "".join(tr_el.itertext())


def set_translation(msg: ET.Element, text: str) -> None:
    tr_el = msg.find("translation")
    if tr_el is None:
        return
    tr_el.text = text
    tr_el.attrib.pop("type", None)


def finalize_ru(path: Path) -> int:
    tree = ET.parse(path)
    root = tree.getroot()
    n = 0
    for msg in root.iter("message"):
        tr_el = msg.find("translation")
        if tr_el is None or tr_el.get("type") == "vanished":
            continue
        src = message_source(msg)
        tr = message_translation(msg)
        if not tr.strip() or tr_el.get("type") == "unfinished":
            set_translation(msg, src)
            n += 1
    tree.write(path, encoding="utf-8", xml_declaration=True)
    return n


def finalize_en(path: Path) -> int:
    tree = ET.parse(path)
    root = tree.getroot()
    n = 0
    for msg in root.iter("message"):
        tr_el = msg.find("translation")
        if tr_el is None or tr_el.get("type") == "vanished":
            continue
        src = message_source(msg)
        tr = message_translation(msg)
        if src in EN and (not tr.strip() or tr == src or tr_el.get("type") == "unfinished"):
            set_translation(msg, EN[src])
            n += 1
        elif not tr.strip() or tr_el.get("type") == "unfinished":
            # Technical tokens: copy source
            set_translation(msg, src)
            n += 1
    tree.write(path, encoding="utf-8", xml_declaration=True)
    return n


def main() -> None:
    ru = ROOT / "translations" / "ru_RU.ts"
    en = ROOT / "translations" / "en_US.ts"
    from apply_remaining_en import apply_en_translations
    print("en dict:", apply_en_translations())
    print("ru_RU:", finalize_ru(ru))
    print("en_US:", finalize_en(en))


if __name__ == "__main__":
    main()
