/*
 *      command.h - xrandr command builder for LuxeRandR
 *
 *      Extracted from lxrandr.c so the command-building logic can be
 *      unit tested without starting the GTK application.
 *
 *      Copyright (C) 2008 Hong Jen Yee(PCMan) <pcman.tw@gmail.com>
 *      Copyright (C) 2011 Julien Lavergne <julien.lavergne@gmail.com>
 *      Copyright (C) 2014 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
 *      Copyright (C) 2026 Jerry Howell
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

#ifndef LUXERANDR_COMMAND_H
#define LUXERANDR_COMMAND_H

#include <gtk/gtk.h>

typedef enum
{
    PLACEMENT_DEFAULT,
    PLACEMENT_RIGHT,
    PLACEMENT_ABOVE,
    PLACEMENT_LEFT,
    PLACEMENT_BELOW
} MonitorPlacement;

typedef struct _Monitor
{
    char* name;
    GSList* mode_lines;
    gint active_mode;
    gint active_rate;
    gint pref_mode;
    gint pref_rate;
    gint try_mode;
    gint try_rate;
    MonitorPlacement placement;
    MonitorPlacement try_placement;
    gint x, y;
    gboolean has_position;

    GtkCheckButton* enable;
    GtkComboBoxText* pos_combo;
    GtkComboBoxText* res_combo;
    GtkComboBoxText* rate_combo;
} Monitor;

/* Live monitor list (in xrandr output order) and the built-in laptop panel
 * (LVDS/eDP/DSI), or NULL when there is none. Owned by this module; the GUI
 * in luxerandr.c only borrows the entries. */
extern GSList* monitors;
extern Monitor* LVDS;

/* Append one connector/mode/rate value as a safely quoted token. Values may
 * contain spaces and quotes but never controls or a leading option marker. */
gboolean append_xrandr_token(GString *cmd, const char *token);

/* Build the "xrandr --output ..." command for the current try_* values, or
 * NULL when a connector, mode, or rate name is unsafe or unavailable. */
GString* get_command_xrandr_info(void);
GString* get_command_xrandr_restore(void);
/* Desktop Entry Exec serialization is separate from guard command parsing. */
GString* get_desktop_exec_xrandr_info(void);

/* Release the monitor list and its mode lines; resets monitors/LVDS. */
void free_monitors(void);

#endif /* LUXERANDR_COMMAND_H */
