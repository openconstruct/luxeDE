/*
 *      desktop-gtk4.h - legacy wallpaper command forwarding to Frame.
 *
 *      Copyright 2026 Jerry Howell <jerr@luxe.desktop>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifndef __DESKTOP_GTK4_H__
#define __DESKTOP_GTK4_H__ 1

#include <glib.h>

G_BEGIN_DECLS

void pcmgr_desktop_preferences(void);
void pcmgr_desktop_manager_init(const char *profile_name);
gboolean pcmgr_desktop_running(void);
void pcmgr_desktop_manager_finalize(void);
void pcmgr_desktop_reload(void);
void pcmgr_set_wallpaper(const char *file, const char *mode_name);

G_END_DECLS

#endif /* __DESKTOP_GTK4_H__ */
