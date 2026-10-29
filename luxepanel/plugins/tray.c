/*
 * System tray plugin to lxpanel
 *
 * Copyright (C) 2008-2009 Marty Jack <martyj19@comcast.net>
 *               2009 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2010-2013 Julien Lavergne <julien.lavergne@gmail.com>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/** Contains code adapted from na-tray-manager.c
 * Copyright (C) 2002 Anders Carlsson <andersca@gnu.org>
 * Copyright (C) 2003-2006 Vincent Untz
 * Copyright (C) 2026 Jerry Howell */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "plugin.h"
#include "misc.h"
#include "icon-grid.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>

/* Standards reference:  http://standards.freedesktop.org/systemtray-spec/ */

/* Xlib error handlers have no closure; park state in statics. */
static gboolean tray_xerror = FALSE;
static Display * tray_xerror_dpy = NULL;

static int tray_error_handler(Display * dpy, XErrorEvent * ev)
{
    if (dpy == tray_xerror_dpy)
        tray_xerror = TRUE;
    return 0;
}

/* Protocol constants. */
#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

#define SYSTEM_TRAY_ORIENTATION_HORZ 0
#define SYSTEM_TRAY_ORIENTATION_VERT 1

struct _balloon_message;
struct _tray_client;
struct _tray_plugin;

/* Representative of a balloon message. */
typedef struct _balloon_message {
    struct _balloon_message * flink;		/* Forward link */
    Window window;				/* X window ID */
    long timeout;				/* Finite display time in milliseconds. */
    long length;				/* Message string length */
    long id;					/* Client supplied unique message ID */
    long remaining_length;			/* Remaining length expected of incomplete message */
    gint64 deadline;				/* Absolute monotonic transfer deadline. */
    char * string;				/* Message string */
} BalloonMessage;

/* Representative of a tray client. */
typedef struct _tray_client {
    struct _tray_client * client_flink;		/* Forward link to next task in X window ID order */
    struct _tray_plugin * tr;			/* Back pointer to tray plugin */
    Window window;				/* X window ID */
    GtkWidget * embed;				/* Raw-XEmbed embedder widget */
} TrayClient;

/* Private context for system tray plugin. */
typedef struct _tray_plugin {
    GtkWidget * plugin;				/* Back pointer to Plugin */
    LuxePanel * panel;
    TrayClient * client_list;			/* List of tray clients */
    BalloonMessage * incomplete_messages;	/* List of balloon messages for which we are awaiting data */
    BalloonMessage * messages;			/* List of balloon messages actively being displayed or waiting to be displayed */
    GtkWidget * balloon_message_popup;		/* Popup showing balloon message */
    guint balloon_message_timer;		/* Timer controlling balloon message */
    Display * dpy;				/* Private X connection (GDK4 has no filters) */
    GSource * source;				/* Event pump for the private connection */
    Window manager_window;			/* Raw X window that holds manager selection */
    Atom selection;				/* _NET_SYSTEM_TRAY_S%d atom */
    guint resync_source;			/* Deferred idle that re-asserts frame geometry */
    gulong layout_changed_id;			/* Panel "layout-changed" handler */
} TrayPlugin;

static void balloon_message_display(TrayPlugin * tr, BalloonMessage * msg);
static void balloon_incomplete_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id);
static void balloon_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id);
static void tray_unmanage_selection(TrayPlugin * tr);
static void tray_destructor(gpointer user_data);
static void client_delete(TrayPlugin * tr, TrayClient * tc, gboolean unlink, gboolean remove);
static void balloon_message_begin_event(TrayPlugin * tr, XClientMessageEvent * xevent);
static void balloon_message_cancel_event(TrayPlugin * tr, XClientMessageEvent * xevent);
static void balloon_message_data_event(TrayPlugin * tr, XClientMessageEvent * xevent);
static void trayclient_request_dock(TrayPlugin * tr, XClientMessageEvent * xevent);
static void tray_schedule_resync(TrayPlugin * tr);
static void tray_panel_layout_changed(LuxePanel * panel, TrayPlugin * tr);

/* Look up a client in the client list. */
static TrayClient * client_lookup(TrayPlugin * tr, Window window)
{
    TrayClient * tc;
    for (tc = tr->client_list; tc != NULL; tc = tc->client_flink)
        {
        if (tc->window == window)
	    return tc;
        if (tc->window > window)
            break;
        }
    return NULL;
}

/*** GTK4: raw-Xlib XEmbed embedder (GtkSocket/GtkPlug are gone) ***/

#define TRAY_TYPE_EMBED (tray_embed_get_type())
typedef struct _TrayEmbed {
    GtkWidget parent;
    Display * dpy;
    LuxePanel * panel;
    Window client;				/* client (plug) window */
    Window frame;				/* embedder frame window, child of panel surface */
    gboolean edge_aware;
} TrayEmbed;
typedef struct _TrayEmbedClass {
    GtkWidgetClass parent_class;
} TrayEmbedClass;
static GType tray_embed_get_type(void);
static void tray_embed_attach(TrayEmbed * te);

G_DEFINE_TYPE(TrayEmbed, tray_embed, GTK_TYPE_WIDGET)

static void tray_embed_realize(GtkWidget * widget)
{
    TrayEmbed * te = (TrayEmbed *) widget;

    GTK_WIDGET_CLASS(tray_embed_parent_class)->realize(widget);
    {
        GdkSurface *surf = gtk_native_get_surface(gtk_widget_get_native(widget));
        if (surf != NULL && GDK_IS_X11_SURFACE(surf))
            te->dpy = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surf));
    }
    if (te->client != None && te->dpy != NULL)
        tray_embed_attach(te);
}

static void tray_embed_release(TrayEmbed *te)
{
    if (te->frame != None && te->dpy != NULL)
    {
        GdkDisplay *display = gdk_display_get_default();
        XWindowAttributes attr;
        if (display == NULL || !GDK_IS_X11_DISPLAY(display))
        {
            te->frame = None;
            return;
        }
        gdk_x11_display_error_trap_push(display);
        /* A plug belongs to another process; keep it alive on tray removal. */
        if (te->client != None && XGetWindowAttributes(te->dpy, te->client, &attr))
        {
            XUnmapWindow(te->dpy, te->client);
            XReparentWindow(te->dpy, te->client, DefaultRootWindow(te->dpy), 0, 0);
            XRemoveFromSaveSet(te->dpy, te->client);
        }
        XDestroyWindow(te->dpy, te->frame);
        gdk_x11_display_error_trap_pop_ignored(display);
        te->frame = None;
    }
}

static void tray_embed_unrealize(GtkWidget * widget)
{
    TrayEmbed * te = (TrayEmbed *) widget;

    tray_embed_release(te);
    GTK_WIDGET_CLASS(tray_embed_parent_class)->unrealize(widget);
}

static void tray_embed_dispose(GObject * object)
{
    TrayEmbed * te = (TrayEmbed *) object;

    tray_embed_release(te);
    G_OBJECT_CLASS(tray_embed_parent_class)->dispose(object);
}

static void tray_embed_measure(GtkWidget * widget,
                               GtkOrientation orientation, int for_size,
                               int * min, int * nat, int * min_base, int * nat_base)
{
    *min = 16;
    *nat = 24;
}

/* Re-assert the position and size of the frame window from the widget's
 * current cached allocation.  Usable both from size_allocate and from
 * deferred resyncs (attach can happen after the last allocation pass,
 * and a screen resize must move already-attached frames). */
static void tray_embed_sync_frame(TrayEmbed * te)
{
    GtkWidget * top;
    gdouble ax = 0, ay = 0;
    int width = gtk_widget_get_width(GTK_WIDGET(te));
    int height = gtk_widget_get_height(GTK_WIDGET(te));

    if (te->frame == None || te->dpy == NULL)
        return;
    top = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(te)));
    if (top != NULL && gtk_widget_translate_coordinates(GTK_WIDGET(te), top, 0, 0, &ax, &ay))
    {
        if (width > 0 && height > 0)
        {
            int x = (int) ax, y = (int) ay;
            if (!te->edge_aware && te->panel &&
                gtk_widget_has_css_class(GTK_WIDGET(te->panel), "system-theme"))
            {
                /* Foreign XEmbed windows render above GTK's inset outline.
                 * Clip only where a frame actually touches that outline;
                 * slots already inside it retain their full size. */
                int panel_width = gtk_widget_get_width(GTK_WIDGET(te->panel));
                int panel_height = gtk_widget_get_height(GTK_WIDGET(te->panel));
                if (panel_width > 2 && panel_height > 2)
                {
                    int right = MIN(x + width, panel_width - 1);
                    int bottom = MIN(y + height, panel_height - 1);
                    x = MAX(x, 1);
                    y = MAX(y, 1);
                    width = MAX(1, right - x);
                    height = MAX(1, bottom - y);
                }
            }
            XMoveResizeWindow(te->dpy, te->frame, x, y, width, height);
            if (te->client != None)
                XMoveResizeWindow(te->dpy, te->client, 0, 0, width, height);
            XMapWindow(te->dpy, te->frame);
        }
        else
            XUnmapWindow(te->dpy, te->frame);
    }
}

static void tray_embed_size_allocate(GtkWidget * widget, int width, int height, int baseline)
{
    TrayEmbed * te = (TrayEmbed *) widget;

    GTK_WIDGET_CLASS(tray_embed_parent_class)->size_allocate(widget, width, height, baseline);
    tray_embed_sync_frame(te);
}

static void tray_embed_class_init(TrayEmbedClass * klass)
{
    GObjectClass * oclass = G_OBJECT_CLASS(klass);
    GtkWidgetClass * wclass = (GtkWidgetClass *) klass;

    oclass->dispose = tray_embed_dispose;
    wclass->realize = tray_embed_realize;
    wclass->unrealize = tray_embed_unrealize;
    wclass->measure = tray_embed_measure;
    wclass->size_allocate = tray_embed_size_allocate;
}

static void tray_embed_init(TrayEmbed * te)
{
    te->client = None;
    te->frame = None;
    te->dpy = NULL;
}

static GtkWidget * tray_embed_new(void)
{
    return GTK_WIDGET(g_object_new(TRAY_TYPE_EMBED, NULL));
}

/* Tray clients which use ParentRelative inherit this XEmbed frame color.
 * Use the theme color, not prefer-dark: fixed-mode dark palettes leave that
 * flag false. Allocate against the client's colormap for non-default visuals.
 * Opaque client-painted backgrounds remain the client's responsibility. */
static unsigned long tray_frame_background_pixel(Display *dpy, Colormap cmap, GtkWidget *widget)
{
    gboolean dark = FALSE;
    XColor color = {0};
    GdkRGBA background;

    if(gtk_style_context_lookup_color(gtk_widget_get_style_context(widget),
                                     "theme_bg_color", &background)) {
        color.red = (unsigned short)(CLAMP(background.red, 0, 1) * 65535 + .5);
        color.green = (unsigned short)(CLAMP(background.green, 0, 1) * 65535 + .5);
        color.blue = (unsigned short)(CLAMP(background.blue, 0, 1) * 65535 + .5);
    } else {
        g_object_get(gtk_settings_get_default(),
                     "gtk-application-prefer-dark-theme", &dark, NULL);
        color.red = color.green = color.blue = dark ? 0x3030 : 0xf2f2;
    }
    if (XAllocColor(dpy, cmap, &color))
        return color.pixel;
    return dark ? 0x303030 : 0xf2f2f2;
}

/* send an XEMBED protocol message to the client */
static void tray_xembed_send(TrayEmbed * te, long message, long d3)
{
    XClientMessageEvent xev;

    memset(&xev, 0, sizeof(xev));
    xev.window = te->client;
    xev.type = ClientMessage;
    xev.message_type = XInternAtom(te->dpy, "_XEMBED", False);
    xev.format = 32;
    xev.data.l[0] = CurrentTime;
    xev.data.l[1] = message;
    xev.data.l[2] = 0;
    xev.data.l[3] = d3;
    xev.data.l[4] = 0;
    XSendEvent(te->dpy, te->client, False, NoEventMask, (XEvent *) &xev);
}

/* reparent the client into our frame window */
static void tray_embed_attach(TrayEmbed * te)
{
    if (te->client == None || te->dpy == NULL)
        return;
    if (te->frame == None)
    {
        XWindowAttributes client_attr;
        XSetWindowAttributes attr;
        GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(GTK_WIDGET(te)));
        if (!XGetWindowAttributes(te->dpy, te->client, &client_attr))
            return;
        /* GtkPlug uses ParentRelative: its new parent must have the same
         * depth, even when the GTK4 panel uses an ARGB visual. */
        attr.background_pixel = tray_frame_background_pixel(te->dpy,
                                                             client_attr.colormap, GTK_WIDGET(te));
        attr.border_pixel = 0;
        attr.colormap = client_attr.colormap;
        te->frame = XCreateWindow(te->dpy, gdk_x11_surface_get_xid(surface),
                                  0, 0, 1, 1, 0, client_attr.depth,
                                  InputOutput, client_attr.visual,
                                  CWBackPixel | CWBorderPixel | CWColormap, &attr);
    }
    XAddToSaveSet(te->dpy, te->client);
    XReparentWindow(te->dpy, te->client, te->frame, 0, 0);
    XResizeWindow(te->dpy, te->client,
                  MAX(1, gtk_widget_get_width(GTK_WIDGET(te))),
                  MAX(1, gtk_widget_get_height(GTK_WIDGET(te))));
    XMapWindow(te->dpy, te->client);
    /* XEMBED_EMBEDDED_NOTIFY */
    tray_xembed_send(te, 0, (long) te->frame);
    XFlush(te->dpy);
}

/*** GTK4: private X connection + GSource pump (gdk_window_add_filter is gone) ***/

typedef struct {
    GSource source;
    GPollFD poll_fd;
    TrayPlugin * tr;
} TraySource;

static gboolean tray_source_prepare(GSource *source, gint *timeout)
{
    TraySource *src = (TraySource *) source;
    *timeout = -1;
    return src->tr->dpy != NULL && XPending(src->tr->dpy) > 0;
}

static gboolean tray_source_check(GSource * source)
{
    TraySource * src = (TraySource *) source;

    if (!(src->poll_fd.revents & G_IO_IN) || src->tr->dpy == NULL)
        return FALSE;
    return XPending(src->tr->dpy) > 0;
}

/* GtkPlug can change its mapped flag after EMBEDDED_NOTIFY. The host
 * must honor that flag, including after an old WM finishes unmapping it. */
static void tray_client_sync_mapped(TrayClient *tc)
{
    TrayEmbed *te = (TrayEmbed *) tc->embed;
    GdkDisplay *display = gdk_display_get_default();
    Atom info, actual;
    int format;
    unsigned long count, remaining;

    if (te == NULL || te->dpy == NULL || display == NULL ||
        !GDK_IS_X11_DISPLAY(display))
        return;
    info = XInternAtom(te->dpy, "_XEMBED_INFO", False);
    unsigned char *data = NULL;
    gdk_x11_display_error_trap_push(display);
    if (XGetWindowProperty(te->dpy, te->client, info, 0, 2, False, info,
                           &actual, &format, &count, &remaining, &data) == Success
        && actual == info && format == 32 && count == 2)
    {
        if (((unsigned long *) data)[1] & 1) /* XEMBED_MAPPED */
            XMapWindow(te->dpy, te->client);
        else
            XUnmapWindow(te->dpy, te->client);
    }
    if (data != NULL)
        XFree(data);
    gdk_x11_display_error_trap_pop_ignored(display);
    XFlush(te->dpy);
}

/* Deferred re-assertion of tray frame geometry.  A client can dock between
 * allocation passes, or the screen can be resized without a full relayout
 * reaching every embed; re-apply each frame from the current allocation. */
static gboolean tray_embed_resync_all(gpointer user_data)
{
    GtkWidget *plugin = user_data;
    TrayPlugin *tr;
    TrayClient *tc;

    if (plugin == NULL)
        return G_SOURCE_REMOVE;
    tr = luxepanel_plugin_get_data(plugin);
    if (tr == NULL)
        return G_SOURCE_REMOVE;
    GdkDisplay * display;

    tr->resync_source = 0;
    if (tr->dpy == NULL)
        return G_SOURCE_REMOVE;
    display = gdk_display_get_default();
    /* Frames are moved on the embeds' GDK connection (te->dpy), so the
     * errors must be trapped there: the private-connection handler set in
     * tray error paths cannot see them, and an untrapped BadWindow would
     * reach GDK's fatal default handler and kill the panel. */
    gdk_x11_display_error_trap_push(display);
    for (tc = tr->client_list; tc != NULL; tc = tc->client_flink)
    {
        if (tc->embed != NULL)
            tray_embed_sync_frame((TrayEmbed *) tc->embed);
    }
    gdk_x11_display_error_trap_pop_ignored(display);
    XFlush(tr->dpy);
    return G_SOURCE_REMOVE;
}

static void tray_schedule_resync(TrayPlugin * tr)
{
    if (tr->resync_source == 0)
        tr->resync_source = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                            tray_embed_resync_all,
                                            g_object_ref(tr->plugin),
                                            g_object_unref);
}

/* The panel re-laid out its contents.  A tray embed can move without its
 * size changing, and GTK4 does not re-run size_allocate for a position-only
 * move, so re-assert every frame's geometry here.  Deferred to an idle so we
 * do not issue X calls from inside the panel's allocation pass. */
static void tray_panel_layout_changed(LuxePanel * panel, TrayPlugin * tr)
{
    tray_schedule_resync(tr);
}

static gboolean tray_source_dispatch(GSource * source, GSourceFunc cb, gpointer data)
{
    TraySource * src = (TraySource *) source;
    TrayPlugin * tr = src->tr;
    Display * dpy = tr->dpy;
    XEvent ev;

    while (dpy != NULL && XPending(dpy) > 0)
    {
        XNextEvent(dpy, &ev);
        if (ev.type == DestroyNotify)
        {
            XDestroyWindowEvent * xev_destroy = &ev.xdestroywindow;
            TrayClient * tc = client_lookup(tr, xev_destroy->window);
            if (tc != NULL)
                client_delete(tr, tc, TRUE, TRUE);
        }
        else if (ev.type == UnmapNotify || ev.type == PropertyNotify)
        {
            Window window = ev.type == UnmapNotify ? ev.xunmap.window : ev.xproperty.window;
            TrayClient *tc = client_lookup(tr, window);
            if (tc != NULL && (ev.type == UnmapNotify ||
                ev.xproperty.atom == XInternAtom(dpy, "_XEMBED_INFO", False)))
                tray_client_sync_mapped(tc);
        }
        else if (ev.type == ClientMessage)
        {
            if (ev.xclient.message_type == a_NET_SYSTEM_TRAY_OPCODE)
            {
                switch (ev.xclient.data.l[1])
                {
                    case SYSTEM_TRAY_REQUEST_DOCK:
                        if (ev.xclient.window == tr->manager_window)
                        {
                            trayclient_request_dock(tr, &ev.xclient);
                        }
                        break;
                    case SYSTEM_TRAY_BEGIN_MESSAGE:
                        balloon_message_begin_event(tr, &ev.xclient);
                        break;
                    case SYSTEM_TRAY_CANCEL_MESSAGE:
                        balloon_message_cancel_event(tr, &ev.xclient);
                        break;
                }
            }
            else if (ev.xclient.message_type == a_NET_SYSTEM_TRAY_MESSAGE_DATA)
            {
                balloon_message_data_event(tr, &ev.xclient);
            }
            /* other client messages on the manager window: ignore */
        }
        else if ((ev.type == SelectionClear)
              && (ev.xclient.window == tr->manager_window))
        {
            tray_unmanage_selection(tr);
        }
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs tray_source_funcs = {
    tray_source_prepare,
    tray_source_check,
    tray_source_dispatch,
    NULL
};

/* Delete a client. */
static void client_delete(TrayPlugin * tr, TrayClient * tc, gboolean unlink, gboolean remove)
{
    //client_print(tr, '-', tc, NULL);

    if (unlink)
    {
        if (tr->client_list == tc)
            tr->client_list = tc->client_flink;
        else
        {
            /* Locate the task and its predecessor in the list and then remove it.  For safety, ensure it is found. */
            TrayClient * tc_pred = NULL;
            TrayClient * tc_cursor;
            for (
              tc_cursor = tr->client_list;
              ((tc_cursor != NULL) && (tc_cursor != tc));
              tc_pred = tc_cursor, tc_cursor = tc_cursor->client_flink) ;
            if (tc_cursor == tc)
                tc_pred->client_flink = tc->client_flink;
        }
    }

    /* Clear out any balloon messages. */
    balloon_incomplete_message_remove(tr, tc->window, TRUE, 0);
    balloon_message_remove(tr, tc->window, TRUE, 0);

    /* Remove the embedder from the icon grid. */
    if (remove && tc->embed != NULL)
    {
        panel_icon_grid_remove(PANEL_ICON_GRID(tr->plugin), tc->embed);
        tc->embed = NULL;
    }

    /* Deallocate the client structure. */
    g_free(tc);
}

/*** Balloon message display ***/

/* Free a balloon message structure. */
static void balloon_message_free(BalloonMessage * message)
{
    g_free(message->string);
    g_free(message);
}

/* General code to deactivate a message and optionally display the next.
 * This is used in three scenarios: balloon clicked, timeout expired, destructor. */
static void balloon_message_advance(TrayPlugin * tr, gboolean destroy_timer, gboolean display_next)
{
    /* Remove the message from the queue. */
    BalloonMessage *msg = tr != NULL ? tr->messages : NULL;
    if (msg == NULL)
        return;
    tr->messages = msg->flink;

    /* Cancel the timer, if set.  This is not done when the timer has expired. */
    if ((destroy_timer) && (tr->balloon_message_timer != 0))
        g_source_remove(tr->balloon_message_timer);
    tr->balloon_message_timer = 0;

    /* Destroy the widget. */
    if (tr->balloon_message_popup != NULL)
        gtk_window_destroy(GTK_WINDOW(tr->balloon_message_popup));
    tr->balloon_message_popup = NULL;

    /* Free the message. */
    balloon_message_free(msg);

    /* If there is another message waiting in the queue, display it.  This is not done in the destructor. */
    if ((display_next) && (tr->messages != NULL))
        balloon_message_display(tr, tr->messages);
}

/* Handler for click gestures from balloon message popup. */
static void balloon_message_activate_event(GtkGestureClick * gesture, gint n,
                                           gdouble x, gdouble y, TrayPlugin * tr)
{
    balloon_message_advance(tr, TRUE, TRUE);
}

/* Timer expiration for balloon message. */
static gboolean balloon_message_timeout(TrayPlugin * tr)
{
    if (tr != NULL)
        balloon_message_advance(tr, FALSE, TRUE);
    return G_SOURCE_REMOVE;
}

/* Create the graphic elements to display a balloon message. */
static void balloon_message_display(TrayPlugin * tr, BalloonMessage * msg)
{
    /* Create a window and an item containing the text. */
    tr->balloon_message_popup = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(tr->balloon_message_popup), FALSE);
    {
        GtkWidget * balloon_text = gtk_label_new(msg->string);
        gtk_label_set_wrap(GTK_LABEL(balloon_text), TRUE);
        gtk_label_set_xalign(GTK_LABEL(balloon_text), 0.5);
        gtk_label_set_yalign(GTK_LABEL(balloon_text), 0.5);
        gtk_window_set_child(GTK_WINDOW(tr->balloon_message_popup), balloon_text);
    }
    gtk_widget_set_margin_top(tr->balloon_message_popup, 4);
    gtk_widget_set_margin_bottom(tr->balloon_message_popup, 4);
    gtk_widget_set_margin_start(tr->balloon_message_popup, 4);
    gtk_widget_set_margin_end(tr->balloon_message_popup, 4);

    /* Connect signals.  Clicking the popup dismisses it and displays the next message, if any. */
    {
        GtkGesture * gesture = gtk_gesture_click_new();
        g_signal_connect(gesture, "pressed", G_CALLBACK(balloon_message_activate_event), (gpointer) tr);
        gtk_widget_add_controller(tr->balloon_message_popup, GTK_EVENT_CONTROLLER(gesture));
    }

    /* Compute the desired position in screen coordinates near the tray plugin. */
    int x;
    int y;
    luxepanel_plugin_popup_set_position_helper(tr->panel, tr->plugin, tr->balloon_message_popup, &x, &y);

    /* Show the popup.  gtk_window_move is gone; move the native surface. */
    gtk_window_present(GTK_WINDOW(tr->balloon_message_popup));
    {
        GdkSurface * surf = gtk_native_get_surface(GTK_NATIVE(tr->balloon_message_popup));
        if (surf != NULL && GDK_IS_X11_SURFACE(surf))
            XMoveWindow(gdk_x11_display_get_xdisplay(gdk_surface_get_display(surf)),
                        gdk_x11_surface_get_xid(surf), x, y);
    }
    /* Set a timer, if the client specified one.  Both are in units of milliseconds. */
    if (msg->timeout != 0)
        tr->balloon_message_timer = g_timeout_add(msg->timeout, (GSourceFunc) balloon_message_timeout, tr);
}

/* Add a balloon message to the tail of the message queue.  If it is the only element, display it immediately. */
static void balloon_message_queue(TrayPlugin * tr, BalloonMessage * msg)
{
    if (tr->messages == NULL)
    {
        tr->messages = msg;
        balloon_message_display(tr, msg);
    }
    else
    {
        BalloonMessage * msg_pred;
        for (msg_pred = tr->messages; ((msg_pred != NULL) && (msg_pred->flink != NULL)); msg_pred = msg_pred->flink) ;
        if (msg_pred != NULL)
            msg_pred->flink = msg;
    }
}

/* Remove an incomplete message from the queue, selected by window and optionally also client's ID.
 * Used in two scenarios: client issues CANCEL (ID significant), client plug removed (ID don't care). */
static void balloon_incomplete_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id)
{
    BalloonMessage * msg_pred = NULL;
    BalloonMessage * msg = tr->incomplete_messages;
    while (msg != NULL)
    {
        /* Establish successor in case of deletion. */
        BalloonMessage * msg_succ = msg->flink;

        if ((msg->window == window) && ((all_ids) || (msg->id == id)))
        {
            /* Found a message matching the criteria.  Unlink and free it. */
            if (msg_pred == NULL)
                tr->incomplete_messages = msg->flink;
            else
                msg_pred->flink = msg->flink;
            balloon_message_free(msg);
        }
        else
            msg_pred = msg;

        /* Advance to successor. */
        msg = msg_succ;
    }
}

/* Remove a message from the message queue, selected by window and optionally also client's ID.
 * Used in two scenarios: client issues CANCEL (ID significant), client plug removed (ID don't care). */
static void balloon_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id)
{
    BalloonMessage * msg_pred = NULL;
    BalloonMessage * msg_head = tr->messages;
    BalloonMessage * msg = msg_head;
    while (msg != NULL)
    {
        /* Establish successor in case of deletion. */
        BalloonMessage * msg_succ = msg->flink;

        if ((msg->window == window) && ((all_ids) || (msg->id == id)))
        {
            /* Found a message matching the criteria. */
            if (msg_pred == NULL)
            {
                /* The message is at the queue head, so is being displayed.  Stop the display. */
                tr->messages = msg->flink;
                if (tr->balloon_message_timer != 0)
                {
                    g_source_remove(tr->balloon_message_timer);
                    tr->balloon_message_timer = 0;
                }
                if (tr->balloon_message_popup != NULL)
                {
                    gtk_window_destroy(GTK_WINDOW(tr->balloon_message_popup));
                    tr->balloon_message_popup = NULL;
                }
            }
            else
                msg_pred->flink = msg->flink;

            /* Free the message. */
            balloon_message_free(msg);
        }
        else
            msg_pred = msg;

        /* Advance to successor. */
        msg = msg_succ;
    }

    /* If there is a new message head, display it now. */
    if ((tr->messages != msg_head) && (tr->messages != NULL))
        balloon_message_display(tr, tr->messages);
}

/*** Event interfaces ***/

#define TRAY_BALLOON_MAX_MESSAGES 32
#define TRAY_BALLOON_MAX_PER_CLIENT 8
#define TRAY_BALLOON_DEFAULT_TIMEOUT 10000
#define TRAY_BALLOON_TRANSFER_TIMEOUT (60 * G_TIME_SPAN_SECOND)
static void balloon_expire_incomplete(TrayPlugin *tr)
{
    gint64 now=g_get_monotonic_time();
    BalloonMessage **link=&tr->incomplete_messages;
    while(*link){
        BalloonMessage *msg=*link;
        if(msg->deadline<=now){*link=msg->flink;balloon_message_free(msg);}
        else link=&msg->flink;
    }
}
static gboolean balloon_has_capacity(TrayPlugin *tr,Window window)
{
    guint total=0,per_client=0;
    for(BalloonMessage *msg=tr->incomplete_messages;msg;msg=msg->flink){total++;if(msg->window==window)per_client++;}
    for(BalloonMessage *msg=tr->messages;msg;msg=msg->flink){total++;if(msg->window==window)per_client++;}
    return total<TRAY_BALLOON_MAX_MESSAGES && per_client<TRAY_BALLOON_MAX_PER_CLIENT;
}

/* Handle a balloon message SYSTEM_TRAY_BEGIN_MESSAGE event. */
static void balloon_message_begin_event(TrayPlugin * tr, XClientMessageEvent * xevent)
{
    TrayClient * client = client_lookup(tr, xevent->window);
    if (client != NULL)
    {
        balloon_expire_incomplete(tr);
        /* Check if the message ID already exists. */
        balloon_incomplete_message_remove(tr, xevent->window, FALSE, xevent->data.l[4]);

        if(!balloon_has_capacity(tr,xevent->window))return;

        /* Allocate a BalloonMessage structure describing the message. */
        BalloonMessage * msg = g_new0(BalloonMessage, 1);
        msg->window = xevent->window;
        msg->timeout = xevent->data.l[2];
        if (msg->timeout <= 0 || msg->timeout > 3600000)
            msg->timeout = TRAY_BALLOON_DEFAULT_TIMEOUT;
        msg->length = xevent->data.l[3];
        if (msg->length < 0 || msg->length > 65536)
        {
            g_warning("Ignoring oversized tray balloon message");
            g_free(msg);
            return;
        }
        msg->id = xevent->data.l[4];
        msg->remaining_length = msg->length;
        msg->deadline = g_get_monotonic_time()+TRAY_BALLOON_TRANSFER_TIMEOUT;
        msg->string = g_new0(char, msg->length + 1);

        /* Message length of 0 indicates that no follow-on messages will be sent. */
        if (msg->length == 0)
            balloon_message_queue(tr, msg);
        else
        {
            /* Add the new message to the queue to await its message text. */
            msg->flink = tr->incomplete_messages;
            tr->incomplete_messages = msg;
        }
    }
}

/* Handle a balloon message SYSTEM_TRAY_CANCEL_MESSAGE event. */
static void balloon_message_cancel_event(TrayPlugin * tr, XClientMessageEvent * xevent)
{
    /* Remove any incomplete messages on this window with the specified ID. */
    balloon_incomplete_message_remove(tr, xevent->window, FALSE, xevent->data.l[2]);

    /* Remove any displaying or waiting messages on this window with the specified ID. */
    TrayClient * client = client_lookup(tr, xevent->window);
    if (client != NULL)
        balloon_message_remove(tr, xevent->window, FALSE, xevent->data.l[2]);
}

/* Handle a balloon message _NET_SYSTEM_TRAY_MESSAGE_DATA event. */
static void balloon_message_data_event(TrayPlugin * tr, XClientMessageEvent * xevent)
{
    balloon_expire_incomplete(tr);
    /* Look up the pending message in the list. */
    BalloonMessage * msg_pred = NULL;
    BalloonMessage * msg;
    for (msg = tr->incomplete_messages; msg != NULL; msg_pred = msg, msg = msg->flink)
    {
        if (xevent->window == msg->window)
        {
            /* Append the message segment to the message. */
            int length = MIN(msg->remaining_length, 20);
            memcpy((msg->string + msg->length - msg->remaining_length), &xevent->data, length);
            msg->remaining_length -= length;

            /* If the message has been completely collected, display it. */
            if (msg->remaining_length == 0)
            {
                /* Unlink the message from the structure. */
                if (msg_pred == NULL)
                    tr->incomplete_messages = msg->flink;
                else
                    msg_pred->flink = msg->flink;

                /* If the client window is valid, queue the message.  Otherwise discard it. */
                TrayClient * client = client_lookup(tr, msg->window);
                if (client != NULL)
                    balloon_message_queue(tr, msg);
                else
                    balloon_message_free(msg);
            }
            break;
        }
    }
}

/* Handler for request dock message. */
static void trayclient_request_dock(TrayPlugin * tr, XClientMessageEvent * xevent)
{
    Window win = (Window)xevent->data.l[2];
    gboolean edge_aware = FALSE;
    /* Search for the window in the client list.  Set up context to do an insert right away if needed. */
    TrayClient * tc_pred = NULL;
    TrayClient * tc_cursor;
    for (tc_cursor = tr->client_list; tc_cursor != NULL; tc_pred = tc_cursor, tc_cursor = tc_cursor->client_flink)
    {
        if (tc_cursor->window == win)
            return;		/* We already got this notification earlier, ignore this one. */
        if (tc_cursor->window > win)
            break;
    }

    /* Dock decision (compat): refuse only None ids and dead windows.
     * _XEMBED_INFO is queried under the X-error trap.  A query that
     * itself raises (BadWindow etc.) means the plug is already gone, so
     * skip it like any other dead plug.  A query that succeeds but finds
     * no (or malformed) _XEMBED_INFO is only logged: many legacy icons
     * (and some current ones) dock without ever setting it, so refusing
     * them would break NetworkManager/Steam-style trays.  Tightening
     * this to fail-closed is left for a future strict mode. */
    if (win == None)
    {
        g_warning("tray: refusing dock request for window id 0");
        return;
    }
    {
        Atom info = XInternAtom(tr->dpy, "_XEMBED_INFO", False);
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char * data = NULL;
        int status;
        XErrorHandler old = XSetErrorHandler(tray_error_handler);
        gboolean dead;

        tray_xerror = FALSE;
        tray_xerror_dpy = tr->dpy;
        status = XGetWindowProperty(tr->dpy, win, info, 0, 2, False, info,
                                    &actual, &format, &count, &remaining, &data);
        Atom marker = XInternAtom(tr->dpy, "_LUXE_TRAY_EDGE_AWARE", False);
        Atom marker_type = None;
        int marker_format = 0;
        unsigned long marker_count = 0, marker_remaining = 0;
        unsigned char * marker_data = NULL;
        int marker_status = XGetWindowProperty(tr->dpy, win, marker, 0, 1,
            False, XA_CARDINAL, &marker_type, &marker_format, &marker_count,
            &marker_remaining, &marker_data);
        if (marker_status == Success && marker_type == XA_CARDINAL &&
            marker_format == 32 && marker_count == 1 && marker_data != NULL &&
            ((unsigned long *)marker_data)[0] == 1)
            edge_aware = TRUE;
        if (marker_data != NULL)
            XFree(marker_data);
        XSync(tr->dpy, False);
        dead = tray_xerror;
        XSetErrorHandler(old);
        tray_xerror_dpy = NULL;
        tray_xerror = FALSE;
        if (dead)
        {
            if (data != NULL)
                XFree(data);
            return;		/* dead window: skip, like other failed plugs */
        }
        if (status != Success || actual != info || format != 32 || count != 2)
            g_message("tray: docking window 0x%lx without valid _XEMBED_INFO (legacy icon, allowing)", win);
        if (data != NULL)
            XFree(data);
    }

    /* Allocate and initialize new client structure. */
    TrayClient * tc = g_new0(TrayClient, 1);
    tc->window = win;
    tc->tr = tr;

    /* Allocate the raw-XEmbed embedder widget. */
    tc->embed = tray_embed_new();

    /* Add the embedder to the icon grid and realize it now so the frame
     * window exists under the panel surface. */
    panel_icon_grid_add(PANEL_ICON_GRID(tr->plugin), tc->embed);
    gtk_widget_show(tc->embed);
    gtk_widget_realize(tc->embed);

    /* Attach the client, trapping X errors (a dead plug must not kill us). */
    {
        TrayEmbed * te = (TrayEmbed *) tc->embed;
        XErrorHandler old;
        gboolean had_error;

        /* The embedder must know which client to reparent. Without this,
         * only an empty frame is displayed (a black square). */
        te->client = tc->window;
        te->panel = tr->panel;
        te->edge_aware = edge_aware;
        tray_xerror = FALSE;
        tray_xerror_dpy = te->dpy;
        old = XSetErrorHandler(tray_error_handler);
        tray_embed_attach(te);
        tray_embed_sync_frame(te);
        XSync(te->dpy, False);
        had_error = tray_xerror;
        XSetErrorHandler(old);
        tray_xerror_dpy = NULL;
        if (had_error)
        {
            panel_icon_grid_remove(PANEL_ICON_GRID(tr->plugin), tc->embed);
            g_free(tc);
            return;
        }
    }
    tray_xerror = FALSE;
    tray_xerror_dpy = tr->dpy;
    XErrorHandler old = XSetErrorHandler(tray_error_handler);
    XSelectInput(tr->dpy, tc->window, StructureNotifyMask | PropertyChangeMask);
    XSync(tr->dpy, False);
    XSetErrorHandler(old);
    tray_xerror_dpy = NULL;
    if (tray_xerror)
    {
        panel_icon_grid_remove(PANEL_ICON_GRID(tr->plugin), tc->embed);
        g_free(tc);
        return;
    }

    tray_client_sync_mapped(tc);
    /* The last allocation pass may have run before the frame existed;
     * re-assert frame geometry once the layout settles. */
    tray_schedule_resync(tr);

    /* Link the client structure into the client list. */
    if (tc_pred == NULL)
    {
        tc->client_flink = tr->client_list;
        tr->client_list = tc;
    }
    else
    {
        tc->client_flink = tc_pred->client_flink;
        tc_pred->client_flink = tc;
    }
}

/* Delete the selection on the manager window. */
static void tray_unmanage_selection(TrayPlugin * tr)
{
    if (tr->dpy != NULL && tr->manager_window != None)
    {
        if (XGetSelectionOwner(tr->dpy, tr->selection) == tr->manager_window)
            XSetSelectionOwner(tr->dpy, tr->selection, None, CurrentTime);
        XDestroyWindow(tr->dpy, tr->manager_window);
        tr->manager_window = None;
        XFlush(tr->dpy);
    }
}

/* Plugin constructor. */
static GtkWidget *tray_constructor(LuxePanel *panel, config_setting_t *settings)
{
    GtkWidget *p;
    TraySource * src;
    Display * dpy;
    Screen * xscreen;
    char * selection_atom_name;
    Atom selection_atom;
    XSetWindowAttributes attr;
    Window manager_window;
    guint32 timestamp;
    XClientMessageEvent xev;
    gulong data;
    gboolean owner;

    /* Private X connection: GDK4 has no event filters. */
    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        g_warning("tray: cannot open X display");
        return NULL;
    }
    xscreen = ScreenOfDisplay(dpy, DefaultScreen(dpy));

    /* Create the selection atom.  This has the screen number in it. */
    selection_atom_name = g_strdup_printf("_NET_SYSTEM_TRAY_S%d", DefaultScreen(dpy));
    selection_atom = XInternAtom(dpy, selection_atom_name, False);
    g_free(selection_atom_name);

    /* If the selection is already owned, there is another tray running. */
    if (XGetSelectionOwner(dpy, selection_atom) != None)
    {
        g_warning("tray: another systray already running");
        XCloseDisplay(dpy);
        return NULL;
    }

    /* Create a raw input-only window to hold the selection. */
    attr.override_redirect = True;
    attr.event_mask = StructureNotifyMask;
    manager_window = XCreateWindow(dpy, RootWindowOfScreen(xscreen),
                                   -1, -1, 1, 1, 0,
                                   CopyFromParent, InputOnly, CopyFromParent,
                                   CWOverrideRedirect | CWEventMask, &attr);

    /* Try to claim the _NET_SYSTEM_TRAY_Sn selection. */
    timestamp = CurrentTime;
    owner = (XSetSelectionOwner(dpy, selection_atom, manager_window, timestamp)
             != BadAlloc
             && XGetSelectionOwner(dpy, selection_atom) == manager_window);
    if (owner)
    {
        /* Send MANAGER client event (ICCCM). */
        memset(&xev, 0, sizeof(xev));
        xev.type = ClientMessage;
        xev.window = RootWindowOfScreen(xscreen);
        xev.message_type = a_MANAGER;
        xev.format = 32;
        xev.data.l[0] = CurrentTime;
        xev.data.l[1] = selection_atom;
        xev.data.l[2] = manager_window;
        xev.data.l[3] = 0;    /* manager specific data */
        xev.data.l[4] = 0;    /* manager specific data */
        XSendEvent(dpy, RootWindowOfScreen(xscreen), False, StructureNotifyMask, (XEvent *) &xev);

        /* Set the orientation property.
         * We always set "horizontal" since even vertical panels are designed to use a lot of width. */
        data = SYSTEM_TRAY_ORIENTATION_HORZ;
        XChangeProperty(
            dpy,
            manager_window,
            a_NET_SYSTEM_TRAY_ORIENTATION,
            XA_CARDINAL, 32,
            PropModeReplace,
            (guchar *) &data, 1);
        XFlush(dpy);
    }
    else
    {
        XDestroyWindow(dpy, manager_window);
        XCloseDisplay(dpy);
        g_printerr("tray: System tray didn't get the system tray manager selection\n");
        return 0;
    }

    /* Allocate plugin context and set into Plugin private data pointer. */
    TrayPlugin * tr = g_new0(TrayPlugin, 1);
    tr->panel = panel;
    tr->dpy = dpy;
    tr->selection = selection_atom;
    tr->manager_window = manager_window;

    /* Install the event pump for the private connection. */
    src = (TraySource *) g_source_new(&tray_source_funcs, sizeof(TraySource));
    src->tr = tr;
    src->poll_fd.fd = ConnectionNumber(dpy);
    src->poll_fd.events = G_IO_IN;
    g_source_add_poll((GSource *) src, &src->poll_fd);
    g_source_attach((GSource *) src, NULL);
    tr->source = (GSource *) src;

    /* Allocate top level widget and set into Plugin widget pointer. */
    tr->plugin = p = panel_icon_grid_new(panel_get_orientation(panel),
                                         panel_get_icon_size(panel),
                                         panel_get_icon_size(panel),
                                         3, 0, panel_get_height(panel));
    /* Frames are positioned from the widget allocation; the panel allocates
     * its contents in its own size_allocate, which does not reach an embed
     * that only moved. */
    tr->layout_changed_id = g_signal_connect(panel, "layout-changed",
                                             G_CALLBACK(tray_panel_layout_changed), tr);
    luxepanel_plugin_set_data(p, tr, tray_destructor);
    gtk_widget_set_name(p, "tray");
    panel_icon_grid_set_aspect_width(PANEL_ICON_GRID(p), TRUE);

    return p;
}

/* Plugin destructor. */
static void tray_destructor(gpointer user_data)
{
    TrayPlugin * tr = user_data;

    /* Stop listening for panel layout changes before teardown. */
    if (tr->layout_changed_id != 0 && tr->panel != NULL)
    {
        g_signal_handler_disconnect(tr->panel, tr->layout_changed_id);
        tr->layout_changed_id = 0;
    }

    /* Remove the private-connection event pump. */
    if (tr->source != NULL)
    {
        g_source_destroy(tr->source);
        g_source_unref(tr->source);
        tr->source = NULL;
    }

    /* Remove a pending frame-geometry resync. */
    if (tr->resync_source != 0)
    {
        g_source_remove(tr->resync_source);
        tr->resync_source = 0;
    }

    /* Make sure we drop the manager selection. */
    tray_unmanage_selection(tr);

    /* Deallocate incomplete messages. */
    while (tr->incomplete_messages != NULL)
    {
        BalloonMessage * msg_succ = tr->incomplete_messages->flink;
        balloon_message_free(tr->incomplete_messages);
        tr->incomplete_messages = msg_succ;
    }

    /* Terminate message display and deallocate messages. */
    while (tr->messages != NULL)
        balloon_message_advance(tr, TRUE, FALSE);

    /* Deallocate client list and embedder widgets. */
    while (tr->client_list != NULL)
    {
        TrayClient * tc = tr->client_list;
        if (tc->embed != NULL)
        {
            panel_icon_grid_remove(PANEL_ICON_GRID(tr->plugin), tc->embed);
            tc->embed = NULL;
        }
        client_delete(tr, tc, TRUE, FALSE);
    }

    /* Close the private X connection. */
    if (tr->dpy != NULL)
    {
        XCloseDisplay(tr->dpy);
        tr->dpy = NULL;
    }

    g_free(tr);
}

/* Callback when panel configuration changes. */
static void tray_panel_configuration_changed(LuxePanel *panel, GtkWidget *p)
{
    /* Set orientation into the icon grid. */
    panel_icon_grid_set_geometry(PANEL_ICON_GRID(p), panel_get_orientation(panel),
                                 panel_get_icon_size(panel),
                                 panel_get_icon_size(panel),
                                 3, 0, panel_get_height(panel));
    TrayPlugin *tr = luxepanel_plugin_get_data(p);
    if (tr != NULL)
        tray_schedule_resync(tr);
}

/* Plugin descriptor. */
LuxePanelPluginInit luxepanel_static_plugin_tray = {
    .name = N_("System Tray"),
    .description = N_("System tray"),

    /* Set a flag to identify the system tray.  It is special in that only one per system can exist. */
    .one_per_system = TRUE,

    .new_instance = tray_constructor,
    .reconfigure = tray_panel_configuration_changed
};

/* vim: set sw=4 sts=4 et : */
