#!/bin/sh
# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
# Isolated Xephyr drag/drop test. It is opt-in: a caller must provide a
# display number that is not already serving another X server.
set -eu

test_bin=$1
if [ -z "${PCMANAGER_TEST_DISPLAY:-}" ]; then
    echo "SKIP: set PCMANAGER_TEST_DISPLAY to an unused X display number" >&2
    exit 77
fi
display=$PCMANAGER_TEST_DISPLAY
socket="/tmp/.X11-unix/X$display"
[ ! -e "$socket" ] || { echo "SKIP: X display :$display is already in use" >&2; exit 77; }

scratch=$(mktemp -d "${TMPDIR:-/tmp}/pcmgr-dnd-XXXXXX")
Xephyr ":$display" -screen 1500x950 -noreset >"$scratch/xephyr.log" 2>&1 &
xpid=$!
cleanup() {
    kill "$xpid" 2>/dev/null || true
    wait "$xpid" 2>/dev/null || true
    rm -rf "$scratch"
}
trap cleanup EXIT INT TERM

mkdir -p "$scratch/left/folder" "$scratch/right/nested" \
    "$scratch/home" "$scratch/config" "$scratch/cache" "$scratch/data" "$scratch/runtime"
chmod 700 "$scratch/runtime"
printf 'alpha\n' > "$scratch/left/alpha.txt"
printf 'beta\n' > "$scratch/left/beta.txt"
printf 'gamma\n' > "$scratch/left/gamma.txt"
printf 'payload\n' > "$scratch/left/folder/payload.txt"

for _ in $(seq 1 50); do
    if ! kill -0 "$xpid" 2>/dev/null; then
        echo "Xephyr exited before becoming ready" >&2
        cat "$scratch/xephyr.log" >&2
        exit 1
    fi
    [ -S "$socket" ] && break
    sleep 0.2
done
[ -S "$socket" ] || { echo "Xephyr did not become ready" >&2; exit 1; }

HOME="$scratch/home" \
XDG_CONFIG_HOME="$scratch/config" \
XDG_CACHE_HOME="$scratch/cache" \
XDG_DATA_HOME="$scratch/data" \
XDG_RUNTIME_DIR="$scratch/runtime" \
DISPLAY=":$display" \
GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none \
    dbus-run-session -- "$test_bin" "$scratch/left" "$scratch/right"
