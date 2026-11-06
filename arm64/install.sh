#!/usr/bin/env bash
# aarch64-linux setup steps that patch.sh does not cover.
#
# Run after:  1) ./patch.sh (builds the libraries and the FMOD shim, incl. fmod/reaper.c)
#             2) Everest's first launch (so Celeste.dll and EverestSplash/ exist)
#
# usage: arm64/install.sh <game dir>
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="${1:?usage: $0 <game dir>}"
GAME="$(readlink -f "$GAME")"

die() { echo "error: $*" >&2; exit 1; }
note() { echo "-- $*"; }

[[ -f "$GAME/Celeste.dll" ]] || die "no Celeste.dll in $GAME (launch the game once so Everest installs itself)"
[[ -d "$GAME/lib-fmod2" ]]  || die "no lib-fmod2 in $GAME (run patch.sh first)"
case "$(uname -m)" in aarch64|arm64) ;; *) echo "warning: this is $(uname -m), not aarch64" >&2 ;; esac

# 0. Steamworks.NET.dll that loads on aarch64 -------------------------------------
# (skipped by the script itself when the DLL in the game folder is already platform-neutral)
bash "$HERE/build-steamworks-net.sh" "$GAME" \
    || echo "warning: could not build Steamworks.NET.dll; the game may fail with 'Could not load file or assembly Steamworks.NET'" >&2

# 1. Steam stand-in ---------------------------------------------------------
if [[ -f "$GAME/lib-fmod2/libsteam_api.so" ]]; then
    note "libsteam_api.so already present, skipping"
elif [[ -f "$GAME/Steamworks.NET.dll" ]]; then
    bash "$HERE/make-steam-stub.sh" "$GAME/Steamworks.NET.dll" "$GAME/lib-fmod2/libsteam_api.so" \
        || echo "warning: Steam stub failed; Celeste will stop at 'steam_api' not found" >&2
else
    echo "warning: no Steamworks.NET.dll in $GAME, skipping the Steam stub" >&2
fi

# 2. Everest splash helper ---------------------------------------------------
bash "$HERE/fix-splash.sh" "$GAME"

# 3. Lua (Everest needs liblua54.so; patch.sh only installs it through apt/dnf) -
if [[ -e "$GAME/lib-fmod2/liblua54.so" ]]; then
    note "liblua54.so already present, skipping"
elif command -v nix-build >/dev/null; then
    LUA="$(nix-build '<nixpkgs>' -A lua5_4 --no-out-link)/lib"
    cp -L "$LUA/liblua.so" "$GAME/lib-fmod2/liblua54.so"   # a copy, so garbage collection can't break it
    note "installed liblua54.so from nixpkgs"
elif lib="$(ldconfig -p 2>/dev/null | awk '/liblua5\.4\.so /{print $NF; exit}')" && [[ -n "$lib" ]]; then
    ln -sf "$lib" "$GAME/lib-fmod2/liblua54.so"
    note "linked liblua54.so -> $lib"
else
    echo "warning: no Lua 5.4 found; install liblua5.4 and link it as lib-fmod2/liblua54.so" >&2
fi

# 4. SDL2 with working window/Vulkan backends (NixOS) ---------------------------
# The SDL2 that patch.sh builds can fail to load Vulkan/X11/Wayland on NixOS because the
# libraries it opens at run time are not on any default path. nixpkgs' SDL2 carries them.
if command -v nix-build >/dev/null && [[ -e "$GAME/lib-fmod2/libSDL2.so" && ! -e "$GAME/lib-fmod2/libSDL2.so.fromsource" ]]; then
    SDL="$(nix-build '<nixpkgs>' -A SDL2 --no-out-link)/lib"
    mv "$GAME/lib-fmod2/libSDL2.so" "$GAME/lib-fmod2/libSDL2.so.fromsource"
    cp -L "$SDL/libSDL2-2.0.so.0" "$GAME/lib-fmod2/libSDL2-2.0.so.0"
    ln -sf libSDL2-2.0.so.0 "$GAME/lib-fmod2/libSDL2.so"
    note "replaced the from-source SDL2 with nixpkgs' (original kept as libSDL2.so.fromsource)"
fi

# 5. Launcher ------------------------------------------------------------------
if [[ ! -e "$GAME/play.sh" ]]; then
    cp "$HERE/play.sh" "$GAME/play.sh"
    note "installed $GAME/play.sh"
elif cmp -s "$HERE/play.sh" "$GAME/play.sh"; then
    note "play.sh is up to date"
else
    cp "$HERE/play.sh" "$GAME/play.sh.new"   # keep any edits made to the installed launcher
    note "play.sh exists and differs; the current version is in play.sh.new (not overwritten)"
fi
chmod +x "$GAME/play.sh"

echo
echo "done. Start the game with: $GAME/play.sh"
