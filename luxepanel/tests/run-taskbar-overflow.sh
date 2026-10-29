#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
test_bin=$1
display=${LUXEPANEL_TEST_DISPLAY:-198}
Xephyr :$display -screen 600x200 -ac -noreset >/dev/null 2>&1 &
xpid=$!
trap 'kill "$xpid" 2>/dev/null || true' EXIT INT TERM
for i in $(seq 1 50); do
    [ -S "/tmp/.X11-unix/X$display" ] && break
    sleep 0.1
done
DISPLAY=:$display GDK_BACKEND=x11 "$test_bin"
