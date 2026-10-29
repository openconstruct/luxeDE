#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
# Unpack only in disposable roots. Never configure packages or run host scripts.
set -eu
[ "$#" -eq 3 ] || { echo 'Usage: check-package-ownership.sh PREVIOUS CANDIDATE LUXUP' >&2; exit 2; }
previous=$(realpath -e -- "$1")
candidate=$(realpath -e -- "$2")
app=$(realpath -e -- "$3")
scratch=$(mktemp -d -t luxe-dpkg-ownership-XXXXXX)
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM
for package in "$previous" "$candidate" "$app"; do
  dpkg-deb --ctrl-tarfile "$package" > "$scratch/control.tar"
  tar -tf "$scratch/control.tar" > "$scratch/control.list"
  if sed 's,^\./,,' "$scratch/control.list" | grep -qx preinst; then
    echo "Refusing preinst: $package" >&2; exit 1
  fi
done
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
icon=usr/share/icons/hicolor/scalable/apps/org.luxe.LuxUp.svg
for label in old-conflict app-first icons-first; do
  root="$scratch/$label"
  mkdir "$root"
  case "$label" in
    old-conflict) set -- "$app" "$previous";;
    app-first) set -- "$app" "$candidate";;
    icons-first) set -- "$candidate" "$app";;
  esac
  status=0
  dpkg --force-not-root --root="$root" --unpack "$@" > "$scratch/out" 2> "$scratch/err" || status=$?
  if [ "$label" = old-conflict ]; then
    [ "$status" -ne 0 ] && grep -q 'trying to overwrite' "$scratch/err" && grep -qF 'org.luxe.LuxUp.svg' "$scratch/err"
  else
    if [ "$status" -ne 0 ]; then cat "$scratch/out" "$scratch/err" >&2; exit 1; fi
    test -f "$root/$icon"
    owners=$(grep -lFx "/$icon" "$root"/var/lib/dpkg/info/*.list)
    test "$owners" = "$root/var/lib/dpkg/info/luxup.list"
  fi
  echo "PASS: $label"
done
