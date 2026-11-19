"""R25 look numbers per frame (art bible: about 70-80 % of a night frame dark or mid-dark, about 10 % neon accent).

python tools/evidence/look_metrics.py <frame.png|dir> [...] [--hud-crop 0.12]

Per frame: dark share (Rec.709 luma < 0.25 of full scale, sRGB-encoded pixels), mid share (0.25-0.5), bright share
(>= 0.5), accent share (saturated magenta or cyan hue with luma >= 0.15), danger share (saturated red), signal share
(lime-yellow), emissive share (HSV value >= 0.6, any hue: the art bible's "neon and emissive light cover roughly
10 %", which counts warm windows, lamps and fire) and loose accent (magenta/cyan hue, saturation >= 0.25, value >= 0.15:
counts the sign rims and tinted pools the strict accent misses; advisor a-11). The top and bottom hud_crop of the frame are left out (HUD bars). A number proves the split, never the
look: keep the frame beside it.
"""
from __future__ import annotations

import argparse
import colorsys
import sys
from pathlib import Path

from PIL import Image


def metrics(path: Path, hud_crop: float) -> dict[str, float]:
    im = Image.open(path).convert("RGB")
    w, h = im.size
    top, bottom = int(h * hud_crop), int(h * (1 - hud_crop))
    im = im.crop((0, top, w, bottom)).resize((max(1, w // 4), max(1, (bottom - top) // 4)))
    counts = dict(dark=0, mid=0, bright=0, accent=0, danger=0, signal=0, emissive=0, accent_loose=0)
    n = 0
    for r, g, b in (im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()):
        n += 1
        y = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
        counts["dark" if y < 0.25 else "mid" if y < 0.5 else "bright"] += 1
        hue, sat, val = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
        deg = hue * 360.0
        counts["emissive"] += val >= 0.6
        counts["accent_loose"] += sat >= 0.25 and val >= 0.15 and (280 <= deg <= 340 or 170 <= deg <= 205)
        if sat < 0.45 or val < 0.3:
            continue
        if 280 <= deg <= 340 or 170 <= deg <= 205:
            counts["accent"] += 1
        elif deg >= 345 or deg <= 12:
            counts["danger"] += 1
        elif 60 <= deg <= 85:
            counts["signal"] += 1
    return {k: v / max(n, 1) for k, v in counts.items()}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--hud-crop", type=float, default=0.12)
    a = ap.parse_args(argv)
    files: list[Path] = []
    for p in map(Path, a.paths):
        files += sorted(p.glob("*.png")) if p.is_dir() else [p]
    print("frame,dark,mid,bright,accent,danger,signal,emissive,accent_loose")
    for f in files:
        m = metrics(f, a.hud_crop)
        print(f"{f.name},{m['dark']:.3f},{m['mid']:.3f},{m['bright']:.3f},{m['accent']:.3f},{m['danger']:.3f},{m['signal']:.3f},{m['emissive']:.3f},{m['accent_loose']:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
