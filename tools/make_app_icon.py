#!/usr/bin/env python3
"""Rasterize OpenVegas logo.svg geometry into logo.ico / PNG sizes.
Does not require Inkscape — draws with Pillow to match resources/icons/logo.svg.
"""
from __future__ import annotations

import math
import os
import sys

def draw_logo(size: int):
    from PIL import Image, ImageDraw

    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Geometry matches logo.svg viewBox 0..256
    s = size / 256.0

    def xy(x: float, y: float) -> tuple[float, float]:
        return (x * s, y * s)

    cx, cy = 128 * s, 128 * s
    # Outer bright blue disc
    r_outer = 128 * s
    draw.ellipse(
        [cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
        fill=(7, 21, 251, 255),
    )
    # Inner navy
    r_inner = 102 * s
    draw.ellipse(
        [cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner],
        fill=(1, 5, 73, 255),
    )
    # Symmetric V — path from logo.svg:
    # M41 76 L128.5 230 L216 76 H184 L128.5 173 L73 76 H41 Z
    v = [
        xy(41, 76),
        xy(128.5, 230),
        xy(216, 76),
        xy(184, 76),
        xy(128.5, 173),
        xy(73, 76),
    ]
    draw.polygon(v, fill=(232, 231, 224, 255))
    return img


def main() -> int:
    try:
        from PIL import Image
    except ImportError:
        print("Install: pip install pillow", file=sys.stderr)
        return 1

    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    icons = os.path.join(project_root, "resources", "icons")
    os.makedirs(icons, exist_ok=True)

    ico_path = os.path.join(icons, "logo.ico")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [draw_logo(s) for s in sizes]
    # Pillow ICO: pass largest and sizes=
    images[-1].save(
        ico_path,
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=images[:-1],
    )
    # Also write PNG masters used by Linux desktop / qrc fallbacks
    draw_logo(256).save(os.path.join(icons, "logo.png"), format="PNG")
    draw_logo(64).save(os.path.join(icons, "logo-64.png"), format="PNG")
    print(f"Created {ico_path}")
    print(f"Created {os.path.join(icons, 'logo.png')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
