"""Strike vs telegraph level in a -dcaudiorec recording (human 15:00 sound check).

Usage: python strike_vs_telegraph.py <dir with mix.wav and game.log>
Aligns the wav to the log by the 'DCCUE recording started' line, then for every Detonation start compares its peak
(0..1.5 s after) with the peak of the telegraph window before it (from the first Telegraph start in the 4 s before the
detonation up to it). Prints one line per strike and the medians (dBFS)."""
import re
import sys
import wave
from datetime import datetime

import numpy as np

STAMP = re.compile(r"^\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d):(\d{3})\]")


def when(line):
    m = STAMP.match(line)
    if not m:
        return None
    return datetime.strptime(m.group(1), "%Y.%m.%d-%H.%M.%S").timestamp() + int(m.group(2)) / 1000.0


def main(d):
    lines = open(f"{d}/game.log", encoding="utf-8", errors="replace").read().splitlines()
    start = next(when(l) for l in lines if "DCCUE recording started" in l and when(l))
    tele = [when(l) - start for l in lines if "DCAUDIO start Telegraph" in l and when(l)]
    det = [when(l) - start for l in lines if "DCAUDIO start Detonation" in l and when(l)]
    w = wave.open(f"{d}/mix.wav")
    sr, sw, ch = w.getframerate(), w.getsampwidth(), w.getnchannels()
    a = np.frombuffer(w.readframes(w.getnframes()), dtype={2: np.int16, 4: np.int32}[sw]).astype(np.float64)
    a = np.abs(a.reshape(-1, ch)).max(1) / float(2 ** (8 * sw - 1))
    db = lambda x: 20 * np.log10(max(x, 1e-9))
    peak = lambda t0, t1: a[max(int(t0 * sr), 0):max(int(t1 * sr), 0)].max(initial=0.0)
    rows, last = [], -10.0
    for t in det:
        if t - last < 1.5 or t + 1.5 > len(a) / sr:
            continue  # one line per strike (merged multi-blasts), inside the recording
        last = t
        before = [x for x in tele if t - 4.0 <= x < t]
        if not before:
            continue
        tp, dp = peak(before[0], t), peak(t, t + 1.5)
        rows.append((t, db(tp), db(dp)))
        print(f"strike at {t:6.1f} s: telegraph peak {db(tp):6.1f} dBFS, detonation peak {db(dp):6.1f} dBFS, diff {db(dp) - db(tp):+5.1f} dB")
    if rows:
        r = np.array(rows)
        print(f"strikes={len(rows)} median telegraph {np.median(r[:, 1]):.1f} dBFS, median detonation {np.median(r[:, 2]):.1f} dBFS, median diff {np.median(r[:, 2] - r[:, 1]):+.1f} dB, clipped samples {int((a >= 0.999).sum())}")


if __name__ == "__main__":
    main(sys.argv[1])
