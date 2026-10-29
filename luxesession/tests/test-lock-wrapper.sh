#!/bin/sh
# Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
set -eu
fixture=$(mktemp -d)
trap 'rm -r -- "$fixture"' EXIT HUP INT TERM
ln -s "$1" "$fixture/luxedm-lock"
PATH="$fixture" "$1" "$2"
