/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   appearance.c for ObConf, the configuration tool for Openbox
   Copyright (c) 2003-2007   Dana Jansens
   Copyright (c) 2003        Tim Riley

   Copyright (C) 2010        Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
   Copyright (C) 2026        Jerry Howell

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

#include "main.h"
#include "tree.h"
#include "preview_update.h"
#include "appearance.h"
#include <ctype.h> /* toupper */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static gboolean mapping = FALSE;

static RrFont *read_font(GtkFontButton *w, const gchar *place);
static RrFont *write_font(GtkFontButton *w, const gchar *place);

/* Forwarded */

void on_title_layout_changed(GtkEditable *w, gpointer data);
void on_font_active_font_set(GtkFontButton *w, gpointer data);
void on_font_inactive_font_set(GtkFontButton *w, gpointer data);
void on_font_menu_header_font_set(GtkFontButton *w, gpointer data);
void on_font_menu_item_font_set(GtkFontButton *w, gpointer data);
void on_font_active_display_font_set(GtkFontButton *w, gpointer data);
void on_font_inactive_display_font_set(GtkFontButton *w, gpointer data);

/* End of forwarded */

void appearance_setup_tab(void)
{
    GtkWidget *w;
    gchar *layout;
    RrFont *f;

    mapping = TRUE;
/*
    w = get_widget("window_border");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),
                                 tree_get_bool("theme/keepBorder", TRUE));

    w = get_widget("animate_iconify");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),
                                 tree_get_bool("theme/animateIconify", TRUE));
*/
    w = get_widget("title_layout");
    layout = tree_get_string("theme/titleLayout", "NLIMC");
    gtk_editable_set_text(GTK_EDITABLE(w), layout);
    preview_update_set_title_layout(layout);
    g_free(layout);

    w = get_widget("font_active");
    f = read_font(GTK_FONT_BUTTON(w), "ActiveWindow");
    preview_update_set_active_font(f);

    w = get_widget("font_inactive");
    f = read_font(GTK_FONT_BUTTON(w), "InactiveWindow");
    preview_update_set_inactive_font(f);

    w = get_widget("font_menu_header");
    f = read_font(GTK_FONT_BUTTON(w), "MenuHeader");
    preview_update_set_menu_header_font(f);

    w = get_widget("font_menu_item");
    f = read_font(GTK_FONT_BUTTON(w), "MenuItem");
    preview_update_set_menu_item_font(f);

    w = get_widget("font_active_display");
    if (!(f = read_font(GTK_FONT_BUTTON(w), "ActiveOnScreenDisplay"))) {
        f = read_font(GTK_FONT_BUTTON(w), "OnScreenDisplay");
        tree_delete_node("theme/font:place=OnScreenDisplay");
    }
    preview_update_set_osd_active_font(f);

    w = get_widget("font_inactive_display");
    f = read_font(GTK_FONT_BUTTON(w), "InactiveOnScreenDisplay");
    preview_update_set_osd_inactive_font(f);

    mapping = FALSE;
}

void on_title_layout_changed(GtkEditable *w, gpointer data)
{
    gchar *layout;
    gchar *read, *write;
    guint seen = 0;
    const gchar *tokens = "NDSLIMC";

    if (mapping) return;

    (void)data;
    layout = g_strdup(gtk_editable_get_text(w));

    write = layout;
    for (read = layout; *read; ++read) {
        gchar upper = g_ascii_toupper(*read);
        const gchar *token = strchr(tokens, upper);
        if (token) {
            guint bit = 1u << (token - tokens);
            if (!(seen & bit)) {
                *write++ = upper;
                seen |= bit;
            }
        }
    }
    *write = '\0';

    /* Only write back when sanitizing actually dropped something,
     * and block re-entrant "changed" emissions while doing so:
     * set_textDelete+insert phases emit mid-mutation states that
     * would otherwise ping-pong the handler forever. */
    if (g_strcmp0(layout, gtk_editable_get_text(w)) != 0) {
        mapping = TRUE;
        gtk_editable_set_text(w, layout);
        mapping = FALSE;
    }
    tree_set_string("theme/titleLayout", layout);
    preview_update_set_title_layout(layout);

    g_free(layout);
}

void on_font_active_font_set(GtkFontButton *w, gpointer data)
{
    (void)data;
    if (mapping) return;

    preview_update_set_active_font(write_font(w, "ActiveWindow"));
}

void on_font_inactive_font_set(GtkFontButton *w, gpointer data)
{
    (void)data;
    if (mapping) return;

    preview_update_set_inactive_font(write_font(w, "InactiveWindow"));
}

void on_font_menu_header_font_set(GtkFontButton *w, gpointer data)
{
    (void)data;
    if (mapping) return;

    preview_update_set_menu_header_font(write_font(w, "MenuHeader"));
}

void on_font_menu_item_font_set(GtkFontButton *w, gpointer data)
{
    (void)data;
    if (mapping) return;

    preview_update_set_menu_item_font(write_font(w, "MenuItem"));
}

void on_font_active_display_font_set(GtkFontButton *w, gpointer data)
{
    (void)data;
    if (mapping) return;
#if RR_CHECK_VERSION(3, 5, 29)
    preview_update_set_osd_active_font(write_font(w, "ActiveOnScreenDisplay"));
#else /* for older versions of openbox */
    preview_update_set_osd_active_font(write_font(w, "OnScreenDisplay"));
#endif
}

void on_font_inactive_display_font_set(GtkFontButton *w, gpointer data)
{
    (void)data;
    if (mapping) return;

    preview_update_set_osd_inactive_font(write_font(w, "InactiveOnScreenDisplay"));
}

static gboolean valid_font_size(const gchar *size, gint *value)
{
    gchar *end = NULL;
    gint64 parsed;
    if (!size || !*size || !g_ascii_isdigit(size[0]))
        return FALSE;
    errno = 0;
    parsed = g_ascii_strtoll(size, &end, 10);
    if (errno == ERANGE || end == size || *end != '\0' || parsed < 1 || parsed > 256)
        return FALSE;
    if (value)
        *value = (gint)parsed;
    return TRUE;
}

static RrFont *read_font(GtkFontButton *w, const gchar *place)
{
    RrFont *font;
    gchar *fontstring, *node;
    gchar *name, **names;
    gchar *size;
    gchar *weight;
    gchar *slant;

    RrFontWeight rr_weight = RR_FONTWEIGHT_NORMAL;
    RrFontSlant rr_slant = RR_FONTSLANT_NORMAL;

    mapping = TRUE;

    node = g_strdup_printf("theme/font:place=%s/name", place);
    name = tree_get_string(node, "Sans");
    g_free(node);
    if (!name || !*name || strlen(name) > 256 ||
        !g_utf8_validate(name, -1, NULL)) {
        g_clear_pointer(&name, g_free);
        name = g_strdup("Sans");
    }

    node = g_strdup_printf("theme/font:place=%s/size", place);
    size = tree_get_string(node, "8");
    g_free(node);
    {
        gint parsed_size;
        if (!valid_font_size(size, &parsed_size)) {
            g_free(size);
            size = g_strdup("8");
        }
    }

    node = g_strdup_printf("theme/font:place=%s/weight", place);
    weight = tree_get_string(node, "");
    g_free(node);
    if (!weight)
        weight = g_strdup("");

    node = g_strdup_printf("theme/font:place=%s/slant", place);
    slant = tree_get_string(node, "");
    g_free(node);
    if (!slant)
        slant = g_strdup("");

    /* get only the first font in the string */
    names = g_strsplit(name, ",", 0);
    g_free(name);
    name = g_strdup(names[0]);
    g_strfreev(names);

    /* don't use "normal" in the gtk string */
    if (!g_ascii_strcasecmp(weight, "normal")) {
        g_free(weight); weight = g_strdup("");
    }
    if (!g_ascii_strcasecmp(slant, "normal")) {
        g_free(slant); slant = g_strdup("");
    }

    fontstring = g_strdup_printf("%s %s %s %s", name, weight, slant, size);
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(w), fontstring);

    if (!g_ascii_strcasecmp(weight, "Bold")) rr_weight = RR_FONTWEIGHT_BOLD;
    if (!g_ascii_strcasecmp(slant, "Italic")) rr_slant = RR_FONTSLANT_ITALIC;
    if (!g_ascii_strcasecmp(slant, "Oblique")) rr_slant = RR_FONTSLANT_OBLIQUE;

    {
        gint parsed_size = 8;
        valid_font_size(size, &parsed_size);
        font = RrFontOpen(rrinst, name, parsed_size, rr_weight, rr_slant);
    }
    g_free(fontstring);
    g_free(slant);
    g_free(weight);
    g_free(size);
    g_free(name);

    mapping = FALSE;

    return font;
}

static RrFont *write_font(GtkFontButton *w, const gchar *place)
{
    gchar *c;
    gchar *font, *node;
    const gchar *size = NULL;
    const gchar *bold = NULL;
    const gchar *italic = NULL;
    gint parsed_size;

    RrFontWeight weight = RR_FONTWEIGHT_NORMAL;
    RrFontSlant slant = RR_FONTSLANT_NORMAL;

    if (mapping) return NULL;

    font = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(w));
    if (!font)
        return NULL;
    while ((c = strrchr(font, ' '))) {
        if (!bold && !italic && !size && valid_font_size(c + 1, NULL))
            size = c + 1;
        else if (!bold && !italic && !g_ascii_strcasecmp(c+1, "italic"))
            italic = c+1;
        else if (!bold && !g_ascii_strcasecmp(c+1, "bold"))
            bold = c+1;
        else
            break;
        *c = '\0';
    }
    if (!size) {
        g_free(font);
        return NULL;
    }
    if (!bold) bold = "Normal";
    if (!italic) italic = "Normal";
    if (!valid_font_size(size, &parsed_size) || !*font ||
        strlen(font) > 256 || !g_utf8_validate(font, -1, NULL)) {
        g_free(font);
        return NULL;
    }

    node = g_strdup_printf("theme/font:place=%s/name", place);
    tree_set_string(node, font);
    g_free(node);

    node = g_strdup_printf("theme/font:place=%s/size", place);
    tree_set_string(node, size);
    g_free(node);

    node = g_strdup_printf("theme/font:place=%s/weight", place);
    tree_set_string(node, bold);
    g_free(node);

    node = g_strdup_printf("theme/font:place=%s/slant", place);
    tree_set_string(node, italic);
    g_free(node);

    if (!g_ascii_strcasecmp(bold, "Bold")) weight = RR_FONTWEIGHT_BOLD;
    if (!g_ascii_strcasecmp(italic, "Italic")) slant = RR_FONTSLANT_ITALIC;
    if (!g_ascii_strcasecmp(italic, "Oblique")) slant = RR_FONTSLANT_OBLIQUE;

    {
        RrFont *f = RrFontOpen(rrinst, font, parsed_size, weight, slant);
        g_free(font);
        return f;
    }
}
