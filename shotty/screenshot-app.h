/* Copyright (C) 2020 Tom Zaluckij. Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef SCREENSHOT_APP_H
#define SCREENSHOT_APP_H

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

G_DECLARE_FINAL_TYPE (ScreenshotApp,
                      screenshot_app,
                      SCREENSHOT,
                      APP,
                      GtkApplication)

ScreenshotApp *screenshot_app_new (void);

#endif // SCREENSHOT_APP_H
