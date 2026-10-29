#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
# Install exactly one Openbox theme through the shared bounded tar reader.
set -eu
export LC_ALL=C

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 theme.obt" >&2
    exit 2
fi
archive=$1
case "$archive" in *.obt|*.tar.gz|*.tgz) ;; *) echo "$archive: unsupported theme archive" >&2; exit 1 ;; esac
if [ ! -f "$archive" ] || [ -L "$archive" ]; then
    echo "$archive: not a regular theme archive" >&2
    exit 1
fi

helper_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
extractor=${OBCONF_THEME_EXTRACTOR:-$helper_dir/obconf-theme-extract}
publisher=${OBCONF_THEME_PUBLISHER:-$helper_dir/obconf-theme-publish}
for helper in "$extractor" "$publisher"; do
    if [ ! -x "$helper" ]; then echo "Missing theme helper: $helper" >&2; exit 1; fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/obconf-theme-XXXXXX") || exit 1
staged=
active_pid=
cleanup() {
    [ -z "$staged" ] || rm -rf -- "$staged"
    rm -rf -- "$tmp"
}
trap cleanup EXIT
stop_installer() {
    status=$1
    if [ -n "$active_pid" ]; then
        kill -TERM "$active_pid" 2>/dev/null || true
        wait "$active_pid" 2>/dev/null || true
        active_pid=
    fi
    exit "$status"
}
trap 'stop_installer 130' INT
trap 'stop_installer 143' TERM
run_bounded() {
    timeout --signal=TERM --kill-after=2s 30s "$@" &
    active_pid=$!
    if wait "$active_pid"; then status=0; else status=$?; fi
    active_pid=
    return "$status"
}

# Snapshot bounded regular input without following a last-component symlink.
# Validation and extraction consume only this private snapshot.
size=$(stat -c '%s' -- "$archive") || { echo "$archive: cannot stat archive" >&2; exit 1; }
case "$size" in ''|*[!0-9]*) echo "$archive: invalid archive size" >&2; exit 1 ;; esac
if [ "$size" -le 0 ] || [ "$size" -gt 134217728 ]; then
    echo "$archive: archive exceeds compressed size limit" >&2
    exit 1
fi
if ! run_bounded dd if="$archive" of="$tmp/archive.tar.gz" iflag=nofollow bs=1M count=129 status=none 2>/dev/null; then
    echo "$archive: cannot snapshot archive" >&2
    exit 1
fi
if [ "$(stat -c '%s' "$tmp/archive.tar.gz")" -ne "$size" ]; then
    echo "$archive: archive changed while being read" >&2
    exit 1
fi

extracted=$tmp/extracted
mkdir "$extracted"
if ! run_bounded "$extractor" "$tmp/archive.tar.gz" "$extracted"; then
    echo "$archive: bounded theme extraction failed" >&2
    exit 1
fi

selected=
for dir in "$extracted"/*; do
    [ -d "$dir" ] || continue
    [ -f "$dir/openbox-3/themerc" ] || continue
    if [ -n "$selected" ]; then
        echo "$archive: archive contains more than one installable theme" >&2
        exit 1
    fi
    selected=$dir
done
if [ -z "$selected" ]; then echo "$archive: no Openbox theme found" >&2; exit 1; fi
name=${selected##*/}
case "$name" in ''|.|..|*/*) echo "$archive: invalid theme name" >&2; exit 1 ;; esac
if printf '%s' "$name" | grep -q '[[:cntrl:]]'; then echo "$archive: theme name contains control characters" >&2; exit 1; fi
if command -v iconv >/dev/null 2>&1; then
    if ! printf '%s' "$name" | iconv -f UTF-8 -t UTF-8 >/dev/null 2>&1; then echo "$archive: theme name is not valid UTF-8" >&2; exit 1; fi
elif printf '%s' "$name" | grep -q '[^ -~]'; then
    echo "$archive: cannot validate non-ASCII theme name" >&2; exit 1
fi

if [ -z "${HOME:-}" ]; then echo "HOME is not set" >&2; exit 1; fi
theme_root=$HOME/.themes
if [ -L "$theme_root" ] || { [ -e "$theme_root" ] && [ ! -d "$theme_root" ]; }; then
    echo "$theme_root: unsafe theme directory" >&2
    exit 1
fi
mkdir -p -- "$theme_root"
theme_root=$(CDPATH= cd -- "$theme_root" && pwd -P)
final=$theme_root/$name
if [ -e "$final" ] || [ -L "$final" ]; then
    echo "$name: a theme with this name already exists" >&2
    exit 1
fi
staged=$(mktemp -d "$theme_root/.obconf-theme-XXXXXX") || exit 1
if ! run_bounded cp -R -- "$selected"/. "$staged"/; then
    echo "$name: could not stage theme" >&2
    exit 1
fi
if ! "$publisher" "$staged" "$final"; then
    echo "$name: could not publish theme without replacing another installation" >&2
    exit 1
fi
staged=
printf '%s\n' "$name"
