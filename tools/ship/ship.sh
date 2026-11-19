#!/usr/bin/env bash
# Ship: build the Windows package people download, from the commit this script is checked out at.
# Run by the fleet (`fleet.py ship <run>`, config ship_cmd) in a snapshot worktree, holding the host's engine slot,
# or by hand from any checkout. Everything it makes goes under $SHIP_OUT:
#   package/Windows/...            the packaged game (credentials staged when the run has them)
#   <name>-Windows-<sha>.zip       the upload, debug symbols left out, under 1 GB
#   SHIP.txt                       commit, time, sizes
#
# Inputs (environment):
#   UE_ROOT       engine root (the folder holding Engine/); needed unless SHIP_PACKAGE is set
#   SHIP_OUT      output folder                               default Saved/Ship
#   SHIP_NAME     label in the zip name                       default the .uproject name
#   SHIP_SHA      commit being shipped                        default HEAD
#   SHIP_RUN      fleet run folder; <SHIP_RUN>/lead/EOS.local.ini is staged into the package when it exists
#   SHIP_EOS_INI  credentials file to stage instead (never printed, never committed)
#   SHIP_PACKAGE  an already built package dir (holding Windows/): skip the build, only stage and zip
set -euo pipefail
unset MSYS_NO_PATHCONV MSYS2_ARG_CONV_EXCL
here="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$here"

uproject="$(ls *.uproject | head -1)"
project="${uproject%.uproject}"
sha="${SHIP_SHA:-$(git rev-parse HEAD)}"
short="${sha:0:7}"
out="${SHIP_OUT:-$here/Saved/Ship}"
name="${SHIP_NAME:-$project}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
echo "ship: $project at $short -> $out"

if [ -n "${SHIP_PACKAGE:-}" ]; then
  built="$SHIP_PACKAGE"
  echo "ship: using the built package $built"
else
  git lfs pull
  FLEET_GATE_TIER=full UNREAL_GATE_PACKAGE=1 bash tools/gate/verify.sh
  built="$here/Saved/Gate/package"
fi
[ -f "$built/Windows/$project.exe" ] || { echo "ship: no $project.exe under $built/Windows" >&2; exit 2; }

rm -rf "$out/package"
mkdir -p "$out/package"
cp -r "$built/." "$out/package/"

ini="${SHIP_EOS_INI:-${SHIP_RUN:+$SHIP_RUN/lead/EOS.local.ini}}"
if [ -n "$ini" ] && [ -f "$ini" ]; then
  bash tools/eos/stage_eos_ini.sh "$ini" "$out/package/Windows/$project"
  eos="staged"
else
  eos="not staged (the game falls back to LAN/IP)"
fi
echo "ship: online credentials $eos"

zip="$out/$name-Windows-$short.zip"
rm -f "$zip"
python tools/package/make_zip.py "$out/package" "$zip"

{
  echo "project: $project"
  echo "commit: $sha"
  echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "online credentials: $eos"
  echo "zip: $(basename "$zip") $(du -m "$zip" | cut -f1) MB"
} > "$out/SHIP.txt"
cat "$out/SHIP.txt"
