#!/bin/bash
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
# GitHub artifact paths cannot contain ':'. Preserve the DEB and its internal
# version; escape the epoch separator in the filename before checksumming.
set -euo pipefail
if [[ $# != 1 || ! -f $1 || -L $1 || $1 != *.deb ]]; then
    echo 'Expected one regular DEB file' >&2
    exit 2
fi
file=$1
directory=$(dirname -- "$file")
name=$(basename -- "$file")
safe_name=${name//:/%3A}
if [[ $safe_name != "$name" ]]; then
    target=$directory/$safe_name
    # A hard link fails if the destination already exists, avoiding an
    # accidental overwrite and keeping the exact package bytes intact.
    ln -- "$file" "$target"
    rm -- "$file"
fi
