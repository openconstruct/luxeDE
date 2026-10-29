#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=${1:?native tool name required}
shift
"$root/tools/build-native.sh" "$target" >&2
build=${LUXE_NATIVE_TOOLS_BUILD_DIR:-"$root/build/native-tools-host"}
exec "$build/$target" "$@"
