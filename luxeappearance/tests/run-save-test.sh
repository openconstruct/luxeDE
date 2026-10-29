#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
# Run the save test and the settings probe in a private Xephyr + D-Bus
# session with fully isolated HOME/XDG state.
set -euo pipefail

test_bin=$1
source_dir=$2
probe_bin=$3
lifecycle_bin=${4:-}
xephyr_bin=${XEPHYR_BIN:-Xephyr}
dbus_bin=${DBUS_RUN_SESSION_BIN:-dbus-run-session}

testdir=$(mktemp -d "${TMPDIR:-/tmp}/luxeappearance-save-XXXXXX")
mkdir -p "$testdir/home" "$testdir/config/gtk-4.0" \
         "$testdir/data/themes/TestGtk2Only/gtk-2.0" \
         "$testdir/system/luxesession/TestSession" "$testdir/config/openbox" \
         "$testdir/cache" "$testdir/state" "$testdir/runtime"
chmod 700 "$testdir/runtime"
mkdir -p "$testdir/system/share/icons/TestSystemPack"
printf '[Icon Theme]\nName=System pack\nDirectories=scalable/apps\n[scalable/apps]\nSize=48\nType=Scalable\n' \
    > "$testdir/system/share/icons/TestSystemPack/index.theme"
printf '[Settings]\ngtk-double-click-time=333\n' > "$testdir/config/gtk-4.0/settings.ini"
# A system-side desktop.conf template for the test session: Apply must
# seed from it instead of creating the file from nothing.
printf '[Session]\ncore1=openbox\n[GTK]\nsNet/ThemeName=Original\n' > "$testdir/system/luxesession/TestSession/desktop.conf"
printf '<openbox_config><theme><name>Original</name></theme></openbox_config>\n' > "$testdir/config/openbox/rc.xml"

"$xephyr_bin" :${LUXEAPPEARANCE_TEST_DISPLAY:-29} -screen 1000x800 -ac -noreset > "$testdir/display.log" 2>&1 &
xp=$!
cleanup() {
    kill "$xp" 2>/dev/null || true
    wait "$xp" 2>/dev/null || true
    # The desktop portal may briefly recreate its private document directory
    # while the test D-Bus session exits. Retry cleanup so a harmless race
    # cannot turn a passing behavior test into a failure.
    for attempt in 1 2 3 4 5; do
        rm -rf "$testdir" 2>/dev/null && return 0
        sleep 0.1
    done
    rm -rf "$testdir" 2>/dev/null || true
}
trap cleanup EXIT
sleep 1

# HOME is isolated too: production code reads ~/.themes, ~/.icons and
# ~/.gtkrc-2.0 directly, so a non-hermetic HOME let the invoking user's
# real themes influence the enumerated lists.
export HOME="$testdir/home"
export DISPLAY=:${LUXEAPPEARANCE_TEST_DISPLAY:-29}
export XDG_CONFIG_HOME="$testdir/config" XDG_DATA_HOME="$testdir/data"
export XDG_CONFIG_DIRS="$testdir/system"
# Keep the real system data dirs so GSettings schemas (org.gnome.desktop.interface)
# still resolve; only the writable HOME/XDG dirs are redirected.
export XDG_DATA_DIRS="$(dirname "$test_bin")/theme-data:$testdir/system/share:/usr/local/share:/usr/share"
export XDG_CACHE_HOME="$testdir/cache" XDG_STATE_HOME="$testdir/state"
export XDG_RUNTIME_DIR="$testdir/runtime"
export GDK_BACKEND=x11 GSK_RENDERER=cairo GTK_A11Y=none GSETTINGS_BACKEND=memory GIO_USE_VFS=local
# An inherited _LUXESESSION_PID from the live desktop would let the test
# invoke the real luxepanelctl and restart the user's panel. It is cleared
# here; the save test sets its own value only for its private stand-in.
unset _LUXESESSION_PID
G_DEBUG=fatal-warnings "$dbus_bin" -- "$test_bin"
G_DEBUG=fatal-warnings "$dbus_bin" -- "$probe_bin"
if [ -n "$lifecycle_bin" ]; then
    G_DEBUG=fatal-warnings "$dbus_bin" -- "$lifecycle_bin"
fi
