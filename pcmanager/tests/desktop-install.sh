#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT HUP INT TERM
DESTDIR="$stage" cmake --install "${1:?build directory required}" --prefix /usr >"$stage/install.log" 2>&1 || {
  cat "$stage/install.log" >&2
  exit 1
}
apps="$stage/usr/share/applications"
test -f "$apps/pcmanager.desktop"
test -f "$apps/pcmanager-desktop-pref.desktop"
grep -Fx 'Exec=pcmanager --desktop-pref' "$apps/pcmanager-desktop-pref.desktop" >/dev/null
if command -v desktop-file-validate >/dev/null 2>&1; then
  desktop-file-validate "$apps/pcmanager.desktop" "$apps/pcmanager-desktop-pref.desktop"
fi
