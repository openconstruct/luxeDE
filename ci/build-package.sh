#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 || $EUID == 0 ]]; then
    echo 'Usage (as an ordinary user): bash ci/build-package.sh COMPONENT DISTRIBUTION [build-only|test-package]' >&2
    exit 2
fi
component=$1
distribution=$2
phase=${3:-all}
case "$distribution" in ubuntu-24.04|debian-13) ;; *) exit 2 ;; esac
case "$phase" in all|build-only|test-package) ;; *) exit 2 ;; esac
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/ci/$distribution/$component"
mkdir -p "$root/build/ci/$distribution"
cmake "-DCOMPONENT=$component" "-DOUTPUT=$root/build/ci/$distribution/$component-selection.json" \
    -P "$root/ci/Matrix.cmake"
options=()
if [[ $component == luxurystore ]]; then
    prefixes="$root/build/ci/$distribution/apt-inputs"
    cmake "-DOUTPUT_ROOT=$prefixes" -P "$root/ci/AptPrefixes.cmake"
    options+=("-DLUXURYSTORE_APT6_ROOT=$prefixes/apt6" "-DLUXURYSTORE_APT7_ROOT=$prefixes/apt7")
fi
if [[ $phase != test-package ]]; then
    cmake -S "$root/$component" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON "${options[@]}"
    cmake --build "$build" --parallel 2
fi
if [[ $phase == build-only ]]; then exit 0; fi
test_command=(ctest --test-dir "$build" --output-on-failure --output-junit "$build/ctest.xml" --timeout 180)
if [[ $component == luxepanel && ${LUXE_PANEL_CONTAINER_INSTALL:-0} == 1 ]]; then
    # The GitHub job installed this build into its disposable container as root.
    # Tests still run as the unprivileged builder with private X/D-Bus fixtures.
    export LUXEPANEL_TEST_INSTALLED_PLUGINS=1
elif [[ $component == luxepanel ]]; then
    # Exercise shipped helper/plugin paths without installing onto the machine.
    stage="$build/test-stage"
    DESTDIR="$stage" cmake --install "$build"
    test_command=(bwrap --die-with-parent --ro-bind / / --dev /dev --proc /proc
        --bind "$root/build" "$root/build" --bind /tmp /tmp
        --ro-bind "$stage/usr/libexec/luxepanel" /usr/libexec/luxepanel
        --ro-bind "$stage/usr/lib/luxepanel" /usr/lib/luxepanel
        --ro-bind "$stage/usr/share/luxepanel" /usr/share/luxepanel
        -- "${test_command[@]}")
fi
xvfb-run -a -s '-screen 0 1920x1080x24 -nolisten tcp' dbus-run-session -- \
    "${test_command[@]}"
cpack --config "$build/CPackConfig.cmake" -G DEB
shopt -s nullglob
debs=("$build"/packages/*.deb)
if [[ ${#debs[@]} != 1 ]]; then
    echo "Expected exactly one DEB for $component; found ${#debs[@]}" >&2
    exit 1
fi
bash "$root/ci/safe-deb-filename.sh" "${debs[0]}"
debs=("$build"/packages/*.deb)
"$root/cmake/AuditDeb.sh" "${debs[0]}" "$component"
(
    cd "$build/packages"
    sha256sum -- *.deb > SHA256SUMS
    dpkg-query -W > build-environment.txt
    printf 'Component: %s\nDistribution: %s\nSource: %s\n' \
        "$component" "$distribution" "${GITHUB_SHA:-local-uncommitted}" > provenance.txt
)
