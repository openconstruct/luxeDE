/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   main.h for ObConf, the configuration tool for Openbox
   Copyright (c) 2003        Dana Jansens

   Copyright (C) 2010        Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

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

/* This file is part of ObConf. It's taken by Hong Jen Yee on
 * 2010-08-07 and some modifications were done to make it a loadable
 * module of LuxeAppearance. */

#ifndef obconf__main_h
#define obconf__main_h

#include <obrender/render.h>
#include <obrender/instance.h>
#include <obt/xml.h>
#include <obt/paths.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

/* gdk_x11_get_xatom_by_name() was removed in GTK 4.18; the
 * for_display variant (deprecated but present) is its successor. */
#include <gdk/x11/gdkx.h>
#define gdk_x11_get_xatom_by_name(name) \
    gdk_x11_get_xatom_by_name_for_display(gdk_display_get_default(), (name))

extern GtkBuilder *builder;
extern RrInstance *rrinst;
extern GtkWidget *mainwin;
extern GtkWidget *apply_button;
extern gchar *obc_config_file;
extern GStatBuf obc_loaded_stat;
extern gboolean obc_loaded_stat_valid;
extern ObtPaths *paths;
extern ObtXmlInst *xml_i;

#define get_widget(s) GTK_WIDGET(gtk_builder_get_object(builder, s))

void obconf_error(gchar *msg, gboolean model);

/* Enable the Apply button; called from tree_set_* after any change. */
void obconf_set_dirty(void);

#endif
