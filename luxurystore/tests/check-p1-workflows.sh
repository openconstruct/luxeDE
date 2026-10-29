#!/bin/sh
# Copyright (C) 2026 Jerry Howell
# SPDX-License-Identifier: GPL-2.0-or-later
# P1 stabilization invariants of LuxuryStore: a failed cache load must
# re-enable the controls the load path disabled, only one review window may
# exist at a time, the helper holds the package-manager lock across
# resolve/plan/commit, and store searches are debounced.

set -eu
main=$1
helper=$2

grep -Fq 'Cannot read package lists' "$main"
grep -Fq 'app.review_open = true' "$main"
grep -Fq 'if (app.review_open) return;' "$main"
grep -Fq 'app.review_open = false' "$main"
grep -Fq 'g_timeout_add(350' "$main"

grep -Fq 'GetLock(_config->FindDir("Dir::Cache::Archives") + "lock")' "$helper"
grep -Fq 'Another package manager is running' "$helper"
grep -Fq 'if (apply) {' "$helper"
