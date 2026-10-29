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
if [[ ${LUXE_CI_COMPONENT:-} == luxesettings ]]; then
    # The translated UI test requires a locale absent from minimal images.
    apt-get install -y --no-install-recommends locales
    localedef -i de_DE -f UTF-8 de_DE.UTF-8
fi
if [[ ${LUXE_CI_COMPONENT:-} == luxurystore ]]; then
    # The package normally owns this directory. CI tests run the uninstalled
    # binaries as an ordinary user, so provide the same installed prerequisite.
    install -d -m 0755 /var/lib/synaptic
fi
# Used by the isolated local Panel fixture and disposable CI container.
mkdir -p /usr/libexec/luxepanel /usr/lib/luxepanel /usr/share/luxepanel
