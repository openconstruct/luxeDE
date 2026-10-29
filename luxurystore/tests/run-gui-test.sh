#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Keep the GUI/backend integration test from aborting in headless package builds.
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "SKIP: no graphical display available for LuxuryStore integration test" >&2
    exit 77
fi
exec "$@"
