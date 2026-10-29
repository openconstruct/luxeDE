/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   desktops.c for ObConf, the configuration tool for Openbox
   Copyright (c) 2026        Jerry Howell <jerr@luxe.desktop>

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

/* Desktop changes are staged in the XML document. Apply writes the file
 * first, then updates Openbox's live EWMH desktop state. */
#include "main.h"
#include "tree.h"
#include "desktops.h"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <glib/gi18n-lib.h>
#include <libxml/tree.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>

#define DESKTOPS_MAX 36
#define DESKTOP_NAME_MAX 255
static gboolean mapping = FALSE;
static gboolean desktops_dirty = FALSE;
static GPtrArray *desktop_entries;

static gboolean valid_desktop_name(const gchar *name)
{
    return name && g_utf8_validate(name, -1, NULL) &&
           g_utf8_strlen(name, -1) <= DESKTOP_NAME_MAX;
}

gboolean desktops_validate(void)
{
    if (!desktop_entries) return TRUE;
    for (guint i = 0; i < desktop_entries->len; ++i) {
        const gchar *name = gtk_editable_get_text(
            GTK_EDITABLE(g_ptr_array_index(desktop_entries, i)));
        if (!valid_desktop_name(name)) {
            obconf_error(_("Desktop names must be valid UTF-8 and at most 255 characters."), FALSE);
            return FALSE;
        }
    }
    return TRUE;
}

static void desktops_write_names(void)
{
    g_autoptr(GPtrArray) names = NULL;
    xmlNodePtr n;

    if (!desktop_entries || !xml_i)
        return;
    /* An invalid edit is still unsaved work and must not clear Apply/Close. */
    obconf_set_dirty();
    names = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < desktop_entries->len; ++i) {
        const gchar *name = gtk_editable_get_text(
            GTK_EDITABLE(g_ptr_array_index(desktop_entries, i)));
        if (!valid_desktop_name(name)) {
            return;
        }
        g_ptr_array_add(names, g_strdup(name));
    }
    n = tree_ensure_node("desktops/names", NULL);
    if (!n)
        return;
    while (n->children) {
        xmlNodePtr child = n->children;
        xmlUnlinkNode(child);
        xmlFreeNode(child);
    }
    for (guint i = 0; i < names->len; ++i)
        xmlNewTextChild(n, NULL, BAD_CAST "name",
                        BAD_CAST g_ptr_array_index(names, i));
    tree_set_int("desktops/number", (gint)names->len);
    desktops_dirty = TRUE;
}

static void on_desktop_name_changed(GtkEditable *entry, gpointer data)
{
    (void)entry; (void)data;
    if (!mapping) desktops_write_names();
}

static void desktop_add_row(const gchar *name)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gchar *number = g_strdup_printf("%u", desktop_entries->len + 1);
    GtkWidget *label = gtk_label_new(number);
    GtkWidget *entry = gtk_entry_new();
    gchar *accessible = g_strdup_printf(_("Desktop %u name"), desktop_entries->len + 1);
    gtk_entry_set_max_length(GTK_ENTRY(entry), DESKTOP_NAME_MAX);
    gtk_widget_set_tooltip_text(entry, _("Up to 255 characters"));
    gtk_widget_set_size_request(label, 28, -1);
    gtk_label_set_xalign(GTK_LABEL(label), 1.0);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(entry), name);
    gtk_accessible_update_property(GTK_ACCESSIBLE(entry), GTK_ACCESSIBLE_PROPERTY_LABEL, accessible, -1);
    gtk_box_append(GTK_BOX(row), label);
    gtk_box_append(GTK_BOX(row), entry);
    gtk_box_append(GTK_BOX(get_widget("desktop_names")), row);
    g_ptr_array_add(desktop_entries, entry);
    g_signal_connect(entry, "changed", G_CALLBACK(on_desktop_name_changed), NULL);
    g_free(number); g_free(accessible);
}

static void desktops_sync_rows(guint count)
{
    while (desktop_entries->len > count) {
        GtkWidget *entry = g_ptr_array_index(desktop_entries, desktop_entries->len - 1);
        GtkWidget *row = gtk_widget_get_parent(entry);
        g_ptr_array_remove_index(desktop_entries, desktop_entries->len - 1);
        gtk_box_remove(GTK_BOX(get_widget("desktop_names")), row);
    }
    while (desktop_entries->len < count) {
        gchar *name = g_strdup_printf(_("desktop %u"), desktop_entries->len + 1);
        desktop_add_row(name);
        g_free(name);
    }
}

static void on_desktop_number_changed(GtkSpinButton *spin, gpointer data)
{
    (void)data;
    if (mapping) return;
    mapping = TRUE;
    desktops_sync_rows((guint)CLAMP(gtk_spin_button_get_value_as_int(spin),
                                   1, DESKTOPS_MAX));
    mapping = FALSE;
    desktops_write_names();
}

gboolean desktops_apply(void)
{
    g_autofree gchar *error_message = NULL;
    GdkDisplay *display;
    GString *names;
    Display *dpy;
    Window root;
    XEvent event = {0};

    if (!desktops_dirty || !desktop_entries || !desktop_entries->len)
        return TRUE;
    if (!desktops_validate())
        return FALSE;
    display = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(display)) {
        obconf_error(_("Desktop names require an X11 display."), FALSE);
        return FALSE;
    }
    dpy = GDK_DISPLAY_XDISPLAY(display);
    root = DefaultRootWindow(dpy);

    names = g_string_new(NULL);
    for (guint i = 0; i < desktop_entries->len; ++i) {
        const gchar *name = gtk_editable_get_text(
            GTK_EDITABLE(g_ptr_array_index(desktop_entries, i)));
        g_string_append(names, name);
        g_string_append_c(names, '\0');
    }
    if (names->len > G_MAXINT) {
        g_string_free(names, TRUE);
        error_message = g_strdup(_("The desktop names are too large."));
        obconf_error(error_message, FALSE);
        return FALSE;
    }

    gdk_x11_display_error_trap_push(display);
    event.xclient.type = ClientMessage;
    event.xclient.display = dpy;
    event.xclient.window = root;
    event.xclient.message_type = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)desktop_entries->len;
    XSendEvent(dpy, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
    XChangeProperty(dpy, root, XInternAtom(dpy, "_NET_DESKTOP_NAMES", False),
                    XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
                    (const unsigned char *)names->str, (int)names->len);
    XFlush(dpy);
    int xerror = gdk_x11_display_error_trap_pop(display);
    g_string_free(names, TRUE);
    if (xerror) {
        obconf_error(_("The desktop names could not be updated on the X11 display."), FALSE);
        return FALSE;
    }
    desktops_dirty = FALSE;
    return TRUE;
}

void desktops_cleanup(void)
{
    g_clear_pointer(&desktop_entries, g_ptr_array_unref);
    desktops_dirty = FALSE;
    mapping = FALSE;
}

void desktops_setup_tab(void)
{
    gint num = CLAMP(tree_get_int("desktops/number", 4), 1, DESKTOPS_MAX);
    xmlNodePtr n;

    desktops_cleanup();
    n = tree_find_node("desktops/names");
    desktop_entries = g_ptr_array_new();
    mapping = TRUE;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(get_widget("desktop_number")), num);
    for (xmlNodePtr c = n ? n->children : NULL;
         c && desktop_entries->len < (guint)num; c = c->next) {
        if (c->type != XML_ELEMENT_NODE || strcmp((const char *)c->name, "name")) continue;
        xmlChar *name = xmlNodeGetContent(c);
        gchar *fallback = g_strdup_printf(_("desktop %u"), desktop_entries->len + 1);
        desktop_add_row(name && valid_desktop_name((const gchar *)name)
                        ? (const gchar *)name : fallback);
        xmlFree(name); g_free(fallback);
    }
    desktops_sync_rows((guint)num);
    mapping = FALSE;
    desktops_dirty = FALSE;
    g_signal_connect(get_widget("desktop_number"), "value-changed", G_CALLBACK(on_desktop_number_changed), NULL);
}
