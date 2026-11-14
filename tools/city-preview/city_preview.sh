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
"$UE_ROOT/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$PROJECT" -run=DCCityPreview -seed="$SEED" -sectors="$SECTORS" -out="$OUTW" -unattended -nullrhi -nosplash > "$OUT/plan.log" 2>&1
for ((S = 0; S < SECTORS; ++S)); do
  timeout 300 "$UE_ROOT/Engine/Binaries/Win64/UnrealEditor.exe" "$PROJECT" -game -windowed -resx=1600 -resy=900 \
    -dcdemo -dcseed="$SEED" -dcstartsector="$S" -dccityshot="$OUTW" -unattended -nosplash -nosound > "$OUT/render_sector$S.log" 2>&1 || true
done
ls "$OUT"/*.png
