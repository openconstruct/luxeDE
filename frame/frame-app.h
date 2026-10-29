/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FRAME_APP_H
#define FRAME_APP_H

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

G_DECLARE_FINAL_TYPE (FrameApp,
                      frame_app,
                      FRAME,
                      APP,
                      GtkApplication)

FrameApp *frame_app_new (void);

#endif // FRAME_APP_H
