"""R13 remix: render, pick and master the remixed cues of tools/audio/sfx.json.

  python tools/audio/make_sfx.py render <raw_dir> [--only Cue,Cue]   # EzAudio on the local ComfyUI, one file per seed
  python tools/audio/make_sfx.py make <raw_dir> [--only Cue,Cue] [--out Content/DC/Audio/Source] [--report r.json]

render: calls the fleet's comfy.py (local ComfyUI EzAudio, never a paid service) for each missing <Cue>_<Seed>.flac.
make: scores every seed's loudness envelope (20 ms RMS in dB) for the shape of a hit then a falling tail: the loudest
moment early (in the first 25 %), and after it a straight, falling dB line (least squares slope and fit). The best
seed is trimmed from 20 ms before its onset to Seconds, faded out over the last FadeSeconds (raised cosine), set so
its loudest 300 ms sit at TargetRmsDb with the peak under -1 dBFS, and written as 16-bit mono Sfx_<Cue>.wav.
"""
import argparse
import os
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
# The fleet's ComfyUI client lives at the monorepo root: the first parent folder that holds it (or $COMFY_PY).
COMFY = next((d / ".claude/skills/cat-fleet/scripts/comfy.py" for d in REPO.parents
              if (d / ".claude/skills/cat-fleet/scripts/comfy.py").exists()), Path("comfy.py"))
PEAK_CEILING_DB = -1.0


def db(x):
    return 20.0 * np.log10(np.maximum(x, 1e-9))


def envelope(m, rate, hop_s=0.02):
    hop = max(int(hop_s * rate), 1)
    n = len(m) // hop
    return db(np.sqrt(np.mean(m[:n * hop].reshape(n, hop) ** 2, axis=1)))


def score(m, rate):
    """Higher is more hit-then-tail. Returns (score, onset sample)."""
    env = envelope(m, rate)
    ip = int(np.argmax(env))
    early = ip <= 0.25 * len(env)
    tail = env[ip:]
    if len(tail) < 10:
        return -1e9, 0
    t = np.arange(len(tail))
    slope, icpt = np.polyfit(t, tail, 1)
    resid = float(np.sqrt(np.mean((tail - (slope * t + icpt)) ** 2)))
    falls = float(tail[:5].mean() - tail[-5:].mean())
    s = falls - 2.0 * resid + (10.0 if early else -30.0)
    above = np.nonzero(env >= env[ip] - 30.0)[0]
    onset = int(above[0]) * int(0.02 * rate) if len(above) else 0
    return s, onset


def master(m, rate, onset, row):
    a = max(onset - int(0.02 * rate), 0)
    seg = m[a:a + int(row["Seconds"] * rate)].astype(np.float64)
    nf = min(int(row["FadeSeconds"] * rate), len(seg))
    seg[len(seg) - nf:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, nf)))
    hop = int(0.3 * rate)
    loud = max(float(np.sqrt(np.mean(seg[i:i + hop] ** 2))) for i in range(0, max(len(seg) - hop, 1), hop // 3))
    gain = min(10 ** ((row["TargetRmsDb"] - float(db(loud))) / 20), 10 ** (PEAK_CEILING_DB / 20) / max(np.abs(seg).max(), 1e-9))
    return (seg * gain).astype(np.float32), float(db(loud * gain)), float(db(np.abs(seg).max() * gain))


def rows_of(a):
    rows = json.loads((HERE / "sfx.json").read_text(encoding="utf-8"))["sfx"]
    only = {s for s in a.only.split(",") if s}
    return [r for r in rows if not only or r["Cue"] in only]


def render(a):
    out, ok = Path(a.raw_dir), True
    for row in rows_of(a):
        for seed in row["Seeds"]:
            name = f"{row['Cue']}_{seed}"
            if (out / f"{name}.flac").exists():
                continue
            rc = subprocess.run([sys.executable, os.environ.get("COMFY_PY", str(COMFY)), "audio", str(out), name, row["Prompt"],
                                 "--seconds", str(row["Render"]), "--seed", str(seed)]).returncode
            ok = ok and rc == 0
    return 0 if ok else 1


def make(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    report, ok = [], True
    for row in rows_of(a):
        scored = []
        for seed in row["Seeds"]:
            f = Path(a.raw_dir) / f"{row['Cue']}_{seed}.flac"
            if not f.exists():
                continue
            data, rate = sf.read(str(f), always_2d=True)
            m = data.mean(axis=1)
            s, onset = score(m, rate)
            scored.append((s, seed, m, rate, onset))
        if not scored:
            print(f"{row['Cue']}: no renders", file=sys.stderr)
            ok = False
            continue
        scored.sort(key=lambda x: -x[0])
        s, seed, m, rate, onset = scored[0]
        wav, rms, peak = master(m, rate, onset, row)
        sf.write(str(out / f"Sfx_{row['Cue']}.wav"), wav, rate, subtype="PCM_16")
        info = {"Cue": row["Cue"], "seed": seed, "score": round(s, 1), "scores": {str(x[1]): round(x[0], 1) for x in scored},
                "onset_s": round(onset / rate, 2), "seconds": round(len(wav) / rate, 2), "rate": rate,
                "loud_rms_db": round(rms, 1), "peak_db": round(peak, 1)}
        report.append(info)
        print(json.dumps(info))
    if a.report:
        Path(a.report).write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0 if ok else 1


def main():
    p = argparse.ArgumentParser()
    p.add_argument("cmd", choices=["render", "make"])
    p.add_argument("raw_dir")
    p.add_argument("--only", default="")
    p.add_argument("--out", default=str(REPO / "Content" / "DC" / "Audio" / "Source"))
    p.add_argument("--report", default="")
    a = p.parse_args()
    return {"render": render, "make": make}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
