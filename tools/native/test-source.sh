#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
binary=$(realpath "$1")
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p "$root/cmake" "$root/example"
printf 'set(LUXE_COMPONENTS example)\n' > "$root/cmake/LuxeComponents.cmake"
touch "$root/example/CMakeLists.txt"
"$binary" "$root"
ln -s /etc/passwd "$root/example/outside"
if "$binary" "$root"; then exit 1; fi
rm "$root/example/outside"
ln -s missing "$root/example/broken"
if "$binary" "$root"; then exit 1; fi
rm "$root/example/broken"
printf '\177ELF' > "$root/example/disguised.txt"
if "$binary" "$root"; then exit 1; fi
rm "$root/example/disguised.txt"
mkdir "$root/example/CMakeFiles"
if "$binary" "$root"; then exit 1; fi
rmdir "$root/example/CMakeFiles"
printf 'set(LUXE_COMPONENTS example example)\n' > "$root/cmake/LuxeComponents.cmake"
if "$binary" "$root"; then exit 1; fi
echo 'PASS: source hygiene rejects external/broken links, ELF files, caches and duplicate components'
