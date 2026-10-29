#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
unset GIO_USE_VFS
export G_DEBUG=fatal-criticals HOME="$root/home" XDG_CONFIG_HOME="$root/config" XDG_CACHE_HOME="$root/cache" XDG_DATA_HOME="$root/data" XDG_STATE_HOME="$root/state"
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_DATA_HOME" "$XDG_STATE_HOME" "$root/left/folder" "$root/right/nested"
for name in alpha beta gamma; do printf '%s\n' "$name" > "$root/left/$name.txt"; done
printf 'payload\n' > "$root/left/folder/payload.txt"
if [ -n "${PCMANAGER_DND_DEBUG:-}" ]; then
  timeout 85 gdb --batch -ex run -ex bt --args "$1" "$root/left" "$root/right"
else
  timeout 85 "$1" "$root/left" "$root/right"
fi
