#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Manual, out-of-tree UI harness. CMake/CTest remains authoritative.
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 /path/to/luxurystore-tests [screenshot ...]" >&2
    exit 2
fi
binary=$1
shift
if [[ ! -x "$binary" ]]; then
    echo "LuxuryStore test binary is not executable: $binary" >&2
    exit 2
fi

testdir=$(mktemp -d /tmp/luxurystore-test-XXXXXX)
xp=''
cleanup() {
    if [[ -n "$xp" ]]; then
        kill "$xp" 2>/dev/null || true
        wait "$xp" 2>/dev/null || true
    fi
}
trap cleanup EXIT
Xephyr :29 -screen 1280x850 -noreset >"$testdir/xephyr.log" 2>&1 &
xp=$!
sleep 1
export DISPLAY=:29 GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none \
    GSETTINGS_BACKEND=memory GIO_USE_VFS=local
G_DEBUG=fatal-warnings timeout 180s dbus-run-session -- "$binary" "$@" \
    >"$testdir/test.log" 2>&1
cat "$testdir/test.log"
printf 'Artifacts: %s\n' "$testdir"
