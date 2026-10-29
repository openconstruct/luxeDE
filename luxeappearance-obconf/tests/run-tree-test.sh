#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
binary=$1
xephyr_bin=${XEPHYR_BIN:-Xephyr}
testdir=$(mktemp -d "${TMPDIR:-/tmp}/obconf-tree-XXXXXX")
mkdir -p "$testdir/home" "$testdir/config" "$testdir/data" "$testdir/cache" "$testdir/state" "$testdir/runtime"
chmod 700 "$testdir/runtime"
"$xephyr_bin" :${LUXEAPPEARANCE_TEST_DISPLAY:-39} -screen 800x600 -ac -noreset >"$testdir/display.log" 2>&1 &
xp=$!
cleanup() {
    kill "$xp" 2>/dev/null || true
    wait "$xp" 2>/dev/null || true
    rm -rf "$testdir" 2>/dev/null || true
}
trap cleanup EXIT
sleep 1
export HOME="$testdir/home" DISPLAY=:${LUXEAPPEARANCE_TEST_DISPLAY:-39}
export XDG_CONFIG_HOME="$testdir/config" XDG_DATA_HOME="$testdir/data"
export XDG_CACHE_HOME="$testdir/cache" XDG_STATE_HOME="$testdir/state"
export XDG_RUNTIME_DIR="$testdir/runtime" XDG_DATA_DIRS="/usr/local/share:/usr/share"
export GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none GSETTINGS_BACKEND=memory GIO_USE_VFS=local
unset _LUXESESSION_PID
# Hold extraction in a cancellable child so the lifecycle case can prove that
# closing the originating editor terminates the real installer before publish.
real_extractor=${OBCONF_THEME_EXTRACTOR:?missing extractor helper}
cat > "$testdir/slow-extractor" <<EOF
#!/bin/sh
exec /bin/sleep 30
EOF
chmod +x "$testdir/slow-extractor"
export OBCONF_THEME_EXTRACTOR="$testdir/slow-extractor"
dbus-run-session -- "$binary"
