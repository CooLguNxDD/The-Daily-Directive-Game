"""Listening evidence for R17 ("no silent action").

Reads the master-mix wav a scripted run recorded (-dcaudiorec=Dir) and the run log's DCCUE lines
("DCCUE <cue> t=<seconds> dist=<cm>"), then measures, for every cue played, the loudest 50 ms of the mix in the
0.6 s after the cue against the quietest 50 ms in the 0.4 s before it. A cue counts as heard when that rise is at
least --min-rise-db, or when the window peaks above --floor-db (a cue fired over another loud sound).

Usage: python tools/evidence/audio_cues.py <mix.wav> <run.log> [--out report.txt]
The report lists, per cue name: times played, times heard, median rise. Exit code 0 always: it is a measurement.
"""
import argparse
import re
import statistics
import wave

import numpy as np

CUE = re.compile(r"DCCUE (\S+) t=([0-9.]+) dist=([0-9]+)")


def load(path):
    with wave.open(path, "rb") as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(w.getnframes())
    kinds = {2: np.int16, 4: np.int32}
    data = np.frombuffer(raw, dtype=kinds[width]).astype(np.float64)
    data = data.reshape(-1, channels).mean(axis=1) / float(np.iinfo(kinds[width]).max)
    return rate, data


def rms_db(x):
    return 20.0 * np.log10(max(float(np.sqrt(np.mean(x * x))) if len(x) else 0.0, 1e-6))


def window_db(data, rate, start, end, pick):
    a, b = max(int(start * rate), 0), min(int(end * rate), len(data))
    hop = int(0.05 * rate)
    vals = [rms_db(data[i:i + hop]) for i in range(a, max(b - hop, a) + 1, hop)] or [-120.0]
    return pick(vals)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("log")
    ap.add_argument("--out")
    ap.add_argument("--min-rise-db", type=float, default=3.0)
    ap.add_argument("--floor-db", type=float, default=-30.0)
    args = ap.parse_args()

    rate, data = load(args.wav)
    cues = []
    with open(args.log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = CUE.search(line)
            if m:
                cues.append((m.group(1), float(m.group(2)), int(m.group(3))))

    per = {}
    for name, t, dist in cues:
        if t * rate >= len(data):
            continue
        before = window_db(data, rate, t - 0.4, t, min)
        after = window_db(data, rate, t, t + 0.6, max)
        heard = after - before >= args.min_rise_db or after >= args.floor_db
        per.setdefault(name, []).append((after - before, after, heard, dist))

    lines = [f"mix {args.wav}: {len(data) / rate:.1f} s at {rate} Hz; {len(cues)} cue plays logged",
             f"heard = rise >= {args.min_rise_db} dB over the 0.4 s before, or window peak >= {args.floor_db} dBFS",
             f"{'cue':<16}{'plays':>6}{'heard':>7}{'median rise dB':>16}{'max peak dBFS':>15}"]
    for name in sorted(per):
        rows = per[name]
        lines.append(f"{name:<16}{len(rows):>6}{sum(r[2] for r in rows):>7}"
                     f"{statistics.median(r[0] for r in rows):>16.1f}{max(r[1] for r in rows):>15.1f}")
    silent = [n for n, rows in per.items() if not any(r[2] for r in rows)]
    lines.append("never heard: " + (", ".join(sorted(silent)) if silent else "none"))
    text = "\n".join(lines)
    print(text)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
