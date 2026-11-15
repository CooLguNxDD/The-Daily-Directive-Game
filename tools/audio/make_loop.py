"""R14: turn a raw ACE-Step render into a seamless game loop.

  python tools/audio/make_loop.py <raw_dir> [--only Id,Id] [--out Content/DC/Audio/Source] [--report report.json]

Per tools/audio/tracks.json row (raw file <raw_dir>/<Id>.flac|wav):
  1. cut Bars bars (4/4 at the row's Bpm) starting TrimHeadSeconds in, plus LoopFadeBars more for the seam,
     shrinking Bars when the render is too short (the render's own ending is never used);
  2. equal-power crossfade of those extra bars into the loop head, so the last sample runs straight into the first;
  3. one gain to TargetRmsDb, capped so the peak stays under -1 dBFS (no limiter, no clipping);
  4. write 16-bit PCM <out>/<Id>.wav, imported by tools/content/build_content.py as S_Music_<Id>.
Prints and (with --report) writes per-track numbers: seconds, rms_db, peak_db, seam jump vs the median sample step.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
PEAK_CEILING_DB = -1.0
TAIL_GUARD_SECONDS = 4.0  # ACE renders fade out at the end; stay clear of it


def db(x: float) -> float:
    return 20.0 * np.log10(max(x, 1e-9))


def make_loop(a: np.ndarray, rate: int, row: dict) -> tuple[np.ndarray, dict]:
    bar = 4 * 60.0 / float(row["Bpm"])
    head = int(round(float(row.get("TrimHeadSeconds", 0.0)) * rate))
    fade_bars = max(int(row.get("LoopFadeBars", 1)), 1)
    bars = int(row["Bars"])
    usable = len(a) - head - int(TAIL_GUARD_SECONDS * rate)
    while bars > 2 and int(round((bars + fade_bars) * bar * rate)) > usable:
        bars -= 1
    n = int(round(bars * bar * rate))
    x = int(round(fade_bars * bar * rate))
    if head + n + x > len(a):
        raise ValueError(f"render too short: {len(a) / rate:.1f}s for {bars} bars at {row['Bpm']} bpm")
    seg = a[head:head + n + x].astype(np.float64)
    out = seg[:n].copy()
    t = (np.arange(x) / x)[:, None]
    out[:x] = seg[:x] * np.sin(t * np.pi / 2) + seg[n:n + x] * np.cos(t * np.pi / 2)
    rms = float(np.sqrt(np.mean(out ** 2)))
    peak = float(np.max(np.abs(out)))
    gain = min(10 ** ((float(row.get("TargetRmsDb", -20.0)) - db(rms)) / 20), 10 ** (PEAK_CEILING_DB / 20) / max(peak, 1e-9))
    out *= gain
    steps = np.abs(np.diff(out, axis=0)).max(axis=1)
    seam = float(np.abs(out[0] - out[-1]).max())
    info = {
        "Id": row["Id"], "seconds": round(n / rate, 3), "bars": bars, "rate": rate, "channels": out.shape[1],
        "rms_db": round(db(float(np.sqrt(np.mean(out ** 2)))), 2), "peak_db": round(db(float(np.max(np.abs(out)))), 2),
        "gain_db": round(db(gain), 2), "seam_jump": round(seam, 5), "median_step": round(float(np.median(steps)), 5),
        "p99_step": round(float(np.percentile(steps, 99)), 5),
    }
    return out.astype(np.float32), info


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("raw_dir")
    p.add_argument("--only", default="")
    p.add_argument("--out", default=str(REPO / "Content" / "DC" / "Audio" / "Source"))
    p.add_argument("--report", default="")
    p.add_argument("--manifest", default=str(HERE / "tracks.json"))
    a = p.parse_args()
    rows = json.loads(Path(a.manifest).read_text(encoding="utf-8"))["tracks"]
    only = {s for s in a.only.split(",") if s}
    out_dir = Path(a.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    report, ok = [], True
    for row in rows:
        if only and row["Id"] not in only:
            continue
        raw = next((f for f in (Path(a.raw_dir) / f"{row['Id']}{e}" for e in (".flac", ".wav", ".mp3")) if f.exists()), None)
        if raw is None:
            print(f"{row['Id']}: no raw render in {a.raw_dir}", file=sys.stderr)
            ok = False
            continue
        data, rate = sf.read(str(raw), always_2d=True)
        try:
            loop, info = make_loop(data, rate, row)
        except ValueError as e:
            print(f"{row['Id']}: {e}", file=sys.stderr)
            ok = False
            continue
        sf.write(str(out_dir / f"{row['Id']}.wav"), loop, rate, subtype="PCM_16")
        info["source"] = raw.name
        report.append(info)
        print(json.dumps(info))
    if a.report:
        Path(a.report).write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
