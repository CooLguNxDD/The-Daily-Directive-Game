"""R13 remix evidence: per-cue levels from a cue-roll recording (-dccueroll -dcaudiorec=Dir).

The cue roll plays every cue alone over muted music, so the mix after each DCCUE line is that cue. For each cue this
measures, over the window up to the next cue: peak dBFS, RMS dB of the loudest 300 ms, crest (peak - RMS), the share
of samples at or above -0.5 dBFS (clipping), the energy share above 4 kHz (harshness) and the tail (seconds from the
peak until the level falls 40 dB). Flags: CLIP (any clipped share), HOT (peak > -1 dBFS), HARSH (> 35 % above 4 kHz),
LOUD / QUIET (loudest-300-ms RMS more than 6 dB from the median cue).

Usage: python tools/evidence/sfx_levels.py <mix.wav> <run.log> [--out report.json]
Exit code 0 always: it is a measurement.
"""
import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from audio_cues import load  # noqa: E402

CUE = re.compile(r"DCCUE (\S+) t=([0-9.]+) dist=([0-9]+)")


def db(x):
    return 20.0 * np.log10(max(float(x), 1e-9))


def measure(seg, rate):
    if len(seg) < rate // 20:
        return None
    peak = float(np.max(np.abs(seg)))
    hop = int(0.3 * rate)
    loud = max((float(np.sqrt(np.mean(seg[i:i + hop] ** 2))) for i in range(0, max(len(seg) - hop, 1), hop // 3)), default=0.0)
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / rate)
    hi = float(spec[f >= 4000].sum() / max(spec.sum(), 1e-12))
    env = np.abs(seg)
    k = int(0.01 * rate)
    env = np.convolve(env, np.ones(k) / k, mode="same")
    ip = int(np.argmax(env))
    below = np.nonzero(env[ip:] < env[ip] * 0.01)[0]
    tail = (below[0] if len(below) else len(env) - ip) / rate
    return {"peak_db": round(db(peak), 1), "loud_rms_db": round(db(loud), 1), "crest_db": round(db(peak) - db(loud), 1),
            "clip_share": round(float(np.mean(np.abs(seg) >= 0.944)), 4), "hi_share": round(hi, 3), "tail_s": round(tail, 2)}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mix")
    p.add_argument("log")
    p.add_argument("--out", default="")
    a = p.parse_args()
    rate, data = load(a.mix)
    cues = [(m.group(1), float(m.group(2))) for m in CUE.finditer(Path(a.log).read_text(encoding="utf-8", errors="replace"))]
    rows = []
    for i, (name, t) in enumerate(cues):
        end = cues[i + 1][1] if i + 1 < len(cues) else t + 3.0
        if end - t > 6.0:  # past the roll: cues in play overlap, not a solo measurement
            continue
        m = measure(data[int(t * rate):int(min(end, t + 3.5) * rate)], rate)
        if m:
            rows.append({"cue": name, "t": t, **m})
    seen, solo = set(), []
    for r in rows:  # the roll plays each cue once; keep the first solo play
        if r["cue"] not in seen:
            seen.add(r["cue"])
            solo.append(r)
    med = float(np.median([r["loud_rms_db"] for r in solo])) if solo else 0.0
    flags = {
        "CLIP": lambda r: r["clip_share"] > 0,
        "HOT": lambda r: r["peak_db"] > -1.0,
        "HARSH": lambda r: r["hi_share"] > 0.35,
        "LOUD": lambda r: r["loud_rms_db"] > med + 6,
        "QUIET": lambda r: r["loud_rms_db"] < med - 6,
    }
    for r in solo:
        r["flags"] = [k for k, f in flags.items() if f(r)]
    solo.sort(key=lambda r: (-len(r["flags"]), -r["loud_rms_db"]))
    print(f"median loudest-300ms RMS {med:.1f} dB over {len(solo)} cues")
    for r in solo:
        print(f"{r['cue']:<14} peak {r['peak_db']:6.1f}  rms {r['loud_rms_db']:6.1f}  crest {r['crest_db']:5.1f}  clip {r['clip_share']:.4f}"
              f"  >4k {r['hi_share']:.2f}  tail {r['tail_s']:4.2f}s  {' '.join(r['flags'])}")
    if a.out:
        Path(a.out).write_text(json.dumps({"median_rms_db": med, "cues": solo}, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
