#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT HUP INT TERM
DESTDIR="$stage" cmake --install "${1:?build directory required}" --prefix /usr >"$stage/install.log" 2>&1 || { cat "$stage/install.log"; exit 1; }
test -d "$stage/var/lib/synaptic"
test ! -e "$stage/usr/var/lib/synaptic"
test "$(stat -c %a "$stage/var/lib/synaptic")" = 755
