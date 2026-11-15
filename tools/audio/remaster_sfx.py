"""R13 strike remaster (human 2026-11-15: the strike SFX is too noisy): a filtered remix of an already-made cue.

  python tools/audio/remaster_sfx.py [--only Cue,Cue]

For every tools/audio/sfx.json row with a "Remaster" block, reads Content/DC/Audio/Source/Sfx_<Cue>.wav and rewrites
it: a low-pass (LowPassHz, 4th-order magnitude roll-off in the FFT) takes the broadband hiss and crackle out, a
low shelf (LowShelfHz, LowShelfDb) deepens the boom, the cue is cut to Seconds from its onset with a raised-cosine
fade over FadeSeconds (a short tail), and its loudest 300 ms are set to TargetRmsDb (peak under -1 dBFS). Pure numpy;
the result is imported as S_Sfx_<Cue> by tools/content/build_content.py (DC_REBUILD_ONLY=S_Sfx_<Cue>).
Prints the before/after numbers: high-band energy share above 4 kHz, length, loudest-300 ms RMS.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import soundfile as sf

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SOURCE = REPO / "Content" / "DC" / "Audio" / "Source"


def db(x):
    return 20.0 * np.log10(np.maximum(x, 1e-12))


def loudest_rms_db(m, rate, win_s=0.3):
    w = max(int(win_s * rate), 1)
    if len(m) <= w:
        return db(np.sqrt(np.mean(m ** 2)))
    c = np.cumsum(np.concatenate([[0.0], m ** 2]))
    return db(np.sqrt(np.max((c[w:] - c[:-w]) / w)))


def high_share(m, rate, hz=4000.0):
    spec = np.abs(np.fft.rfft(m)) ** 2
    f = np.fft.rfftfreq(len(m), 1.0 / rate)
    return float(spec[f >= hz].sum() / max(spec.sum(), 1e-12))


def remaster(m, rate, r):
    spec = np.fft.rfft(m)
    f = np.fft.rfftfreq(len(m), 1.0 / rate)
    lp = r.get("LowPassHz", 0)
    gain = np.ones_like(f)
    if lp > 0:
        gain *= 1.0 / np.sqrt(1.0 + (f / lp) ** 8)  # 4th-order Butterworth-like magnitude
    shelf_hz, shelf_db = r.get("LowShelfHz", 0), r.get("LowShelfDb", 0.0)
    if shelf_hz > 0 and shelf_db:
        g = 10 ** (shelf_db / 20.0)
        gain *= 1.0 + (g - 1.0) / (1.0 + (f / shelf_hz) ** 4)
    out = np.fft.irfft(spec * gain, n=len(m))
    onset = int(np.argmax(np.abs(out) > 0.05 * np.max(np.abs(out))))
    start = max(onset - int(0.02 * rate), 0)
    out = out[start:start + int(r.get("Seconds", 2.5) * rate)]
    fade = min(int(r.get("FadeSeconds", 1.0) * rate), len(out))
    if fade > 0:
        out[-fade:] *= 0.5 * (1.0 + np.cos(np.linspace(0.0, np.pi, fade)))
    out *= 10 ** ((r.get("TargetRmsDb", -15.0) - loudest_rms_db(out, rate)) / 20.0)
    peak = np.max(np.abs(out))
    ceiling = 10 ** (-1.0 / 20.0)
    if peak > ceiling:
        out *= ceiling / peak
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = {c for c in a.only.split(",") if c}
    rows = json.loads((HERE / "sfx.json").read_text(encoding="utf-8"))["sfx"]
    for row in rows:
        r = row.get("Remaster")
        if not r or (only and row["Cue"] not in only):
            continue
        path = SOURCE / f"Sfx_{row['Cue']}.wav"
        m, rate = sf.read(str(path), always_2d=False)
        m = m if m.ndim == 1 else m.mean(axis=1)
        out = remaster(m.astype(np.float64), rate, r)
        print(f"{row['Cue']}: before {len(m) / rate:.2f} s, >4 kHz share {high_share(m, rate):.3f}, loudest300 "
              f"{loudest_rms_db(m, rate):.1f} dB | after {len(out) / rate:.2f} s, >4 kHz share {high_share(out, rate):.3f}, "
              f"loudest300 {loudest_rms_db(out, rate):.1f} dB")
        sf.write(str(path), out.astype(np.float32), rate, subtype="PCM_16")


if __name__ == "__main__":
    main()
