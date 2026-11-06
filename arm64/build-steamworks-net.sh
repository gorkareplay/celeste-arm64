#!/usr/bin/env bash
# Builds a Steamworks.NET.dll that loads on aarch64.
#
# The Steamworks.NET.dll shipped with the game is stamped x86-64-only, and the .NET runtime on
# aarch64 then fails with "Could not load file or assembly 'Steamworks.NET'" even though the
# file is right there. Building it as AnyCPU fixes that.
#
# usage: build-steamworks-net.sh <game dir> [--force] [tag]
#   tag    Steamworks.NET git tag; default: taken from <game dir>/Celeste.deps.json
#          (e.g. "Steamworks.NET/2024.8.0.0" -> tag 2024.8.0), else 2024.8.0
#   --force  rebuild even if the DLL in the game folder is already platform-neutral
#
# Needs: git, dotnet (SDK 8), network access to GitHub and NuGet.
set -euo pipefail

GAME=""; FORCE=0; TAG=""
for a in "$@"; do
    case "$a" in
        --force) FORCE=1 ;;
        *) if [[ -z "$GAME" ]]; then GAME="$a"; else TAG="$a"; fi ;;
    esac
done
[[ -n "$GAME" ]] || { echo "usage: $0 <game dir> [--force] [tag]" >&2; exit 1; }
GAME="$(readlink -f "$GAME")"
DLL="$GAME/Steamworks.NET.dll"

# optional header magic: 010b = PE32 (platform-neutral managed DLLs), 020b = PE32+ (64-bit only)
pe_magic() {
    local lfanew
    lfanew="$(od -An -t u4 -j 60 -N 4 "$1" | tr -d ' ')"
    od -An -t x2 -j $((lfanew + 24)) -N 2 "$1" | tr -d ' \n'
}

if [[ -f "$DLL" && "$FORCE" -eq 0 ]]; then
    if [[ "$(pe_magic "$DLL")" == "010b" ]]; then
        echo "Steamworks.NET.dll is already platform-neutral, nothing to do (use --force to rebuild)"
        exit 0
    fi
fi

command -v git >/dev/null    || { echo "git not found" >&2; exit 1; }
command -v dotnet >/dev/null || { echo "dotnet not found (install the .NET 8 SDK)" >&2; exit 1; }

if [[ -z "$TAG" && -f "$GAME/Celeste.deps.json" ]]; then
    ver="$(grep -o '"Steamworks.NET/[0-9][0-9.]*"' "$GAME/Celeste.deps.json" | head -1 | sed 's|.*/||;s|"||')"
    # 2024.8.0.0 -> 2024.8.0
    [[ "$ver" =~ ^([0-9]+\.[0-9]+\.[0-9]+)\.0$ ]] && TAG="${BASH_REMATCH[1]}"
fi
TAG="${TAG:-2024.8.0}"
echo "-- building Steamworks.NET $TAG"

SRC="${XDG_CACHE_HOME:-$HOME/.cache}/celeste-arm64/Steamworks.NET-$TAG"
if [[ ! -d "$SRC/.git" ]]; then
    mkdir -p "$(dirname "$SRC")"
    git clone --branch "$TAG" --depth 1 https://github.com/rlabrecque/Steamworks.NET.git "$SRC"
fi

cd "$SRC"
dotnet build Standalone/Steamworks.NET.Standard.csproj \
    -c OSX-Linux \
    -p:Platform=x64 \
    -p:PlatformTarget=AnyCPU

BUILT="Standalone/bin/x64/OSX-Linux/Steamworks.NET.dll"
if [[ ! -f "$BUILT" ]]; then
    BUILT="$(find Standalone -name Steamworks.NET.dll -path '*bin*' -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2-)"
fi
[[ -n "$BUILT" && -f "$BUILT" ]] || { echo "build finished but no Steamworks.NET.dll was found" >&2; exit 1; }

if [[ "$(pe_magic "$BUILT")" != "010b" ]]; then
    echo "warning: $BUILT is still 64-bit-only (PE32+); it may not load on aarch64" >&2
fi

if [[ -f "$DLL" && ! -f "$DLL.x64" ]]; then
    cp -a "$DLL" "$DLL.x64"
    echo "-- kept the original as Steamworks.NET.dll.x64"
fi
cp "$BUILT" "$DLL"
echo "installed $DLL (from $BUILT)"
