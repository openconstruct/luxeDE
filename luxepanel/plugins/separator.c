/*
 * Copyright (C) 2006-2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2009 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2014 Andriy Grytsenko <andrej@rep.kiev.ua>
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

#include "plugin.h"

#include <glib/gi18n.h>

/* Plugin constructor. */
static GtkWidget *separator_constructor(LuxePanel *panel, config_setting_t *settings)
{
    GtkWidget *instance, *sep;

    /* GtkEventBox is gone; a plain Box wrapper is enough since click
     * handling is installed by the plugin core. */
    instance = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    /* Allocate separator as a child of top level. */
    sep = panel_separator_new(panel);
    g_object_set_data(G_OBJECT(sep), "vertical",
                      GINT_TO_POINTER(panel_get_orientation(panel) == GTK_ORIENTATION_VERTICAL));
    gtk_box_append(GTK_BOX(instance), sep);
    gtk_widget_show(sep);

    /* Show the widget and return. */
    return instance;
}

/* Callback when panel configuration changes. */
static void separator_reconfigure(LuxePanel *panel, GtkWidget *instance)
{
    /* GtkSeparator orientation is not queryable; it is recorded in qdata. */
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gtk_widget_get_first_child(instance)), "vertical"))
        == (panel_get_orientation(panel) == GTK_ORIENTATION_VERTICAL))
        return;

    /* If the orientation changed, recreate the separator. */
    {
        GtkWidget * sep = panel_separator_new(panel);
        g_object_set_data(G_OBJECT(sep), "vertical",
                          GINT_TO_POINTER(panel_get_orientation(panel) == GTK_ORIENTATION_VERTICAL));
        gtk_widget_show(sep);
        gtk_box_remove(GTK_BOX(instance), gtk_widget_get_first_child(instance));
        gtk_box_append(GTK_BOX(instance), sep);
    }
}

/* Plugin descriptor. */
LuxePanelPluginInit luxepanel_static_plugin_separator = {
    .name = N_("Separator"),
    .description = N_("Add a separator to the panel"),

    .new_instance = separator_constructor,
    .reconfigure = separator_reconfigure
};
