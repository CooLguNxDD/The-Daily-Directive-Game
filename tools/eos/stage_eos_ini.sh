#!/usr/bin/env bash
# R26: put the human's local EOS credentials where the game reads them, without ever printing a value.
#   stage_eos_ini.sh <local EOS.ini> [<project or package dir>...]   copy to <dir>/Config/Custom/EOS.ini
#   stage_eos_ini.sh --remove [<dir>...]                              delete it again (fallback proof)
# A <dir> is the project root (editor / dev runs) or a packaged game's project folder
# (<pkg>/Windows/Epic_AI_Mega_Jam2). Default <dir>: the project root. Config/Custom/ is gitignored and UAT never
# stages Config/Custom/*.ini into a pak, so the file only exists where this script put it.
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "${1:-}" = "--remove" ]; then
  shift
  for dir in "${@:-$here}"; do rm -f "$dir/Config/Custom/EOS.ini"; echo "stage_eos_ini: removed $dir/Config/Custom/EOS.ini"; done
  exit 0
fi
src="${1:?usage: stage_eos_ini.sh <local EOS.ini> [dir...]}"; shift || true
[ -f "$src" ] || { echo "stage_eos_ini: $src not found" >&2; exit 2; }
# Report which keys are filled (names and lengths only, never values).
for key in ProductId SandboxId DeploymentId ClientId ClientSecret; do
  len=$(grep -E "^[[:space:]]*$key[[:space:]]*=" "$src" | head -1 | sed -E 's/^[^=]*=[[:space:]]*//' | tr -d '\r' | awk '{print length($0)}')
  [ -n "${len:-}" ] && [ "$len" -gt 0 ] || { echo "stage_eos_ini: $key is empty in $src (the game will fall back to LAN/IP)" >&2; }
done
for dir in "${@:-$here}"; do
  mkdir -p "$dir/Config/Custom"
  cp "$src" "$dir/Config/Custom/EOS.ini"
  echo "stage_eos_ini: staged to $dir/Config/Custom/EOS.ini"
done
