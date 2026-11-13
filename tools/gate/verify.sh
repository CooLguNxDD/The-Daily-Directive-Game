#!/usr/bin/env bash
# Unreal gate template for a Cat Fleet run (copy to <repo>/tools/gate/verify.sh, set verify_cmd to
# `bash tools/gate/verify.sh`). Fast tier checked on UE 5.9 (Epic_AI_Mega_Jam2, 2026-11-13: compile 99 s cold,
# tests 24 s); the full tier (cook, package) has not run yet.
#
# Tiers (FLEET_GATE_TIER, set by the fleet; default fast):
#   fast  A: editor target compiles          B: automation tests under -nullrhi
#   full  A + B, then                        C: cook (or package with UNREAL_GATE_PACKAGE=1)
# The lead runs the fast tier itself before a CHECKPOINT; the loop runs full at checkpoints, in the GATE worktree.
#
# Needs: UE_ROOT (engine root holding Engine/), one *.uproject in the repo root.
# Optional: UNREAL_TEST_FILTER (default "<Project>."), UNREAL_PLATFORM (Win64), UNREAL_CONFIG (Development),
#           UNREAL_GATE_PACKAGE=1, UNREAL_GATE_TIMEOUT_S per stage (default 3600).
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$(pwd -W 2>/dev/null || pwd)"
tier="${FLEET_GATE_TIER:-fast}"
: "${UE_ROOT:?set UE_ROOT to the engine root (the folder that holds Engine/)}"
uproject="$(ls "$ROOT"/*.uproject 2>/dev/null | head -1)"
[[ -n "$uproject" ]] || { echo "gate: no .uproject in $ROOT"; exit 2; }
project="$(basename "$uproject" .uproject)"
platform="${UNREAL_PLATFORM:-Win64}"
config="${UNREAL_CONFIG:-Development}"
filter="${UNREAL_TEST_FILTER:-$project.}"
limit="${UNREAL_GATE_TIMEOUT_S:-3600}"
out="$ROOT/Saved/Gate"; mkdir -p "$out"

stage() {  # stage <name> <log> <command...>: one line per stage, the failing stage named on exit
  local name="$1" log="$2"; shift 2
  echo "gate: $name"
  local t0=$SECONDS
  if timeout "$limit" "$@" >"$log" 2>&1; then echo "gate: $name PASS ($((SECONDS - t0)) s)"; return 0; fi
  echo "gate: $name FAIL ($((SECONDS - t0)) s), log $log"
  grep -E "error [A-Z]+[0-9]+|Error:|LogAutomationController: Error|Test Completed. Result=\{Fail" "$log" | head -20
  return 1
}

# A: compile the editor target (what the tests and the cook load)
stage "tier A: compile ${project}Editor" "$out/build.log" \
  "$UE_ROOT/Engine/Build/BatchFiles/Build.bat" "${project}Editor" "$platform" "$config" \
  -project="$uproject" -waitmutex -NoHotReload -NoHotReloadFromIDE || exit 1

# B: automation tests, no window, no GPU. The process exit code is not proof: read the report.
rm -rf "$out/report"
stage "tier B: automation tests ($filter)" "$out/tests.log" \
  "$UE_ROOT/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$uproject" \
  -ExecCmds="Automation RunTests $filter; Quit" -ReportExportPath="$out/report" \
  -unattended -nullrhi -nosplash -nopause -nosound -stdout -log || exit 1
if [[ -f "$out/report/index.json" ]]; then
  read -r ok failed < <(python - "$out/report/index.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1], encoding="utf-8-sig"))
print(d.get("succeeded", 0) + d.get("succeededWithWarnings", 0), d.get("failed", 0) + d.get("notRun", 0))
PY
  )
  ok="${ok//[!0-9]/}"; failed="${failed//[!0-9]/}"  # Windows python prints CRLF: "0
" is not a number to (( ))
  echo "gate: tests passed=$ok failed=$failed"
  (( ok > 0 && failed == 0 )) || { echo "gate: tier B FAIL (no test ran, or one failed)"; exit 1; }
else
  echo "gate: tier B FAIL (no report at $out/report/index.json: the tests did not run)"; exit 1
fi

[[ "$tier" == full ]] || { echo "gate: tier C skipped (fast tier)"; exit 0; }

# C: cook, or a full package when asked. Cook errors and missing references only show here.
if [[ "${UNREAL_GATE_PACKAGE:-0}" == 1 ]]; then
  stage "tier C: package $platform $config" "$out/package.log" \
    "$UE_ROOT/Engine/Build/BatchFiles/RunUAT.bat" BuildCookRun -project="$uproject" -noP4 -utf8output \
    -platform="$platform" -clientconfig="$config" -build -cook -stage -pak -archive \
    -archivedirectory="$out/package" -unattended -nullrhi || exit 1
else
  stage "tier C: cook $platform" "$out/cook.log" \
    "$UE_ROOT/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$uproject" -run=cook \
    -targetplatform="$([[ "$platform" == Win64 ]] && echo Windows || echo "$platform")" \
    -unattended -nullrhi -nosplash -stdout || exit 1
fi
echo "gate: all tiers PASS"
