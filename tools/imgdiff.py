#!/usr/bin/env python3
"""Compare two renderer captures: mean absolute difference and a side-by-side image. Needs Pillow."""
import sys
from PIL import Image, ImageChops, ImageStat

a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
if a.size != b.size:
    b = b.resize(a.size)
d = ImageChops.difference(a, b)
mean = sum(ImageStat.Stat(d).mean) / 3
bbox = d.point(lambda v: 255 if v > 48 else 0).getbbox()
print(f"mean abs diff {mean:.2f} (0 identical, ~2 same frame with motion noise); large differences in {bbox}")
if len(sys.argv) > 3:
    w, h = a.size
    out = Image.new("RGB", (w, h * 3))
    out.paste(a, (0, 0)); out.paste(b, (0, h)); out.paste(d.point(lambda v: min(255, v * 4)), (0, h * 2))
    out.save(sys.argv[3])
    print(f"wrote {sys.argv[3]} (top: first, middle: second, bottom: difference x4)")
sys.exit(0 if mean < 6 else 1)
