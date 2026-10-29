#!/bin/sh
# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu
source_file=$1
grep -Fq '#define SINGLE_INST_MAX_REQUEST (64 * 1024)' "$source_file"
grep -Fq '#define SINGLE_INST_MAX_ARGS 256' "$source_file"
grep -Fq 'Ignoring malformed PCManager IPC request' "$source_file"
grep -Fq 'Ignoring oversized PCManager IPC request' "$source_file"
! grep -Fq 'g_error("client connection: invalid char' "$source_file"
