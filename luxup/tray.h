/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXUP_TRAY_H
#define LUXUP_TRAY_H
#include <glib.h>

typedef enum {
    LUXUP_TRAY_CHECKING,
    LUXUP_TRAY_UPTODATE,
    LUXUP_TRAY_UPDATES,
    LUXUP_TRAY_ERROR,
    LUXUP_TRAY_UNKNOWN
} LuxUpTrayState;

typedef struct _LuxUpTray LuxUpTray;

/* button is the X11 button number (1 left, 2 middle, 3 right). */
typedef void (*LuxUpTrayActivateFunc)(LuxUpTray *tray, guint button, gpointer user_data);

LuxUpTray *luxup_tray_new(LuxUpTrayActivateFunc activate, gpointer user_data);
void luxup_tray_set_state(LuxUpTray *tray, LuxUpTrayState state);
void luxup_tray_set_tooltip(LuxUpTray *tray, const char *tooltip);
void luxup_tray_redock(LuxUpTray *tray);
void luxup_tray_free(LuxUpTray *tray);

#endif
