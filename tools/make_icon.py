# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Guidjlk
"""Render the font-free vector monogram as a transparent PS3 XMB icon.

Requires Pillow. The small renderer supports only the geometry used by
assets/icon.svg; it keeps transparency in the border and letter cutouts.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent


def main():
    factor = 4
    scale = 168 * factor / 256
    x_offset, y_offset = 76 * factor, 4 * factor
    image = Image.new("RGBA", (320 * factor, 176 * factor), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    def point(x, y):
        return round(x_offset + x * scale), round(y_offset + y * scale)

    for shape in ET.parse(ROOT / "assets/icon.svg").getroot():
        tag = shape.tag.rsplit("}", 1)[-1]
        a = shape.attrib
        if tag == "rect":
            x, y, w, h = (float(a[k]) for k in ("x", "y", "width", "height"))
            draw.rounded_rectangle(
                (*point(x, y), *point(x + w, y + h)),
                radius=float(a["rx"]) * scale,
                outline=a["stroke"], width=round(float(a["stroke-width"]) * scale),
            )
        elif tag == "line":
            draw.line([point(float(a["x1"]), float(a["y1"])),
                       point(float(a["x2"]), float(a["y2"]))],
                      fill=a["stroke"], width=round(float(a["stroke-width"]) * scale))
        elif tag == "polygon":
            coordinates = [float(v) for v in re.findall(r"[\d.]+", a["points"])]
            draw.polygon([point(x, y) for x, y in zip(coordinates[::2], coordinates[1::2])], fill=a["fill"])
        elif tag == "path":
            for index, contour in enumerate(filter(str.strip, a["d"].split("Z"))):
                coordinates = [float(v) for v in re.findall(r"[\d.]+", contour)]
                draw.polygon([point(x, y) for x, y in zip(coordinates[::2], coordinates[1::2])],
                             fill=a["fill"] if index == 0 else (0, 0, 0, 0))
        elif tag != "title":
            raise ValueError(f"Unsupported icon shape: {tag}")
    image.resize((320, 176), Image.Resampling.LANCZOS).save(ROOT / "assets/ICON0.PNG")


if __name__ == "__main__":
    main()
