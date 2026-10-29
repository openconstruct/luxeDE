/* Copyright (C) 2020 Tom Zaluckij. Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef SCREENSHOT_MENU_H
#define SCREENSHOT_MENU_H

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

#include "screenshot-app.h"

G_DECLARE_FINAL_TYPE (ScreenshotMenu,
                      screenshot_menu,
                      SCREENSHOT,
                      MENU,
                      GtkApplicationWindow)

ScreenshotMenu *screenshot_menu_new (ScreenshotApp *app);

#endif // SCREENSHOT_MENU_H
