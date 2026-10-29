#!/bin/sh
# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
# Step 10b wiring audit: external MOVE completion must trash (never delete)
# source files, and same-process drops must be flagged so drag-end cannot
# delete files the drop already moved, copied, or trashed.

set -eu
source_file=$1
grep -Fq 'pcmgr-internal-drop' "$source_file"
grep -Fq 'gdk_drop_get_drag(request->drop)' "$source_file"
grep -Fq 'PCMGR_TRASH, files' "$source_file"
grep -Fq 'drag_move_done' "$source_file"
! grep -Fq '(void)delete_data' "$source_file"
