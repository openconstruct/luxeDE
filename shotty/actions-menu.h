/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef ACTIONS_MENU_H
#define ACTIONS_MENU_H

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

#include "screenshot-app.h"

G_DECLARE_FINAL_TYPE (ActionsMenu,
                      actions_menu,
                      ACTIONS,
                      MENU,
                      GtkApplicationWindow)

ActionsMenu *actions_menu_new (GdkPixbuf *screenshot,
                               ScreenshotApp *app);
gboolean actions_menu_is_handoff (ActionsMenu *win);
gboolean shotty_cleanup_handoff_cache (GError **error);

#endif // ACTIONS_MENU_H
