/* Copyright (C) 2020 Tom Zaluckij. Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef SCREENSHOT_LOGIC_H
#define SCREENSHOT_LOGIC_H

#include <gtk/gtk.h>
#include <X11/Xlib.h>

#define AREA_SCREEN 0
#define AREA_WINDOW 1
#define AREA_REGION 2

GdkPixbuf *
screenshot (gint     area,
            gboolean include_cursor,
            gboolean include_decorations,
            GError **error);

/* Installed around Xlib calls so stale windows cannot kill the process.
 * Exposed for the active-window test. */
int
ignore_xlib_errors (Display * dpy, XErrorEvent * event);

#endif // SCREENSHOT_LOGIC_H
