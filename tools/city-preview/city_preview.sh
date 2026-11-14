#!/usr/bin/env bash
# City builder preview (R16): for one run seed, writes the plan of every sector (DCCityPreview commandlet: street grid,
# lots, landmark, plazas, rubble zones, sight lines) and three rendered aerial frames of every built sector (-dccityshot).
# Usage: UE_ROOT=L:/unreal/UE_5.9 bash tools/city-preview/city_preview.sh <seed> <out dir> [sectors=3]
set -euo pipefail
SEED="${1:?seed}"; OUT="${2:?out dir}"; SECTORS="${3:-3}"
UE_ROOT="${UE_ROOT:-L:/unreal/UE_5.9}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd -W 2>/dev/null || pwd)"
PROJECT="$ROOT/Epic_AI_Mega_Jam2.uproject"
mkdir -p "$OUT"
OUTW="$(cd "$OUT" && (pwd -W 2>/dev/null || pwd))"
# One commandlet writes both: the plans, then (-render) it starts the game per sector for the rendered aerials.
"$UE_ROOT/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$PROJECT" -run=DCCityPreview -seed="$SEED" -sectors="$SECTORS" -out="$OUTW" -render -unattended -nullrhi -nosplash > "$OUT/plan.log" 2>&1
ls "$OUT"/*.png
