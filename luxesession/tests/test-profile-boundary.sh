#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
program=$(realpath "${1:?program required}")
export HOME="$root/home" XDG_CONFIG_HOME="$root/config" XDG_CACHE_HOME="$root/cache"
check() {
  status=0
  timeout 5 "$program" "$@" >"$root/stdout" 2>"$root/stderr" || status=$?
  test "$status" -ne 0 && test "$status" -ne 124 && test "$status" -ne 137
}
check --session ../escape
check --session name.with.dot
check --de /absolute
test ! -e "$root/config/escape"
test ! -e "$root/config/luxesession/name.with.dot"
echo 'PASS: session and desktop profile path boundaries'
