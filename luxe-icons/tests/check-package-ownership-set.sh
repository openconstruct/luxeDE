#!/usr/bin/env bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
icon_package=$1
shift
owners=("$@")
test "${#owners[@]}" -gt 0
tmp=$(mktemp -d -t luxe-icons-owner-set-XXXXXX)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM

icon_name=$(dpkg-deb -f "$icon_package" Package)
packages=("$icon_package")
luxup_package=
for record in "${owners[@]}"; do
  IFS='|' read -r owner alias package <<< "$record"
  test "$(dpkg-deb -f "$package" Package)" = "$owner"
  list="$tmp/$owner.list"
  dpkg-deb --fsys-tarfile "$package" | tar -tf - | sed 's,^\./,,' > "$list"
  if ! grep -E "^usr/share/icons/hicolor/[^/]+/apps/${alias//./\.}\.(svg|png)$" "$list" >/dev/null; then
    echo "${owner} package does not install its declared ${alias} hicolor icon" >&2
    exit 1
  fi
  packages+=("$package")
  if [[ "$alias" == "org.luxe.LuxUp" ]]; then luxup_package=$package; fi
done

for order in apps-first icons-first; do
  root="$tmp/$order"
  mkdir -p "$root"
  if [[ "$order" == apps-first ]]; then
    if ! dpkg --force-not-root --root="$root" --unpack "${packages[@]:1}" "$icon_package" >"$tmp/out" 2>"$tmp/err"; then
      cat "$tmp/out" "$tmp/err" >&2; exit 1
    fi
  else
    if ! dpkg --force-not-root --root="$root" --unpack "$icon_package" "${packages[@]:1}" >"$tmp/out" 2>"$tmp/err"; then
      cat "$tmp/out" "$tmp/err" >&2; exit 1
    fi
  fi
  for record in "${owners[@]}"; do
    IFS='|' read -r owner alias package <<< "$record"
    list="$tmp/$owner.list"
    while IFS= read -r path; do
      [[ "$path" == */ ]] && continue
      [[ "$path" == usr/share/icons/hicolor/*/apps/* ]] || continue
      if grep -Fxq "$path" "$root/var/lib/dpkg/info/$icon_name.list" 2>/dev/null; then
        echo "Duplicate icon ownership at $path (${owner} and ${icon_name})" >&2
        exit 1
      fi
      matches=$(grep -lFx "/$path" "$root"/var/lib/dpkg/info/*.list || true)
      [[ "$matches" == "$root/var/lib/dpkg/info/$owner.list" ]] || {
        echo "Unexpected owner for $path: $matches" >&2; exit 1;
      }
    done < "$list"
  done
  echo "PASS: ${order} package installation"
done

if [[ -n "$luxup_package" ]]; then
  conflict="$tmp/conflict"
  mkdir -p "$conflict/DEBIAN" "$conflict/usr/share/icons/hicolor/scalable/apps"
  chmod 755 "$conflict/DEBIAN"
  cat > "$conflict/DEBIAN/control" <<'EOF'
Package: luxe-icons-conflict-fixture
Version: 1.0
Architecture: all
Maintainer: Luxe Audit <audit@example.invalid>
Description: deliberately conflicting icon fixture
EOF
  printf '%s\n' '<svg xmlns="http://www.w3.org/2000/svg"/>' > \
    "$conflict/usr/share/icons/hicolor/scalable/apps/org.luxe.LuxUp.svg"
  dpkg-deb --build "$conflict" "$tmp/conflict.deb" >/dev/null
  conflict_root="$tmp/conflict-root"
  mkdir -p "$conflict_root"
  if dpkg --force-not-root --root="$conflict_root" --unpack "$luxup_package" \
      "$tmp/conflict.deb" >"$tmp/conflict.out" 2>"$tmp/conflict.err"; then
    echo 'Deliberately conflicting package fixture unexpectedly installed' >&2
    exit 1
  fi
  grep -q 'trying to overwrite' "$tmp/conflict.err"
  echo 'PASS: independent verifier rejects deliberate duplicate ownership'
fi
