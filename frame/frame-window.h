/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FRAME_WINDOW_H
#define FRAME_WINDOW_H

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

G_DECLARE_FINAL_TYPE (FrameWindow,
                      frame_window,
                      FRAME,
                      WINDOW,
                      GtkApplicationWindow)

FrameWindow *frame_window_new (GtkApplication *app);
void frame_window_open (FrameWindow *win, GFile *file);

#endif // FRAME_WINDOW_H
