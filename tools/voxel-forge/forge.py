#!/usr/bin/env python3
"""voxel-forge: turn a short text recipe into a voxel model for Director's Cut (R17).

    python tools/voxel-forge/forge.py tools/voxel-forge/recipes/swarmer.vfr            # one model
    python tools/voxel-forge/forge.py tools/voxel-forge/recipes/                        # every recipe
    python tools/voxel-forge/forge.py recipe.vfr --out Content/DC/Voxel --preview Saved/VoxelForge

Writes <out>/<name>.dcvox (text, read by the game's FDCVoxelModel) and <preview>/<name>.png (iso + front + side
views, coloured from the game's voxel palette in Config/DefaultGame.ini). No engine needed.

Recipe language: one op per line, '#' starts a comment. Coordinates are voxels, x right, y forward, z up.
    name swarmer                     model name (default: file stem)
    grid 5                           voxel size in cm (5 props/enemies, 10 buildings, 25-100 big rubble)
    size 24 24 16                    volume in voxels
    slot body Ink                    a palette slot -> a palette material id from the game ini
    box x0 y0 z0 x1 y1 z1 body       filled box, max exclusive
    shell x0 y0 z0 x1 y1 z1 t body   hollow box with wall thickness t (walls and floor/roof)
    sphere cx cy cz r body           ball (voxel centres within r)
    ellipsoid cx cy cz rx ry rz body
    cyl cx cy z0 z1 r body           vertical cylinder
    cone cx cy z0 z1 r0 r1 body      vertical cone / frustum (r0 at z0, r1 at z1)
    beam x0 y0 z0 x1 y1 z1 r body    thick line between two points (legs, masts, barrels)
    poly z0 z1 body x,y x,y x,y ...  2D outline extruded from z0 to z1
    carve <shape args...>            any shape above with no slot: sets air
    paint <shape args...> slot       recolours solid voxels only
    mirror x|y|z                     copies the half below the middle onto the other side
    noise p seed slot                removes surface voxels of slot with probability p (weathering)
    pivot x y z                      model origin in voxels (default: bottom centre)
"""
from __future__ import annotations

import argparse
import math
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
INI = ROOT / "Config" / "DefaultGame.ini"


def load_palette(ini: Path = INI) -> dict[str, tuple[int, tuple[float, float, float]]]:
    """Palette name -> (game material id, linear rgb) from the [DCVoxelSettings] +Palette rows."""
    out: dict[str, tuple[int, tuple[float, float, float]]] = {}
    rx = re.compile(r'^\+Palette=\(Id="(\w+)",Color=\(R=([\d.]+),G=([\d.]+),B=([\d.]+)')
    section = False
    for line in ini.read_text(encoding="utf-8").splitlines():
        if line.startswith("["):
            section = line.strip() == "[/Script/Epic_AI_Mega_Jam2.DCVoxelSettings]"
            continue
        m = rx.match(line) if section else None
        if m:
            out[m.group(1)] = (len(out) + 1, (float(m.group(2)), float(m.group(3)), float(m.group(4))))
    return out


class Model:
    def __init__(self, name: str):
        self.name = name
        self.grid_cm = 5
        self.v = np.zeros((16, 16, 16), dtype=np.uint8)  # [x, y, z], value = slot index + 1
        self.slots: list[tuple[str, str]] = []  # (slot, palette name)
        self.pivot: tuple[int, int, int] | None = None

    # ---- shapes: each returns a boolean mask over the volume
    def coords(self):
        sx, sy, sz = self.v.shape
        return np.meshgrid(np.arange(sx) + 0.5, np.arange(sy) + 0.5, np.arange(sz) + 0.5, indexing="ij")

    def shape(self, kind: str, a: list[str]) -> np.ndarray:
        X, Y, Z = self.coords()
        if kind == "poly":
            return self._poly(X, Y, Z, float(a[0]), float(a[1]), a[2:])
        f = [float(x) for x in a]
        shapes = {
            "box": lambda: (X >= f[0]) & (Y >= f[1]) & (Z >= f[2]) & (X < f[3]) & (Y < f[4]) & (Z < f[5]),
            "shell": lambda: self.shape("box", a[:6]) & ~self.shape("box", [f[0] + f[6], f[1] + f[6], f[2] + f[6], f[3] - f[6], f[4] - f[6], f[5] - f[6]]),
            "sphere": lambda: (X - f[0]) ** 2 + (Y - f[1]) ** 2 + (Z - f[2]) ** 2 <= f[3] ** 2,
            "ellipsoid": lambda: ((X - f[0]) / f[3]) ** 2 + ((Y - f[1]) / f[4]) ** 2 + ((Z - f[2]) / f[5]) ** 2 <= 1.0,
            "cyl": lambda: ((X - f[0]) ** 2 + (Y - f[1]) ** 2 <= f[4] ** 2) & (Z >= f[2]) & (Z < f[3]),
            "cone": lambda: self._cone(X, Y, Z, f),
            "beam": lambda: self._beam(X, Y, Z, f),
        }
        if kind not in shapes:
            raise ValueError(f"unknown shape '{kind}'")
        return shapes[kind]()

    @staticmethod
    def _cone(X, Y, Z, f):
        t = np.clip((Z - f[2]) / max(f[3] - f[2], 1e-6), 0.0, 1.0)
        r = f[4] + (f[5] - f[4]) * t
        return ((X - f[0]) ** 2 + (Y - f[1]) ** 2 <= r ** 2) & (Z >= f[2]) & (Z < f[3])

    @staticmethod
    def _beam(X, Y, Z, f):
        a = np.array(f[0:3])
        b = np.array(f[3:6])
        d = b - a
        L2 = max(float(d @ d), 1e-6)
        t = np.clip(((X - a[0]) * d[0] + (Y - a[1]) * d[1] + (Z - a[2]) * d[2]) / L2, 0.0, 1.0)
        px, py, pz = a[0] + t * d[0], a[1] + t * d[1], a[2] + t * d[2]
        return (X - px) ** 2 + (Y - py) ** 2 + (Z - pz) ** 2 <= f[6] ** 2

    @staticmethod
    def _poly(X, Y, Z, z0, z1, pts):
        poly = [tuple(float(c) for c in p.split(",")) for p in pts]
        inside = np.zeros(X.shape, dtype=bool)
        n = len(poly)
        for i in range(n):  # even-odd ray cast in x/y
            (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
            cond = (Y < y0) != (Y < y1)
            xint = x0 + (Y - y0) * (x1 - x0) / ((y1 - y0) if y1 != y0 else 1e-6)
            inside ^= cond & (X < xint)
        return inside & (Z >= z0) & (Z < z1)

    def slot_index(self, slot: str) -> int:
        for i, (s, _) in enumerate(self.slots):
            if s == slot:
                return i + 1
        raise ValueError(f"unknown slot '{slot}' (declare it with: slot {slot} <PaletteName>)")

    SHAPE_ARGS = {"box": 6, "shell": 7, "sphere": 4, "ellipsoid": 6, "cyl": 5, "cone": 6, "beam": 7}

    def split_shape(self, toks: list[str], with_slot: bool):
        kind = toks[0]
        if kind == "poly":
            return kind, (toks[1:-1] if with_slot else toks[1:]), (toks[-1] if with_slot else None)
        n = self.SHAPE_ARGS.get(kind)
        if n is None:
            raise ValueError(f"unknown shape '{kind}'")
        return kind, toks[1:1 + n], (toks[1 + n] if with_slot else None)

    # ---- ops (a registry: one handler per keyword)
    def op(self, line: str, lineno: int):
        toks = line.split()
        k = toks[0]
        handlers = {
            "name": lambda: setattr(self, "name", toks[1]),
            "grid": lambda: setattr(self, "grid_cm", int(toks[1])),
            "size": lambda: setattr(self, "v", np.zeros((int(toks[1]), int(toks[2]), int(toks[3])), dtype=np.uint8)),
            "slot": lambda: self.slots.append((toks[1], toks[2])),
            "pivot": lambda: setattr(self, "pivot", (int(toks[1]), int(toks[2]), int(toks[3]))),
            "carve": lambda: self._carve(toks[1:]),
            "paint": lambda: self._paint(toks[1:]),
            "mirror": lambda: self._mirror(toks[1]),
            "noise": lambda: self._noise(float(toks[1]), int(toks[2]), toks[3]),
        }
        if k in handlers:
            handlers[k]()
            return
        if k == "poly":  # poly z0 z1 slot x,y x,y ...
            self.v[self.shape("poly", [toks[1], toks[2]] + toks[4:])] = self.slot_index(toks[3])
            return
        if k in self.SHAPE_ARGS:
            kind, args, slot = self.split_shape(toks, True)
            self.v[self.shape(kind, args)] = self.slot_index(slot)
            return
        raise ValueError(f"line {lineno}: unknown op '{k}'")

    def _carve(self, toks):
        if toks[0] == "poly":
            self.v[self.shape("poly", toks[1:])] = 0
            return
        kind, args, _ = self.split_shape(toks, False)
        self.v[self.shape(kind, args)] = 0

    def _paint(self, toks):
        slot = toks[-1]
        if toks[0] == "poly":
            mask = self.shape("poly", toks[1:-1])
        else:
            kind, args, _ = self.split_shape(toks[:-1], False)
            mask = self.shape(kind, args)
        self.v[mask & (self.v != 0)] = self.slot_index(slot)

    def _mirror(self, axis: str):
        ax = "xyz".index(axis)
        n = self.v.shape[ax]
        half = n // 2
        src = np.take(self.v, range(half), axis=ax)
        flipped = np.flip(src, axis=ax)
        idx = [slice(None)] * 3
        idx[ax] = slice(n - half, n)
        self.v[tuple(idx)] = flipped

    def _noise(self, p: float, seed: int, slot: str):
        s = self.slot_index(slot)
        solid = self.v != 0
        exposed = np.zeros_like(solid)
        for ax in range(3):
            for d in (1, -1):
                exposed |= solid & ~np.roll(solid, d, axis=ax)
        rng = np.random.default_rng(seed)
        kill = exposed & (self.v == s) & (rng.random(self.v.shape) < p)
        self.v[kill] = 0


def parse(path: Path) -> Model:
    m = Model(path.stem)
    for i, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if line:
            m.op(line, i)
    return m


def write_dcvox(m: Model, palette: dict, out: Path):
    """Text format, one model: header lines, then run-length tokens over the volume, x fastest then y then z.
    Each token is count*slot (slot 0 = air). Slots map to palette names, resolved to ids by the game."""
    for _, pal in m.slots:
        if pal not in palette:
            raise ValueError(f"{m.name}: palette material '{pal}' is not in Config/DefaultGame.ini")
    sx, sy, sz = m.v.shape
    pivot = m.pivot or (sx // 2, sy // 2, 0)
    flat = m.v.transpose(2, 1, 0).reshape(-1)  # z, y, x order -> x fastest
    runs = []
    prev, count = int(flat[0]), 0
    for val in flat:
        val = int(val)
        if val == prev:
            count += 1
        else:
            runs.append(f"{count}*{prev}")
            prev, count = val, 1
    runs.append(f"{count}*{prev}")
    lines = [
        "dcvox 1",
        f"name {m.name}",
        f"grid_cm {m.grid_cm}",
        f"size {sx} {sy} {sz}",
        f"pivot {pivot[0]} {pivot[1]} {pivot[2]}",
        "slots " + " ".join(pal for _, pal in m.slots),
        f"solid {int((m.v != 0).sum())}",
    ]
    body = []
    for i in range(0, len(runs), 24):
        body.append(" ".join(runs[i:i + 24]))
    out.write_text("\n".join(lines + ["rle"] + body) + "\n", encoding="utf-8")


def render(m: Model, palette: dict, out: Path, scale: int = 6):
    """Iso, front and side views from voxel faces (painter's order), shaded per face like the game's flat look."""
    from PIL import Image, ImageDraw

    cols = {}
    for i, (_, pal) in enumerate(m.slots, 1):
        lin = palette[pal][1]
        cols[i] = tuple(int(255 * (min(max(c, 0.0), 1.0) ** (1 / 2.2))) for c in lin)
    sx, sy, sz = m.v.shape
    solid = m.v != 0

    def shade(c, k):
        return tuple(int(v * k) for v in c)

    W = 360
    img = Image.new("RGB", (W * 3, W + 30), (242, 232, 208))
    d = ImageDraw.Draw(img)
    # iso view
    s = max(2, int(W / (sx + sy + sz) * 1.2))
    ox, oy = W // 2, W - 20
    xs, ys, zs = np.nonzero(solid)
    order = np.argsort(xs + ys + zs)
    for i in order:
        x, y, z = int(xs[i]), int(ys[i]), int(zs[i])
        c = cols[int(m.v[x, y, z])]
        px = ox + (x - y) * s * 0.87
        py = oy - (x + y) * s * 0.5 - z * s
        top = [(px, py - s), (px + s * 0.87, py - s * 0.5), (px, py), (px - s * 0.87, py - s * 0.5)]
        left = [(px - s * 0.87, py - s * 0.5), (px, py), (px, py + s), (px - s * 0.87, py + s * 0.5)]
        right = [(px, py), (px + s * 0.87, py - s * 0.5), (px + s * 0.87, py + s * 0.5), (px, py + s)]
        d.polygon(top, fill=shade(c, 1.0), outline=(22, 22, 26))
        d.polygon(left, fill=shade(c, 0.72), outline=(22, 22, 26))
        d.polygon(right, fill=shade(c, 0.86), outline=(22, 22, 26))

    def ortho(panel, axis_u, axis_v, depth_axis, flip_depth):
        cell = max(1, int((W - 20) / max(m.v.shape[axis_u], m.v.shape[axis_v])))
        x0 = panel * W + 10
        y0 = 10
        vol = m.v if not flip_depth else np.flip(m.v, axis=depth_axis)
        first = np.argmax(np.moveaxis(vol != 0, depth_axis, -1), axis=-1)
        any_ = np.any(np.moveaxis(vol != 0, depth_axis, -1), axis=-1)
        vals = np.take_along_axis(np.moveaxis(vol, depth_axis, -1), first[..., None], axis=-1)[..., 0]
        # remaining dims are (axis_u, axis_v) in increasing axis order
        rem = [a for a in range(3) if a != depth_axis]
        for i in range(vals.shape[0]):
            for j in range(vals.shape[1]):
                if not any_[i, j]:
                    continue
                u, v = (i, j) if rem[0] == axis_u else (j, i)
                c = shade(cols[int(vals[i, j])], 0.9)
                X = x0 + u * cell
                Y = y0 + (m.v.shape[axis_v] - 1 - v) * cell
                d.rectangle([X, Y, X + cell - 1, Y + cell - 1], fill=c, outline=(22, 22, 26) if cell > 3 else None)

    ortho(1, 0, 2, 1, False)  # front: looking along +y
    ortho(2, 1, 2, 0, False)  # side: looking along +x
    d.text((10, W + 8), f"{m.name}  {sx}x{sy}x{sz} @ {m.grid_cm} cm  solid {int(solid.sum())}   iso | front | side", fill=(22, 22, 26))
    img.save(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("recipes", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, default=ROOT / "Content" / "DC" / "Voxel")
    ap.add_argument("--preview", type=Path, default=ROOT / "Saved" / "VoxelForge")
    a = ap.parse_args(argv)
    palette = load_palette()
    files = []
    for r in a.recipes:
        files += sorted(r.glob("*.vfr")) if r.is_dir() else [r]
    a.out.mkdir(parents=True, exist_ok=True)
    a.preview.mkdir(parents=True, exist_ok=True)
    for f in files:
        m = parse(f)
        write_dcvox(m, palette, a.out / f"{m.name}.dcvox")
        render(m, palette, a.preview / f"{m.name}.png")
        print(f"forge: {f.name} -> {m.name}.dcvox  {m.v.shape} @ {m.grid_cm} cm, solid {int((m.v != 0).sum())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
