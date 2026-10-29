/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   preview.c for ObConf, the configuration tool for Openbox
   Copyright (c) 2007        Javeed Shaikh
   Copyright (c) 2007        Dana Jansens
   Copyright (C) 2026 Jerry Howell

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "theme.h"
#include "main.h"
#include "tree.h"

#include <string.h>

#include <obrender/theme.h>

#include <gdk/x11/gdkx.h>
#include <cairo-xlib.h>

#define PADDING 2 /* openbox does it :/ */
#define PREVIEW_MAX_DIMENSION 16384

/* Forwarded */

GdkPixbuf *preview_theme(const gchar *name, const gchar *titlelayout,
                         RrFont *active_window_font,
                         RrFont *inactive_window_font,
                         RrFont *menu_title_font,
                         RrFont *menu_item_font,
                         RrFont *osd_active_font,
                         RrFont *osd_inactive_font);

/* End forwarded */

/* Blit an obrender Pixmap into the composed preview pixbuf at (x, y).
 * The old GTK3 attempt assigned gdk_pixbuf_get_from_surface() straight
 * to pixbuf (leaking the canvas and returning only the last piece) and
 * sized the cairo surface with the destination x/y instead of w/h.
 * Correct sequence: temp piece + copy_area + unref. */
static void preview_blit(GdkPixbuf *pixbuf, Pixmap pmap,
                         gint w, gint h, gint x, gint y)
{
    GdkDisplay *display = gdk_display_get_default();
    Display *dpy;
    cairo_surface_t *surface;
    GdkPixbuf *piece;

    if (!GDK_IS_X11_DISPLAY(display) || !pixbuf || !pmap || w <= 0 || h <= 0)
        return;
    dpy = GDK_DISPLAY_XDISPLAY(display);

    /* Clamp against the canvas: degenerate theme metrics (e.g. an empty
     * title layout while typing) can otherwise address outside it. */
    if (x < 0 || y < 0 ||
        x > gdk_pixbuf_get_width(pixbuf) - w ||
        y > gdk_pixbuf_get_height(pixbuf) - h)
        return;
    surface = cairo_xlib_surface_create(dpy, pmap,
                                        DefaultVisual(dpy, DefaultScreen(dpy)),
                                        w, h);
    piece = gdk_pixbuf_get_from_surface(surface, 0, 0, w, h);
    cairo_surface_destroy(surface);
    if (!piece)
        return;
    gdk_pixbuf_copy_area(piece, 0, 0, w, h, pixbuf, x, y);
    g_object_unref(piece);
}

static void theme_pixmap_paint(RrAppearance *a, gint w, gint h)
{
    Pixmap out;
    if (!a || !a->inst || w <= 0 || h <= 0)
        return;
    out = RrPaintPixmap(a, w, h);
    if (out) XFreePixmap(RrDisplay(a->inst), out);
}

static guint32 rr_color_pixel(const RrColor *c)
{
    if (!c)
        return 0xff000000u;
    return ((guint32)RrColorRed(c) << 24) |
           ((guint32)RrColorGreen(c) << 16) |
           ((guint32)RrColorBlue(c) << 8) | 0xffu;
}

static gboolean valid_preview_metric(gint value)
{
    return value >= 0 && value <= PREVIEW_MAX_DIMENSION;
}

static gboolean valid_theme_metrics(const RrTheme *theme)
{
    return theme &&
           valid_preview_metric(theme->mbwidth) &&
           valid_preview_metric(theme->fbwidth) &&
           valid_preview_metric(theme->paddingx) &&
           valid_preview_metric(theme->paddingy) &&
           valid_preview_metric(theme->handle_height) &&
           valid_preview_metric(theme->title_height) &&
           valid_preview_metric(theme->button_size) &&
           valid_preview_metric(theme->label_height) &&
           valid_preview_metric(theme->grip_width) &&
           valid_preview_metric(theme->menu_title_height) &&
           valid_preview_metric(theme->cbwidthx) &&
           valid_preview_metric(theme->cbwidthy);
}

static gboolean valid_min_size(gint width, gint height)
{
    return valid_preview_metric(width) && valid_preview_metric(height);
}

static gboolean valid_window_appearances(RrTheme *theme)
{
    if (!theme || !theme->a_icon || !theme->a_focused_label ||
        !theme->a_unfocused_label || !theme->a_focused_handle ||
        !theme->a_unfocused_handle || !theme->a_focused_grip ||
        !theme->a_unfocused_grip)
        return FALSE;
#if RR_CHECK_VERSION(3, 5, 28)
    return theme->btn_desk && theme->btn_shade && theme->btn_iconify &&
           theme->btn_max && theme->btn_close &&
           theme->btn_desk->a_focused_unpressed &&
           theme->btn_desk->a_unfocused_unpressed &&
           theme->btn_shade->a_focused_unpressed &&
           theme->btn_shade->a_unfocused_unpressed &&
           theme->btn_iconify->a_focused_unpressed &&
           theme->btn_iconify->a_unfocused_unpressed &&
           theme->btn_max->a_focused_unpressed &&
           theme->btn_max->a_unfocused_unpressed &&
           theme->btn_close->a_focused_unpressed &&
           theme->btn_close->a_unfocused_unpressed;
#else
    return theme->a_focused_unpressed_desk &&
           theme->a_unfocused_unpressed_desk &&
           theme->a_focused_unpressed_shade &&
           theme->a_unfocused_unpressed_shade &&
           theme->a_focused_unpressed_iconify &&
           theme->a_unfocused_unpressed_iconify &&
           theme->a_focused_unpressed_max &&
           theme->a_unfocused_unpressed_max &&
           theme->a_focused_unpressed_close &&
           theme->a_unfocused_unpressed_close;
#endif
}

/* XXX: Make this more general */
static GdkPixbuf* preview_menu(RrTheme *theme)
{
    RrAppearance *title;
    RrAppearance *title_text;

    RrAppearance *menu;
    RrAppearance *background;

    RrAppearance *normal;
    RrAppearance *disabled;
    RrAppearance *selected;
    RrAppearance *bullet; /* for submenu */
    GdkPixbuf *pixbuf;

    /* width and height of the whole menu */
    gint width, height;
    gint x, y;
    gint title_h;
    gint tw, th;
    gint bw, bh;
    gint unused;

    if (!valid_theme_metrics(theme) || !theme->a_menu_title ||
        !theme->a_menu_text_title || !theme->a_menu_text_normal ||
        !theme->a_menu_text_disabled || !theme->a_menu_text_selected ||
        !theme->a_menu_bullet_normal || !theme->a_menu ||
        !theme->a_menu_normal || !theme->a_menu_selected)
        return NULL;

    /* set up appearances */
    title = theme->a_menu_title;

    title_text = theme->a_menu_text_title;
    title_text->surface.parent = title;
    title_text->texture[0].data.text.string = "Menu";

    normal = theme->a_menu_text_normal;
    normal->texture[0].data.text.string = "Normal";

    disabled = theme->a_menu_text_disabled;
    disabled->texture[0].data.text.string = "Disabled";

    selected = theme->a_menu_text_selected;
    selected->texture[0].data.text.string = "Selected";

    bullet = theme->a_menu_bullet_normal;

    /* determine window size */
    RrMinSize(normal, &width, &th);
    if (width < 0 || th < 0 || width > PREVIEW_MAX_DIMENSION ||
        th > PREVIEW_MAX_DIMENSION || theme->mbwidth < 0 ||
        theme->mbwidth > PREVIEW_MAX_DIMENSION)
        return NULL;
    width += th + PADDING; /* make space for the bullet */
    //height = th;

    width += 2*theme->mbwidth + 2*PADDING;

    /* get minimum title size */
    RrMinSize(title, &tw, &title_h);
    if (tw < 0 || title_h < 0 || tw > PREVIEW_MAX_DIMENSION ||
        title_h > PREVIEW_MAX_DIMENSION)
        return NULL;

    /* size of background behind each text line */
    bw = width - 2*theme->mbwidth;
    //title_h += 2*PADDING;
    title_h = theme->menu_title_height;
    if (title_h < 0 || title_h > PREVIEW_MAX_DIMENSION)
        return NULL;

    RrMinSize(normal, &unused, &th);
    if (th < 0 || th > PREVIEW_MAX_DIMENSION)
        return NULL;
    bh = th + 2*PADDING;

    height = title_h + 3*bh + 3*theme->mbwidth;

    if (width <= 0 || height <= 0 || width > PREVIEW_MAX_DIMENSION ||
        height > PREVIEW_MAX_DIMENSION || bw <= 0 || bh <= 0)
        return NULL;

    /* set border */
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);
    if (!pixbuf)
        return NULL;
    gdk_pixbuf_fill(pixbuf, rr_color_pixel(theme->menu_border_color));

    /* draw title */
    x = y = theme->mbwidth;
    theme_pixmap_paint(title, bw, title_h);

    /* draw title text */
    title_text->surface.parentx = 0;
    title_text->surface.parenty = 0;

    theme_pixmap_paint(title_text, bw, title_h);

    preview_blit(pixbuf, title_text->pixmap, bw, title_h, x, y);
    /* menu appears after title */
    y += theme->mbwidth + title_h;

    /* fill in menu appearance, used as the parent to every menu item's bg */
    menu = theme->a_menu;
    th = height - 3*theme->mbwidth - title_h;
    theme_pixmap_paint(menu, bw, th);

    preview_blit(pixbuf, menu->pixmap, bw, th, x, y);

    /* fill in background appearance, used as the parent to text items */
    background = theme->a_menu_normal;
    background->surface.parent = menu;
    background->surface.parentx = 0;
    background->surface.parenty = 0;

    /* draw background for normal entry */
    theme_pixmap_paint(background, bw, bh);
    preview_blit(pixbuf, background->pixmap, bw, bh, x, y);

    /* draw normal entry */
    normal->surface.parent = background;
    normal->surface.parentx = PADDING;
    normal->surface.parenty = PADDING;
    x += PADDING;
    y += PADDING;
    RrMinSize(normal, &tw, &th);
    if (!valid_min_size(tw, th)) {
        g_object_unref(pixbuf);
        return NULL;
    }
    theme_pixmap_paint(normal, tw, th);
    preview_blit(pixbuf, normal->pixmap, tw, th, x, y);

    /* draw bullet */
    RrMinSize(normal, &tw, &th);
    if (!valid_min_size(tw, th)) {
        g_object_unref(pixbuf);
        return NULL;
    }
    bullet->surface.parent = background;
    bullet->surface.parentx = bw - th;
    bullet->surface.parenty = PADDING;
    theme_pixmap_paint(bullet, th, th);
    preview_blit(pixbuf, bullet->pixmap, th, th,
                 width - theme->mbwidth - th, y);
    y += th + 2*PADDING;

    /* draw background for disabled entry */
    background->surface.parenty = bh;
    theme_pixmap_paint(background, bw, bh);
    preview_blit(pixbuf, background->pixmap, bw, bh,
                 x - PADDING, y - PADDING);
    /* draw disabled entry */
    RrMinSize(disabled, &tw, &th);
    if (!valid_min_size(tw, th)) {
        g_object_unref(pixbuf);
        return NULL;
    }
    disabled->surface.parent = background;
    disabled->surface.parentx = PADDING;
    disabled->surface.parenty = PADDING;
    theme_pixmap_paint(disabled, tw, th);
    preview_blit(pixbuf, disabled->pixmap, tw, th, x, y);
    y += th + 2*PADDING;

    /* draw background for selected entry */
    background = theme->a_menu_selected;
    background->surface.parent = menu;
    background->surface.parentx = 2*bh;

    theme_pixmap_paint(background, bw, bh);
    preview_blit(pixbuf, background->pixmap, bw, bh,
                 x - PADDING, y - PADDING);

    /* draw selected entry */
    RrMinSize(selected, &tw, &th);
    if (!valid_min_size(tw, th)) {
        g_object_unref(pixbuf);
        return NULL;
    }
    selected->surface.parent = background;
    selected->surface.parentx = PADDING;
    selected->surface.parenty = PADDING;
    theme_pixmap_paint(selected, tw, th);
    preview_blit(pixbuf, selected->pixmap, tw, th, x, y);
    return pixbuf;
}

static GdkPixbuf* preview_window(RrTheme *theme, const gchar *titlelayout,
                                 gboolean focus, gint width, gint height)
{
    RrAppearance *title;
    RrAppearance *handle;
    RrAppearance *a;
    GdkPixbuf *pixbuf = NULL;
    GdkPixbuf *scratch;

    gint w, label_w, h, x, y;

    const gchar *layout;

    if (!theme || !titlelayout || strlen(titlelayout) > 64 ||
        !valid_theme_metrics(theme) || !valid_window_appearances(theme))
        return NULL;
    title = focus ? theme->a_focused_title : theme->a_unfocused_title;
    if (!title)
        return NULL;

    /* set border */
    if (width <= 0 || height <= 0)
        return NULL;
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
    if (!pixbuf)
        return NULL;
    gdk_pixbuf_fill(pixbuf,
                    rr_color_pixel(focus ?
                                   theme->frame_focused_border_color :
                                   theme->frame_unfocused_border_color));

    /* title */
    w = width - 2*theme->fbwidth;
    h = theme->title_height;
    theme_pixmap_paint(title, w, h);

    x = y = theme->fbwidth;
    preview_blit(pixbuf, title->pixmap, w, h, x, y);
    /* calculate label width */
    label_w = width - (theme->paddingx + theme->fbwidth + 1) * 2;

    for (layout = titlelayout; *layout; layout++) {
        switch (*layout) {
        case 'N':
            label_w -= theme->button_size + 2 + theme->paddingx + 1;
            break;
        case 'D':
        case 'S':
        case 'I':
        case 'M':
        case 'C':
            label_w -= theme->button_size + theme->paddingx + 1;
            break;
        default:
            break;
        }
    }

    x = theme->paddingx + theme->fbwidth + 1;
    y += theme->paddingy;
    for (layout = titlelayout; *layout; layout++) {
        /* icon */
        if (*layout == 'N') {
            a = theme->a_icon;
            /* set default icon */
            a->texture[0].type = RR_TEXTURE_RGBA;
            a->texture[0].data.rgba.width = 48;
            a->texture[0].data.rgba.height = 48;
            a->texture[0].data.rgba.alpha = 0xff;
            a->texture[0].data.rgba.data = theme->def_win_icon;

            a->surface.parent = title;
            a->surface.parentx = x - theme->fbwidth;
            a->surface.parenty = theme->paddingy;

            w = h = theme->button_size + 2;

            theme_pixmap_paint(a, w, h);
    preview_blit(pixbuf, a->pixmap, w, h, x, y);

            x += theme->button_size + 2 + theme->paddingx + 1;
        } else if (*layout == 'L') { /* label */
            a = focus ? theme->a_focused_label : theme->a_unfocused_label;
            a->texture[0].data.text.string = focus ? "Active" : "Inactive";

            a->surface.parent = title;
            a->surface.parentx = x - theme->fbwidth;
            a->surface.parenty = theme->paddingy;
            w = label_w;
            h = theme->label_height;

            theme_pixmap_paint(a, w, h);
    preview_blit(pixbuf, a->pixmap, w, h, x, y);

            x += w + theme->paddingx + 1;
        } else {
            /* buttons */
            switch (*layout) {
            case 'D':
                a = focus ?
#if RR_CHECK_VERSION(3, 5, 28)
                    theme->btn_desk->a_focused_unpressed :
                    theme->btn_desk->a_unfocused_unpressed;
#else
                    theme->a_focused_unpressed_desk :
                    theme->a_unfocused_unpressed_desk;
#endif
                break;
            case 'S':
                a = focus ?
#if RR_CHECK_VERSION(3, 5, 28)
                    theme->btn_shade->a_focused_unpressed :
                    theme->btn_shade->a_unfocused_unpressed;
#else
                    theme->a_focused_unpressed_shade :
                    theme->a_unfocused_unpressed_shade;
#endif
                break;
            case 'I':
                a = focus ?
#if RR_CHECK_VERSION(3, 5, 28)
                    theme->btn_iconify->a_focused_unpressed :
                    theme->btn_iconify->a_unfocused_unpressed;
#else
                    theme->a_focused_unpressed_iconify :
                    theme->a_unfocused_unpressed_iconify;
#endif
                break;
            case 'M':
                a = focus ?
#if RR_CHECK_VERSION(3, 5, 28)
                    theme->btn_max->a_focused_unpressed :
                    theme->btn_max->a_unfocused_unpressed;
#else
                    theme->a_focused_unpressed_max :
                    theme->a_unfocused_unpressed_max;
#endif
                break;
            case 'C':
                a = focus ?
#if RR_CHECK_VERSION(3, 5, 28)
                    theme->btn_close->a_focused_unpressed :
                    theme->btn_close->a_unfocused_unpressed;
#else
                    theme->a_focused_unpressed_close :
                    theme->a_unfocused_unpressed_close;
#endif
                break;
            default:
                continue;
            }

            a->surface.parent = title;
            a->surface.parentx = x - theme->fbwidth;
            a->surface.parenty = theme->paddingy + 1;

            w = theme->button_size;
            h = theme->button_size;

            theme_pixmap_paint(a, w, h);
    /* y + 1: buttons are centered wrt the label */
    preview_blit(pixbuf, a->pixmap, w, h, x, y + 1);

            x += theme->button_size + theme->paddingx + 1;
        }
    }

    if (theme->handle_height) {
        /* handle */
        handle = focus ? theme->a_focused_handle : theme->a_unfocused_handle;
        x = 2*theme->fbwidth + theme->grip_width;
        y = height - theme->fbwidth - theme->handle_height;
        w = width - 4*theme->fbwidth - 2*theme->grip_width;
        h = theme->handle_height;

        theme_pixmap_paint(handle, w, h);
    preview_blit(pixbuf, handle->pixmap, w, h, x, y);

        /* openbox handles this drawing stuff differently (it fills the bottom
         * of the window with the handle), so it avoids this bug where
         * parentrelative grips are not fully filled. i'm avoiding it slightly
         * differently. */

        theme_pixmap_paint(handle, width, h);

        /* grips */
        a = focus ? theme->a_focused_grip : theme->a_unfocused_grip;
        a->surface.parent = handle;

        x = theme->fbwidth;
        /* same y and h as handle */
        w = theme->grip_width;

        theme_pixmap_paint(a, w, h);
    preview_blit(pixbuf, a->pixmap, w, h, x, y);

        /* right grip */
        x = width - theme->fbwidth - theme->grip_width;
    preview_blit(pixbuf, a->pixmap, w, h, x, y);
    }

    /* title separator colour */
    x = theme->fbwidth;
    y = theme->fbwidth + theme->title_height;
    w = width - 2*theme->fbwidth;
    h = theme->fbwidth;

    if (h > 0)
    {
        scratch = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
        if (scratch) {
            gdk_pixbuf_fill(scratch, rr_color_pixel(focus ?
                                                    theme->title_separator_focused_color :
                                                    theme->title_separator_unfocused_color));
            gdk_pixbuf_copy_area(scratch, 0, 0, w, h, pixbuf, x, y);
            g_object_unref(scratch);
        }
    }

    /* retarded way of adding client colour */
    x = theme->fbwidth;
    y = theme->title_height + 2*theme->fbwidth;
    w = width - 2*theme->fbwidth;
    h = height - theme->title_height - 3*theme->fbwidth -
        (theme->handle_height ? (theme->fbwidth + theme->handle_height) : 0);

    if (w > 0 && h > 0) {
        scratch = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
        if (!scratch) {
            g_object_unref(pixbuf);
            return NULL;
        }
        gdk_pixbuf_fill(scratch, rr_color_pixel(focus ?
                                                theme->cb_focused_color :
                                                theme->cb_unfocused_color));

        gdk_pixbuf_copy_area(scratch, 0, 0, w, h, pixbuf, x, y);

        /* clear (no alpha!) the area where the client resides */
        gdk_pixbuf_fill(scratch, 0xffffffff);
        if (w > 2*theme->cbwidthx && h > 2*theme->cbwidthy)
            gdk_pixbuf_copy_area(scratch, 0, 0,
                                 w - 2*theme->cbwidthx,
                                 h - 2*theme->cbwidthy,
                                 pixbuf,
                                 x + theme->cbwidthx,
                                 y + theme->cbwidthy);
        g_object_unref(scratch);
    }

    return pixbuf;
}

static gint theme_label_width(RrTheme *theme, gboolean active)
{
    RrAppearance *label;

    if (!theme)
        return 0;
    if (active) {
        label = theme->a_focused_label;
        if (label)
            label->texture[0].data.text.string = "Active";
    } else {
        label = theme->a_unfocused_label;
        if (label)
            label->texture[0].data.text.string = "Inactive";
    }
    return label ? RrMinWidth(label) : 0;
}

static gint theme_window_min_width(RrTheme *theme, const gchar *titlelayout)
{
    gsize length;
    gint64 width;
    gint label_width;

    if (!theme || !titlelayout)
        return 0;
    length = strlen(titlelayout);
    if (length > 64)
        return 0;
    width = (gint64)2 * theme->fbwidth +
            ((gint64)length + 3) * ((gint64)theme->paddingx + 1);
    if (g_strrstr(titlelayout, "L")) {
        if (length == 0)
            return 0;
        length--;
        label_width = MAX(theme_label_width(theme, TRUE),
                           theme_label_width(theme, FALSE));
        if (label_width <= 0)
            return 0;
        width += label_width;
    }
    width += (gint64)theme->button_size * (gint64)length;
    if (width <= 0 || width > G_MAXINT)
        return 0;
    return (gint)width;
}

GdkPixbuf *preview_theme(const gchar *name, const gchar *titlelayout,
                         RrFont *active_window_font,
                         RrFont *inactive_window_font,
                         RrFont *menu_title_font,
                         RrFont *menu_item_font,
                         RrFont *osd_active_font,
                         RrFont *osd_inactive_font)
{
    GdkPixbuf *preview = NULL;
    GdkPixbuf *menu;
    GdkPixbuf *window;
    gint window_w;
    gint menu_w;
    gint menu_h;
    gint preview_w, preview_h;
    gint64 total_w, total_h;
    RrTheme *theme;

    if (!name || !titlelayout || !rrinst || strlen(titlelayout) > 64)
        return NULL;
    theme = RrThemeNew(rrinst, name, FALSE,
                       active_window_font, inactive_window_font,
                       menu_title_font, menu_item_font,
                       osd_active_font, osd_inactive_font);
    if (!theme)
        return NULL;

    menu = preview_menu(theme);
    if (!menu) {
        RrThemeFree(theme);
        return NULL;
    }
    window_w = theme_window_min_width(theme, titlelayout);
    menu_w = gdk_pixbuf_get_width(menu);
    menu_h = gdk_pixbuf_get_height(menu);
    total_w = (gint64)MAX(window_w, menu_w) + 20;
    total_h = (gint64)menu_h +
              2 * ((gint64)theme->title_height + 5) + 1;
    if (theme->title_height < 0 || menu_w <= 0 || menu_h <= 0 ||
        total_w <= 0 || total_h <= 0 || total_w > PREVIEW_MAX_DIMENSION ||
        total_h > PREVIEW_MAX_DIMENSION) {
        g_object_unref(menu);
        RrThemeFree(theme);
        return NULL;
    }
    preview_w = (gint)total_w;
    preview_h = (gint)total_h;
    if (!window_w)
        window_w = menu_w;
    preview = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
                             preview_w, preview_h);
    if (!preview) {
        g_object_unref(menu);
        RrThemeFree(theme);
        return NULL;
    }
    gdk_pixbuf_fill(preview, 0);

    window = preview_window(theme, titlelayout, FALSE, window_w, menu_h);
    if (window) {
        gdk_pixbuf_copy_area(window, 0, 0, window_w, menu_h,
                             preview, 20, 0);
        g_object_unref(window);
    }
    window = preview_window(theme, titlelayout, TRUE, window_w, menu_h);
    if (window) {
        gdk_pixbuf_copy_area(window, 0, 0, window_w, menu_h,
                             preview, 10, theme->title_height + 5);
        g_object_unref(window);
    }
    gdk_pixbuf_copy_area(menu, 0, 0, menu_w, menu_h,
                         preview, 0, 2 * (theme->title_height + 5));
    g_object_unref(menu);
    RrThemeFree(theme);
    return preview;
}
