#!/usr/bin/env bash
# Builds a stand-in for libsteam_api.so so Celeste starts without a Steam client.
#
# usage: make-steam-stub.sh <Steamworks.NET.dll> <output libsteam_api.so>
#
# The function list is read from the DLL itself (monodis), so it matches whatever
# Steamworks.NET version your Everest install uses. Needs: monodis (mono), gcc, awk.
#
# Behaviour of the stub (Steam never starts, achievements/cloud API calls do nothing):
#   SteamAPI_Init / InitSafe / IsSteamRunning ........ return 1
#   SteamInternal_SteamAPI_Init ...................... return 0 (= "OK")
#   interface getters (SteamAPI_Steam*_v###, SteamInternal_*, SteamAPI_GetHSteam*,
#   SteamAPI_ISteamClient_GetISteam*/CreateSteamPipe/ConnectToGlobalUser/CreateLocalUser)
#                                                      return a non-null pointer to zeroed memory
#   everything else (incl. SteamAPI_RestartAppIfNecessary) return 0
set -euo pipefail

DLL="${1:?usage: $0 <Steamworks.NET.dll> <output libsteam_api.so>}"
OUT="${2:?usage: $0 <Steamworks.NET.dll> <output libsteam_api.so>}"

command -v monodis >/dev/null || { echo "monodis not found (install mono)" >&2; exit 1; }
command -v gcc >/dev/null || { echo "gcc not found" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

monodis "$DLL" | grep -a pinvokeimpl | grep -oE 'as "[A-Za-z0-9_]+"' \
    | sed 's/as "//;s/"//' | sort -u > "$WORK/names.txt"

COUNT="$(wc -l < "$WORK/names.txt")"
if [[ "$COUNT" -lt 5 ]]; then
    echo "only $COUNT P/Invoke names found in $DLL - is it the right file?" >&2
    exit 1
fi

{
    echo 'static char fake[4096] __attribute__((aligned(256)));'
    awk '
      $1 == "SteamInternal_SteamAPI_Init" { print "long " $1 "(void) { return 0; }"; next }
      $1 ~ /^SteamAPI_(Init|InitSafe|IsSteamRunning)$/ { print "long " $1 "(void) { return 1; }"; next }
      $1 ~ /^SteamAPI_ISteamClient_(GetISteam[A-Za-z]*|CreateSteamPipe|ConnectToGlobalUser|CreateLocalUser)$/ ||
      $1 ~ /^SteamAPI_Steam[A-Za-z]*_v[0-9]+$/ || $1 ~ /^SteamInternal_/ || $1 ~ /^SteamAPI_GetHSteam/ {
        print "void* " $1 "(void) { return fake; }"; next }
      { print "long " $1 "(void) { return 0; }" }
    ' "$WORK/names.txt"
} > "$WORK/steam_api_stub.c"

gcc -shared -fPIC -O2 -Wl,-soname,libsteam_api.so -o "$OUT" "$WORK/steam_api_stub.c"
echo "built $OUT ($COUNT functions)"
