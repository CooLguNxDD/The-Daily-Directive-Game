"""R1 frame time (advisor a-33): who owns each DCSPIKE frame.

Reads a -dcperf game log. Every DCSPIKE line carries the frame's thread times and the frame-scope table
(DCFrameScopes: owners=A:ms xcalls, ours, world, gc, unowned). A spike is bound by the slowest of game, render and
gpu; a game-bound spike is owned by its biggest scope when our scopes cover at least half of the game thread, by GC
when GC does, else it is "unowned" (engine work outside our scopes). Prints counts per run phase, then the sector-play
table (phase=1 loading=0), which is the one the frame budget is judged on.

Capture frames (R24 chip spike hunt, chips-efe1a5b): an evidence screenshot reads the frame back, which flushes the
render thread and the GPU for ~180 ms that no thread timer counts. Those frames are owned by "capture": live from the
spike line's shot=1, and in older logs from a DEMO shot / DEMO chipfire line one or two engine frames earlier. The
per-second summary (DCPERF lines) prints the seconds over 33 ms with and without the seconds that took a capture, the
number to compare with a run that took no frames.

usage: python tools/evidence/spike_owners.py <game.log> [label]
"""
import collections
import re
import sys

PHASES = {0: "lobby", 1: "sector", 2: "draft", 3: "reel", 4: "finalcut", 5: "vote", 6: "frontpage"}
NUM = re.compile(r"(\w+)=(-?[\d.]+)(?=\s|$)")
OWNERS = re.compile(r"owners=(\S+)")
FRAME = re.compile(r"^\[([^\]]+)\]\[\s*(\d+)\]")
SHOT_LINES = ("DEMO shot ", "DEMO chipfire chipfire_")
OVER_MS = 33.0


def parse(line):
    vals = {k: float(v) for k, v in NUM.findall(line)}
    owners = []
    m = OWNERS.search(line)
    if m and m.group(1) != "-":
        for item in m.group(1).split(","):
            name, rest = item.split(":", 1)
            ms, _, calls = rest.partition("x")
            owners.append((name, float(ms), int(calls or 0)))
    return vals, owners


def classify(vals, owners, shot=False):
    if shot or vals.get("shot", 0.0) >= 1.0:
        return "capture", "screenshot"
    threads = {"game": vals.get("game_ms", 0.0), "render": vals.get("render_ms", 0.0), "gpu": vals.get("gpu_ms", 0.0)}
    bound = max(threads, key=threads.get)
    if bound != "game":
        return bound, "-"
    game = max(threads["game"], 1e-3)
    if vals.get("gc", 0.0) >= 0.5 * game:
        return bound, "GC"
    if "ours" not in vals:
        return bound, "untimed"  # a log from before the frame scopes
    if vals["ours"] >= 0.5 * game and owners:
        return bound, owners[0][0]
    return bound, "unowned"


def main():
    path = sys.argv[1]
    label = sys.argv[2] if len(sys.argv) > 2 else path
    by_phase = collections.Counter()
    sector = collections.Counter()
    worst = collections.defaultdict(float)
    examples = {}
    total = 0
    shot_frame = None  # engine frame (mod 1000, as the log prints it) of the last capture request line
    shot_seconds = set()  # log second stamps that held a capture frame
    seconds = []  # (second stamp, max frame ms) of every sector-play DCPERF line
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = FRAME.match(line)
            frame = int(m.group(2)) if m else None
            if m and any(t in line for t in SHOT_LINES):
                shot_frame = frame
            if "DCPERF" in line and m:
                vals, _ = parse(line[line.index("DCPERF"):])
                if int(vals.get("phase", -1)) == 1 and int(vals.get("loading", 0)) == 0:
                    seconds.append((m.group(1)[:-4], vals.get("max_ms", 0.0)))
                continue
            if "DCSPIKE" not in line:
                continue
            vals, owners = parse(line[line.index("DCSPIKE"):])
            total += 1
            phase = PHASES.get(int(vals.get("phase", -1)), "?")
            loading = int(vals.get("loading", 0))
            retro_shot = shot_frame is not None and frame is not None and (frame - shot_frame) % 1000 <= 2
            bound, owner = classify(vals, owners, retro_shot)
            if owner == "screenshot" and m:
                shot_seconds.add(m.group(1)[:-4])
            by_phase[(phase, loading, bound)] += 1
            if phase == "sector" and loading == 0:
                key = (bound, owner)
                sector[key] += 1
                if vals.get("frame_ms", 0.0) > worst[key]:
                    worst[key] = vals.get("frame_ms", 0.0)
                    examples[key] = line.strip()[-260:]
    print(f"spike owners: {label}")
    print(f"DCSPIKE lines: {total}")
    for (phase, loading, bound), n in sorted(by_phase.items(), key=lambda kv: -kv[1]):
        print(f"  phase={phase} loading={loading} {bound}-bound: {n}")
    n_sector = sum(sector.values())
    print(f"sector play (phase=1 loading=0): {n_sector} spikes")
    for (bound, owner), n in sorted(sector.items(), key=lambda kv: -kv[1]):
        print(f"  {bound}-bound owner={owner}: {n} (worst frame {worst[(bound, owner)]:.1f} ms)")
    over = [stamp for stamp, ms in seconds if ms > OVER_MS]
    clean = [stamp for stamp in over if stamp not in shot_seconds]
    print(f"sector-play seconds (DCPERF): {len(seconds)}; with a frame over {OVER_MS:.0f} ms: {len(over)}; "
          f"without the capture seconds: {len(clean)} of {len(seconds) - (len(over) - len(clean))}")
    print("worst example per owner:")
    for key, text in examples.items():
        print(f"  [{key[0]}/{key[1]}] {text}")


if __name__ == "__main__":
    main()
