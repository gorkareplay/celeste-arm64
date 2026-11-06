#!/usr/bin/env bash
# Launcher for Celeste + Everest on aarch64 Linux.
#
#   FNA3D_FORCE_DRIVER   OpenGL (default) or Vulkan. Vulkan currently gives a black window
#                        on Asahi (Honeykrisp); see arm64/README.md.
#   DOTNET               path to the dotnet binary (default: "dotnet" from PATH)
#   CELESTE_FMOD_FADE_MS fade length for music events that FMOD 2 would never stop (1500)
#   CELESTE_FMOD_REAPER  set to 0 to turn the music fix off
#
# Extra command-line arguments are ignored on purpose, so this can be used as a Steam
# launch option ("/path/to/play.sh %command%") without Steam's own command leaking in.
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

export FNA3D_FORCE_DRIVER="${FNA3D_FORCE_DRIVER:-OpenGL}"

libs="$PWD/lib-fmod2"
[[ -d /run/opengl-driver/lib ]] && libs="$libs:/run/opengl-driver/lib"   # NixOS GPU drivers
export LD_LIBRARY_PATH="$libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec "${DOTNET:-dotnet}" Celeste.dll
