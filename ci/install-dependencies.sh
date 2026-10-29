#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
# Run only inside a disposable CI/container environment.
set -euo pipefail
if [[ ${LUXE_CI_CONTAINER:-} != 1 || $EUID != 0 ]]; then
    echo 'Dependency setup requires root inside an opted-in CI container (LUXE_CI_CONTAINER=1).' >&2
    exit 2
fi
. /etc/os-release
case "$ID:$VERSION_ID" in
    ubuntu:24.04|debian:13) ;;
    *) echo "Unsupported CI distribution: $ID $VERSION_ID" >&2; exit 2 ;;
esac
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
mapfile -t packages < <(sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d' "$script_dir/dependencies.txt")
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends "${packages[@]}"
# Mountpoints only: actual Panel test payloads are private namespace binds.
mkdir -p /usr/libexec/luxepanel /usr/lib/luxepanel /usr/share/luxepanel
