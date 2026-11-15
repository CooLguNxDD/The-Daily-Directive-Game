"""R14: render the music tracks of tools/audio/tracks.json with ACE-Step 1.5 on the local ComfyUI (built-in nodes).

Local generation only (the asset-gen skill's ComfyUI on the LAN box), never a paid service.

  python tools/audio/gen_music.py <out_dir> [--only Id,Id] [--url http://192.168.50.74:24002] [--timeout 1800]

Writes <out_dir>/<Id>.<ext> (whatever SaveAudio produced, usually flac) and <Id>.md with the prompt, seed and date.
Exit 0 = every requested track written; 1 = a track failed; 2 = usage.
"""
import argparse
import json
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent


def graph(row: dict, prefix: str) -> dict:
    """ComfyUI's built-in ACE-Step 1.5 graph (the turbo all-in-one checkpoint, the stock template's settings)."""
    key = row.get("Key", "D Minor").replace("Minor", "minor").replace("Major", "major")
    return {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": row.get("Checkpoint", "ace_step_1.5_turbo_aio.safetensors")}},
        "2": {"class_type": "ModelSamplingAuraFlow", "inputs": {"model": ["1", 0], "shift": float(row.get("Shift", 3.0))}},
        "3": {"class_type": "TextEncodeAceStepAudio1.5", "inputs": {
            "clip": ["1", 1], "tags": row["Caption"], "lyrics": "[Instrumental]", "seed": int(row["Seed"]),
            "bpm": int(row["Bpm"]), "duration": float(row["Seconds"]), "timesignature": "4", "language": "en",
            "keyscale": key, "generate_audio_codes": True, "cfg_scale": 2.0, "temperature": 0.85, "top_p": 0.9,
            "top_k": 0, "min_p": 0.0}},
        "4": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["3", 0]}},
        "5": {"class_type": "EmptyAceStep1.5LatentAudio", "inputs": {"seconds": float(row["Seconds"]), "batch_size": 1}},
        "6": {"class_type": "KSampler", "inputs": {
            "model": ["2", 0], "seed": int(row["Seed"]), "steps": int(row.get("Steps", 8)), "cfg": float(row.get("Cfg", 1.0)),
            "sampler_name": "euler", "scheduler": "simple", "positive": ["3", 0], "negative": ["4", 0],
            "latent_image": ["5", 0], "denoise": 1.0}},
        "7": {"class_type": "VAEDecodeAudio", "inputs": {"samples": ["6", 0], "vae": ["1", 2]}},
        "8": {"class_type": "SaveAudio", "inputs": {"audio": ["7", 0], "filename_prefix": prefix}},
    }


def call(url: str, path: str, body: dict | None = None, timeout: int = 30) -> bytes:
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url + path, data=data, headers={"Content-Type": "application/json"} if data else {})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def render(url: str, row: dict, out: Path, timeout: int) -> bool:
    pid = json.loads(call(url, "/prompt", {"prompt": graph(row, f"dc_music/{row['Id']}")}))["prompt_id"]
    print(f"{row['Id']}: queued {pid}", flush=True)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        time.sleep(10)
        try:
            hist = json.loads(call(url, f"/history/{pid}")).get(pid)
        except Exception:
            continue
        if not hist:
            continue
        if (hist.get("status") or {}).get("status_str") == "error":
            print(f"{row['Id']}: failed {json.dumps(hist.get('status'))[:600]}", file=sys.stderr)
            return False
        files = [f for o in (hist.get("outputs") or {}).values() for f in o.get("audio", [])]
        if not files:
            continue
        f = files[0]
        q = urllib.parse.urlencode({"filename": f["filename"], "subfolder": f.get("subfolder", ""),
                                    "type": f.get("type", "output")})
        dest = out / (row["Id"] + Path(f["filename"]).suffix)
        dest.write_bytes(call(url, f"/view?{q}", timeout=300))
        (out / f"{row['Id']}.md").write_text(
            f"# {row['Id']}\n\ntool: ACE-Step 1.5 turbo (ComfyUI built-in nodes) at {url}\nseed: {row['Seed']}\n"
            f"bpm: {row['Bpm']}  key: {row.get('Key', '')}  seconds: {row['Seconds']}\n"
            f"date: {time.strftime('%Y-%m-%dT%H:%M:%S%z')}\n\n## Caption\n{row['Caption']}\n", encoding="utf-8")
        print(f"{row['Id']}: wrote {dest}", flush=True)
        return True
    print(f"{row['Id']}: timed out after {timeout}s", file=sys.stderr)
    return False


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("out_dir")
    p.add_argument("--only", default="")
    p.add_argument("--url", default="http://192.168.50.74:24002")
    p.add_argument("--timeout", type=int, default=1800)
    p.add_argument("--manifest", default=str(HERE / "tracks.json"))
    a = p.parse_args()
    rows = json.loads(Path(a.manifest).read_text(encoding="utf-8"))["tracks"]
    only = {s for s in a.only.split(",") if s}
    rows = [r for r in rows if not only or r["Id"] in only]
    if not rows:
        print("no tracks selected", file=sys.stderr)
        return 2
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    ok = True
    for row in rows:
        try:
            ok = render(a.url.rstrip("/"), row, out, a.timeout) and ok
        except Exception as e:  # one track failing never stops the rest
            print(f"{row['Id']}: error {e}", file=sys.stderr)
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
