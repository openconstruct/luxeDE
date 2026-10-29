#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
[ "$#" -gt 0 ] || { echo 'Native target required' >&2; exit 2; }
for target do
  case "$target" in check-source|run-nested-x|package-tools|deduplicate-icons|diagnostics|behavior-fixtures|icon-fixtures-gtk4|icon-fixtures-gtk3|gtk3-fixtures|import-themes|panel-profile) ;; *) echo "Unknown native tool: $target" >&2; exit 2;; esac
done
build=${LUXE_NATIVE_TOOLS_BUILD_DIR:-"$root/build/native-tools-host"}
mkdir -p "$build"
(
  flock 9
  cmake -S "$root/tools/native" -B "$build" -G 'Unix Makefiles'
  cmake --build "$build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" --target "$@"
) 9>"$build/.build.lock"
