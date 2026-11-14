#!/usr/bin/env bash
# Everest starts the game by running the file "Celeste" in the game folder (for example when it
# restarts itself to set LD_LIBRARY_PATH: "Restarting with LD_LIBRARY_PATH=..."). Everest's
# download ships that file as an x86-64 program, which fails here with "Exec format error".
# This replaces it with a small script that starts the game through dotnet instead.
#
# usage: fix-launcher.sh <game dir>
set -euo pipefail

GAME="${1:?usage: $0 <game dir>}"
F="$GAME/Celeste"

if [[ -f "$F" ]]; then
    # ELF header: e_machine at byte offset 18 (little endian): 3e00 = x86-64, b700 = aarch64
    machine="$(od -An -t x1 -j 18 -N 2 "$F" | tr -d ' \n')"
    if [[ "$(head -c 4 "$F")" == $'\x7f'ELF ]]; then
        if [[ "$machine" == "3e00" ]]; then
            mv "$F" "$F.x86bak"
        else
            echo "Celeste is a native program for this machine (machine=$machine), leaving it alone"
            exit 0
        fi
    elif head -c 2 "$F" | grep -q '^#!'; then
        if grep -q 'dotnet' "$F"; then
            echo "Celeste launcher already in place"; exit 0
        fi
        mv "$F" "$F.bak"          # some other script; keep it
    fi
fi

# printf, not a heredoc: an interactive zsh can swallow the '!' of a shebang line
printf '%s\n' \
    '#!/usr/bin/env bash' \
    '# written by arm64/fix-launcher.sh: start the game through dotnet (the shipped binary is x86-64)' \
    'cd "$(dirname "$(readlink -f "$0")")"' \
    'exec "${DOTNET:-dotnet}" Celeste.dll "$@"' > "$F"
chmod +x "$F"
echo "installed launcher $F (original kept as .x86bak or .bak)"
