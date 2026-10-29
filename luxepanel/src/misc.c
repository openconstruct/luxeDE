/*
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008-2009 Fred Chien <fred@lxde.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2011 Julien Lavergne <julien.lavergne@gmail.com>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2014 SHiNE CsyFeK <csyfek@users.sourceforge.net>
 *               2014 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2026 Jerry Howell <jerr@luxe.desktop>
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

#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <gtk/gtk.h>
#include "luxe-dialogs-gtk.h"
#include <gdk/gdk.h>
#include <gdk/x11/gdkx.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <glib/gi18n.h>
#include <libfm/fm.h>

#include "misc.h"
#include "private.h"
#include "gtk-compat.h"

#include "dbg.h"

static Display *luxepanel_xdisplay(void);

/* Icon theme for the default display (gtk_icon_theme_get_default is
 * gone in GTK4). */
static GtkIconTheme *luxepanel_icon_theme(void)
{
    return gtk_icon_theme_get_for_display(gdk_display_get_default());
}

/* Render a GdkPaintable into a GdkPixbuf (RGBA, unpremultiplied) so
 * the pixel-twiddling highlight code can keep working. */
GdkPixbuf *luxepanel_pixbuf_from_paintable(GdkPaintable *paintable, gint size)
{
    GtkSnapshot *snap;
    GskRenderNode *node;
    cairo_surface_t *surf;
    cairo_t *cr;
    GdkPixbuf *pixbuf = NULL;
    int w, h;

    w = (size > 0) ? size : 32;
    h = w;
    snap = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snap, w, h);
    node = gtk_snapshot_to_node(snap);
    g_object_unref(snap);
    if (node == NULL)
        return NULL;
    surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cr = cairo_create(surf);
    gsk_render_node_draw(node, cr);
    cairo_destroy(cr);
    gsk_render_node_unref(node);
    if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_flush(surf);
        const guint8 *src = cairo_image_surface_get_data(surf);
        int stride = cairo_image_surface_get_stride(surf);
        int row, col;

        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
        if (pixbuf != NULL)
        {
            guint8 *dst = gdk_pixbuf_get_pixels(pixbuf);
            int dstride = gdk_pixbuf_get_rowstride(pixbuf);
            for (row = 0; row < h; row++)
            {
                const guint32 *s = (const guint32 *)(src + row * stride);
                guint8 *d = dst + row * dstride;
                for (col = 0; col < w; col++)
                {
                    /* Cairo ARGB32 is a native-endian 0xAARRGGBB word.
                     * Reading its blue byte as alpha hides valid app icons. */
                    guint32 p = s[col];
                    guint8 a = (p >> 24) & 0xFF;
                    guint8 r = (p >> 16) & 0xFF;
                    guint8 gg = (p >> 8) & 0xFF;
                    guint8 b = p & 0xFF;
                    if (a != 0)
                    {
                        r = (r * 255 + a / 2) / a;
                        gg = (gg * 255 + a / 2) / a;
                        b = (b * 255 + a / 2) / a;
                    }
                    d[0] = r;
                    d[1] = gg;
                    d[2] = b;
                    d[3] = a;
                    d += 4;
                }
            }
        }
    }
    cairo_surface_destroy(surf);
    return pixbuf;
}

/* Load a themed icon as GdkPixbuf via GtkIconPaintable. */
static GdkPixbuf *pixbuf_from_themed_names(const char * const * names,
                                           const char *fallback, gint size,
                                           GtkWidget *for_widget)
{
    GtkIconTheme *theme = luxepanel_icon_theme();
    GtkIconPaintable *pt = NULL;
    GdkPixbuf *pixbuf = NULL;
    int i;
    const char *fb[2];
    GtkTextDirection dir = (for_widget != NULL) ?
        gtk_widget_get_direction(for_widget) : GTK_TEXT_DIR_LTR;

    fb[0] = (fallback != NULL && fallback[0] != '/') ?
        fallback : "application-x-executable";
    fb[1] = NULL;
    /* Resolve each requested name through the entire inheritance chain
     * before trying a generic fallback. GTK can otherwise select a fallback
     * in the current theme ahead of the real icon in a parent/hicolor theme. */
    for (i = 0; names != NULL && names[i] != NULL && pt == NULL; i++)
        if (gtk_icon_theme_has_icon(theme, names[i]))
            pt = gtk_icon_theme_lookup_icon(theme, names[i], NULL, size, 1, dir,
                                            0);
    if (pt == NULL)
        pt = gtk_icon_theme_lookup_icon(theme, fb[0], NULL, size, 1, dir,
                                        0);
    if (pt != NULL)
    {
        pixbuf = luxepanel_pixbuf_from_paintable(GDK_PAINTABLE(pt), size);
        g_object_unref(pt);
    }
    return pixbuf;
}

/* data used by themed images buttons */
typedef struct {
    FmIcon *icon;
    guint theme_changed_handler;
    guint icon_changed_handler;
    guint font_changed_handler;
    GdkPixbuf* pixbuf;
    GdkPixbuf* hilight;
    gulong hicolor;
    gint size; /* desired size */
    LuxePanel *panel;
    char *fallback;
} ImgData;

static GQuark img_data_id = 0;

static void _gtk_image_set_from_file_scaled(GtkWidget *img, ImgData *data);

/* X11 data types */
Atom a_UTF8_STRING;
Atom a_XROOTPMAP_ID;

/* old WM spec */
Atom a_WM_STATE;
Atom a_WM_CLASS = XA_WM_CLASS;
Atom a_WM_DELETE_WINDOW;
Atom a_WM_PROTOCOLS;

/* new NET spec */
Atom a_NET_WORKAREA;
Atom a_NET_CLIENT_LIST;
Atom a_NET_CLIENT_LIST_STACKING;
Atom a_NET_NUMBER_OF_DESKTOPS;
Atom a_NET_CURRENT_DESKTOP;
Atom a_NET_DESKTOP_VIEWPORT;
Atom a_NET_DESKTOP_NAMES;
Atom a_NET_ACTIVE_WINDOW;
Atom a_NET_CLOSE_WINDOW;
Atom a_NET_SHOWING_DESKTOP;
Atom a_NET_SUPPORTED;
Atom a_NET_WM_STATE;
Atom a_NET_WM_STATE_SKIP_TASKBAR;
Atom a_NET_WM_STATE_SKIP_PAGER;
Atom a_NET_WM_STATE_STICKY;
Atom a_NET_WM_STATE_HIDDEN;
Atom a_NET_WM_STATE_SHADED;
Atom a_NET_WM_WINDOW_TYPE;
Atom a_NET_WM_WINDOW_TYPE_DESKTOP;
Atom a_NET_WM_WINDOW_TYPE_DOCK;
Atom a_NET_WM_WINDOW_TYPE_TOOLBAR;
Atom a_NET_WM_WINDOW_TYPE_MENU;
Atom a_NET_WM_WINDOW_TYPE_UTILITY;
Atom a_NET_WM_WINDOW_TYPE_SPLASH;
Atom a_NET_WM_WINDOW_TYPE_DIALOG;
Atom a_NET_WM_WINDOW_TYPE_NORMAL;
Atom a_NET_WM_DESKTOP;
Atom a_NET_WM_PID;
Atom a_NET_WM_NAME;
Atom a_NET_WM_VISIBLE_NAME;
Atom a_NET_WM_STRUT;
Atom a_NET_WM_STRUT_PARTIAL;
Atom a_NET_WM_ICON;
Atom a_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR;

/* SYSTEM TRAY spec */
Atom a_NET_SYSTEM_TRAY_OPCODE;
Atom a_NET_SYSTEM_TRAY_MESSAGE_DATA;
Atom a_NET_SYSTEM_TRAY_ORIENTATION;
Atom a_MANAGER;

Atom a_LUXEPANEL_CMD; /* for private client message */

/* if current window manager is EWMH conforming. */
gboolean is_ewmh_supported;

enum{
    I_UTF8_STRING,
    I_XROOTPMAP_ID,
    I_WM_STATE,
    I_WM_DELETE_WINDOW,
    I_WM_PROTOCOLS,

    I_NET_WORKAREA,
    I_NET_CLIENT_LIST,
    I_NET_CLIENT_LIST_STACKING,
    I_NET_NUMBER_OF_DESKTOPS,
    I_NET_CURRENT_DESKTOP,
    I_NET_DESKTOP_VIEWPORT,
    I_NET_DESKTOP_NAMES,
    I_NET_ACTIVE_WINDOW,
    I_NET_SHOWING_DESKTOP,
    I_NET_SUPPORTED,

    I_NET_WM_STATE,
    I_NET_WM_STATE_SKIP_TASKBAR,
    I_NET_WM_STATE_SKIP_PAGER,
    I_NET_WM_STATE_STICKY,
    I_NET_WM_STATE_HIDDEN,
    I_NET_WM_STATE_SHADED,
    I_NET_WM_WINDOW_TYPE,

    I_NET_WM_WINDOW_TYPE_DESKTOP,
    I_NET_WM_WINDOW_TYPE_DOCK,
    I_NET_WM_WINDOW_TYPE_TOOLBAR,
    I_NET_WM_WINDOW_TYPE_MENU,
    I_NET_WM_WINDOW_TYPE_UTILITY,
    I_NET_WM_WINDOW_TYPE_SPLASH,
    I_NET_WM_WINDOW_TYPE_DIALOG,
    I_NET_WM_WINDOW_TYPE_NORMAL,
    I_NET_WM_DESKTOP,
    I_NET_WM_PID,
    I_NET_WM_NAME,
    I_NET_WM_VISIBLE_NAME,
    I_NET_WM_STRUT,
    I_NET_WM_STRUT_PARTIAL,
    I_NET_WM_ICON,
    I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR,

    I_NET_SYSTEM_TRAY_OPCODE,
    I_NET_SYSTEM_TRAY_MESSAGE_DATA,
    I_NET_SYSTEM_TRAY_ORIENTATION,
    I_MANAGER,

    I_LUXEPANEL_CMD,
    N_ATOMS
};

pair allign_pair[] = {
    { ALIGN_NONE, "none" },
    { ALIGN_LEFT, "left" },
    { ALIGN_RIGHT, "right" },
    { ALIGN_CENTER, "center"},
    { 0, NULL },
};

pair edge_pair[] = {
    { EDGE_NONE, "none" },
    { EDGE_LEFT, "left" },
    { EDGE_RIGHT, "right" },
    { EDGE_TOP, "top" },
    { EDGE_BOTTOM, "bottom" },
    { 0, NULL },
};

pair width_pair[] = {
    { WIDTH_NONE, "none" },
    { WIDTH_REQUEST, "request" },
    { WIDTH_PIXEL, "pixel" },
    { WIDTH_PERCENT, "percent" },
    { 0, NULL },
};

pair height_pair[] = {
    { HEIGHT_NONE, "none" },
    { HEIGHT_PIXEL, "pixel" },
    { 0, NULL },
};

pair bool_pair[] = {
    { 0, "0" },
    { 1, "1" },
    { 0, NULL },
};

int
str2num(pair *p, const gchar *str, int defval)
{
    ENTER;
    for (;p && p->str; p++) {
        if (!g_ascii_strcasecmp(str, p->str))
            RET(p->num);
    }
    RET(defval);
}

const gchar *
num2str(pair *p, int num, const gchar *defval)
{
    ENTER;
    for (;p && p->str; p++) {
        if (num == p->num)
            RET(p->str);
    }
    RET(defval);
}

void resolve_atoms()
{
    static const char* atom_names[ N_ATOMS ];

    atom_names[ I_UTF8_STRING ] = "UTF8_STRING";
    atom_names[ I_XROOTPMAP_ID ] = "_XROOTPMAP_ID";
    atom_names[ I_WM_STATE ] = "WM_STATE";
    atom_names[ I_WM_DELETE_WINDOW ] = "WM_DELETE_WINDOW";
    atom_names[ I_WM_PROTOCOLS ] = "WM_PROTOCOLS";
    atom_names[ I_NET_WORKAREA ] = "_NET_WORKAREA";
    atom_names[ I_NET_CLIENT_LIST ] = "_NET_CLIENT_LIST";
    atom_names[ I_NET_CLIENT_LIST_STACKING ] = "_NET_CLIENT_LIST_STACKING";
    atom_names[ I_NET_NUMBER_OF_DESKTOPS ] = "_NET_NUMBER_OF_DESKTOPS";
    atom_names[ I_NET_CURRENT_DESKTOP ] = "_NET_CURRENT_DESKTOP";
    atom_names[ I_NET_DESKTOP_VIEWPORT ] = "_NET_DESKTOP_VIEWPORT";
    atom_names[ I_NET_DESKTOP_NAMES ] = "_NET_DESKTOP_NAMES";
    atom_names[ I_NET_ACTIVE_WINDOW ] = "_NET_ACTIVE_WINDOW";
    atom_names[ I_NET_SHOWING_DESKTOP ] = "_NET_SHOWING_DESKTOP";
    atom_names[ I_NET_SUPPORTED ] = "_NET_SUPPORTED";
    atom_names[ I_NET_WM_DESKTOP ] = "_NET_WM_DESKTOP";
    atom_names[ I_NET_WM_STATE ] = "_NET_WM_STATE";
    atom_names[ I_NET_WM_STATE_SKIP_TASKBAR ] = "_NET_WM_STATE_SKIP_TASKBAR";
    atom_names[ I_NET_WM_STATE_SKIP_PAGER ] = "_NET_WM_STATE_SKIP_PAGER";
    atom_names[ I_NET_WM_STATE_STICKY ] = "_NET_WM_STATE_STICKY";
    atom_names[ I_NET_WM_STATE_HIDDEN ] = "_NET_WM_STATE_HIDDEN";
    atom_names[ I_NET_WM_STATE_SHADED ] = "_NET_WM_STATE_SHADED";
    atom_names[ I_NET_WM_WINDOW_TYPE ] = "_NET_WM_WINDOW_TYPE";

    atom_names[ I_NET_WM_WINDOW_TYPE_DESKTOP ] = "_NET_WM_WINDOW_TYPE_DESKTOP";
    atom_names[ I_NET_WM_WINDOW_TYPE_DOCK ] = "_NET_WM_WINDOW_TYPE_DOCK";
    atom_names[ I_NET_WM_WINDOW_TYPE_TOOLBAR ] = "_NET_WM_WINDOW_TYPE_TOOLBAR";
    atom_names[ I_NET_WM_WINDOW_TYPE_MENU ] = "_NET_WM_WINDOW_TYPE_MENU";
    atom_names[ I_NET_WM_WINDOW_TYPE_UTILITY ] = "_NET_WM_WINDOW_TYPE_UTILITY";
    atom_names[ I_NET_WM_WINDOW_TYPE_SPLASH ] = "_NET_WM_WINDOW_TYPE_SPLASH";
    atom_names[ I_NET_WM_WINDOW_TYPE_DIALOG ] = "_NET_WM_WINDOW_TYPE_DIALOG";
    atom_names[ I_NET_WM_WINDOW_TYPE_NORMAL ] = "_NET_WM_WINDOW_TYPE_NORMAL";
    atom_names[ I_NET_WM_DESKTOP ] = "_NET_WM_DESKTOP";
    atom_names[ I_NET_WM_PID ] = "_NET_WM_PID";
    atom_names[ I_NET_WM_NAME ] = "_NET_WM_NAME";
    atom_names[ I_NET_WM_VISIBLE_NAME ] = "_NET_WM_VISIBLE_NAME";
    atom_names[ I_NET_WM_STRUT ] = "_NET_WM_STRUT";
    atom_names[ I_NET_WM_STRUT_PARTIAL ] = "_NET_WM_STRUT_PARTIAL";
    atom_names[ I_NET_WM_ICON ] = "_NET_WM_ICON";
    atom_names[ I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR ] = "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR";

    atom_names[ I_NET_SYSTEM_TRAY_OPCODE ] = "_NET_SYSTEM_TRAY_OPCODE";
    atom_names[ I_NET_SYSTEM_TRAY_MESSAGE_DATA ] = "_NET_SYSTEM_TRAY_MESSAGE_DATA";
    atom_names[ I_NET_SYSTEM_TRAY_ORIENTATION ] = "_NET_SYSTEM_TRAY_ORIENTATION";
    atom_names[ I_MANAGER ] = "MANAGER";

    atom_names[ I_LUXEPANEL_CMD ] = "_LUXEPANEL_CMD";

    Atom atoms[ N_ATOMS ];

    ENTER;

    /* luxepanel is X11-only for now: bail out gracefully (instead of
     * crashing in XInternAtoms) when GDK picked a non-X11 backend,
     * e.g. under a Wayland session (GNOME) without GDK_BACKEND=x11 */
    {
        GdkDisplay *gdpy = gdk_display_get_default();

        if (gdpy == NULL || !GDK_IS_X11_DISPLAY(gdpy))
        {
            g_warning( "luxepanel requires X11; use GDK_BACKEND=x11 under Wayland" );
            return;
        }
    }

    if (luxepanel_xdisplay() == NULL ||
        !XInternAtoms(luxepanel_xdisplay(), (char **)atom_names,
            N_ATOMS, False, atoms ) )
    {
        g_warning( "Error: unable to return Atoms" );
        return;
    }

    a_UTF8_STRING = atoms[ I_UTF8_STRING ];
    a_XROOTPMAP_ID = atoms[ I_XROOTPMAP_ID ];
    a_WM_STATE = atoms[ I_WM_STATE ];
    a_WM_DELETE_WINDOW = atoms[ I_WM_DELETE_WINDOW ];
    a_WM_PROTOCOLS = atoms[ I_WM_PROTOCOLS ];

    a_NET_WORKAREA = atoms[ I_NET_WORKAREA ];
    a_NET_CLIENT_LIST = atoms[ I_NET_CLIENT_LIST ];
    a_NET_CLIENT_LIST_STACKING = atoms[ I_NET_CLIENT_LIST_STACKING ];
    a_NET_NUMBER_OF_DESKTOPS = atoms[ I_NET_NUMBER_OF_DESKTOPS ];
    a_NET_CURRENT_DESKTOP = atoms[ I_NET_CURRENT_DESKTOP ];
    a_NET_DESKTOP_VIEWPORT = atoms[ I_NET_DESKTOP_VIEWPORT ];
    a_NET_DESKTOP_NAMES = atoms[ I_NET_DESKTOP_NAMES ];
    a_NET_ACTIVE_WINDOW = atoms[ I_NET_ACTIVE_WINDOW ];
    a_NET_SHOWING_DESKTOP = atoms[ I_NET_SHOWING_DESKTOP ];
    a_NET_SUPPORTED = atoms[ I_NET_SUPPORTED ];
    a_NET_WM_STATE = atoms[ I_NET_WM_STATE ];
    a_NET_WM_STATE_SKIP_TASKBAR = atoms[ I_NET_WM_STATE_SKIP_TASKBAR ];
    a_NET_WM_STATE_SKIP_PAGER = atoms[ I_NET_WM_STATE_SKIP_PAGER ];
    a_NET_WM_STATE_STICKY = atoms[ I_NET_WM_STATE_STICKY ];
    a_NET_WM_STATE_HIDDEN = atoms[ I_NET_WM_STATE_HIDDEN ];
    a_NET_WM_STATE_SHADED = atoms[ I_NET_WM_STATE_SHADED ];
    a_NET_WM_WINDOW_TYPE = atoms[ I_NET_WM_WINDOW_TYPE ];

    a_NET_WM_WINDOW_TYPE_DESKTOP = atoms[ I_NET_WM_WINDOW_TYPE_DESKTOP ];
    a_NET_WM_WINDOW_TYPE_DOCK = atoms[ I_NET_WM_WINDOW_TYPE_DOCK ];
    a_NET_WM_WINDOW_TYPE_TOOLBAR = atoms[ I_NET_WM_WINDOW_TYPE_TOOLBAR ];
    a_NET_WM_WINDOW_TYPE_MENU = atoms[ I_NET_WM_WINDOW_TYPE_MENU ];
    a_NET_WM_WINDOW_TYPE_UTILITY = atoms[ I_NET_WM_WINDOW_TYPE_UTILITY ];
    a_NET_WM_WINDOW_TYPE_SPLASH = atoms[ I_NET_WM_WINDOW_TYPE_SPLASH ];
    a_NET_WM_WINDOW_TYPE_DIALOG = atoms[ I_NET_WM_WINDOW_TYPE_DIALOG ];
    a_NET_WM_WINDOW_TYPE_NORMAL = atoms[ I_NET_WM_WINDOW_TYPE_NORMAL ];
    a_NET_WM_DESKTOP = atoms[ I_NET_WM_DESKTOP ];
    a_NET_WM_PID = atoms[ I_NET_WM_PID ];
    a_NET_WM_NAME = atoms[ I_NET_WM_NAME ];
    a_NET_WM_VISIBLE_NAME = atoms[ I_NET_WM_VISIBLE_NAME ];
    a_NET_WM_STRUT = atoms[ I_NET_WM_STRUT ];
    a_NET_WM_STRUT_PARTIAL = atoms[ I_NET_WM_STRUT_PARTIAL ];
    a_NET_WM_ICON = atoms[ I_NET_WM_ICON ];
    a_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR = atoms[ I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR ];

    a_NET_SYSTEM_TRAY_OPCODE = atoms[ I_NET_SYSTEM_TRAY_OPCODE ];
    a_NET_SYSTEM_TRAY_MESSAGE_DATA = atoms [ I_NET_SYSTEM_TRAY_MESSAGE_DATA ];
    a_NET_SYSTEM_TRAY_ORIENTATION = atoms[ I_NET_SYSTEM_TRAY_ORIENTATION ];
    a_MANAGER = atoms[ I_MANAGER ];

    a_LUXEPANEL_CMD = atoms[ I_LUXEPANEL_CMD ];

    RET();
}

void
Xclimsgx(Screen *screen, Window win, Atom type, long l0, long l1, long l2, long l3, long l4)
{
    Display *display;
    XClientMessageEvent xev;

    if (screen == NULL)
        return;
    display = DisplayOfScreen(screen);
    if (display == NULL)
        return;
    xev = (XClientMessageEvent){0};
    xev.type = ClientMessage;
    xev.send_event = True;
    xev.display = display;
    xev.window = win;
    xev.message_type = type;
    xev.format = 32;
    xev.data.l[0] = l0;
    xev.data.l[1] = l1;
    xev.data.l[2] = l2;
    xev.data.l[3] = l3;
    xev.data.l[4] = l4;
    XSendEvent(display, RootWindowOfScreen(screen), False,
               (SubstructureNotifyMask | SubstructureRedirectMask),
               (XEvent *)&xev);
}

void
Xclimsg(Window win, Atom type, long l0, long l1, long l2, long l3, long l4)
{
    Display *dpy = luxepanel_xdisplay();
    if (dpy != NULL)
        Xclimsgx(DefaultScreenOfDisplay(dpy), win, type, l0, l1, l2, l3, l4);
}

void
Xclimsgwm(Window win, Atom type, Atom arg)
{
    Display *dpy = luxepanel_xdisplay();
    XClientMessageEvent xev;

    if (dpy == NULL)
        return;
    xev = (XClientMessageEvent){0};
    xev.type = ClientMessage;
    xev.send_event = True;
    xev.display = dpy;
    xev.window = win;
    xev.message_type = type;
    xev.format = 32;
    xev.data.l[0] = arg;
    xev.data.l[1] = GDK_CURRENT_TIME;
    XSendEvent(dpy, win, False, 0L, (XEvent *)&xev);
}

#define X_PROPERTY_MAX_BYTES (1024U * 1024U)
#define X_PROPERTY_LONG_LENGTH (X_PROPERTY_MAX_BYTES / 4U)

static Display *
luxepanel_xdisplay(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (!luxepanel_display_usable(display) || !GDK_IS_X11_DISPLAY(display))
        return NULL;
    return GDK_DISPLAY_XDISPLAY(display);
}

void *
get_utf8_property(Window win, Atom atom)
{
    Display *dpy = luxepanel_xdisplay();
    Atom type = None;
    int format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    gchar *retval = NULL;
    guchar *tmp = NULL;

    if (dpy == NULL)
        return NULL;
    if (XGetWindowProperty(dpy, win, atom, 0, X_PROPERTY_LONG_LENGTH, False,
                           a_UTF8_STRING, &type, &format, &nitems,
                           &bytes_after, &tmp) != Success)
    {
        if (tmp != NULL)
            XFree(tmp);
        return NULL;
    }
    if (type == a_UTF8_STRING && format == 8 && nitems != 0 &&
        nitems <= X_PROPERTY_MAX_BYTES && bytes_after == 0 && tmp != NULL)
        retval = g_strndup((const gchar *)tmp, nitems);
    if (tmp != NULL)
        XFree(tmp);
    return retval;
}

char **
get_utf8_property_list(Window win, Atom atom, int *count)
{
    Display *dpy = luxepanel_xdisplay();
    Atom type = None;
    int format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    guchar *tmp = NULL;
    gchar **retval = NULL;
    gsize pos = 0;
    gsize used = 0;

    if (count == NULL)
        return NULL;
    *count = 0;
    if (dpy == NULL ||
        XGetWindowProperty(dpy, win, atom, 0, X_PROPERTY_LONG_LENGTH, False,
                           a_UTF8_STRING, &type, &format, &nitems,
                           &bytes_after, &tmp) != Success ||
        type != a_UTF8_STRING || format != 8 || nitems == 0 ||
        nitems > X_PROPERTY_MAX_BYTES || bytes_after != 0 || tmp == NULL)
    {
        if (tmp != NULL)
            XFree(tmp);
        return NULL;
    }

    /* Parse the byte-bounded property without calling strlen() on a final
     * segment that is not NUL-terminated. */
    retval = g_new0(gchar *, (gsize)nitems + 1);
    while (pos < nitems)
    {
        const gchar *start = (const gchar *)tmp + pos;
        gsize remaining = nitems - pos;
        const gchar *nul = memchr(start, '\0', remaining);
        gsize length = nul != NULL ? (gsize)(nul - start) : remaining;

        retval[used++] = g_strndup(start, length);
        if (nul == NULL)
            break;
        pos += length + 1;
    }
    XFree(tmp);
    *count = (int)MIN(used, (gsize)G_MAXINT);
    return retval;
}

void *
get_xaproperty(Window win, Atom prop, Atom type, int *nitems)
{
    Display *dpy = luxepanel_xdisplay();
    Atom type_ret = None;
    int format_ret = 0;
    unsigned long items_ret = 0;
    unsigned long after_ret = 0;
    unsigned char *prop_data = NULL;

    ENTER;
    if (nitems != NULL)
        *nitems = 0;
    if (dpy == NULL ||
        XGetWindowProperty(dpy, win, prop, 0, X_PROPERTY_LONG_LENGTH, False,
                           type, &type_ret, &format_ret, &items_ret,
                           &after_ret, &prop_data) != Success ||
        type_ret != type || format_ret != 32 || items_ret == 0 ||
        items_ret > X_PROPERTY_MAX_BYTES / sizeof(unsigned long) ||
        after_ret != 0)
    {
        if (prop_data != NULL)
            XFree(prop_data);
        RET(NULL);
    }
    if (nitems != NULL)
        *nitems = (int)MIN(items_ret, (unsigned long)G_MAXINT);
    RET(prop_data);
}

static char*
text_property_to_utf8 (const XTextProperty *prop)
{
  char **list;
  int count;
  char *retval;

  ENTER;
  list = NULL;
  count = 0;
  retval = NULL;
  /* GDK text-property helpers are gone; plain Xlib conversion. */
  {
      Display *dpy = luxepanel_xdisplay();
      if (dpy == NULL)
          return NULL;
      if (prop->encoding == a_UTF8_STRING)
      {
          /* already UTF-8 */
          retval = g_strndup((const char *)prop->value, prop->nitems);
          RET(retval);
      }
      count = XmbTextPropertyToTextList(dpy, (XTextProperty *)prop, &list, &count);
      if (count < 1 || list == NULL)
          return NULL;
      /* convert from locale encoding to UTF-8 */
      retval = g_locale_to_utf8(list[0], -1, NULL, NULL, NULL);
      if (retval == NULL)
          retval = g_strdup(list[0]);
      XFreeStringList(list);
      RET(retval);
  }

  DBG("count=%d\n", count);
  if (count == 0)
    return NULL;

  retval = list[0];
  list[0] = g_strdup (""); /* something to free */

  g_strfreev (list);

  RET(retval);
}

char *
get_textproperty(Window win, Atom atom)
{
    XTextProperty text_prop;
    char *retval;
    Display *dpy;

    ENTER;
    dpy = luxepanel_xdisplay();
    if (dpy == NULL)
        RET(NULL);
    if (XGetTextProperty(dpy, win, &text_prop, atom))
    {
        DBG("format=%d enc=%d nitems=%d value=%s   \n",
              text_prop.format,
              text_prop.encoding,
              text_prop.nitems,
              text_prop.value);
        retval = text_property_to_utf8(&text_prop);
        if (text_prop.nitems > 0)
            XFree(text_prop.value);
        RET(retval);
    }
    RET(NULL);
}

int
get_net_number_of_desktops()
{
    int desknum;
    gulong *data;
    Display *dpy = luxepanel_xdisplay();

    ENTER;
    if (dpy == NULL)
        RET(0);
    data = get_xaproperty(DefaultRootWindow(dpy), a_NET_NUMBER_OF_DESKTOPS,
          XA_CARDINAL, 0);
    if (!data)
        RET(0);

    desknum = *data;
    XFree (data);
    RET(desknum);
}

int
get_net_current_desktop ()
{
    int desk;
    gulong *data;
    Display *dpy = luxepanel_xdisplay();

    ENTER;
    if (dpy == NULL)
        RET(0);
    data = get_xaproperty(DefaultRootWindow(dpy), a_NET_CURRENT_DESKTOP,
                          XA_CARDINAL, 0);
    if (!data)
        RET(0);

    desk = *data;
    XFree (data);
    RET(desk);
}

int
get_net_wm_desktop(Window win)
{
    int desk = 0;
    gulong *data;

    ENTER;
    data = get_xaproperty (win, a_NET_WM_DESKTOP, XA_CARDINAL, 0);
    if (data) {
        desk = *data;
        XFree (data);
    }
    RET(desk);
}

GPid
get_net_wm_pid(Window win)
{
    GPid pid = 0;
    gulong *data;

    ENTER;
    data = get_xaproperty (win, a_NET_WM_PID, XA_CARDINAL, 0);
    if (data) {
        pid = *data;
        XFree (data);
    }
    RET(pid);
}

void
get_net_wm_state(Window win, NetWMState *nws)
{
    Atom *state;
    int num3;

    ENTER;
    if (nws == NULL)
        RET();
    memset(nws, 0, sizeof(*nws));
    if (!(state = get_xaproperty(win, a_NET_WM_STATE, XA_ATOM, &num3)))
        RET();

    DBG( "%x: netwm state = { ", (unsigned int)win);
    while (--num3 >= 0) {
        if (state[num3] == a_NET_WM_STATE_SKIP_PAGER) {
            DBG("NET_WM_STATE_SKIP_PAGER ");
            nws->skip_pager = 1;
        } else if (state[num3] == a_NET_WM_STATE_SKIP_TASKBAR) {
            DBG( "NET_WM_STATE_SKIP_TASKBAR ");
        nws->skip_taskbar = 1;
    } else if (state[num3] == a_NET_WM_STATE_STICKY) {
            DBG( "NET_WM_STATE_STICKY ");
        nws->sticky = 1;
        } else if (state[num3] == a_NET_WM_STATE_HIDDEN) {
            DBG( "NET_WM_STATE_HIDDEN ");
            nws->hidden = 1;
    } else if (state[num3] == a_NET_WM_STATE_SHADED) {
            DBG( "NET_WM_STATE_SHADED ");
            nws->shaded = 1;
    //FIXME: modal maximized_vert maximized_horz fullscreen above below demands_attention
    } else {
        DBG( "... ");
    }
    }
    XFree(state);
    DBG( "}\n");
    RET();
}

void
get_net_wm_window_type(Window win, NetWMWindowType *nwwt)
{
    Atom *state;
    int num3;

    ENTER;
    memset(nwwt, 0, sizeof(*nwwt));
    if (!(state = get_xaproperty(win, a_NET_WM_WINDOW_TYPE, XA_ATOM, &num3)))
        RET();

    DBG( "%x: netwm state = { ", (unsigned int)win);
    while (--num3 >= 0) {
        if (state[num3] == a_NET_WM_WINDOW_TYPE_DESKTOP) {
            DBG("NET_WM_WINDOW_TYPE_DESKTOP ");
            nwwt->desktop = 1;
        } else if (state[num3] == a_NET_WM_WINDOW_TYPE_DOCK) {
            DBG( "NET_WM_WINDOW_TYPE_DOCK ");
        nwwt->dock = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_TOOLBAR) {
            DBG( "NET_WM_WINDOW_TYPE_TOOLBAR ");
        nwwt->toolbar = 1;
        } else if (state[num3] == a_NET_WM_WINDOW_TYPE_MENU) {
            DBG( "NET_WM_WINDOW_TYPE_MENU ");
            nwwt->menu = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_UTILITY) {
            DBG( "NET_WM_WINDOW_TYPE_UTILITY ");
            nwwt->utility = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_SPLASH) {
            DBG( "NET_WM_WINDOW_TYPE_SPLASH ");
            nwwt->splash = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_DIALOG) {
            DBG( "NET_WM_WINDOW_TYPE_DIALOG ");
            nwwt->dialog = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_NORMAL) {
            DBG( "NET_WM_WINDOW_TYPE_NORMAL ");
            nwwt->normal = 1;
    } else {
        DBG( "... ");
    }
    }
    XFree(state);
    DBG( "}\n");
    RET();
}

int
get_wm_state (Window win)
{
    unsigned long *data;
    int ret = 0;

    ENTER;
    data = get_xaproperty (win, a_WM_STATE, a_WM_STATE, 0);
    if (data) {
        ret = data[0];
        XFree (data);
    }
    RET(ret);
}

int panel_handle_x_error(Display * d, XErrorEvent * ev)
{
    char buf[256];

    XGetErrorText(d, ev->error_code, buf, 256);
    g_warning("luxepanel : X error: %s", buf);
    return 0;    /* Ignored */
}

int panel_handle_x_error_swallow_BadWindow_BadDrawable(Display * d, XErrorEvent * ev)
{
    if ((ev->error_code != BadWindow) && (ev->error_code != BadDrawable))
        panel_handle_x_error(d, ev);
    return 0;    /* Ignored */
}

static void
calculate_width(int scrw, int pw, int wtype, int align, int margin,
      int *panw, int *x)
{
    ENTER;
    DBG("scrw=%d\n", scrw);
    DBG("IN panw=%d, margin=%d\n", *panw, margin);
    //scrw -= 2;
    if (wtype != WIDTH_REQUEST)
        *panw = pw;
    if (wtype == WIDTH_PERCENT) {
        /* sanity check */
        if (*panw > 100)
            *panw = 100;
        else if (*panw < 0)
            *panw = 1;
        *panw = ((gfloat) scrw * (gfloat) *panw) / 100.0;
    }
    if (align != ALIGN_CENTER) {
        if (margin > scrw) {
            g_warning( "margin is bigger then edge size %d > %d. Ignoring margin",
                  margin, scrw);
            margin = 0;
        }
	*panw = MIN(scrw - margin, *panw);
    }
    DBG("OUT panw=%d\n", *panw);
    if (align == ALIGN_LEFT)
        *x += margin;
    else if (align == ALIGN_RIGHT) {
        *x += scrw - *panw - margin;
        if (*x < 0)
            *x = 0;
    } else if (align == ALIGN_CENTER)
        *x += (scrw - *panw) / 2;
    RET();
}

void _calculate_position(LuxePanel *panel, GdkRectangle *rect)
{
    Panel *np = panel->priv;
    GdkRectangle marea;

    ENTER;
    /* GdkScreen is gone; use GdkMonitor geometry (union for -1). */
    if (np->monitor < 0) /* all monitors */
    {
        /* The cached display may be stale if it was closed; fall back to
         * the default display and bail out if neither is usable. */
        GdkDisplay *display = (np->display != NULL) ? np->display
                                                    : gdk_display_get_default();
        GListModel *monitors;
        guint i, n;

        marea.x = 0;
        marea.y = 0;
        marea.width = 0;
        marea.height = 0;
        if (!luxepanel_display_usable(display))
            return;
        monitors = gdk_display_get_monitors(display);
        if (!G_IS_LIST_MODEL(monitors))
            return;
        n = g_list_model_get_n_items(monitors);
        for (i = 0; i < n; i++)
        {
            GdkMonitor *mon = GDK_MONITOR(g_list_model_get_item(monitors, i));
            GdkRectangle g;

            if (mon == NULL)
                continue;
            gdk_monitor_get_geometry(mon, &g);
            if (g.width > 0 || g.height > 0)
            {
                if (marea.width == 0 && marea.height == 0)
                    marea = g;
                else
                {
                    gint x2 = MAX(marea.x + marea.width, g.x + g.width);
                    gint y2 = MAX(marea.y + marea.height, g.y + g.height);
                    marea.x = MIN(marea.x, g.x);
                    marea.y = MIN(marea.y, g.y);
                    marea.width = x2 - marea.x;
                    marea.height = y2 - marea.y;
                }
            }
            g_object_unref(mon);
        }
    }
    else
    {
        /* gdk_display_get_monitor() is gone; index into the GListModel. */
        GdkDisplay *display = (np->display != NULL) ? np->display
                                                    : gdk_display_get_default();
        GListModel *monitors;
        GdkMonitor *mon;

        if (!luxepanel_display_usable(display))
        {
            marea.x = 0;
            marea.y = 0;
            marea.width = 0;
            marea.height = 0;
        }
        else
        {
            monitors = gdk_display_get_monitors(display);
            mon = G_IS_LIST_MODEL(monitors)
                ? GDK_MONITOR(g_list_model_get_item(monitors, np->monitor))
                : NULL;
            if (mon != NULL)
            {
                gdk_monitor_get_geometry(mon, &marea);
                g_object_unref(mon);
            }
            else
            {
                marea.x = 0;
                marea.y = 0;
                marea.width = 0;
                marea.height = 0;
            }
        }
    }

    if (np->edge == EDGE_TOP || np->edge == EDGE_BOTTOM) {
        rect->x = marea.x;
        calculate_width(marea.width, np->width, np->widthtype, np->align, np->margin,
              &rect->width, &rect->x);
        rect->height = ((( ! np->autohide) || (np->visible)) ? np->height : np->height_when_hidden);
        rect->y = marea.y + ((np->edge == EDGE_TOP) ? 0 : (marea.height - rect->height));

    } else {
        rect->y = marea.y;
        calculate_width(marea.height, np->width, np->widthtype, np->align, np->margin,
              &rect->height, &rect->y);
        rect->width = ((( ! np->autohide) || (np->visible)) ? np->height : np->height_when_hidden);
        rect->x = marea.x + ((np->edge == EDGE_LEFT) ? 0 : (marea.width - rect->width));
    }
    //g_debug("%s - x=%d y=%d w=%d h=%d\n", __FUNCTION__, np->ax, np->ay, np->aw, np->ah);
    RET();
}

void calculate_position(Panel *np)
{
    GdkRectangle rect;

    rect.width = np->aw;
    rect.height = np->ah;
    _calculate_position(np->topgwin, &rect);
    np->aw = rect.width;
    np->ah = rect.height;
    np->ax = rect.x;
    np->ay = rect.y;
}

gchar *
expand_tilda(const gchar *file)
{
    ENTER;
    RET((file[0] == '~') ?
        g_strdup_printf("%s%s", getenv("HOME"), file+1)
        : g_strdup(file));

}

/*
 * SuxPanel version 0.1
 * Copyright (c) 2003 Leandro Pereira <leandro@linuxmag.com.br>
 *
 * This program may be distributed under the terms of GNU General
 * Public License version 2. You should have received a copy of the
 * license with this program; if not, please consult http://www.fsf.org/.
 *
 * This program comes with no warranty. Use at your own risk.
 *
 */

/* DestroyNotify handler for image data in _gtk_image_new_from_file_scaled. */
static void img_data_free(ImgData * data)
{
    g_object_unref(data->icon);
    if (data->theme_changed_handler != 0)
        g_signal_handler_disconnect(luxepanel_icon_theme(), data->theme_changed_handler);
    if (data->panel != NULL)
    {
        g_object_remove_weak_pointer(G_OBJECT(data->panel), (gpointer *)&data->panel);
        g_signal_handler_disconnect(data->panel, data->icon_changed_handler);
        if (data->font_changed_handler != 0)
            g_signal_handler_disconnect(data->panel, data->font_changed_handler);
    }
    if (data->pixbuf != NULL)
        g_object_unref(data->pixbuf);
    if (data->hilight != NULL)
        g_object_unref(data->hilight);
    if (data->fallback != NULL)
        g_free(data->fallback);
    g_free(data);
}

/* Handler for "changed" signal in _gtk_image_new_from_file_scaled. */
static void on_theme_changed(GtkWidget * img, GObject * object)
{
    ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(img), img_data_id);
    _gtk_image_set_from_file_scaled(img, data);
}

/* consumes reference on icon */
static void _luxepanel_button_set_icon(GtkWidget* btn, FmIcon* icon, gint size)
{
    /* Locate the image within the button. */
    GtkWidget * child;
    GtkWidget * img = NULL;
    if (GTK_IS_BUTTON(btn))
        child = gtk_button_get_child(GTK_BUTTON(btn));
    else
        child = gtk_widget_get_first_child(btn);
    if (GTK_IS_IMAGE(child))
        img = child;
    else if (GTK_IS_BOX(child))
    {
        GtkWidget *iter;
        for (iter = gtk_widget_get_first_child(child); iter != NULL;
             iter = gtk_widget_get_next_sibling(iter))
            if (GTK_IS_IMAGE(iter))
            {
                img = iter;
                break;
            }
    }

    if (img != NULL)
    {
        ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(img), img_data_id);

        if (size == 0) /* 0: set as before; -1: reset to panel icon size */
            size = data->size;
        if (icon != data->icon || size != data->size) /* something was changed */
        {
            g_object_unref(data->icon);
            data->icon = icon;
            data->size = size;
            _gtk_image_set_from_file_scaled(img, data);
        }
        else
            g_object_unref(icon);
    }
    else
        g_object_unref(icon);
}

void luxepanel_button_set_icon(GtkWidget* btn, const gchar *name, gint size)
{
    _luxepanel_button_set_icon(btn, fm_icon_from_name(name), size);
}

void luxepanel_button_update_icon(GtkWidget* btn, FmIcon *icon, gint size)
{
    _luxepanel_button_set_icon(btn, g_object_ref(icon), size);
}

gboolean luxepanel_button_set_label(GtkWidget *btn, const char *label)
{
    /* Locate the image within the button. */
    GtkWidget * child;
    GtkWidget * lbl = NULL;
    GtkWidget * img = NULL;
    ImgData * data = NULL;
    GtkWidget * iter;

    if (GTK_IS_BUTTON(btn))
        child = gtk_button_get_child(GTK_BUTTON(btn));
    else
        child = gtk_widget_get_first_child(btn);
    if (GTK_IS_BOX(child))
    {
        for (iter = gtk_widget_get_first_child(child); iter != NULL;
             iter = gtk_widget_get_next_sibling(iter))
            if (GTK_IS_LABEL(iter))
                lbl = iter;
            else if (GTK_IS_IMAGE(iter))
                img = iter;
    }

    if (G_UNLIKELY(lbl == NULL))
        return FALSE;

    if (G_LIKELY(img != NULL))
        data = (ImgData *) g_object_get_qdata(G_OBJECT(img), img_data_id);

    if (G_LIKELY(data != NULL && data->panel != NULL))
        luxepanel_draw_label_text(data->panel, lbl, label, FALSE, 1, TRUE);
    else
        gtk_label_set_text(GTK_LABEL(lbl), label);
    return TRUE;
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
void fb_button_set_from_file(GtkWidget * btn, const char * img_file, gint width, gint height, gboolean keep_ratio)
{
    luxepanel_button_set_icon(btn, img_file, height);
}

static void _gtk_image_set_from_file_scaled(GtkWidget * img, ImgData * data)
{
    gint size = data->size;

    if (size < 0 && data->panel)
    {
        gint tile = MAX(1, MIN(panel_get_icon_size(data->panel),
                               panel_get_height(data->panel)));
        size = MAX(1, tile - 2);
        /* Keep the tile/hit area; inset only the artwork. */
        gtk_widget_set_size_request(img, size, size);
        gtk_widget_set_margin_top(img, 1);
        gtk_widget_set_margin_bottom(img, 1);
        gtk_widget_set_margin_start(img, 1);
        gtk_widget_set_margin_end(img, 1);
        gtk_image_set_pixel_size(GTK_IMAGE(img), size);
    }
    else if (data->panel)
    {
        /* An auto-sized image can later receive an explicit size (taskbar). */
        gtk_widget_set_size_request(img, -1, -1);
        gtk_widget_set_margin_top(img, 0);
        gtk_widget_set_margin_bottom(img, 0);
        gtk_widget_set_margin_start(img, 0);
        gtk_widget_set_margin_end(img, 0);
        gtk_image_set_pixel_size(GTK_IMAGE(img), -1);
    }

    if (data->pixbuf != NULL)
    {
        g_object_unref(data->pixbuf);
        data->pixbuf = NULL;
    }

    /* if there is a cached hilighted version of this pixbuf, free it */
    if (data->hilight != NULL)
    {
        g_object_unref(data->hilight);
        data->hilight = NULL;
    }

    /* GtkIconTheme has no pixbuf-loading API in 4.18; go through
     * GtkIconPaintable and render to pixels. */
    if (G_LIKELY(G_IS_THEMED_ICON(data->icon)))
    {
        const char * const * names =
            g_themed_icon_get_names(G_THEMED_ICON(data->icon));
        const char *fallback = data->fallback;

        if (fallback != NULL && fallback[0] == '/')
            fallback = NULL; /* file-path fallback handled below */
        data->pixbuf = pixbuf_from_themed_names(names, fallback, size, img);
    }
    else
    {
        char *file = g_icon_to_string(G_ICON(data->icon));
        data->pixbuf = gdk_pixbuf_new_from_file_at_scale(file, -1, size, TRUE, NULL);
        g_free(file);
    }

    if (data->pixbuf == NULL && data->fallback != NULL && data->fallback[0] == '/')
    {
        /* if fallback was provided as a file path */
        data->pixbuf = gdk_pixbuf_new_from_file_at_scale(data->fallback, -1, size, TRUE, NULL);
    }

    if (data->pixbuf != NULL)
    {
        /* Set the pixbuf into the image widget. */
        gtk_image_set_from_pixbuf((GtkImage *)img, data->pixbuf);
    }
    else
    {
        /* No pixbuf available.  Set the "missing image" icon. */
        gtk_image_set_from_icon_name(GTK_IMAGE(img), "image-missing");
    }
}

/* consumes reference on icon */
static GtkWidget *_gtk_image_new_for_icon(LuxePanel *p, FmIcon *icon, gint size,
                                          const char *fallback)
{
    GtkWidget * img = gtk_image_new();
    ImgData * data = g_new0(ImgData, 1);

    data->icon = icon;
    data->size = size;
    data->fallback = g_strdup(fallback);
    if (img_data_id == 0)
        img_data_id = g_quark_from_static_string("ImgData");
    g_object_set_qdata_full(G_OBJECT(img), img_data_id, data, (GDestroyNotify) img_data_free);
    if (p && size < 0)
    {
        data->panel = p;
        data->icon_changed_handler = g_signal_connect_swapped(p, "icon-size-changed",
                                                G_CALLBACK(on_theme_changed), img);
        /* it is in fact not required if image is panel child but let be safe */
        g_object_add_weak_pointer(G_OBJECT(p), (gpointer *)&data->panel);
    }
    _gtk_image_set_from_file_scaled(img, data);
    if (G_IS_THEMED_ICON(data->icon))
    {
        /* This image is loaded from icon theme.  Update the image if the icon theme is changed. */
        data->theme_changed_handler = g_signal_connect_swapped(luxepanel_icon_theme(),
                                                "changed", G_CALLBACK(on_theme_changed), img);
    }
    return img;
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
GtkWidget * _gtk_image_new_from_file_scaled(const gchar * file, gint width, gint height, gboolean keep_ratio)
{
    return _gtk_image_new_for_icon(NULL, fm_icon_from_name(file), height, NULL);
}

GtkWidget *luxepanel_image_new_for_icon(LuxePanel *panel, const gchar *name,
                                      gint height, const char *fallback)
{
    return _gtk_image_new_for_icon(panel, fm_icon_from_name(name), height, fallback);
}

GtkWidget *luxepanel_image_new_for_fm_icon(LuxePanel *panel, FmIcon *icon,
                                         gint height, const char *fallback)
{
    return _gtk_image_new_for_icon(panel, g_object_ref(icon), height, fallback);
}

gboolean luxepanel_image_change_icon(GtkWidget *img, const gchar *name, const char *fallback)
{
    ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(img), img_data_id);

    g_return_val_if_fail(data != NULL && name != NULL, FALSE);
    g_object_unref(data->icon);
    g_free(data->fallback);
    data->icon = fm_icon_from_name(name);
    data->fallback = g_strdup(fallback);
    if (!G_IS_THEMED_ICON(data->icon))
    {
        if (data->theme_changed_handler != 0)
            g_signal_handler_disconnect(luxepanel_icon_theme(), data->theme_changed_handler);
        data->theme_changed_handler = 0;
    }
    else if (data->theme_changed_handler == 0)
    {
        /* This image is loaded from icon theme.  Update the image if the icon theme is changed. */
        data->theme_changed_handler = g_signal_connect_swapped(luxepanel_icon_theme(),
                                                "changed", G_CALLBACK(on_theme_changed), img);
    }
    _gtk_image_set_from_file_scaled(img, data);
    return TRUE;
}

void
get_button_spacing(GtkRequisition *req, GtkWidget *parent, gchar *name)
{
    GtkWidget *b;

    ENTER;
    b = gtk_button_new();
    gtk_widget_set_name(GTK_WIDGET(b), name);
    gtk_widget_set_focusable(b, FALSE);

    if (parent)
        gtk_widget_set_parent(b, parent);

    gtk_widget_show(b);
    {
        int minimum, natural;
        gtk_widget_measure(b, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, &natural, NULL, NULL);
        req->width = minimum;
        gtk_widget_measure(b, GTK_ORIENTATION_VERTICAL, -1, &minimum, &natural, NULL, NULL);
        req->height = minimum;
        gtk_widget_unparent(b);
    }
    RET();
}

guint32 gcolor2rgb24(GdkRGBA *color)
{
    guint32 i;

    ENTER;

    i = ((guint)(color->red * 255.0 + 0.5)) & 0xFF;
    i <<= 8;
    i |= ((guint)(color->green * 255.0 + 0.5)) & 0xFF;
    i <<= 8;
    i |= ((guint)(color->blue * 255.0 + 0.5)) & 0xFF;
    DBG("i=%x\n", i);
    RET(i);
}

/* Handler for pointer enter/leave on image that has highlighting
 * requested.  GTK4: enter/leave-notify-event are gone; use a motion
 * controller.  The controller is attached to the composed widget. */
typedef struct
{
    GtkImage *img;
    GtkEventController *ctrl;
} FbButtonHighlight;

static void fb_button_enter(GtkEventControllerMotion * ctrl, gdouble x, gdouble y, gpointer user_data)
{
    GtkImage * widget = GTK_IMAGE(user_data);
    ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(widget), img_data_id);

    /* GTK_IMAGE_PIXBUF storage is gone; ImgData presence is the test. */
    if (data != NULL && data->pixbuf != NULL)
    {
        if (data->hilight == NULL)
        {
            GdkPixbuf * dark = data->pixbuf;
            int height = gdk_pixbuf_get_height(dark);
            int rowstride = gdk_pixbuf_get_rowstride(dark);
            gulong hicolor = data->hicolor;

            GdkPixbuf * light = gdk_pixbuf_add_alpha(dark, FALSE, 0, 0, 0);
            if (light != NULL)
            {
                guchar extra[3];
                int i;
                for (i = 2; i >= 0; i--, hicolor >>= 8)
                    extra[i] = hicolor & 0xFF;

                guchar * src = gdk_pixbuf_get_pixels(light);
                guchar * up;
                for (up = src + height * rowstride; src < up; src += 4)
                {
                    if (src[3] != 0)
                    {
                        for (i = 0; i < 3; i++)
                        {
                        int value = src[i] + extra[i];
                        if (value > 255) value = 255;
                        src[i] = value;
                        }
                    }
                }
                data->hilight = light;
            }
        }

        if (data->hilight != NULL)
            gtk_image_set_from_pixbuf(widget, data->hilight);
    }
}

/* Handler for pointer leave on image that has highlighting requested. */
static void fb_button_leave(GtkEventControllerMotion * ctrl, gpointer user_data)
{
    GtkImage * widget = GTK_IMAGE(user_data);
    ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(widget), img_data_id);

    if (data != NULL && data->pixbuf != NULL)
        gtk_image_set_from_pixbuf(widget, data->pixbuf);
}

static void on_font_changed(LuxePanel * panel, GtkLabel * lbl)
{
    const char *label = gtk_label_get_text(lbl);
    luxepanel_draw_label_text(panel, GTK_WIDGET(lbl), label, FALSE, 1, TRUE);
}

static GtkWidget *_luxepanel_button_compose(GtkWidget *event_box, GtkWidget *image,
                                          gulong highlight_color, const gchar *label)
{
    ImgData * data = (ImgData *) g_object_get_qdata(G_OBJECT(image), img_data_id);

    gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
    if (highlight_color != 0 && data != NULL)
    {
        data->hicolor = highlight_color;
        {
            GtkEventController *ctrl = GTK_EVENT_CONTROLLER(gtk_event_controller_motion_new());
            g_signal_connect(ctrl, "enter", G_CALLBACK(fb_button_enter), image);
            g_signal_connect(ctrl, "leave", G_CALLBACK(fb_button_leave), image);
            gtk_widget_add_controller(event_box, ctrl);
        }
    }

    if (label == NULL)
    {
        gtk_widget_set_parent(image, event_box);
    }
    else
    {
        GtkWidget *inner, *lbl;

        inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_focusable(inner, FALSE);
        gtk_widget_set_parent(inner, event_box);

        gtk_box_append(GTK_BOX(inner), image);

        lbl = gtk_label_new("");
        if (G_LIKELY(data != NULL && data->panel != NULL))
        {
            luxepanel_draw_label_text(data->panel, lbl, label, FALSE, 1, TRUE);
            data->font_changed_handler = g_signal_connect(data->panel,
                                                          "panel-font-changed",
                                                          G_CALLBACK(on_font_changed),
                                                          lbl);
        }
        else
            gtk_label_set_text(GTK_LABEL(lbl), label);
        gtk_widget_set_margin_start(lbl, 2);
        gtk_widget_set_margin_end(lbl, 2);
        gtk_box_append(GTK_BOX(inner), lbl);
    }

    return event_box;
}

GtkWidget *luxepanel_button_compose(GtkWidget *event_box, GtkWidget *image,
                                  GdkRGBA *color, const gchar *label)
{
    gulong highlight_color = color ? gcolor2rgb24(color) : PANEL_ICON_HIGHLIGHT;
    return _luxepanel_button_compose(event_box, image, highlight_color, label);
}

/* consumes reference on icon */
static GtkWidget *_luxepanel_button_new_for_icon(LuxePanel *panel, FmIcon *icon,
                                               gint size, gulong highlight_color,
                                               const gchar *label)
{
    GtkWidget * event_box;
    GtkWidget * image = _gtk_image_new_for_icon(panel, icon, size, NULL);

    /* GtkEventBox is gone; a plain Box serves as the clickable
     * container (events arrive via controllers attached above). */
    event_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_focusable(event_box, FALSE);

    return _luxepanel_button_compose(event_box, image, highlight_color, label);
}

GtkWidget *luxepanel_button_new_for_icon(LuxePanel *panel, const gchar *name, GdkRGBA *color, const gchar *label)
{
    gulong highlight_color = color ? gcolor2rgb24(color) : PANEL_ICON_HIGHLIGHT;
    return _luxepanel_button_new_for_icon(panel, fm_icon_from_name(name), -1,
                                        highlight_color, label);
}

GtkWidget *luxepanel_button_new_for_fm_icon(LuxePanel *panel, FmIcon *icon, GdkRGBA *color, const gchar *label)
{
    gulong highlight_color = color ? gcolor2rgb24(color) : PANEL_ICON_HIGHLIGHT;
    return _luxepanel_button_new_for_icon(panel, g_object_ref(icon), -1,
                                        highlight_color, label);
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
GtkWidget * fb_button_new_from_file(
    const gchar * image_file, int width, int height, gulong highlight_color, gboolean keep_ratio)
{
    return fb_button_new_from_file_with_label(image_file, width, height, highlight_color, keep_ratio, NULL, NULL);
}

/* parameters width and keep_ratio are unused, kept for backward compatibility */
GtkWidget * fb_button_new_from_file_with_label(
    const gchar * image_file, int width, int height, gulong highlight_color, gboolean keep_ratio, Panel * panel, const gchar * label)
{
    return _luxepanel_button_new_for_icon(panel->topgwin, fm_icon_from_name(image_file), height, highlight_color, label);
}

char* translate_exec_to_cmd( const char* exec, const char* icon,
                             const char* title, const char* fpath )
{
    GString* cmd = g_string_sized_new( 256 );
    if (!exec)
	    return NULL;
    for( ; *exec; ++exec )
    {
        if( G_UNLIKELY(*exec == '%') )
        {
            ++exec;
            if( !*exec )
                break;
            switch( *exec )
            {
                case 'c':
                    if( title )
                    {
                        g_string_append( cmd, title );
                    }
                    break;
                case 'i':
                    if( icon )
                    {
                        g_string_append( cmd, "--icon " );
                        g_string_append( cmd, icon );
                    }
                    break;
                case 'k':
                    if( fpath )
                    {
                        char* uri = g_filename_to_uri( fpath, NULL, NULL );
                        g_string_append( cmd, uri );
                        g_free( uri );
                    }
                    break;
                case '%':
                    g_string_append_c( cmd, '%' );
                    break;
            }
        }
        else
            g_string_append_c( cmd, *exec );
    }
    return g_string_free( cmd, FALSE );
}

/*
 This function is used to re-create a new box with different
 orientation from the old one, add all children of the old one to
 the new one, and then destroy the old box.
 It's mainly used when we need to change the orientation of the panel or
 any plugin with a layout box. Since GtkHBox cannot be changed to GtkVBox,
 recreating a new box to replace the old one is required.
*/
/* for compatibility with old plugins */
GtkWidget* recreate_box( GtkBox* oldbox, GtkOrientation orientation )
{
    gtk_orientable_set_orientation(GTK_ORIENTABLE(oldbox), orientation);
    return GTK_WIDGET(oldbox);
}

/* for compatibility with old plugins */
void show_error( GtkWindow* parent_win, const char* msg )
{
    /* fm_show_error is libfm-gtk; a plain async alert dialog. */
    luxe_show_error(parent_win, msg ? msg : "", NULL);
}

/* old plugins compatibility mode, use fm_pixbuf_from_icon_with_fallback() instead */
GdkPixbuf * luxepanel_load_icon(const char * name, int width, int height, gboolean use_fallback)
{
    const char *names[2];
    const char *fb = NULL;

    names[0] = name ? name : "application-x-executable";
    names[1] = NULL;
    if (use_fallback)
        fb = "application-x-executable";
    return pixbuf_from_themed_names(names, fb, height, NULL);
}

/*
 * Taken from pcmanfm:
 * Parse Exec command line of app desktop file, and translate
 * it into a real command which can be passed to g_spawn_command_line_async().
 * file_list is a null-terminated file list containing full
 * paths of the files passed to app.
 * returned char* should be freed when no longer needed.
 */
static char* translate_app_exec_to_command_line( const char* pexec,
                                                 GList* file_list )
{
    char* file;
    GList* l;
    gchar *tmp;
    GString* cmd = g_string_new("");
    gboolean add_files = FALSE;

    for( ; *pexec; ++pexec )
    {
        if( *pexec == '%' )
        {
            ++pexec;
            switch( *pexec )
            {
            case 'U':
                for( l = file_list; l; l = l->next )
                {
                    tmp = g_filename_to_uri( (char*)l->data, NULL, NULL );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    if (l->next)
                        g_string_append_c( cmd, ' ' );
                    g_free( file );
                }
                add_files = TRUE;
                break;
            case 'u':
                if( file_list && file_list->data )
                {
                    file = (char*)file_list->data;
                    tmp = g_filename_to_uri( file, NULL, NULL );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    g_free( file );
                    add_files = TRUE;
                }
                break;
            case 'F':
            case 'N':
                for( l = file_list; l; l = l->next )
                {
                    file = (char*)l->data;
                    tmp = g_shell_quote( file );
                    g_string_append( cmd, tmp );
                    if (l->next)
                        g_string_append_c( cmd, ' ' );
                    g_free( tmp );
                }
                add_files = TRUE;
                break;
            case 'f':
            case 'n':
                if( file_list && file_list->data )
                {
                    file = (char*)file_list->data;
                    tmp = g_shell_quote( file );
                    g_string_append( cmd, tmp );
                    g_free( tmp );
                    add_files = TRUE;
                }
                break;
            case 'D':
                for( l = file_list; l; l = l->next )
                {
                    tmp = g_path_get_dirname( (char*)l->data );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    if (l->next)
                        g_string_append_c( cmd, ' ' );
                    g_free( file );
                }
                add_files = TRUE;
                break;
            case 'd':
                if( file_list && file_list->data )
                {
                    tmp = g_path_get_dirname( (char*)file_list->data );
                    file = g_shell_quote( tmp );
                    g_free( tmp );
                    g_string_append( cmd, file );
                    g_free( tmp );
                    add_files = TRUE;
                }
                break;
            case 'c':
                break;
            case 'i':
                /* Add icon name */
                break;
            case 'k':
                /* Location of the desktop file */
                break;
            case 'v':
                /* Device name */
                break;
            case '%':
                g_string_append_c ( cmd, '%' );
                break;
            case '\0':
                goto _finish;
                break;
            }
        }
        else  /* not % escaped part */
        {
            g_string_append_c ( cmd, *pexec );
        }
    }
_finish:
    if( ! add_files )
    {
        for( l = file_list; l; l = l->next )
        {
            g_string_append_c( cmd, ' ' );
            file = (char*)l->data;
            tmp = g_shell_quote( file );
            g_string_append( cmd, tmp );
            g_free( tmp );
        }
    }

    return g_string_free( cmd, FALSE );
}

gboolean spawn_command_async(GtkWindow *parent_window, gchar const* workdir,
        gchar const* cmd)
{
    GError* err = NULL;
    gchar** argv = NULL;

    g_info("luxepanel: spawning \"%s\"...", cmd);

    g_shell_parse_argv(cmd, NULL, &argv, &err);
    if (!err)
        g_spawn_async(workdir, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);

    if (err)
    {
        g_warning("%s\n", err->message);
        show_error(parent_window, err->message);
        g_error_free(err);
    }

    g_strfreev(argv);

    return !err;
}

/* FIXME: this should be replaced with fm_launch_file_simple() */
gboolean luxepanel_launch_app(const char* exec, GList* files, gboolean in_terminal, char const* in_workdir)
{
    GError *error = NULL;
    char* cmd;
    if( ! exec )
        return FALSE;
    cmd = translate_app_exec_to_command_line(exec, files);
    if( in_terminal )
    {
	char * escaped_cmd = g_shell_quote(cmd);
        char* term_cmd;
        const char* term = fm_config->terminal ? fm_config->terminal : "luxeterm";
        if( strstr(term, "%s") )
            term_cmd = g_strdup_printf(term, escaped_cmd);
        else
            term_cmd = g_strconcat( term, " -e ", escaped_cmd, NULL );
	g_free(escaped_cmd);
        if( cmd != exec )
            g_free(cmd);
        cmd = term_cmd;
    }

    spawn_command_async(NULL, in_workdir, cmd);

    g_free(cmd);

    return (error == NULL);
}

/* vim: set sw=4 et sts=4 : */
