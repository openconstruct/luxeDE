/*
 *      utils.c
 *
 *      Copyright 2008 PCMan <pcman.tw@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include "utils.h"
#include "../luxe-dialogs.h"

extern GtkWidget *main_window;

void show_error( const char* format, ... )
{
    char* msg;
    va_list ap;
    va_start(ap, format);
    msg = g_strdup_vprintf( format, ap );
    va_end(ap);

    luxe_show_error( GTK_WINDOW (main_window), msg, NULL );
    g_free( msg );
}

static void on_confirm_response( gint button_index, gpointer data )
{
    TaskConfirm *c = data;
    c->on_answer( button_index == 1, c->user_data );
    g_free( c );
}

void confirm( const char* question, LuxeConfirmCallback on_answer, gpointer user_data )
{
    TaskConfirm *c = g_new( TaskConfirm, 1 );
    c->on_answer = on_answer;
    c->user_data = user_data;
    luxe_confirm( GTK_WINDOW (main_window), question, NULL, TRUE, on_confirm_response, c );
}

char* size_to_string(char *buf, guint64 size)
{
    static const char *units[] = { "KB", "MB", "GB", "TB", "PB", "EB" };
    if (size < 1024) {
        g_snprintf(buf, 32, "%u %s", (guint)size, size == 1 ? "Byte" : "Bytes");
    } else {
        gdouble value = size / 1024.0;
        guint unit = 0;
        while (value >= 1024 && unit + 1 < G_N_ELEMENTS(units)) {
            value /= 1024;
            unit++;
        }
        g_snprintf(buf, 32, "%.1f %s", value, units[unit]);
    }
    return buf;
}
