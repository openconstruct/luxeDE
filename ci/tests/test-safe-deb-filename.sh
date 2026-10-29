#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p "$scratch/tree/DEBIAN" "$scratch/packages"
chmod 0755 "$scratch/tree" "$scratch/tree/DEBIAN"
cat > "$scratch/tree/DEBIAN/control" <<'EOF'
Package: artifact-fixture
Version: 2:0.2.3-1
Architecture: all
Maintainer: Test <test@example.invalid>
Description: Artifact filename fixture
EOF
original="$scratch/packages/artifact-fixture_2:0.2.3-1_all.deb"
safe="$scratch/packages/artifact-fixture_2%3A0.2.3-1_all.deb"
dpkg-deb --root-owner-group --build "$scratch/tree" "$original" >/dev/null
before=$(sha256sum "$original" | cut -d ' ' -f 1)
bash "$root/ci/safe-deb-filename.sh" "$original"
test ! -e "$original"
test -f "$safe"
test "$(sha256sum "$safe" | cut -d ' ' -f 1)" = "$before"
test "$(dpkg-deb -f "$safe" Version)" = '2:0.2.3-1'
(cd "$scratch/packages" && sha256sum -- *.deb > SHA256SUMS && sha256sum -c SHA256SUMS)
bash "$root/ci/safe-deb-filename.sh" "$safe"
cp -- "$safe" "$original"
if bash "$root/ci/safe-deb-filename.sh" "$original" 2>/dev/null; then
    echo 'Conflicting artifact filename was overwritten' >&2
    exit 1
fi
test -f "$original" && test -f "$safe"
echo 'PASS: safe artifact name preserves Debian epoch, bytes and checksums'
