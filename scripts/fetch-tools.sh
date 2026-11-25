#!/bin/sh
# Populate tools/ with the Metrowerks CodeWarrior 4.2 compiler/linker and
# elf2rel, taken from SyriingePluginTemplate at a pinned commit. These are
# third-party binaries and are never committed to this repository.
#
# Usage: scripts/fetch-tools.sh [--from DIR] [--dest DIR]
#   --from DIR   copy from an existing SyriingePluginTemplate checkout instead
#                of cloning it (must contain the same files as the pinned commit)
#   --dest DIR   where to put the tools (default: tools)
#
# Every file is checked against a SHA-256 recorded below, so a mismatched
# checkout or a corrupt download is rejected. Re-running is a no-op once the
# tools are in place.

set -eu

UPSTREAM_URL=https://github.com/Sammi-Husky/SyriingePluginTemplate
UPSTREAM_COMMIT=2b3ba22c5be9a29d2f7d33f98f1fd31c74f7df24

# sha256  path (relative to the template's tools/ folder)
CHECKSUMS='
e3d4cd15ff05208055597475e0a2015d775e293e4a551938b8ee61b876d31b42  elf2rel.exe
9fe9d8939383ffec02752b86f7b18b8005426bac021c9066e64a5c220ed863b0  MWCC4_2/lmgr326b.dll
9fe9d8939383ffec02752b86f7b18b8005426bac021c9066e64a5c220ed863b0  MWCC4_2/lmgr8c.dll
e6913e71b836b12c59c7823688ac568c05ef8c5b63cfe3ae3c19582f395ae8aa  MWCC4_2/mwasmeppc.exe
edc130f6843d61f1374d071c820bcc5bf20acb7cc50733bd95bb189636214e54  MWCC4_2/mwcceppc.exe
4abefbf70c469e721de57624e413cd299a9e1bc10b8954021ba3963274aa14ac  MWCC4_2/mwldeppc.exe
'

die() { echo "fetch-tools: $*" >&2; exit 1; }

from=
dest=tools
while [ $# -gt 0 ]; do
    case "$1" in
        --from) [ $# -ge 2 ] || die "--from needs a directory"; from=$2; shift 2 ;;
        --dest) [ $# -ge 2 ] || die "--dest needs a directory"; dest=$2; shift 2 ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
done

command -v sha256sum >/dev/null 2>&1 || die "sha256sum not found"

# verify DIR: succeeds if every listed file under DIR has the expected hash.
verify() {
    ( cd "$1" 2>/dev/null && printf '%s' "$CHECKSUMS" | sed '/^$/d' \
        | sha256sum -c --status - ) 2>/dev/null
}

if verify "$dest"; then
    echo "fetch-tools: $dest is up to date (SyriingePluginTemplate @ $(printf %.7s "$UPSTREAM_COMMIT"))"
    exit 0
fi

tmp=
cleanup() { if [ -n "$tmp" ]; then rm -rf "$tmp"; fi; }
trap cleanup EXIT INT TERM

if [ -n "$from" ]; then
    src=$from/tools
    [ -d "$src" ] || die "$src does not exist"
    echo "fetch-tools: copying from $src"
else
    command -v git >/dev/null 2>&1 || die "git not found"
    tmp=$(mktemp -d)
    echo "fetch-tools: fetching $UPSTREAM_URL @ $UPSTREAM_COMMIT"
    git init -q "$tmp/template"
    git -C "$tmp/template" fetch -q --depth 1 "$UPSTREAM_URL" "$UPSTREAM_COMMIT"
    git -C "$tmp/template" -c advice.detachedHead=false checkout -q FETCH_HEAD
    src=$tmp/template/tools
fi

verify "$src" || die "files in $src do not match the pinned commit $UPSTREAM_COMMIT"

mkdir -p "$dest/MWCC4_2"
printf '%s' "$CHECKSUMS" | sed '/^$/d' | while read -r _ path; do
    cp "$src/$path" "$dest/$path"
done

verify "$dest" || die "verification of $dest failed after copying"
echo "fetch-tools: installed into $dest"
