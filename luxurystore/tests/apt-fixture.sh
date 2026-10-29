#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir -p "$root/repo" "$root/lists/partial" "$root/archives/partial" "$root/empty"
for spec in flatpak:1 luxe-test-dependency-one:1 luxe-test-dependency-two:1 libgtk4-1:1 libgtksourceview-5-0:1 libspelling-1-2:1 luxe-test-upgradable:2 luxe-test-manual:1; do
  name=${spec%:*}; version=${spec#*:}
  printf 'Package: %s\nVersion: %s\nArchitecture: amd64\nMaintainer: Fixture <fixture@example.test>\nFilename: %s.deb\nSize: 1\n' "$name" "$version" "$name" >> "$root/repo/Packages"
  if [ "$name" = flatpak ]; then
    printf 'Depends: luxe-test-dependency-one, luxe-test-dependency-two\n' >> "$root/repo/Packages"
  fi
  printf 'Description: Local APT fixture\n\n' >> "$root/repo/Packages"
  case "$name" in
    flatpak|luxe-test-dependency-one|luxe-test-dependency-two) ;;
    *)
    printf 'Package: %s\nStatus: install ok installed\nVersion: 1\nArchitecture: amd64\nMaintainer: Fixture <fixture@example.test>\nDescription: Local APT fixture\n\n' "$name" >> "$root/status"
    ;;
  esac
done
printf 'deb [trusted=yes] file:%s/repo ./\n' "$root" > "$root/sources.list"
cat > "$root/apt.conf" <<CONFIG
Dir::Etc::sourcelist "$root/sources.list";
Dir::Etc::sourceparts "$root/empty";
Dir::Etc::parts "$root/empty";
Dir::Etc::main "$root/empty.conf";
Dir::State::status "$root/status";
Dir::State::lists "$root/lists";
Dir::State::extended_states "$root/extended_states";
Dir::Cache::archives "$root/archives";
Dir::Cache::pkgcache "";
Dir::Cache::srcpkgcache "";
Dir::Log "$root";
APT::Sandbox::User "$(id -un)";
APT::Architecture "amd64";
Acquire::Languages "none";
CONFIG
export APT_CONFIG="$root/apt.conf"
# Only the private file repository; no host configuration hooks or transactions.
timeout 15 /usr/bin/apt-get update > "$root/update.log" 2>&1 || { cat "$root/update.log" >&2; exit 1; }
timeout 60 "$@"
