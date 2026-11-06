#!/usr/bin/env bash
# Everest ships its loading-splash helper as an x86-64 binary, which cannot run on aarch64
# ("Exec format error" in the Everest log). This swaps it for a harmless stand-in.
# Everest then logs "Could not connect to splash", which is expected and safe to ignore.
#
# usage: fix-splash.sh <game dir>
set -euo pipefail

GAME="${1:?usage: $0 <game dir>}"
F="$GAME/EverestSplash/EverestSplash-linux"

[[ -f "$F" ]] || { echo "no $F (has Everest been installed?)"; exit 0; }

# ELF header: e_machine at byte offset 18; 0x3e00 (little endian) = x86-64
machine="$(od -An -t x1 -j 18 -N 2 "$F" | tr -d ' \n')"
if [[ "$machine" == "3e00" ]]; then
    mv "$F" "$F.x86bak"
elif [[ -x "$F" ]] && head -c 2 "$F" | grep -q '^#!'; then
    echo "splash helper already replaced"; exit 0
else
    echo "splash helper is not an x86-64 ELF (machine=$machine), leaving it alone"; exit 0
fi

# printf, not a heredoc: an interactive zsh can swallow the '!' of a shebang line
printf '%s\n' '#!/usr/bin/env bash' 'exec sleep 86400' > "$F"
chmod +x "$F"
echo "replaced $F (original kept as .x86bak)"
