/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _DEMO_H_
#define _DEMO_H_

#include <gtk/gtk.h>

/* Build the in-process theme preview and return its root widget.
 * The caller appends it to the demo area. */
GtkWidget* show_demo( void );

/* Rebuild the demo toolbar buttons for the current toolbar style. */
void demo_refresh_toolbar( void );

#endif
