#!/usr/bin/env bash
# R26 proof on a packaged build, two instances on one PC (every process under timeout, cleanup scoped to this run).
#   eos_coop_proof.sh online   <package dir> <out dir> <local EOS.ini>
#       A hosts over EOS (HOST ONLINE pressed by -dceoshost), writes its join code; B logs in as a fresh device user
#       (-dceosfreshdevice: device ids are per Windows user), joins by that code and plays a sector beside A's pilot.
#   eos_coop_proof.sh fallback <package dir> <out dir>
#       No credentials file: A presses HOST ONLINE, falls back to a LAN host with the message; B joins by IP.
# Prints the DCEOS lines, the voxel op hash matches on B and a secret scan of both logs (counts only, never values).
set -uo pipefail
mode="${1:?mode}"; PKG="${2:?package dir}"; D="${3:?out dir}"; INI="${4:-}"
X="$PKG/Windows/Epic_AI_Mega_Jam2.exe"
PROJ="$PKG/Windows/Epic_AI_Mega_Jam2"
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$D/client"; rm -f "$D/code.txt"
MARK="$(cd "$D" && (pwd -W 2>/dev/null || pwd))"
kill_run() {
  GK_MARK="$MARK" powershell -NoProfile -Command '$m = $env:GK_MARK.Replace([char]92, [char]47).ToLower(); Get-CimInstance Win32_Process -Filter "Name like ''Epic_AI_Mega_Jam2%''" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace([char]92, [char]47).ToLower().Contains($m) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }' >/dev/null 2>&1
}
common=(-windowed -nosound -unattended -dcenemytrace)
if [ "$mode" = online ]; then
  bash "$here/stage_eos_ini.sh" "${INI:?local EOS.ini}" "$PROJ"
  host_extra=(-dceoscodeout="$MARK/code.txt")
  # B runs from its own copy: its own Saved/ (EOS cache, logs) beside A's.
  rm -rf "$D/pkgB"; cp -r "$PKG" "$D/pkgB"
  bash "$here/stage_eos_ini.sh" "$INI" "$D/pkgB/Windows/Epic_AI_Mega_Jam2"
  XB="$D/pkgB/Windows/Epic_AI_Mega_Jam2.exe"
else
  bash "$here/stage_eos_ini.sh" --remove "$PROJ"
  host_extra=(-dceoscreds="$MARK/no-such-EOS.ini")
fi
timeout -k 5 260 "$X" L_City -ResX=1280 -ResY=720 -WinX=0 -WinY=0 "${common[@]}" -dcdemo -dcwaitplayers=2 -dcseed=5151 \
  -dcsectorseconds=60 -dceoshost -dceosshot="$MARK/host_lobby.png" "${host_extra[@]}" -abslog="$MARK/host.log" &
H=$!
if [ "$mode" = online ]; then
  for _ in $(seq 1 90); do [ -s "$D/code.txt" ] && break; sleep 1; done
  [ -s "$D/code.txt" ] || { echo "eos-proof: host never published a code"; grep -h "DCEOS" "$D/host.log"; kill_run; exit 1; }
  echo "eos-proof: host code $(cat "$D/code.txt")"
  join=(L_City -dceosjoin=@"$MARK/code.txt" -dceosfreshdevice)
else
  for _ in $(seq 1 60); do grep -q "DCEOS fallback" "$D/host.log" 2>/dev/null && break; sleep 1; done
  sleep 12 # the LAN listen map reopens
  join=(127.0.0.1:7777)
  XB="$X"
fi
timeout -k 5 170 "$XB" "${join[@]}" -ResX=960 -ResY=540 -WinX=900 -WinY=500 "${common[@]}" -dcshots="$MARK/client" -dcshotevery=5 \
  -abslog="$MARK/client.log" &
C=$!
wait $C
kill $H 2>/dev/null; kill_run; wait 2>/dev/null
[ "$mode" = online ] && { bash "$here/stage_eos_ini.sh" --remove "$PROJ" >/dev/null; rm -rf "$D/pkgB"; }
echo "--- host"; grep -h "DCEOS\|LogSocketSubsystemEOS.*\(Warning\|Error\)" "$D/host.log" | head -30
echo "--- client"; grep -h "DCEOS\|LogNet.*\(Welcomed\|Join succeeded\|NetworkFailure\)\|LogSocketSubsystemEOS.*\(Warning\|Error\)" "$D/client.log" | head -30
echo "client ops: $(grep -c 'DCVOXEL op .* match=host' "$D/client.log") match=host, $(grep -c 'MISMATCH=host' "$D/client.log") MISMATCH"
echo "client driver: $(grep -ho 'NetDriverEOS\|IpNetDriver' "$D/client.log" | sort | uniq -c | tr '\n' ' ')"
if [ -n "$INI" ] && [ -f "$INI" ]; then
  for key in ProductId SandboxId DeploymentId ClientId ClientSecret; do
    v="$(grep -E "^[[:space:]]*$key[[:space:]]*=" "$INI" | head -1 | sed -E 's/^[^=]*=[[:space:]]*//' | tr -d '[:cntrl:]')"
    [ -n "$v" ] && echo "secret scan $key: $(cat "$D"/*.log | grep -cF "$v") log lines"
  done
fi
