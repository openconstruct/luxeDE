#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
binary=$(realpath "${1:?package-tools binary required}")
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
make_deb() {
  local name=$1 version=$2 filename=$3 payload=${4:-$1}
  local tree="$scratch/tree-$name-$version"
  mkdir -p "$tree/DEBIAN" "$tree/usr/share/$payload"
  printf 'Package: %s\nVersion: %s\nArchitecture: all\nMaintainer: Test <test@example.invalid>\nDescription: migration fixture\n' "$name" "$version" > "$tree/DEBIAN/control"
  printf '%s\n' "$version" > "$tree/usr/share/$payload/data"
  chmod -R u=rwX,go=rX "$tree"
  dpkg-deb --root-owner-group --build "$tree" "$filename" >/dev/null
}
mkdir -p "$scratch/root/debs" "$scratch/root/old"
make_deb app 1 "$scratch/root/old/app_1.deb"
make_deb app 2 "$scratch/root/debs/app_2.deb"
make_deb app 3 "$scratch/app_3.deb"
make_deb other 7 "$scratch/root/debs/other_7.deb"
"$binary" audit "$scratch/app_3.deb" app
if "$binary" audit "$scratch/app_3.deb" wrong; then exit 1; fi
"$binary" ownership "$scratch/app_3.deb" "$scratch/root/debs/other_7.deb"
make_deb conflict 1 "$scratch/conflict.deb" app
if "$binary" ownership "$scratch/app_3.deb" "$scratch/conflict.deb"; then exit 1; fi
chmod 0666 "$scratch/tree-conflict-1/usr/share/app/data"
dpkg-deb --root-owner-group --build "$scratch/tree-conflict-1" "$scratch/bad-mode.deb" >/dev/null
if "$binary" audit "$scratch/bad-mode.deb" conflict; then exit 1; fi
printf 'invalid deb\n' > "$scratch/truncated.deb"
if "$binary" audit "$scratch/truncated.deb" app; then exit 1; fi
"$binary" organize --root "$scratch/root" --incoming "$scratch/app_3.deb"
test -f "$scratch/root/debs/app_3.deb"
test -f "$scratch/root/old/app_1.deb"
test -f "$scratch/root/old/other_7.deb"
(cd "$scratch/root/debs" && sha256sum -c SHA256SUMS)
(cd "$scratch/root/old" && sha256sum -c SHA256SUMS)
"$binary" organize --root "$scratch/root" --incoming "$scratch/tree-app-2/../root/old/app_1.deb" --dry-run
make_deb app 4 "$scratch/app_4.deb"
"$binary" organize --root "$scratch/root" --incoming "$scratch/app_4.deb" --rotate-old
test -f "$scratch/root/old/app_3.deb"
"$binary" organize --root "$scratch/root" --keep-old 0
test "$(find "$scratch/root/old" -name '*.deb' | wc -l)" -eq 0
# Debian epoch ordering, not lexical ordering.
make_deb epoch '2:1+luxe8' "$scratch/epoch-high.deb"
make_deb epoch '1:9+luxe99' "$scratch/epoch-low.deb"
"$binary" organize --root "$scratch/epochs" --incoming "$scratch/epoch-high.deb" "$scratch/epoch-low.deb"
test -f "$scratch/epochs/debs/epoch-high.deb"
echo 'PASS: package audits, conflicts, frozen rollback, rotation, checksums, version ordering'
