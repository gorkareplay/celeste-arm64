#!/usr/bin/env bash
# Updates Everest on aarch64 Linux.
#
# The in-game updater (and Everest's own Linux installers) run MiniInstaller-linux, which is an
# x86-64 binary and cannot run here. This script does the same job with the portable
# MiniInstaller.dll through dotnet, then re-applies this fork's aarch64 fixes (an update brings
# back the x86-64 splash helper, for example).
#
# usage: update-everest.sh [-b stable|beta|dev] [-v <build id>] [-l] [--finish] [--no-post] <game dir>
#   -b BRANCH   which branch to install (default: stable)
#   -v ID       install one specific build (the id from -l)
#   -l          list the newest build of each branch and exit (needs no game dir)
#   --finish    do not download anything: run the installer on the files already in the game
#               folder. Use this after the in-game updater downloaded an update but failed
#               to run MiniInstaller-linux.
#   --no-post   skip re-applying the aarch64 fixes (install.sh) afterwards
#
# Needs: curl, jq, unzip, rsync, dotnet. Builds come from Everest's Azure DevOps pipeline,
# the same source other Linux installers use; the API details are not under this fork's control.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
API='https://dev.azure.com/EverestAPI/Everest/_apis/build/builds'
API_VERSION='7.1'

BRANCH=stable; VERSION=""; LIST=0; FINISH=0; POST=1; GAME=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -b) BRANCH="${2:?-b needs a branch}"; shift 2 ;;
        -v) VERSION="${2:?-v needs a build id}"; shift 2 ;;
        -l) LIST=1; shift ;;
        --finish) FINISH=1; shift ;;
        --no-post) POST=0; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        -*) echo "unknown option: $1" >&2; exit 1 ;;
        *) GAME="$1"; shift ;;
    esac
done
case "$BRANCH" in stable|beta|dev) ;; *) echo "unknown branch: $BRANCH (stable, beta or dev)" >&2; exit 1 ;; esac

die() { echo "error: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "$1 not found"; }

builds() {
    # id, branch, build number, finish time of every finished, successful build
    curl -fsSL "${API}?api-version=${API_VERSION}" | jq -r '.value[]
        | select((.status == "completed" and .result == "succeeded")
                 and (.reason == "manual" or .reason == "individualCI"))
        | [(.id | tostring), (.sourceBranch | sub("refs/heads/"; "")), (.buildNumber // "?"), (.finishTime // "?")]
        | @tsv'
}

if [[ "$LIST" -eq 1 ]]; then
    need curl; need jq
    all="$(builds)"
    printf '%-8s %-8s %-14s %s\n' BRANCH ID BUILD FINISHED
    for b in stable beta dev; do
        line="$(awk -F'\t' -v b="$b" '$2 == b {print; exit}' <<<"$all")"
        [[ -n "$line" ]] && awk -F'\t' '{printf "%-8s %-8s %-14s %s\n", $2, $1, $3, $4}' <<<"$line"
    done
    exit 0
fi

[[ -n "$GAME" ]] || die "usage: $0 [-b stable|beta|dev] [-v id] [-l] [--finish] <game dir>"
GAME="$(readlink -f "$GAME")"
[[ -f "$GAME/Celeste.dll" || -f "$GAME/MiniInstaller.dll" ]] || die "$GAME does not look like an Everest install"
need "${DOTNET:-dotnet}"

if [[ "$FINISH" -eq 0 ]]; then
    need curl; need jq; need unzip; need rsync

    if [[ -z "$VERSION" ]]; then
        VERSION="$(builds | awk -F'\t' -v b="$BRANCH" '$2 == b {print $1; exit}')"
        [[ -n "$VERSION" ]] || die "no finished build found for branch '$BRANCH'"
    fi

    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT

    echo "-- downloading Everest build $VERSION ($BRANCH)"
    curl -fsSL -o "$TMP/everest.zip" \
        "${API}/${VERSION}/artifacts?artifactName=main&api-version=${API_VERSION}&%24format=zip"
    unzip -q -d "$TMP/everest" "$TMP/everest.zip"
    [[ -d "$TMP/everest/main" ]] || die "the download has no 'main' folder"

    rsync -a "$TMP/everest/main/" "$GAME/"
    echo "-- files copied into $GAME"
fi

[[ -f "$GAME/MiniInstaller.dll" ]] || die "no MiniInstaller.dll in $GAME"

# The package brings x86-64 native libraries back; remove them as the upstream launcher does.
rm -rf "$GAME/lib64-linux"
rm -f "$GAME"/*.so*

echo "-- running the installer (MiniInstaller.dll)"
( cd "$GAME" && "${DOTNET:-dotnet}" MiniInstaller.dll ) \
    || die "the installer failed; see $GAME/miniinstaller-log.txt (re-running it is safe)"

if [[ "$POST" -eq 1 ]]; then
    echo "-- re-applying the aarch64 fixes"
    bash "$HERE/install.sh" "$GAME"
fi

echo "done."
