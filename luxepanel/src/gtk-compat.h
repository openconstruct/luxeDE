/*
 * Copyright (C) 2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *
 * This file is a part of LXPanel project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __GTK_COMPAT_H__
#define __GTK_COMPAT_H__ 1

/* compatibility things */

#  define  GTK_DIALOG_NO_SEPARATOR      0

#include <gdk/x11/gdkx.h>

/* X11 window access: GdkDrawable/GdkWindow XIDs are gone in GTK4 */
#  define  GDK_ROOT_WINDOW() \
      DefaultRootWindow(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()))
#  define  GDK_WINDOW_XWINDOW(w)        gdk_x11_surface_get_xid(w)
#  define  gdk_x11_drawable_get_xid(w)  gdk_x11_surface_get_xid(GDK_SURFACE(w))
#  define  gdk_window_get_screen(w)     gdk_surface_get_display(w)
#  define  gtk_widget_get_window(w)     gtk_native_get_surface(GTK_NATIVE(w))
#  define  GDK_WINDOW_XID(w)            gdk_x11_surface_get_xid(GDK_SURFACE(w))
/* color parsing (GdkColor is gone) */
static inline gboolean gdk_color_parse_string(const char *spec, GdkRGBA *color)
{
    return gdk_rgba_parse(color, spec);
}

#endif
