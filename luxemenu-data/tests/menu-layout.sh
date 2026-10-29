#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
if [ "$#" -lt 4 ] || [ -z "$1" ] || [ ! -x "$1" ]; then
  echo "SKIP: private Luxe menu-cache-gen is unavailable" >&2
  exit 77
fi
program=$(realpath "$1")
layout=$(realpath "$2")
policy=$(realpath "$3")
source_root=$(realpath "$4")
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p "$root/data/applications" "$root/data/desktop-directories" \
  "$root/config/menus/luxe-applications-merged" \
  "$root/config/menus/lxde-applications-merged"
cp "$source_root/luxemenu-data/desktop-directories/"*.directory "$root/data/desktop-directories/"

while IFS='|' read -r name source_relative; do
  [ -n "$name" ] || continue
  source="$source_root/$source_relative"
  [ -f "$source" ] || { echo "Missing package desktop source for $name: $source_relative" >&2; exit 1; }
  grep -q '^Categories=.*Settings;' "$source" || {
    echo "Excluded desktop entry does not declare the Settings category: $name" >&2
    exit 1
  }
  cp "$source" "$root/data/applications/$name"
done < "$policy"

cp "$source_root/luxesettings/data/org.luxe.Settings.desktop" "$root/data/applications/"
entry() {
  printf '[Desktop Entry]\nType=Application\nName=%s\nExec=/bin/true\nCategories=%s\n' "$1" "$2" > "$root/data/applications/$1"
}
entry luxurystore.desktop 'System;PackageManager;'
entry third-party.desktop 'Settings;'
entry terminal.desktop 'Utility;'
entry merged-luxe.desktop 'Utility;'
entry merged-legacy.desktop 'Utility;'

cat > "$root/config/menus/luxe-applications-merged/override.menu" <<'MENU'
<Menu><Name>Applications</Name><Menu><Name>Merge Test</Name><Include><Filename>merged-luxe.desktop</Filename></Include></Menu></Menu>
MENU
cat > "$root/config/menus/lxde-applications-merged/override.menu" <<'MENU'
<Menu><Name>Applications</Name><Menu><Name>Merge Test</Name><Include><Filename>merged-legacy.desktop</Filename></Include></Menu></Menu>
MENU
cp "$layout" "$root/config/menus/luxe-applications.menu"
ln -s luxe-applications.menu "$root/config/menus/lxde-applications.menu"

export XDG_DATA_HOME="$root/data" XDG_DATA_DIRS="$root/empty-data"
export XDG_CONFIG_HOME="$root/config" XDG_CONFIG_DIRS="$root/empty-config"
export XDG_CACHE_HOME="$root/cache" XDG_CURRENT_DESKTOP=X-Luxe LANG=C.UTF-8
timeout 20 "$program" "--input=$root/config/menus/luxe-applications.menu" "--output=$root/luxe.cache"
timeout 20 "$program" "--input=$root/config/menus/lxde-applications.menu" "--output=$root/lxde.cache"

while IFS='|' read -r name source_relative; do
  [ -n "$name" ] || continue
  if grep -Fxq -- "-$name" "$root/luxe.cache"; then
    echo "Settings entry escaped exclusion: $name" >&2
    exit 1
  fi
done < "$policy"
for name in org.luxe.Settings.desktop luxurystore.desktop third-party.desktop terminal.desktop merged-luxe.desktop; do
  grep -Fxq -- "-$name" "$root/luxe.cache" || { echo "Required menu entry missing: $name" >&2; exit 1; }
done
grep -Fxq -- '-merged-legacy.desktop' "$root/lxde.cache" || {
  echo 'Compatibility menu symlink did not use the installed-name merge directory' >&2
  exit 1
}
echo 'PASS: actual settings entries, hub/store visibility, third-party entries, and both installed-name merge directories'
