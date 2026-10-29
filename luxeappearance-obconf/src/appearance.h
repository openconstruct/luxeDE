/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   appearance.h for ObConf, the configuration tool for Openbox
   Copyright (c) 2003-2007   Dana Jansens
   Copyright (c) 2003        Tim Riley

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#ifndef obconf__appearance_h
#define obconf__appearance_h

#include <gtk/gtk.h>

void appearance_setup_tab(void);

/* Builder signal handlers (registered via cscope in main.c). */
void on_title_layout_changed(GtkEditable *w, gpointer data);
void on_font_active_font_set(GtkFontButton *w, gpointer data);
void on_font_inactive_font_set(GtkFontButton *w, gpointer data);
void on_font_menu_header_font_set(GtkFontButton *w, gpointer data);
void on_font_menu_item_font_set(GtkFontButton *w, gpointer data);
void on_font_active_display_font_set(GtkFontButton *w, gpointer data);
void on_font_inactive_display_font_set(GtkFontButton *w, gpointer data);

#endif
