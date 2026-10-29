#!/bin/sh
# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

script=$1
defaults=$2
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT HUP INT TERM

mkdir -p "$root/etc" "$root/state"
cp "$defaults" "$root/etc/luxedm.conf"
cat > "$root/etc/slim.conf" <<'EOF'
default_user        migrated-user
auto_login          yes
focus_password      yes
EOF

LUXEDM_ETC_DIR="$root/etc" LUXEDM_STATE_DIR="$root/state" "$script"

grep -Eq '^default_user[[:space:]]+migrated-user$' "$root/etc/luxedm.conf"
grep -Eq '^focus_password[[:space:]]+yes$' "$root/etc/luxedm.conf"
grep -Eq '^auto_login[[:space:]]+no$' "$root/etc/luxedm.conf"
test -f "$root/state/slim-config-migrated"

# A root-run migration must not follow a replacement symlink at the target.
printf '%s\n' untouched > "$root/outside"
rm -f "$root/etc/luxedm.conf" "$root/state/slim-config-migrated"
ln -s "$root/outside" "$root/etc/luxedm.conf"
if LUXEDM_ETC_DIR="$root/etc" LUXEDM_STATE_DIR="$root/state" "$script"; then
    echo "migration unexpectedly followed target symlink" >&2
    exit 1
fi
grep -qx untouched "$root/outside"
