#include "../luxe-about.h"
#include "luxe-dialogs-gtk.h"
/*
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Jürgen Hötzel <juergen@archlinux.org>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Lajos Kamocsay <lajos@panka.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Jack Chen <speed.up08311990@gmail.com>
 *               2012 Rafał Mużyło <galtgendo@gmail.com>
 *               2012 Michael Rawson <michaelrawson76@gmail.com>
 *               2012 Julien Lavergne <julien.lavergne@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2013 peadaredwards <peadaredwards@users.sourceforge.net>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2015 Rafał Mużyło <galtgendo@gmail.com>
 *               2015 Hanno Zulla <hhz@users.sf.net>
 *               2018 Mamoru TASAKA <mtasaka@fedoraproject.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <gdk/x11/gdkx.h>
#include <libfm/fm.h>
#include <cairo-xlib.h>

#define __LUXEPANEL_INTERNALS__

#include "private.h"
#include "misc.h"
#include "space.h"

#include "luxepanelctl.h"
#include "dbg.h"
#include "gtk-compat.h"

gchar *cprofile = "default";

GSList* all_panels = NULL;  /* a single-linked list storing all panels */

gboolean is_in_lxde = FALSE;

static GtkWindowGroup* win_grp = NULL; /* window group used to limit the scope of model dialog. */

static gulong monitors_handler = 0;

/* Geometry changes of an existing monitor emit GdkMonitor::invalidate, not a
 * monitors-list change.  Track per-monitor handlers so a screen resize or a
 * mode switch relayouts the panels (tray frames, plugin geometry, struts). */
static GHashTable* monitor_invalidate_ids = NULL;

static void panel_start_gui(LuxePanel *p, config_setting_t *list);
static void on_display_closed(GdkDisplay *display, gpointer user_data);
static void ah_start(LuxePanel *p);
static void ah_stop(LuxePanel *p);
static void _panel_update_background(LuxePanel * p, gboolean enforce);

enum
{
    ICON_SIZE_CHANGED,
    PANEL_FONT_CHANGED,
    PANEL_LAYOUT_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_TYPE(PanelToplevel, luxepanel, GTK_TYPE_WINDOW);

static void luxepanel_finalize(GObject *object)
{
    LuxePanel *self = LUXEPANEL(object);
    Panel *p = self->priv;

    if( p->config_changed )
        luxepanel_config_save( self );
    config_destroy(p->config);

    //XFree(p->workarea);
    g_free( p->background_file );
    g_slist_free( p->system_menus );

    g_free( p->name );
    g_free(p);

    G_OBJECT_CLASS(luxepanel_parent_class)->finalize(object);
}

static void panel_stop_gui(LuxePanel *self)
{
    Panel *p = self->priv;
    Display *xdisplay = NULL;
    GdkDisplay *display = p->display;

    if (p->gui_stopped)
        return;
    p->gui_stopped = TRUE;
    g_debug("panel_stop_gui on '%s'", p->name ? p->name : "(unnamed)");
    if (p->autohide)
        ah_stop(self);

    if (p->pref_dialog != NULL)
    {
        GtkWidget *dialog = p->pref_dialog;
        p->pref_dialog = NULL;
        gtk_window_destroy(GTK_WINDOW(dialog));
    }

    if (p->plugin_pref_dialog != NULL)
    {
        GtkWidget *dialog = p->plugin_pref_dialog;
        p->plugin_pref_dialog = NULL;
        /* just close the dialog, it will do all required cleanup */
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
    }

    if (p->initialized)
    {
        if (win_grp != NULL)
            gtk_window_group_remove_window(win_grp, GTK_WINDOW(self));
        if (luxepanel_display_usable(display))
        {
            xdisplay = GDK_DISPLAY_XDISPLAY(display);
            gdk_display_flush(display);
            XFlush(xdisplay);
            XSync(xdisplay, True);
        }
        p->initialized = FALSE;
    }
    if (p->surface != NULL)
    {
        cairo_surface_destroy(p->surface);
        p->surface = NULL;
    }

    if (p->background_update_queued)
    {
        g_source_remove(p->background_update_queued);
        p->background_update_queued = 0;
    }
    if (p->strut_update_queued)
    {
        g_source_remove(p->strut_update_queued);
        p->strut_update_queued = 0;
    }
    if (p->reconfigure_queued)
    {
        g_source_remove(p->reconfigure_queued);
        p->reconfigure_queued = 0;
    }
    if (p->display_closed_handler != 0 && display != NULL &&
        G_IS_OBJECT(display))
    {
        g_signal_handler_disconnect(display, p->display_closed_handler);
        p->display_closed_handler = 0;
    }

    if (p->box != NULL)
    {
        if (gtk_widget_get_parent(p->box) == GTK_WIDGET(self))
            gtk_widget_unparent(p->box);
        p->box = NULL;
    }
}

static void luxepanel_dispose(GObject *object)
{
    panel_stop_gui(LUXEPANEL(object));
    G_OBJECT_CLASS(luxepanel_parent_class)->dispose(object);
}

static gboolean idle_update_background(gpointer p)
{
    LuxePanel *panel = LUXEPANEL(p);

    if (panel->priv->gui_stopped)
        return G_SOURCE_REMOVE;

    /* The source owns a panel reference, so this remains safe even when the
     * window was disposed between queueing and dispatch. */
    if (gtk_widget_get_realized(GTK_WIDGET(panel)))
    {
        gdk_display_sync(gtk_widget_get_display(GTK_WIDGET(panel)));
        _panel_update_background(panel, FALSE);
    }
    panel->priv->background_update_queued = 0;

    return G_SOURCE_REMOVE;
}

void _panel_queue_update_background(LuxePanel *panel)
{
    if (panel == NULL || panel->priv->gui_stopped ||
        panel->priv->background_update_queued)
        return;
    panel->priv->background_update_queued = g_idle_add_full(G_PRIORITY_HIGH,
                                                            idle_update_background,
                                                            g_object_ref(panel),
                                                            g_object_unref);
}

static gboolean idle_update_strut(gpointer p)
{
    LuxePanel *panel = LUXEPANEL(p);

    if (panel->priv->gui_stopped)
        return G_SOURCE_REMOVE;
    _panel_set_wm_strut(panel);
    panel->priv->strut_update_queued = 0;

    return G_SOURCE_REMOVE;
}

static void luxepanel_realize(GtkWidget *widget)
{
    GTK_WIDGET_CLASS(luxepanel_parent_class)->realize(widget);

    _panel_queue_update_background(LUXEPANEL(widget));
}

static void luxepanel_size_request(GtkWidget *widget, GtkRequisition *req);
static void luxepanel_button_press(GtkWidget *widget, GdkEvent *event);

/* GTK4: replacement for the legacy get_preferred_width/height pair --
 * the geometry math lives in luxepanel_size_request. */
static void luxepanel_measure(GtkWidget *widget, GtkOrientation orientation,
                            int for_size, int *minimum, int *natural,
                            int *minimum_baseline, int *natural_baseline)
{
    GtkRequisition requisition;
    int min = 0, nat = 0;

    if (minimum_baseline)
        *minimum_baseline = -1;
    if (natural_baseline)
        *natural_baseline = -1;

    /* measure the child box, then apply panel geometry */
    luxepanel_size_request(widget, &requisition);
    (void)orientation;
    (void)for_size;
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        min = requisition.width;
        nat = requisition.width;
    }
    else
    {
        min = requisition.height;
        nat = requisition.height;
    }
    if (minimum)
        *minimum = min;
    if (natural)
        *natural = nat;
}

static void luxepanel_size_request(GtkWidget *widget, GtkRequisition *req)
{
    LuxePanel *panel = LUXEPANEL(widget);
    Panel *p = panel->priv;
    GdkRectangle rect;

    {
        int min_w, nat_w, min_h, nat_h;
        GtkWidget *child = gtk_widget_get_first_child(widget);
        if (child != NULL && GTK_IS_WIDGET(child))
        {
            gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1,
                               &min_w, &nat_w, NULL, NULL);
            req->width = nat_w;
            gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, -1,
                               &min_h, &nat_h, NULL, NULL);
            req->height = nat_h;
        }
        else
        {
            req->width = req->height = 0;
        }
    }

    if (!p->visible)
        /* When the panel is in invisible state, the content box also got hidden, thus always
         * report 0 size.  Ask the content box instead for its size. */
    {
        GtkWidget *child = gtk_widget_get_first_child(widget);
        if (child != NULL && GTK_IS_WIDGET(child))
        {
            int min_w, nat_w, min_h, nat_h;
            gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1, &min_w, &nat_w, NULL, NULL);
            req->width = nat_w;
            gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, -1, &min_h, &nat_h, NULL, NULL);
            req->height = nat_h;
        }
    }

    rect.width = req->width;
    rect.height = req->height;
    _calculate_position(panel, &rect);
    req->width = rect.width;
    req->height = rect.height;
    /* update data ahead of configuration request */
    p->cw = rect.width;
    p->ch = rect.height;
}

static void luxepanel_size_allocate(GtkWidget *widget, int width, int height, int baseline);

static void luxepanel_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
    LuxePanel *panel = LUXEPANEL(widget);
    Panel *p = panel->priv;
    GdkRectangle rect;
    gint x = p->ax, y = p->ay;

    /* some WM like mwm are too generous giving us space more than requested
       so let correct it right now, as much as we can */
    rect.x = p->ax;
    rect.y = p->ay;
    rect.width = MAX(8, MIN(p->cw, width));
    rect.height = MAX(8, MIN(p->ch, height));
    _calculate_position(panel, &rect);

    /* Chain to GtkWindow's size_allocate: it drives the surface resize
       and allocates the child box.  Calling gtk_widget_size_allocate()
       here would re-enter this vfunc instead (and GTK drops it as the
       alloc-needed flag is already consumed), leaving the content box
       unallocated ("snapshot without a current allocation"). */
    GTK_WIDGET_CLASS(luxepanel_parent_class)->size_allocate(widget, rect.width, rect.height, baseline);

    /* GTK4 does not re-run a child's size_allocate when the child only moves
     * (its size is unchanged), so widgets that position foreign X windows
     * from their allocation -- the tray -- must be told to re-read it. */
    g_signal_emit(panel, signals[PANEL_LAYOUT_CHANGED], 0);

    if (p->widthtype == WIDTH_REQUEST)
        p->width = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? rect.width : rect.height;
    if (p->heighttype == HEIGHT_REQUEST)
        p->height = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? rect.height : rect.width;

    p->ax = rect.x;
    p->ay = rect.y;

    if (rect.width != p->aw || rect.height != p->ah || x != p->ax || y != p->ay)
    {
        p->aw = rect.width;
        p->ah = rect.height;
        /* gtk_window_move is gone; position via X11 directly */
        {
            GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));
            if (surface != NULL)
                XMoveWindow(GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(surface)),
                            gdk_x11_surface_get_xid(surface), p->ax, p->ay);
        }
        /* SF bug #708: strut update does not work while in size allocation */
        if (!panel->priv->strut_update_queued)
            panel->priv->strut_update_queued = g_idle_add_full(G_PRIORITY_HIGH,
                                                               idle_update_strut,
                                                               g_object_ref(panel),
                                                               g_object_unref);
        _panel_queue_update_background(panel);
    }

    if (gtk_widget_get_mapped(widget))
        _panel_establish_autohide(panel);
}

static void luxepanel_map(GtkWidget *widget)
{
    Panel *p = LUXEPANEL(widget)->priv;

    GTK_WIDGET_CLASS(luxepanel_parent_class)->map(widget);

    if (p->autohide)
        ah_start(LUXEPANEL(widget));
}

/* GTK4: draw the computed background surface (paint_root_pixmap result
 * etc.), replacing the app_paintable/back-pixmap mechanism. */
static void luxepanel_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    LuxePanel *panel = LUXEPANEL(widget);
    Panel *p = panel->priv;

    if (p->surface != NULL)
    {
        graphene_rect_t bounds;
        cairo_t *cr;

        graphene_rect_init(&bounds, 0, 0,
                           gtk_widget_get_width(widget),
                           gtk_widget_get_height(widget));
        cr = gtk_snapshot_append_cairo(snapshot, &bounds);
        cairo_set_source_surface(cr, p->surface, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
    }

    GTK_WIDGET_CLASS(luxepanel_parent_class)->snapshot(widget, snapshot);
}

static void luxepanel_click_gesture(GtkGestureClick *gesture, gint n,
                                  gdouble x, gdouble y, gpointer user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));

    luxepanel_button_press(widget, event);
}

static void luxepanel_click_release(GtkGestureClick *gesture, gint n,
                                  gdouble x, gdouble y, gpointer user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));

    if (event)
        _luxepanel_button_release(widget, event);
}

static void luxepanel_motion_cb(GtkEventControllerMotion *m,
                              gdouble x, gdouble y, gpointer user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(m));
    GdkEvent *event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(m));

    if (event)
        _luxepanel_motion_notify(widget, event);
}

static void luxepanel_button_press(GtkWidget *widget, GdkEvent *event)
{
    LuxePanel *panel = PLUGIN_PANEL(widget);

    guint button = gdk_button_event_get_button(event);
    GdkModifierType state = gdk_event_get_modifier_state(event);

    if ((state & gtk_accelerator_get_default_mod_mask()) != 0)
        /* ignore clicks with modifiers */
        return;

    if (button == 3) /* right button */
    {
        GMenu *popup = luxepanel_get_plugin_menu(panel, NULL, FALSE);
        luxepanel_show_popup_menu_at_widget(popup, widget);
        return;
    }
    else if (button == 2) /* middle button */
    {
        Panel *p = panel->priv;
        if (p->move_state == PANEL_MOVE_STOP)
        {
            GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));
            gint ox = 0, oy = 0;
            if (surface != NULL)
            {
                Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(surface));
                Window root;
                XTranslateCoordinates(dpy, gdk_x11_surface_get_xid(surface),
                                      DefaultRootWindow(dpy), 0, 0, &ox, &oy, &root);
            }
            {
                gdouble ex, ey;
                gdk_event_get_position(event, &ex, &ey);
                p->move_x = ox + (gint)ex - p->ax;
                p->move_y = oy + (gint)ey - p->ay;
            }
            p->move_state = PANEL_MOVE_DETECT;
            p->move_device = gdk_event_get_device(event);
            /* rest of work see in panel-plugin-move.c file */
        }
    }
}

static void luxepanel_class_init(PanelToplevelClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

    gobject_class->finalize = luxepanel_finalize;
    gobject_class->dispose = luxepanel_dispose;
    widget_class->realize = luxepanel_realize;
    widget_class->measure = luxepanel_measure;
    widget_class->size_allocate = luxepanel_size_allocate;
    widget_class->map = luxepanel_map;
    widget_class->snapshot = luxepanel_snapshot;

    signals[ICON_SIZE_CHANGED] =
        g_signal_new("icon-size-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(PanelToplevelClass, icon_size_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0, G_TYPE_NONE);

    signals[PANEL_FONT_CHANGED] =
        g_signal_new("panel-font-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(PanelToplevelClass, panel_font_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0, G_TYPE_NONE);

    signals[PANEL_LAYOUT_CHANGED] =
        g_signal_new("layout-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(PanelToplevelClass, layout_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0, G_TYPE_NONE);
}

static void luxepanel_init(PanelToplevel *self)
{
    Panel *p = g_new0(Panel, 1);

    self->priv = p;
    p->topgwin = self;
    p->display = gdk_display_get_default();
    gtk_widget_add_css_class(GTK_WIDGET(self), "luxe-panel");
    static gsize styles_loaded;
    if(g_once_init_enter(&styles_loaded)) {
        GtkCssProvider *styles = gtk_css_provider_new();
        gtk_css_provider_load_from_string(styles,
            ".luxe-panel { background-color: transparent; }"
            ".luxe-panel.system-theme { background-color: @theme_bg_color; color: @theme_fg_color;"
            " box-shadow: inset 0 0 0 1px alpha(@theme_fg_color, .22); }"
            ".luxe-panel.system-theme.high-contrast { box-shadow: inset 0 0 0 1px @theme_fg_color; }"
            "button.luxe-menu-wordmark { min-height: 0; min-width: 0; padding: 0 10px; margin: 1px;"
            " border-radius: 3px; border: 1px solid alpha(@theme_fg_color, .22);"
            " color: @theme_fg_color; font-size: 18px; font-weight: 400; box-shadow: none; text-shadow: none;"
            " background-image: linear-gradient(to bottom, shade(@theme_bg_color, 1.08), shade(@theme_bg_color, .98)); }"
            "button.luxe-menu-wordmark.menu-hover { background-image: linear-gradient(to bottom, shade(@theme_bg_color, 1.16), shade(@theme_bg_color, 1.04)); }"
            "button.luxe-menu-wordmark.menu-open { border-bottom-color: @theme_selected_bg_color;"
            " background-image: linear-gradient(to bottom, mix(@theme_bg_color, @theme_selected_bg_color, .15), @theme_bg_color); }"
            "button.luxe-menu-wordmark.vertical { padding: 0 1px; font-size: 10px; }"
            /* Adwaita's 5px vertical button padding leaves a 28px panel's task
             * buttons too short for their own label; their border still frames them. */
            "button.luxe-task-button { min-height: 0; padding-top: 0; padding-bottom: 0; }"
            ".luxe-panel popover > contents { border: 1px solid alpha(@theme_fg_color, .25); }"
            ".luxe-panel popover list { background-color: transparent; }"
            ".luxe-panel popover row { padding: 3px 6px; }"
            ".luxe-panel popover row:selected { background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }"
            ".luxe-application-menu row.luxe-menu-item { border-radius: 4px;"
            " transition: background-color 100ms ease-out, box-shadow 100ms ease-out; }"
            ".luxe-application-menu row.luxe-menu-item.menu-hover:not(:selected) {"
            " background-color: alpha(@theme_selected_bg_color, .18); color: @theme_fg_color;"
            " box-shadow: inset 3px 0 alpha(@theme_selected_bg_color, .8); }"
            ".luxe-application-menu row.luxe-menu-item:selected {"
            " background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }"
            ".luxe-application-menu row.luxe-menu-item:focus-visible { outline-color: @theme_selected_bg_color; }"
            ".luxe-panel.high-contrast button.luxe-menu-wordmark { background-image: none;"
            " background-color: @theme_bg_color; border: 1px solid @theme_fg_color; }");
        gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(styles), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(styles);
        g_once_init_leave(&styles_loaded, 1);
    }
    /* click handling for panel context menu / middle-drag */
    {
        GtkGesture *gesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
        g_signal_connect(gesture, "pressed",
                         G_CALLBACK(luxepanel_click_gesture), NULL);
        g_signal_connect(gesture, "released",
                         G_CALLBACK(luxepanel_click_release), NULL);
        gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(gesture));
        /* motion events drive the middle-button drag-move */
        {
            GtkEventController *motion = gtk_event_controller_motion_new();
            g_signal_connect(motion, "motion",
                             G_CALLBACK(luxepanel_motion_cb), NULL);
            gtk_widget_add_controller(GTK_WIDGET(self), motion);
        }
    }
    p->align = ALIGN_CENTER;
    p->edge = EDGE_NONE;
    p->widthtype = WIDTH_PERCENT;
    p->width = 100;
    p->heighttype = HEIGHT_PIXEL;
    p->height = PANEL_HEIGHT_DEFAULT;
    p->monitor = 0;
    p->setdocktype = 1;
    p->setstrut = 1;
    p->round_corners = 0;
    p->autohide = 0;
    p->visible = TRUE;
    p->height_when_hidden = 2;
    p->transparent = 0;
    p->alpha = 255;
    p->tintcolor = gcolor2rgb24(&p->gtintcolor);
    p->usefontcolor = 0;
    p->fontcolor = 0x00000000;
    p->usefontsize = 0;
    p->fontsize = 10;
    p->spacing = 0;
    p->icon_size = PANEL_ICON_SIZE;
    p->icon_theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    p->defstyle = gtk_widget_get_style_context(GTK_WIDGET(self));
    p->gtintcolor.red = p->gtintcolor.green = p->gtintcolor.blue = 1.0;
    p->gtintcolor.alpha = 1.0;

    p->tintcolor = gcolor2rgb24(&p->gtintcolor);
    p->usefontcolor = 0;
    p->fontcolor = 0x00000000;
    p->usefontsize = 0;
    p->fontsize = 10;
    p->icon_size = PANEL_ICON_SIZE;
    p->config = config_new();
}

/* Allocate and initialize new Panel structure. */
static LuxePanel* panel_allocate(void)
{
    return g_object_new(LX_TYPE_PANEL,
                        "decorated", FALSE,
                        "name", "PanelToplevel",
                        "resizable", FALSE,
                        "title", "panel",
                        NULL);
}

void _panel_emit_icon_size_changed(LuxePanel *p)
{
    g_signal_emit(p, signals[ICON_SIZE_CHANGED], 0);
}

void _panel_emit_font_changed(LuxePanel *p)
{
    g_signal_emit(p, signals[PANEL_FONT_CHANGED], 0);
}

/* Normalize panel configuration after load from file or reconfiguration. */
static void panel_normalize_configuration(Panel* p)
{
    if (p == NULL || p->topgwin == NULL)
        return;
    panel_set_panel_configuration_changed(p);
    p->margin = CLAMP(p->margin, 0, 10000);
    if (p->widthtype == WIDTH_PERCENT)
        p->width = CLAMP(p->width, 1, 100);
    else
        p->width = CLAMP(p->width, 1, 10000);
    p->heighttype = HEIGHT_PIXEL;
    p->height = CLAMP(p->height, PANEL_HEIGHT_MIN, PANEL_HEIGHT_MAX);
    p->monitor = MAX(p->monitor, -1);
    p->alpha = CLAMP(p->alpha, 0, 255);
    p->height_when_hidden = CLAMP(p->height_when_hidden, 0, PANEL_HEIGHT_MAX);
    p->fontsize = CLAMP(p->fontsize, 6, 96);
    p->icon_size = CLAMP(p->icon_size, 8, 256);
    if (p->background)
        p->transparent = 0;
}

gboolean _panel_edge_can_strut(LuxePanel *panel, int edge, gint monitor, gulong *size)
{
    Panel *p;
    GdkRectangle rect;
    GdkRectangle rect2;
    gint n, i;
    gulong s;

    if (panel == NULL || panel->priv == NULL)
        return FALSE;
    if (!gtk_widget_get_mapped(GTK_WIDGET(panel)))
        return FALSE;

    p = panel->priv;
    /* The display may have been closed (X error); never touch a stale pointer.
     * A closed display is non-NULL but fails GDK_IS_DISPLAY; querying
     * monitors on it segfaults (exit 11). */
    if (!luxepanel_display_usable(p->display))
        return FALSE;
    /* Handle autohide case.  EWMH recommends having the strut be the minimized size. */
    if (p->autohide)
        s = p->height_when_hidden;
    else switch (edge)
    {
    case EDGE_LEFT:
    case EDGE_RIGHT:
        s = p->aw;
        break;
    case EDGE_TOP:
    case EDGE_BOTTOM:
        s = p->ah;
        break;
    default: /* error! */
        return FALSE;
    }
    if (s == 0)
        return FALSE; /* nothing to strut here */

    if (monitor < 0) /* screen span */
    {
        if (G_LIKELY(size))
            *size = s;
        return TRUE;
    }

    {
        GListModel *monitor_model = gdk_display_get_monitors(p->display);
        if (!G_IS_LIST_MODEL(monitor_model))
            return FALSE;
        n = g_list_model_get_n_items(monitor_model);
    }
    if (monitor >= n) /* hidden now */
        return FALSE;
    {
        GdkMonitor *mon = GDK_MONITOR(g_list_model_get_item(gdk_display_get_monitors(p->display), monitor));
        if (mon == NULL)
            return FALSE;
        gdk_monitor_get_geometry(mon, &rect);
        g_object_unref(mon);
    }
    {
        GdkMonitor *mon;
        guint j;
        gint screen_w = 0, screen_h = 0;
        GListModel *monitors = gdk_display_get_monitors(p->display);
        if (!G_IS_LIST_MODEL(monitors))
            return FALSE;
        for (j = 0; j < g_list_model_get_n_items(monitors); j++)
        {
            GdkRectangle g;
            mon = GDK_MONITOR(g_list_model_get_item(monitors, j));
            if (mon == NULL)
                continue;
            gdk_monitor_get_geometry(mon, &g);
            g_object_unref(mon);
            screen_w = MAX(screen_w, g.x + g.width);
            screen_h = MAX(screen_h, g.y + g.height);
        }
        switch (edge)
        {
            case EDGE_LEFT:
                rect.width = rect.x;
                rect.x = 0;
                s += rect.width;
                break;
            case EDGE_RIGHT:
                rect.x += rect.width;
                rect.width = screen_w - rect.x;
                s += rect.width;
                break;
            case EDGE_TOP:
                rect.height = rect.y;
                rect.y = 0;
                s += rect.height;
                break;
            case EDGE_BOTTOM:
                rect.y += rect.height;
                rect.height = screen_h - rect.y;
                s += rect.height;
                break;
            default: ;
        }
        if (rect.height == 0 || rect.width == 0) ; /* on a border of monitor */
        else
        {
            for (i = 0; i < n; i++)
            {
                if (i == monitor)
                    continue;
                mon = GDK_MONITOR(g_list_model_get_item(monitors, i));
                if (mon == NULL)
                    continue;
                gdk_monitor_get_geometry(mon, &rect2);
                g_object_unref(mon);
                if (gdk_rectangle_intersect(&rect, &rect2, NULL))
                    /* that monitor lies over the edge */
                    return FALSE;
            }
        }
    }
    if (G_LIKELY(size))
        *size = s;
    return TRUE;
}

/****************************************************
 *         panel's handlers for WM events           *
 ****************************************************/

void panel_set_wm_strut(Panel *p)
{
    if (p == NULL || p->topgwin == NULL)
        return;
    _panel_set_wm_strut(p->topgwin);
}

void _panel_set_wm_strut(LuxePanel *panel)
{
    int index;
    Display *xdisplay;
    Panel *p = panel != NULL ? panel->priv : NULL;

    if (p == NULL || !luxepanel_display_usable(p->display) || p->topxwin == None)
        return;
    xdisplay = GDK_DISPLAY_XDISPLAY(p->display);
    gulong strut_size;
    gulong strut_lower;
    gulong strut_upper;

    if (!gtk_widget_get_mapped(GTK_WIDGET(panel)))
        return;
    /* most wm's tend to ignore struts of unmapped windows, and that's how
     * luxepanel hides itself. so no reason to set it. */
    if (p->autohide && p->height_when_hidden <= 0)
        return;

    /* Dispatch on edge to set up strut parameters. */
    switch (p->edge)
    {
        case EDGE_LEFT:
            index = 0;
            strut_lower = p->ay;
            strut_upper = p->ay + p->ah;
            break;
        case EDGE_RIGHT:
            index = 1;
            strut_lower = p->ay;
            strut_upper = p->ay + p->ah;
            break;
        case EDGE_TOP:
            index = 2;
            strut_lower = p->ax;
            strut_upper = p->ax + p->aw;
            break;
        case EDGE_BOTTOM:
            index = 3;
            strut_lower = p->ax;
            strut_upper = p->ax + p->aw;
            break;
        default:
            return;
    }

    /* Set up strut value in property format. */
    gulong desired_strut[12];
    memset(desired_strut, 0, sizeof(desired_strut));
    if (p->setstrut &&
        _panel_edge_can_strut(panel, p->edge, p->monitor, &strut_size))
    {
        desired_strut[index] = strut_size;
        desired_strut[4 + index * 2] = strut_lower;
        desired_strut[5 + index * 2] = strut_upper - 1;
    }
    else
    {
        strut_size = 0;
        strut_lower = 0;
        strut_upper = 0;
    }

    /* If strut value changed, set the property value on the panel window.
     * This avoids property change traffic when the panel layout is recalculated but strut geometry hasn't changed. */
    if ((p->strut_size != strut_size) || (p->strut_lower != strut_lower) || (p->strut_upper != strut_upper) || (p->strut_edge != p->edge))
    {
        p->strut_size = strut_size;
        p->strut_lower = strut_lower;
        p->strut_upper = strut_upper;
        p->strut_edge = p->edge;

        /* If window manager supports STRUT_PARTIAL, it will ignore STRUT.
         * Set STRUT also for window managers that do not support STRUT_PARTIAL. */
        if (strut_size != 0)
        {
            XChangeProperty(xdisplay, p->topxwin, a_NET_WM_STRUT_PARTIAL,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 12);
            XChangeProperty(xdisplay, p->topxwin, a_NET_WM_STRUT,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 4);
        }
        else
        {
            XDeleteProperty(xdisplay, p->topxwin, a_NET_WM_STRUT);
            XDeleteProperty(xdisplay, p->topxwin, a_NET_WM_STRUT_PARTIAL);
        }
    }
}

/****************************************************
 *         panel's handlers for GTK events          *
 ****************************************************/

static void paint_root_pixmap(LuxePanel *panel, cairo_t *cr)
{
    /*
     * this code was extracted from code for FbBg object
     *
     * Copyright (C) 2001, 2002 Ian McKellar <yakk@yakk.net>
     *                     2002 Sun Microsystems, Inc.
     */
    XGCValues gcv;
    uint mask;
    Window xroot;
    GC gc;
    Display *dpy;
    Pixmap *prop;
    cairo_surface_t *surface;
    Pixmap xpixmap;
    Panel *p = panel->priv;

    if (!luxepanel_display_usable(p->display))
        return;
    dpy = GDK_DISPLAY_XDISPLAY(p->display);
    xroot = DefaultRootWindow(dpy);
    gcv.ts_x_origin = 0;
    gcv.ts_y_origin = 0;
    gcv.fill_style = FillTiled;
    mask = GCTileStipXOrigin | GCTileStipYOrigin | GCFillStyle;
    prop = get_xaproperty(xroot, a_XROOTPMAP_ID, XA_PIXMAP, NULL);
    /* The tile pixmap is owned by the WM/compositor: between reading the
     * property and our requests it can die (WM restart), and the resulting
     * BadPixmap/BadMatch on the default GDK connection would abort the
     * panel. Trap these errors; an invalid tile just falls through. */
    {
        GdkDisplay *display = p->display;
        gdk_x11_display_error_trap_push(display);
    }
    if (prop)
    {
        gcv.tile = *prop;
        mask |= GCTile;
        XFree(prop);
    }
    gc = XCreateGC(dpy, xroot, mask, &gcv);
    xpixmap = XCreatePixmap(dpy, xroot, p->aw, p->ah,
                            DefaultDepth(dpy, DefaultScreen(dpy)));
    surface = cairo_xlib_surface_create(dpy, xpixmap,
                                        DefaultVisual(dpy, DefaultScreen(dpy)),
                                        p->aw, p->ah);
    XSetTSOrigin(dpy, gc, -p->ax, -p->ay);
    XFillRectangle(dpy, xpixmap, gc, 0, 0, p->aw, p->ah);
    XFreeGC(dpy, gc);
    gdk_x11_display_error_trap_pop_ignored(p->display);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
    XFreePixmap(dpy, xpixmap);
}

/* Convert a GdkPixbuf to a premultiplied ARGB32 cairo surface. */
static cairo_surface_t *cairo_surface_from_pixbuf(GdkPixbuf *pixbuf)
{
    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    const guint8 *src = gdk_pixbuf_get_pixels(pixbuf);
    int sstride = gdk_pixbuf_get_rowstride(pixbuf);
    guint8 *dst = cairo_image_surface_get_data(surf);
    int dstride = cairo_image_surface_get_stride(surf);
    int row, col;

    for (row = 0; row < h; row++)
    {
        const guint8 *s = src + row * sstride;
        guint32 *d = (guint32 *)(dst + row * dstride);
        for (col = 0; col < w; col++)
        {
            guint8 r = s[0], g = s[1], b = s[2], a = has_alpha ? s[3] : 255;
            if (a != 255)
            {
                r = r * a / 255;
                g = g * a / 255;
                b = b * a / 255;
            }
            d[col] = GUINT32_TO_LE(((guint32)a << 24) | (b << 16) | (g << 8) | r);
            s += has_alpha ? 4 : 3;
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

static void _panel_determine_background_pixmap(LuxePanel * panel)
{
    GtkWidget * widget = GTK_WIDGET(panel);
    Panel * p = panel->priv;
    cairo_t *cr;
    gint x = 0, y = 0;

    gboolean stock_theme_background = FALSE;
    g_autofree char *theme_name = NULL;
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme_name, NULL);
    stock_theme_background = g_str_has_prefix(theme_name ? theme_name : "", "Luxe-") &&
        p->background && g_strcmp0(p->background_file, PACKAGE_DATA_DIR "/images/background.png") == 0;
    if((!p->background && !p->transparent) || stock_theme_background)
        gtk_widget_add_css_class(widget, "system-theme");
    else gtk_widget_remove_css_class(widget, "system-theme");
    if(theme_name && g_str_has_prefix(theme_name, "HighContrast"))
        gtk_widget_add_css_class(widget, "high-contrast");
    else gtk_widget_remove_css_class(widget, "high-contrast");
    if ((!p->background && !p->transparent) || stock_theme_background)
        goto not_paintable;
    else if (p->aw <= 1 || p->ah <= 1)
        goto not_paintable;
    else if (p->surface == NULL)
    {
        GdkPixbuf *pixbuf = NULL;

        p->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, p->aw, p->ah);
        cr = cairo_create(p->surface);
        if (p->background)
        {
            /* User specified background pixmap. */
            pixbuf = gdk_pixbuf_new_from_file(p->background_file, NULL);
        }
        if ((p->transparent && p->alpha != 255) || /* ignore it for opaque panel */
            (pixbuf != NULL && gdk_pixbuf_get_has_alpha(pixbuf)))
        {
            /* Transparent.  Determine the appropriate value from the root pixmap. */
            paint_root_pixmap(panel, cr);
        }
        if (pixbuf != NULL)
        {
            gint w = gdk_pixbuf_get_width(pixbuf);
            gint h = gdk_pixbuf_get_height(pixbuf);
            cairo_surface_t *isurf = cairo_surface_from_pixbuf(pixbuf);
            cairo_set_source_surface(cr, isurf, 0, 0);
            /* Tile the image */
            for (y = 0; y < p->ah; y += h)
                for (x = 0; x < p->aw; x += w)
                {
                    cairo_set_source_surface(cr, isurf, x, y);
                    cairo_paint(cr);
                }
            cairo_surface_destroy(isurf);
            y = 0;
            g_object_unref(pixbuf);
        }
        else
        {
            /* Either color is set or image is invalid, fill the background */
            cairo_set_source_rgba(cr, p->gtintcolor.red, p->gtintcolor.green,
                                  p->gtintcolor.blue,
                                  p->transparent ? (double)p->alpha/255 : 1.0);
            cairo_paint_with_alpha(cr, p->transparent ? (double)p->alpha/255 : 1.0);
        }
        cairo_destroy(cr);
    }

    if (p->surface != NULL)
    {
        /* the surface is drawn by luxepanel_snapshot(); nothing to set */
        gtk_widget_queue_draw(widget);
    }
    else
    {
not_paintable:
        ; /* nothing to clear -- snapshot simply has no surface */
    }
}

/* Update the background of the entire panel.
 * This function should only be called after the panel has been realized. */
void panel_update_background(Panel * p)
{
    _panel_update_background(p->topgwin, TRUE);
}

static void _panel_update_background(LuxePanel * p, gboolean enforce)
{
    GtkWidget *w = GTK_WIDGET(p);

    /* reset background image */
    if (p->priv->surface != NULL) /* FIXME: honor enforce on composited screen */
    {
        cairo_surface_destroy(p->priv->surface);
        p->priv->surface = NULL;
    }

    /* Redraw the top level widget.  GTK4: the recomputed surface is
     * drawn by luxepanel_snapshot(). */
    _panel_determine_background_pixmap(p);
    gtk_widget_queue_draw(w);
}

/****************************************************
 *         autohide : borrowed from fbpanel         *
 ****************************************************/

/* Autohide is behaviour when panel hides itself when mouse is "far enough"
 * and pops up again when mouse comes "close enough".
 * Formally, it's a state machine with 3 states that driven by mouse
 * coordinates and timer:
 * 1. VISIBLE - ensures that panel is visible. When/if mouse goes "far enough"
 *      switches to WAITING state
 * 2. WAITING - starts timer. If mouse comes "close enough", stops timer and
 *      switches to VISIBLE.  If timer expires, switches to HIDDEN
 * 3. HIDDEN - hides panel. When mouse comes "close enough" switches to VISIBLE
 *
 * Note 1
 * Mouse coordinates are queried every PERIOD milisec
 *
 * Note 2
 * If mouse is less then GAP pixels to panel it's considered to be close,
 * otherwise it's far
 */

#define GAP 2
/* Pointer sampling for autohide.  There is no pointer-enter/leave signal
 * for "near the panel while hidden", so poll with XQueryPointer, but at
 * 2 s: a 24/7 panel must not wake every 300 ms just to watch the mouse.
 * The deferred-hide delay below is unchanged (600 ms). */
#define PERIOD 2000
#define AH_HIDE_DELAY 600

typedef enum
{
    AH_STATE_VISIBLE,
    AH_STATE_WAITING,
    AH_STATE_HIDDEN
} PanelAHState;

static void ah_state_set(LuxePanel *p, PanelAHState ah_state);

static gboolean
mouse_watch(LuxePanel *panel)
{
    Panel *p = panel->priv;
    gint x, y;

    if (p->gui_stopped || !luxepanel_display_usable(p->display))
        return G_SOURCE_REMOVE;

    ENTER;
    /* gdk_display_get_pointer is gone; Xlib query on the root window. */
    {
        Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
        Window root = DefaultRootWindow(dpy), r, c;
        int rx, ry, wx, wy;
        unsigned int mask;
        x = y = 0;
        if (XQueryPointer(dpy, root, &r, &c, &rx, &ry, &wx, &wy, &mask))
        {
            /* wx/wy are relative to the child window under the pointer;
             * the panel needs root coordinates for the geometry checks. */
            x = rx;
            y = ry;
        }
    }

/*  Reduce sensitivity area
    p->ah_far = ((x < p->cx - GAP) || (x > p->cx + p->cw + GAP)
        || (y < p->cy - GAP) || (y > p->cy + p->ch + GAP));
*/

    gint cx, cy, cw, ch, gap;

    cx = p->ax;
    cy = p->ay;
    cw = p->cw;
    ch = p->ch;

    if (p->move_state != PANEL_MOVE_STOP)
        /* prevent autohide when dragging is on */
        return TRUE;

    if (cw == 1) cw = 0;
    if (ch == 1) ch = 0;
    /* reduce area which will raise panel so it does not interfere with apps */
    if (p->ah_state == AH_STATE_HIDDEN) {
        gap = MAX(p->height_when_hidden, GAP);
        switch (p->edge) {
        case EDGE_LEFT:
            cw = gap;
            break;
        case EDGE_RIGHT:
            cx = cx + cw - gap;
            cw = gap;
            break;
        case EDGE_TOP:
            ch = gap;
            break;
        case EDGE_BOTTOM:
            cy = cy + ch - gap;
            ch = gap;
            break;
       }
    }
    p->ah_far = ((x < cx) || (x > cx + cw) || (y < cy) || (y > cy + ch));

    ah_state_set(panel, p->ah_state);
    RET(TRUE);
}

static gboolean ah_state_hide_timeout(gpointer p)
{
    LuxePanel *panel = LUXEPANEL(p);
    Panel *panel_data = panel->priv;

    if (!panel_data->gui_stopped)
    {
        ah_state_set(panel, AH_STATE_HIDDEN);
        panel_data->hide_timeout = 0;
    }
    return G_SOURCE_REMOVE;
}

static void ah_state_set(LuxePanel *panel, PanelAHState ah_state)
{
    Panel *p = panel->priv;
    GdkRectangle rect;

    ENTER;
    if (p->ah_state != ah_state) {
        p->ah_state = ah_state;
        switch (ah_state) {
        case AH_STATE_VISIBLE:
            p->visible = TRUE;
            _calculate_position(panel, &rect);
            {
                GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(panel));
                if (surface != NULL)
                    XMoveWindow(GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(surface)),
                                gdk_x11_surface_get_xid(surface), rect.x, rect.y);
            }
            gtk_widget_show(GTK_WIDGET(panel));
            gtk_widget_show(p->box);
            gtk_widget_queue_resize(GTK_WIDGET(panel));
            break;
        case AH_STATE_WAITING:
            if (p->hide_timeout)
                g_source_remove(p->hide_timeout);
            p->hide_timeout = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                 AH_HIDE_DELAY,
                                                 ah_state_hide_timeout,
                                                 g_object_ref(panel),
                                                 g_object_unref);
            break;
        case AH_STATE_HIDDEN:
            if (p->height_when_hidden > 0)
                gtk_widget_hide(p->box);
            else
                gtk_widget_hide(GTK_WIDGET(panel));
            p->visible = FALSE;
        }
    } else if (p->autohide && p->ah_far) {
        switch (ah_state) {
        case AH_STATE_VISIBLE:
            ah_state_set(panel, AH_STATE_WAITING);
            break;
        case AH_STATE_WAITING:
            break;
        case AH_STATE_HIDDEN:
            /* configurator might change height_when_hidden value */
            if (p->height_when_hidden > 0)
            {
                if (gtk_widget_get_visible(p->box))
                {
                    gtk_widget_hide(p->box);
                    gtk_widget_show(GTK_WIDGET(panel));
                }
            }
            else
                if (gtk_widget_get_visible(GTK_WIDGET(panel)))
                {
                    gtk_widget_hide(GTK_WIDGET(panel));
                    gtk_widget_show(p->box);
                }
        }
    } else {
        switch (ah_state) {
        case AH_STATE_VISIBLE:
            break;
        case AH_STATE_WAITING:
            if (p->hide_timeout)
                g_source_remove(p->hide_timeout);
            p->hide_timeout = 0;
            /* continue with setting visible */
        case AH_STATE_HIDDEN:
            ah_state_set(panel, AH_STATE_VISIBLE);
        }
    }
    RET();
}

/* starts autohide behaviour */
static void ah_start(LuxePanel *p)
{
    ENTER;
    if (!p->priv->mouse_timeout)
        p->priv->mouse_timeout = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                     PERIOD,
                                                     (GSourceFunc)mouse_watch,
                                                     g_object_ref(p),
                                                     g_object_unref);
    RET();
}

/* stops autohide */
static void ah_stop(LuxePanel *p)
{
    ENTER;
    if (p->priv->mouse_timeout) {
        g_source_remove(p->priv->mouse_timeout);
        p->priv->mouse_timeout = 0;
    }
    if (p->priv->hide_timeout) {
        g_source_remove(p->priv->hide_timeout);
        p->priv->hide_timeout = 0;
    }
    RET();
}
/* end of the autohide code
 * ------------------------------------------------------------- */

static gint
panel_popupmenu_configure(GtkWidget *widget, gpointer user_data)
{
    panel_configure( (LuxePanel*)user_data, 0 );
    return TRUE;
}

static void panel_popupmenu_config_plugin( void *item, GtkWidget* plugin )
{
    Panel *panel = PLUGIN_PANEL(plugin)->priv;

    luxepanel_plugin_show_config_dialog(plugin);

    /* FIXME: this should be more elegant */
    panel->config_changed = TRUE;
}

static void panel_popupmenu_add_item( void *item, LuxePanel* panel )
{
    /* panel_add_plugin( panel, panel->topgwin ); */
    panel_configure( panel, 2 );
}

static void panel_popupmenu_remove_item( void *item, GtkWidget* plugin )
{
    luxepanel_remove_plugin(PLUGIN_PANEL(plugin), plugin);
}

void luxepanel_remove_plugin(LuxePanel *p, GtkWidget *plugin)
{
    Panel* panel = p->priv;

    /* If the configuration dialog is open, there will certainly be a crash if the
     * user manipulates the Configured Plugins list, after we remove this entry.
     * Close the configuration dialog if it is open. */
    if (panel->pref_dialog != NULL)
    {
        gtk_window_destroy(GTK_WINDOW(panel->pref_dialog));
        panel->pref_dialog = NULL;
    }
    _luxepanel_remove_plugin(p, plugin);
}

void _luxepanel_remove_plugin(LuxePanel *p, GtkWidget *plugin)
{
    Panel* panel = p->priv;
    GtkWidget *prev, *next;
    gint idx = -1, size1, size2;
    gboolean expand;

    config_setting_destroy(g_object_get_qdata(G_OBJECT(plugin), luxepanel_plugin_qconf));
    /* reset conf pointer because the widget still may be referenced by configurator */
    g_object_set_qdata(G_OBJECT(plugin), luxepanel_plugin_qconf, NULL);

    /* squash previous and next spaces if this plugin was between two spaces */
    idx = -1;
    prev = next = NULL;
    {
        GtkWidget *iter;
        for (iter = gtk_widget_get_first_child(panel->box); iter != NULL;
             iter = gtk_widget_get_next_sibling(iter))
        {
            if (iter == plugin)
                break;
            prev = iter;
            idx++;
        }
        next = (iter != NULL) ? gtk_widget_get_next_sibling(iter) : NULL;
    }
    if (idx > 0 && next && PANEL_IS_SPACE(next) && PANEL_IS_SPACE(prev))
    {
        /* expand flags live in the plugins' config settings now */
        config_setting_t *conf;
        conf = g_object_get_qdata(G_OBJECT(prev), luxepanel_plugin_qconf);
        expand = FALSE;
        {
            gint i;
            if (conf && config_setting_lookup_int(conf, "expand", &i))
                expand = (i != 0);
        }
        if (expand == TRUE)
        {
            /* prev is expandable, remove next */
            config_setting_destroy(g_object_get_qdata(G_OBJECT(next), luxepanel_plugin_qconf));
            g_object_set_qdata(G_OBJECT(next), luxepanel_plugin_qconf, NULL);
            gtk_widget_unparent(next);
        }
        else
        {
            conf = g_object_get_qdata(G_OBJECT(next), luxepanel_plugin_qconf);
            expand = FALSE;
            if (conf)
            {
                gint i;
                if (config_setting_lookup_int(conf, "expand", &i))
                    expand = (i != 0);
            }
            if (expand == TRUE)
            {
                /* next is expandable, remove prev */
                config_setting_destroy(g_object_get_qdata(G_OBJECT(prev), luxepanel_plugin_qconf));
                g_object_set_qdata(G_OBJECT(prev), luxepanel_plugin_qconf, NULL);
                gtk_widget_unparent(prev);
            }
            else
            {
                /* calculate size of prev -- add size of this and next to it */
                GtkAllocation alloc;
                size1 = _panel_space_get_size(prev);
                size2 = _panel_space_get_size(next);
                gtk_widget_get_allocation(plugin, &alloc);
                if (panel->orientation == GTK_ORIENTATION_HORIZONTAL)
                    size1 += alloc.width + size2;
                else
                    size1 += alloc.height + size2;
                /* remove next */
                config_setting_destroy(g_object_get_qdata(G_OBJECT(next), luxepanel_plugin_qconf));
                g_object_set_qdata(G_OBJECT(next), luxepanel_plugin_qconf, NULL);
                gtk_widget_unparent(next);
                _panel_space_resize(prev, size1);
            }
        }
    }

    luxepanel_config_save(p);
    gtk_widget_unparent(plugin);
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static char* gen_panel_name( int edge, gint monitor )
{
    const char* edge_str = num2str( edge_pair, edge, "" );
    char* name = NULL;
    char* dir = _user_config_file_name("panels", NULL);
    int i;
    for( i = 0; i < G_MAXINT; ++i )
    {
        char* f;
        if(monitor != 0)
            name = g_strdup_printf( "%s-m%d-%d", edge_str, monitor, i );
        else if( G_LIKELY( i > 0 ) )
            name =  g_strdup_printf( "%s%d", edge_str, i );
        else
            name = g_strdup( edge_str );

        f = g_build_filename( dir, name, NULL );
        if( ! g_file_test( f, G_FILE_TEST_EXISTS ) )
        {
            g_free( f );
            break;
        }
        g_free( name );
        g_free( f );
    }
    g_free( dir );
    return name;
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static void panel_popupmenu_create_panel( void *item, LuxePanel* panel )
{
    gint m, e, monitors;
    LuxePanel *new_panel = panel_allocate();
    Panel *p = new_panel->priv;
    config_setting_t *global;

    if (!luxepanel_display_usable(p->display))
    {
        gtk_window_destroy(GTK_WINDOW(new_panel));
        g_warning("Error adding panel: display is not available");
        return;
    }

    /* Allocate the edge. */
    {
        GListModel *model = gdk_display_get_monitors(p->display);
        monitors = G_IS_LIST_MODEL(model) ? (gint)g_list_model_get_n_items(model) : 0;
    }
    /* try to allocate edge on current monitor first */
    m = panel->priv->monitor;
    if (m < 0)
    {
        /* panel is spanned over the screen, guess from pointer now */
        {
            Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
            Window r, c;
            int rx, ry, wx, wy;
            unsigned int mask;
            m = 0;
            if (XQueryPointer(dpy, DefaultRootWindow(dpy), &r, &c, &rx, &ry, &wx, &wy, &mask))
            {
                /* Match monitors against root coordinates (rx/ry), not the
                 * child-relative wx/wy. */
                GListModel *mons = gdk_display_get_monitors(p->display);
                guint j;
                m = 0;
                if (G_IS_LIST_MODEL(mons))
                for (j = 0; j < g_list_model_get_n_items(mons); j++)
                {
                    GdkMonitor *mon = GDK_MONITOR(g_list_model_get_item(mons, j));
                    GdkRectangle g;
                    if (mon == NULL)
                        continue;
                    gdk_monitor_get_geometry(mon, &g);
                    g_object_unref(mon);
                    if (rx >= g.x && rx < g.x + g.width && ry >= g.y && ry < g.y + g.height)
                    {
                        m = j;
                        break;
                    }
                }
            }
        }
    }
    for (e = 1; e < 5; ++e)
    {
        if (panel_edge_available(p, e, m))
        {
            p->edge = e;
            p->monitor = m;
            goto found_edge;
        }
    }
    /* try all monitors */
    for(m=0; m<monitors; ++m)
    {
        /* try each of the four edges */
        for(e=1; e<5; ++e)
        {
            if(panel_edge_available(p,e,m)) {
                p->edge = e;
                p->monitor = m;
                goto found_edge;
            }
        }
    }

    gtk_window_destroy(GTK_WINDOW(new_panel));
    g_warning("Error adding panel: There is no room for another panel. All the edges are taken.");
    show_error(NULL, _("There is no room for another panel. All the edges are taken."));
    return;

found_edge:
    p->name = gen_panel_name(p->edge, p->monitor);

    /* create new config with first group "Global" */
    global = config_group_add_subgroup(config_root_setting(p->config), "Global");
    config_group_set_string(global, "edge", num2str(edge_pair, p->edge, "none"));
    config_group_set_int(global, "monitor", p->monitor);
    panel_configure(new_panel, 0);
    panel_normalize_configuration(p);
    panel_start_gui(new_panel, NULL);

    luxepanel_config_save(new_panel);
    all_panels = g_slist_prepend(all_panels, new_panel);
}

static void panel_delete_panel( LuxePanel* panel )
{
    gchar *fname;
    all_panels = g_slist_remove( all_panels, panel );

    /* delete the config file of this panel */
    fname = _user_config_file_name("panels", panel->priv->name);
    g_unlink( fname );
    g_free(fname);
    panel->priv->config_changed = 0;
    gtk_window_destroy(GTK_WINDOW(panel));
}

/* GTK4 has no gtk_dialog_run(); the delete confirmation is asynchronous. */
static void panel_delete_confirmed( gint button_index, gpointer data )
{
    if( button_index == 1 )
        panel_delete_panel( (LuxePanel*)data );
}

static void panel_confirm_delete( LuxePanel* panel )
{
    luxe_confirm( GTK_WINDOW(panel), _("Really delete this panel?"),
                  _("Warning: This can not be recovered."), TRUE,
                  panel_delete_confirmed, panel );
}

static void panel_popupmenu_delete_panel( void *item, LuxePanel* panel )
{
    (void)item;
    panel_confirm_delete(panel);
}

static void panel_popupmenu_about( void *item, Panel* panel )
{
    (void)item;
    luxe_show_about_full( GTK_WINDOW(panel), _( "LuxePanel" ), VERSION,
                          _( "Desktop panel for Luxe project" ), _( "translator-credits" ) );
}

void panel_apply_icon( GtkWindow *w )
{
    GdkPixbuf* window_icon = NULL;

    if(gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(gdk_display_get_default()), "video-display"))
    {
        GtkIconPaintable *pt = gtk_icon_theme_lookup_icon(
            gtk_icon_theme_get_for_display(gdk_display_get_default()),
            "video-display", NULL, 24, 1, GTK_TEXT_DIR_LTR, 0);
        if (pt != NULL)
        {
            g_object_unref(pt);
            gtk_window_set_icon_name(w, "video-display");
            return;
        }
    }
    else if(gtk_icon_theme_has_icon(gtk_icon_theme_get_for_display(gdk_display_get_default()), "start-here"))
    {
        GtkIconPaintable *pt = gtk_icon_theme_lookup_icon(
            gtk_icon_theme_get_for_display(gdk_display_get_default()),
            "start-here", NULL, 24, 1, GTK_TEXT_DIR_LTR, 0);
        if (pt != NULL)
        {
            g_object_unref(pt);
            gtk_window_set_icon_name(w, "video-display");
            return;
        }
    }
    window_icon = gdk_pixbuf_new_from_file(PACKAGE_DATA_DIR "/images/my-computer.png", NULL);
    if (window_icon != NULL)
    {
        gtk_window_set_icon_name(w, "video-display"); /* fallback name */
        g_object_unref(window_icon);
    }
}

typedef struct
{
    LuxePanel *panel;
    GtkWidget *plugin;
} PanelMenuCtx;

static void panel_menu_action_ctx(GSimpleAction *action, GVariant *param, gpointer data)
{
    PanelMenuCtx *ctx = data;
    const char *name = g_action_get_name(G_ACTION(action));

    if (strcmp(name, "configure") == 0)
    {
        /* Plugin instance settings open the plugin's own config dialog;
         * only the bare panel menu opens the panel preferences. */
        if (ctx->plugin)
            panel_popupmenu_config_plugin(NULL, ctx->plugin);
        else
            panel_popupmenu_configure(NULL, ctx->panel);
    }
    else if (strcmp(name, "configure-panel") == 0)
        panel_popupmenu_configure(NULL, ctx->panel);
    else if (strcmp(name, "add-item") == 0)
        panel_popupmenu_add_item(NULL, ctx->panel);
    else if (strcmp(name, "remove-item") == 0)
        panel_popupmenu_remove_item(NULL, ctx->plugin);
    else if (strcmp(name, "new-panel") == 0)
        panel_popupmenu_create_panel(NULL, ctx->panel);
    else if (strcmp(name, "delete-panel") == 0)
        panel_popupmenu_delete_panel(NULL, ctx->panel);
    else if (strcmp(name, "about") == 0)
        panel_popupmenu_about(NULL, ctx->panel->priv);
}

static void panel_menu_ctx_free(gpointer data, GObject *where_object)
{
    g_free(data);
}

GMenu* luxepanel_get_plugin_menu( LuxePanel* panel, GtkWidget* plugin, gboolean use_sub_menu )
{
    GMenu *ret, *menu;
    GSimpleActionGroup *group;
    GSimpleAction *act;
    const LuxePanelPluginInit *init;
    PanelMenuCtx *ctx = g_new0(PanelMenuCtx, 1);
    char *tmp;

    ctx->panel = panel;
    ctx->plugin = plugin;

    ret = g_menu_new();
    menu = ret;

    group = g_simple_action_group_new();
    /* stash the context on the group so callbacks can find it */
    g_object_set_data_full(G_OBJECT(group), "menu-ctx", ctx, NULL);
    g_object_weak_ref(G_OBJECT(group), panel_menu_ctx_free, ctx);

    if (plugin)
    {
        GMenuItem *item;

        init = PLUGIN_CLASS(plugin);
        /* create single item - plugin instance settings */
        tmp = g_strdup_printf(_("\"%s\" Settings"),
                              g_dgettext(init->gettext_package, init->name));
        act = g_simple_action_new("configure", NULL);
        g_simple_action_set_enabled(act, init->config != NULL);
        g_signal_connect(act, "activate", G_CALLBACK(panel_menu_action_ctx), ctx);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_object_unref(act);
        item = g_menu_item_new(tmp, "win.configure");
        g_menu_prepend_item(menu, item);
        g_object_unref(item);
        g_free(tmp);
        /* add custom items by plugin if requested */
        if (init->update_context_menu != NULL)
            use_sub_menu = init->update_context_menu(plugin, menu);
        /* GMenu has no separators; sections are used instead */
    }
    /* GMenu has no separator concept; rebuild without them */
    menu = g_menu_new();

    act = g_simple_action_new("add-item", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(panel_menu_action_ctx), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_object_unref(act);
    g_menu_append(menu, _("Add / Remove Panel Items"), "win.add-item");

    if (plugin)
    {
        tmp = g_strdup_printf(_("Remove \"%s\" From Panel"), _(init->name));
        act = g_simple_action_new("remove-item", NULL);
        g_signal_connect(act, "activate", G_CALLBACK(panel_menu_action_ctx), ctx);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_object_unref(act);
        g_menu_append(menu, tmp, "win.remove-item");
        g_free(tmp);
    }

    act = g_simple_action_new("configure-panel", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(panel_menu_action_ctx), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_object_unref(act);
    g_menu_append(menu, _("Panel Settings"), "win.configure-panel");

    act = g_simple_action_new("new-panel", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(panel_menu_action_ctx), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_object_unref(act);
    g_menu_append(menu, _("Create New Panel"), "win.new-panel");

    act = g_simple_action_new("delete-panel", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(panel_menu_action_ctx), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_object_unref(act);
    g_menu_append(menu, _("Delete This Panel"), "win.delete-panel");

    act = g_simple_action_new("about", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(panel_menu_action_ctx), ctx);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
    g_object_unref(act);
    g_menu_append(menu, _("About"), "win.about");

    if (use_sub_menu)
    {
        g_menu_append_submenu(ret, _("Panel"), G_MENU_MODEL(menu));
        g_object_unref(menu);
    }
    else
    {
        /* combine plugin section and panel section */
        g_menu_append_section(ret, NULL, G_MENU_MODEL(menu));
        g_object_unref(menu);
    }

    gtk_widget_insert_action_group(GTK_WIDGET(panel), "win",
                                   G_ACTION_GROUP(group));
    g_object_unref(group);

    return ret;
}

/****************************************************
 *         panel creation                           *
 ****************************************************/

static void
make_round_corners(Panel *p)
{
    /* FIXME: This should be re-written with shape extension of X11 */
    /* gdk_window_shape_combine_mask() can be used */
}

void panel_set_dock_type(Panel *p)
{
    Display *xdisplay;

    if (p == NULL || !luxepanel_display_usable(p->display) || p->topxwin == None)
        return;
    xdisplay = GDK_DISPLAY_XDISPLAY(p->display);

    if (p->setdocktype) {
        Atom state = a_NET_WM_WINDOW_TYPE_DOCK;
        XChangeProperty(xdisplay, p->topxwin,
                        a_NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *) &state, 1);
    }
    else {
        XDeleteProperty( xdisplay, p->topxwin, a_NET_WM_WINDOW_TYPE );
    }
}

void panel_establish_autohide(Panel *p)
{
    _panel_establish_autohide(p->topgwin);
}

void _panel_establish_autohide(LuxePanel *p)
{
    if (p == NULL || p->priv == NULL)
        return;
    if (p->priv->autohide)
        ah_start(p);
    else
    {
        ah_stop(p);
        ah_state_set(p, AH_STATE_VISIBLE);
    }
}

/* Set an image from a file with scaling to the panel icon size. */
void panel_image_set_from_file(Panel * p, GtkWidget * image, const char * file)
{
    GdkPixbuf *pixbuf;
    if (p == NULL || image == NULL || file == NULL)
        return;
    pixbuf = gdk_pixbuf_new_from_file_at_scale(file, p->icon_size, p->icon_size, TRUE, NULL);
    if (pixbuf != NULL)
    {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
        g_object_unref(pixbuf);
    }
}

void luxepanel_image_set_from_file(LuxePanel * p, GtkWidget * image, const char * file)
{
    if (p == NULL)
        return;
    panel_image_set_from_file(p->priv, image, file);
}

/* Set an image from a icon theme with scaling to the panel icon size. */
gboolean panel_image_set_icon_theme(Panel * p, GtkWidget * image, const gchar * icon)
{
    if (p == NULL || image == NULL || icon == NULL)
        return FALSE;
    if (gtk_icon_theme_has_icon(p->icon_theme, icon))
    {
        GtkIconPaintable *pt = gtk_icon_theme_lookup_icon(p->icon_theme, icon,
            NULL, p->icon_size, 1, gtk_widget_get_direction(image),
            0);
        if (pt != NULL)
        {
            GdkPixbuf *pixbuf = luxepanel_pixbuf_from_paintable(GDK_PAINTABLE(pt), p->icon_size);
            g_object_unref(pt);
            if (pixbuf != NULL)
            {
                gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
                g_object_unref(pixbuf);
                return TRUE;
            }
        }
    }
    return FALSE;
}

gboolean luxepanel_image_set_icon_theme(LuxePanel * p, GtkWidget * image, const gchar * icon)
{
    return panel_image_set_icon_theme(p->priv, image, icon);
}

static int
panel_parse_plugin(LuxePanel *p, config_setting_t *cfg)
{
    const char *type = NULL;

    ENTER;
    config_setting_lookup_string(cfg, "type", &type);
    DBG("plug %s\n", type);

    if (!type || luxepanel_add_plugin(p, type, cfg, -1) == NULL) {
        g_warning( "luxepanel: can't load %s plugin", type);
        goto error;
    }
    RET(1);

error:
    RET(0);
}

static void
panel_start_gui(LuxePanel *panel, config_setting_t *list)
{
    Atom state[3];
    XWMHints wmhints;
    gulong val;
    Screen *xscreen;
    Display *xdisplay;
    Panel *p = panel->priv;
    GtkWidget *w = GTK_WIDGET(panel);
    config_setting_t *s;
    GdkRectangle rect;
    int i;

    ENTER;

    p->gui_stopped = FALSE;
    g_debug("panel_start_gui on '%s'", p->name ? p->name : "(unnamed)");
    p->curdesk = get_net_current_desktop();
    p->desknum = get_net_number_of_desktops();
    //p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);
    p->ax = p->ay = p->aw = p->ah = 0;

    p->display = gdk_display_get_default();
    if (!luxepanel_display_usable(p->display))
    {
        g_warning("luxepanel: cannot start panel without a usable display");
        return;
    }
    p->display_closed_handler = g_signal_connect(p->display, "closed",
                                                   G_CALLBACK(on_display_closed), NULL);
    xdisplay = GDK_DISPLAY_XDISPLAY(p->display);
    xscreen = DefaultScreenOfDisplay(xdisplay);
    /* WM_CLASS set via Xlib after realize (gtk_window_set_wmclass gone). */

    if (G_UNLIKELY(win_grp == NULL))
    {
        win_grp = gtk_window_group_new();
        g_object_add_weak_pointer(G_OBJECT(win_grp), (gpointer *)&win_grp);
        gtk_window_group_add_window(win_grp, (GtkWindow*)panel);
        g_object_unref(win_grp);
    }
    else
        gtk_window_group_add_window(win_grp, (GtkWindow*)panel);

    /* events via gesture controllers (attached in luxepanel_init below) */

    gtk_widget_realize(w);
    //gdk_window_set_decorations(gtk_widget_get_window(p->topgwin), 0);

    // main layout manager as a single child of panel
    p->box = panel_box_new(panel, FALSE, 0);
    gtk_window_set_child(GTK_WINDOW(panel), p->box);
    gtk_widget_show(p->box);
    if (p->round_corners)
        make_round_corners(p);

    {
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(w));
        if (surface == NULL)
        {
            g_warning("luxepanel: panel surface was not realized");
            panel_stop_gui(panel);
            return;
        }
        p->topxwin = gdk_x11_surface_get_xid(surface);
    }
    DBG("topxwin = %x\n", p->topxwin);
    /* deliver StructureNotify (destroy) to the root watcher */
    x11_root_watch_window(p->topxwin);
    {
        XClassHint *hint = XAllocClassHint();
        if (hint != NULL)
        {
            hint->res_name = (char *)"panel";
            hint->res_class = (char *)"luxepanel";
            XSetClassHint(xdisplay, p->topxwin, hint);
            XFree(hint);
        }
    }

    /* the settings that should be done before window is mapped */
    wmhints.flags = InputHint;
    wmhints.input = 0;
    XSetWMHints (xdisplay, p->topxwin, &wmhints);
#define WIN_HINTS_SKIP_FOCUS      (1<<0)    /* "alt-tab" skips this win */
    val = WIN_HINTS_SKIP_FOCUS;
    XChangeProperty(xdisplay, p->topxwin,
          XInternAtom(xdisplay, "_WIN_HINTS", False), XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    panel_set_dock_type(p);

    /* window mapping point */
    p->visible = TRUE;
    _calculate_position(panel, &rect);
    {
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(panel));
        if (surface != NULL)
            XMoveWindow(GDK_DISPLAY_XDISPLAY(gdk_surface_get_display(surface)),
                        gdk_x11_surface_get_xid(surface), rect.x, rect.y);
    }
    gtk_window_present(GTK_WINDOW(panel));

    /* the settings that should be done after window is mapped */

    /* send it to running wm */
    Xclimsgx(xscreen, p->topxwin, a_NET_WM_DESKTOP, G_MAXULONG, 0, 0, 0, 0);
    /* and assign it ourself just for case when wm is not running */
    val = G_MAXULONG;
    XChangeProperty(xdisplay, p->topxwin, a_NET_WM_DESKTOP, XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    state[0] = a_NET_WM_STATE_SKIP_PAGER;
    state[1] = a_NET_WM_STATE_SKIP_TASKBAR;
    state[2] = a_NET_WM_STATE_STICKY;
    XChangeProperty(xdisplay, p->topxwin, a_NET_WM_STATE, XA_ATOM,
          32, PropModeReplace, (unsigned char *) state, 3);

    p->initialized = TRUE;

    if (list) for (i = 1; (s = config_setting_get_elem(list, i)) != NULL; )
    {
        if (g_strcmp0(config_setting_get_name(s), "Plugin") == 0 &&
            panel_parse_plugin(panel, s)) /* success on plugin start */
            i++;
        else /* remove invalid data from config */
            config_setting_remove_elem(list, i);
    }

    RET();
}

/* Exchange the "width" and "height" terminology for vertical and horizontal panels. */
static void panel_alignment_set_label(GtkWidget *widget, const char *text)
{
    gtk_check_button_set_label(GTK_CHECK_BUTTON(widget), text);
}

void panel_adjust_geometry_terminology(Panel * p)
{
    if ((p->height_label != NULL) && (p->width_label != NULL)
    && (p->alignment_left_label != NULL) && (p->alignment_right_label != NULL))
    {
        if ((p->edge == EDGE_TOP) || (p->edge == EDGE_BOTTOM))
        {
            gtk_label_set_text(GTK_LABEL(p->height_label), _("Height:"));
            gtk_label_set_text(GTK_LABEL(p->width_label), _("Width:"));
            panel_alignment_set_label(p->alignment_left_label, _("Left"));
            panel_alignment_set_label(p->alignment_right_label, _("Right"));
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(p->height_label), _("Width:"));
            gtk_label_set_text(GTK_LABEL(p->width_label), _("Height:"));
            panel_alignment_set_label(p->alignment_left_label, _("Top"));
            panel_alignment_set_label(p->alignment_right_label, _("Bottom"));
        }
    }
}

/* Draw text into a label, with the user preference color and optionally bold. */
static
void panel_draw_label_text_with_color(Panel * p, GtkWidget * label, const char * text,
                           gboolean bold, float custom_size_factor,
                           gboolean custom_color,
                           GdkRGBA *gdkcolor)
{
    if (p == NULL || label == NULL)
        return;
    if (text == NULL)
    {
        /* Null string. */
        gtk_label_set_text(GTK_LABEL(label), NULL);
        return;
    }

    /* Compute an appropriate size so the font will scale with the panel's icon size. */
    int font_desc;
    if (p->usefontsize)
        font_desc = p->fontsize;
    else
    {
        /* GtkStyle is gone; read the label's font from CSS. */
        {
            PangoContext *pctx = gtk_widget_get_pango_context(label);
            const PangoFontDescription *fdesc =
                pango_context_get_font_description(pctx);
            font_desc = pango_font_description_get_size(fdesc) / PANGO_SCALE;
            if (font_desc <= 0)
                font_desc = p->fontsize > 0 ? p->fontsize : 10;
        }
    }
    font_desc *= custom_size_factor;

    /* Check the string for characters that need to be escaped.
     * If any are found, create the properly escaped string and use it instead. */
    const char * valid_markup = text;
    char * escaped_text = NULL;
    const char * q;
    for (q = text; *q != '\0'; q += 1)
    {
        if ((*q == '<') || (*q == '>') || (*q == '&'))
        {
            escaped_text = g_markup_escape_text(text, -1);
            valid_markup = escaped_text;
            break;
        }
    }

    /* The stock profile's white label override was chosen for its old dark
     * bitmap. When that exact stock background follows a global palette,
     * inherit CSS text color too. Preserve other explicit custom colors. */
    if(!gdkcolor && custom_color && p->usefontcolor &&
       p->fontcolor == 0xffffff &&
       gtk_widget_has_css_class(GTK_WIDGET(p->topgwin), "system-theme") &&
       p->background && !g_strcmp0(p->background_file, PACKAGE_DATA_DIR "/images/background.png"))
        custom_color = FALSE;
    gchar * formatted_text;
    if (gdkcolor || ((custom_color) && (p->usefontcolor)))
    {
        /* Color, optionally bold. */
        guint32 rgb24 = gdkcolor ? gcolor2rgb24(gdkcolor) : gcolor2rgb24(&p->gfontcolor);
        formatted_text = g_strdup_printf("<span font_desc=\"%d\" color=\"#%06x\">%s%s%s</span>",
                font_desc,
                rgb24,
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
    }
    else
    {
        /* No color, optionally bold. */
        formatted_text = g_strdup_printf("<span font_desc=\"%d\">%s%s%s</span>",
                font_desc,
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
    }

    gtk_label_set_markup(GTK_LABEL(label), formatted_text);
    g_free(formatted_text);
    g_free(escaped_text);
}

void panel_draw_label_text(Panel * p, GtkWidget * label, const char * text,
                           gboolean bold, float custom_size_factor,
                           gboolean custom_color)
{
    panel_draw_label_text_with_color(p, label, text, bold, custom_size_factor, custom_color, NULL);
}

void luxepanel_draw_label_text(LuxePanel * p, GtkWidget * label, const char * text,
                           gboolean bold, float custom_size_factor,
                           gboolean custom_color)
{
    panel_draw_label_text(p->priv, label, text, bold, custom_size_factor, custom_color);
}

void luxepanel_draw_label_text_with_color(LuxePanel * p, GtkWidget * label, const char * text,
                                    gboolean bold, float custom_size_factor,
                                    GdkRGBA *color)
{
    panel_draw_label_text_with_color(p->priv, label, text, bold, custom_size_factor, FALSE, color);
}

void panel_set_panel_configuration_changed(Panel *p)
{
    _panel_set_panel_configuration_changed(p->topgwin);
}

static inline void _update_orientation(Panel *p)
{
    p->orientation = (p->edge == EDGE_TOP || p->edge == EDGE_BOTTOM)
                        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
}

static GtkWidget *compat_hbox_new(gboolean homogeneous, gint spacing);
static GtkWidget *compat_vbox_new(gboolean homogeneous, gint spacing);
static GtkWidget *compat_vseparator_new(void);
static GtkWidget *compat_hseparator_new(void);

static gboolean _panel_idle_reconfigure(gpointer widget)
{
    LuxePanel *panel;
    Panel *p;
    GtkOrientation previous_orientation;

    panel = LUXEPANEL(widget);
    p = panel->priv;
    if (p->gui_stopped)
        return G_SOURCE_REMOVE;
    previous_orientation = p->orientation;
    _update_orientation(p);

    /* either first run or orientation was changed */
    if (previous_orientation != p->orientation)
    {
        panel_adjust_geometry_terminology(p);
        p->height = ((p->orientation == GTK_ORIENTATION_HORIZONTAL) ? PANEL_HEIGHT_DEFAULT : PANEL_WIDTH_DEFAULT);
        if (p->height_control != NULL)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->height_control), p->height);
        if ((p->widthtype == WIDTH_PIXEL) && (p->width_control != NULL))
        {
            Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
            int value = ((p->orientation == GTK_ORIENTATION_HORIZONTAL) ?
                WidthOfScreen(DefaultScreenOfDisplay(dpy)) :
                HeightOfScreen(DefaultScreenOfDisplay(dpy)));
            gtk_spin_button_set_range(GTK_SPIN_BUTTON(p->width_control), 0, value);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->width_control), value);
        }
    }

    /* FIXME: it's deprecated, kept for binary compatibility */
    if (p->orientation == GTK_ORIENTATION_HORIZONTAL) {
        p->my_box_new = compat_hbox_new;
        p->my_separator_new = compat_vseparator_new;
    } else {
        p->my_box_new = compat_vbox_new;
        p->my_separator_new = compat_hseparator_new;
    }

    /* recreate the main layout box */
    if (p->box != NULL)
    {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(p->box), p->orientation);
    }

    /* NOTE: This loop won't be executed when panel started since
       plugins are not loaded at that time.
       This is used when the orientation of the panel is changed
       from the config dialog, and plugins should be re-layout.
    */
    for (GtkWidget *iter = p->box ? gtk_widget_get_first_child(p->box) : NULL;
         iter != NULL; iter = gtk_widget_get_next_sibling(iter))
    {
        const LuxePanelPluginInit *init = PLUGIN_CLASS(iter);
        if (init->reconfigure)
            init->reconfigure(panel, iter);
    }
    /* panel geometry changed? update panel background then */
    _panel_queue_update_background(panel);

    p->reconfigure_queued = 0;

    return G_SOURCE_REMOVE;
}

void _panel_set_panel_configuration_changed(LuxePanel *panel)
{
    if (panel == NULL || panel->priv->gui_stopped ||
        panel->priv->reconfigure_queued)
        return;
    panel->priv->reconfigure_queued = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                                       _panel_idle_reconfigure,
                                                       g_object_ref(panel),
                                                       g_object_unref);
}

static int
panel_parse_global(Panel *p, config_setting_t *cfg)
{
    const char *str;
    gint i;

    /* check Global config */
    if (!cfg || g_strcmp0(config_setting_get_name(cfg), "Global") != 0)
    {
        g_warning( "luxepanel: Global section not found");
        RET(0);
    }
    if (config_setting_lookup_string(cfg, "edge", &str))
        p->edge = str2num(edge_pair, str, EDGE_NONE);
    if (config_setting_lookup_string(cfg, "align", &str) ||
        /* NOTE: supporting "allign" for backward compatibility */
        config_setting_lookup_string(cfg, "allign", &str))
        p->align = str2num(allign_pair, str, ALIGN_NONE);
    config_setting_lookup_int(cfg, "monitor", &p->monitor);
    config_setting_lookup_int(cfg, "margin", &p->margin);
    if (config_setting_lookup_string(cfg, "widthtype", &str))
        p->widthtype = str2num(width_pair, str, WIDTH_PERCENT);
    if (p->widthtype != WIDTH_NONE && p->widthtype != WIDTH_REQUEST &&
        p->widthtype != WIDTH_PIXEL && p->widthtype != WIDTH_PERCENT)
        p->widthtype = WIDTH_PERCENT;
    config_setting_lookup_int(cfg, "width", &p->width);
    if (config_setting_lookup_string(cfg, "heighttype", &str))
        p->heighttype = str2num(height_pair, str, HEIGHT_NONE);
    config_setting_lookup_int(cfg, "height", &p->height);
    if (config_setting_lookup_int(cfg, "spacing", &i) && i > 0)
        p->spacing = i;
    if (config_setting_lookup_int(cfg, "setdocktype", &i))
        p->setdocktype = i != 0;
    if (config_setting_lookup_int(cfg, "setpartialstrut", &i))
        p->setstrut = i != 0;
    if (config_setting_lookup_int(cfg, "RoundCorners", &i))
        p->round_corners = i != 0;
    if (config_setting_lookup_int(cfg, "transparent", &i))
        p->transparent = i != 0;
    if (config_setting_lookup_int(cfg, "alpha", &p->alpha))
    {
        if (p->alpha > 255)
            p->alpha = 255;
    }
    if (config_setting_lookup_int(cfg, "autohide", &i))
        p->autohide = i != 0;
    if (config_setting_lookup_int(cfg, "heightwhenhidden", &i))
        p->height_when_hidden = MAX(0, i);
    if (config_setting_lookup_string(cfg, "tintcolor", &str))
    {
        if (!gdk_color_parse_string (str, &p->gtintcolor))
            gdk_color_parse_string ("white", &p->gtintcolor);
        p->tintcolor = gcolor2rgb24(&p->gtintcolor);
            DBG("tintcolor=%x\n", p->tintcolor);
    }
    if (config_setting_lookup_int(cfg, "usefontcolor", &i))
        p->usefontcolor = i != 0;
    if (config_setting_lookup_string(cfg, "fontcolor", &str))
    {
        if (!gdk_color_parse_string (str, &p->gfontcolor))
            gdk_color_parse_string ("black", &p->gfontcolor);
        p->fontcolor = gcolor2rgb24(&p->gfontcolor);
            DBG("fontcolor=%x\n", p->fontcolor);
    }
    if (config_setting_lookup_int(cfg, "usefontsize", &i))
        p->usefontsize = i != 0;
    if (config_setting_lookup_int(cfg, "fontsize", &i) && i > 0)
        p->fontsize = i;
    if (config_setting_lookup_int(cfg, "background", &i))
        p->background = i != 0;
    if (config_setting_lookup_string(cfg, "backgroundfile", &str))
    {
        g_free(p->background_file);
        p->background_file = g_strdup(str);
    }
    config_setting_lookup_int(cfg, "iconsize", &p->icon_size);

    _update_orientation(p);
    panel_normalize_configuration(p);

    return 1;
}

/* The display was closed (e.g. after an X error); drop all stale pointers.
 * Limiping on with a dead display segfaults: every monitor query fails
 * GDK_IS_DISPLAY, returns NULL, and timers/measure deref it (exit 11).
 * Exit non-zero so luxesession ("Exit not normal, try to reload") or the
 * user (luxepanelctl restart, now with respawn fallback) restarts us
 * cleanly instead of spinning in a SIGSEGV loop. */
static void on_display_closed(GdkDisplay *display, gpointer user_data)
{
    GSList *pl;

    (void)user_data;
    for (pl = all_panels; pl; pl = pl->next)
    {
        LuxePanel *panel = pl->data;

        if (panel->priv->display == display)
            panel->priv->display = NULL;
    }
    g_warning("luxepanel: display closed, exiting so session manager restarts cleanly");
    exit(1);
}

/* Relayout every started panel.  Shared by monitor changes and the
 * root-geometry watcher below. */
static void panel_relayout_all(void)
{
    GSList* pl;
    for (pl = all_panels; pl; pl = pl->next)
    {
        LuxePanel* p = pl->data;

        if (!luxepanel_display_usable(p->priv->display))
            continue;
        /* SF bug #666: after screen resize panel cannot establish
           right size since cannot be moved while is hidden */
        if (p->priv->initialized)
        {
            ah_state_set(p, AH_STATE_VISIBLE);
            gtk_widget_queue_resize(GTK_WIDGET(p));
        }
    }
}

static void on_monitor_invalidated(GdkMonitor* monitor, gpointer user_data)
{
    panel_relayout_all();
}

static void monitor_weak_notify(gpointer user_data, GObject* monitor)
{
    if (monitor_invalidate_ids != NULL)
        g_hash_table_remove(monitor_invalidate_ids, monitor);
}

static void monitor_invalidate_connect(GdkMonitor* monitor)
{
    gulong id;
    if (monitor_invalidate_ids == NULL)
        monitor_invalidate_ids = g_hash_table_new(g_direct_hash, g_direct_equal);
    if (g_hash_table_contains(monitor_invalidate_ids, monitor))
        return;
    id = g_signal_connect(monitor, "invalidate", G_CALLBACK(on_monitor_invalidated), NULL);
    g_hash_table_insert(monitor_invalidate_ids, monitor, (gpointer)id);
    g_object_weak_ref(G_OBJECT(monitor), monitor_weak_notify, NULL);
}

static void monitor_invalidate_connect_all(void)
{
    GdkDisplay *display = gdk_display_get_default();
    GListModel* monitors;
    guint n;
    guint i;
    if (!luxepanel_display_usable(display))
        return;
    monitors = gdk_display_get_monitors(display);
    if (!G_IS_LIST_MODEL(monitors))
        return;
    n = g_list_model_get_n_items(monitors);
    for (i = 0; i < n; i++)
    {
        GdkMonitor* mon = GDK_MONITOR(g_list_model_get_item(monitors, i));
        if (mon == NULL)
            continue;
        monitor_invalidate_connect(mon);
        g_object_unref(mon);
    }
}

/* GDK4's X11 backend updates its monitor cache on RandR changes but emits no
 * signal for geometry-only changes (mode switches and screen resizes emit
 * neither items-changed nor GdkMonitor::invalidate).  Watch the root window
 * on a private X connection so screen resizes relayout the panels; this
 * mirrors the tray plugin's private-connection event pump. */
typedef struct {
    GSource source;
    GPollFD poll_fd;
} PanelResizeSource;

static Display * panel_resize_dpy = NULL;
static GSource *panel_resize_source = NULL;
static int panel_root_width = -1;
static int panel_root_height = -1;

static gboolean panel_resize_source_prepare(GSource *source, gint *timeout)
{
    *timeout = -1;
    return panel_resize_dpy != NULL && XPending(panel_resize_dpy) > 0;
}

static gboolean panel_resize_source_check(GSource * source)
{
    PanelResizeSource * src = (PanelResizeSource *) source;

    return (src->poll_fd.revents & G_IO_IN)
        && panel_resize_dpy != NULL
        && XPending(panel_resize_dpy) > 0;
}

static gboolean panel_resize_source_dispatch(GSource * source, GSourceFunc cb, gpointer data)
{
    XEvent ev;
    while (panel_resize_dpy != NULL && XPending(panel_resize_dpy) > 0)
    {
        XNextEvent(panel_resize_dpy, &ev);
        if (ev.type == ConfigureNotify
            && ev.xconfigure.window == DefaultRootWindow(panel_resize_dpy)
            && (ev.xconfigure.width != panel_root_width
                || ev.xconfigure.height != panel_root_height))
        {
            panel_root_width = ev.xconfigure.width;
            panel_root_height = ev.xconfigure.height;
            panel_relayout_all();
        }
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs panel_resize_source_funcs = {
    panel_resize_source_prepare,
    panel_resize_source_check,
    panel_resize_source_dispatch,
    NULL
};

static gboolean panel_resize_watcher_started = FALSE;

static void panel_resize_watcher_start(void)
{
    PanelResizeSource * src;
    Display * dpy;
    Window root, root_ret;
    unsigned int rw, rh, dd, bw;
    int sx, sy;

    if (panel_resize_watcher_started)
        return;
    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
        return;
    panel_resize_watcher_started = TRUE;
    root = DefaultRootWindow(dpy);
    XSelectInput(dpy, root, StructureNotifyMask);
    XSync(dpy, False);
    if (XGetGeometry(dpy, root, &root_ret, &sx, &sy, &rw, &rh, &bw, &dd))
    {
        panel_root_width = rw;
        panel_root_height = rh;
    }
    panel_resize_dpy = dpy;
    src = (PanelResizeSource *) g_source_new(&panel_resize_source_funcs, sizeof(PanelResizeSource));
    src->poll_fd.fd = ConnectionNumber(dpy);
    src->poll_fd.events = G_IO_IN;
    g_source_add_poll((GSource *) src, &src->poll_fd);
    panel_resize_source = (GSource *)src;
    g_source_attach(panel_resize_source, NULL);
    g_source_unref(panel_resize_source);
}

void panel_resize_watcher_stop(void)
{
    if (panel_resize_source != NULL)
    {
        g_source_destroy(panel_resize_source);
        g_source_unref(panel_resize_source);
        panel_resize_source = NULL;
    }
    if (panel_resize_dpy != NULL)
    {
        XCloseDisplay(panel_resize_dpy);
        panel_resize_dpy = NULL;
    }
    panel_resize_watcher_started = FALSE;
}

static void on_monitors_changed(GdkDisplay* display, gpointer unused)
{
    GSList *pl;
    GListModel *model;
    int monitors;
    (void)unused;
    if (!luxepanel_display_usable(display))
        return;
    model = gdk_display_get_monitors(display);
    if (!G_IS_LIST_MODEL(model))
        return;
    monitors = g_list_model_get_n_items(model);

    for (pl = all_panels; pl; pl = pl->next)
    {
        LuxePanel *p = pl->data;

        if (!luxepanel_display_usable(p->priv->display))
            continue;
        /* handle connecting and disconnecting monitors now */
        if (p->priv->monitor < monitors && !p->priv->initialized)
            panel_start_gui(p, config_setting_get_member(config_root_setting(p->priv->config), ""));
        else if (p->priv->monitor >= monitors && p->priv->initialized)
            panel_stop_gui(p);
        /* resize panel if appropriate monitor changed its size or position */
        else
        {
            /* SF bug #666: after screen resize panel cannot establish
               right size since cannot be moved while is hidden */
            ah_state_set(p, AH_STATE_VISIBLE);
            gtk_widget_queue_resize(GTK_WIDGET(p));
        }
    }

    /* Replacement monitor objects also need invalidate handlers. */
    monitor_invalidate_connect_all();
}

static int panel_start(LuxePanel *p)
{
    config_setting_t *list;
    GdkDisplay *display = gdk_display_get_default();

    /* parse global section */
    ENTER;

    list = config_setting_get_member(config_root_setting(p->priv->config), "");
    if (!list || !panel_parse_global(p->priv, config_setting_get_elem(list, 0)))
        RET(0);

    if (luxepanel_display_usable(display))
    {
        GListModel *model = gdk_display_get_monitors(display);
        if (G_IS_LIST_MODEL(model))
        {
            int monitor_count = (int)g_list_model_get_n_items(model);
            if (p->priv->monitor >= monitor_count)
                p->priv->monitor = -1;
            if (p->priv->monitor < monitor_count)
                panel_start_gui(p, list);
        }
    }
    if (monitors_handler == 0)
        /* GdkDisplayManager/GdkDisplay have no monitors-changed in GTK4;
           monitors are a GListModel -> use items-changed. */
        if (luxepanel_display_usable(display))
        {
            GListModel *model = gdk_display_get_monitors(display);
            if (G_IS_LIST_MODEL(model))
                monitors_handler = g_signal_connect(model,
                                                    "items-changed",
                                                    G_CALLBACK(on_monitors_changed), NULL);
        }
    /* Watch geometry changes of existing monitors, and RandR screen
     * resizes that GDK reports without any signal. */
    monitor_invalidate_connect_all();
    panel_resize_watcher_start();
    return 1;
}

void panel_destroy(Panel *p)
{
    gtk_window_destroy(GTK_WINDOW(p->topgwin));
}

LuxePanel* panel_new( const char* config_file, const char* config_name )
{
    LuxePanel* panel = NULL;

    if (G_LIKELY(config_file))
    {
        panel = panel_allocate();
        panel->priv->name = g_strdup(config_name);
        g_debug("starting panel from file %s",config_file);
        if (!config_read_file(panel->priv->config, config_file) ||
            !panel_start(panel))
        {
            g_warning( "luxepanel: can't start panel");
            gtk_window_destroy(GTK_WINDOW(panel));
            panel = NULL;
        }
    }
    return panel;
}

GtkOrientation panel_get_orientation(LuxePanel *panel)
{
    return panel->priv->orientation;
}

gint panel_get_icon_size(LuxePanel *panel)
{
    return panel->priv->icon_size;
}

gint panel_get_height(LuxePanel *panel)
{
    return panel->priv->height;
}

Window panel_get_xwindow(LuxePanel *panel)
{
    return panel->priv->topxwin;
}

gint panel_get_monitor(LuxePanel *panel)
{
    return panel->priv->monitor;
}

GtkStyleContext *panel_get_defstyle(LuxePanel *panel)
{
    return panel->priv->defstyle;
}

GtkIconTheme *panel_get_icon_theme(LuxePanel *panel)
{
    return panel->priv->icon_theme;
}

gboolean panel_is_at_bottom(LuxePanel *panel)
{
    return panel->priv->edge == EDGE_BOTTOM;
}

gboolean panel_is_dynamic(LuxePanel *panel)
{
    return panel->priv->widthtype == WIDTH_REQUEST;
}

static GtkWidget *compat_hbox_new(gboolean homogeneous, gint spacing)
{
    return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing);
}
static GtkWidget *compat_vbox_new(gboolean homogeneous, gint spacing)
{
    return gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
}
static GtkWidget *compat_vseparator_new(void)
{
    return gtk_separator_new(GTK_ORIENTATION_VERTICAL);
}
static GtkWidget *compat_hseparator_new(void)
{
    return gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
}

GtkWidget *panel_box_new(LuxePanel *panel, gboolean homogeneous, gint spacing)
{
    if (panel->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        return compat_hbox_new(homogeneous, spacing);
    return compat_vbox_new(homogeneous, spacing);
}

GtkWidget *panel_separator_new(LuxePanel *panel)
{
    if (panel->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        return compat_vseparator_new();
    return compat_hseparator_new();
}

gboolean _class_is_present(const LuxePanelPluginInit *init)
{
    GSList *sl;

    for (sl = all_panels; sl; sl = sl->next )
    {
        LuxePanel *panel = (LuxePanel*)sl->data;

        if (panel->priv->box == NULL)
            continue;
        for (GtkWidget *iter = gtk_widget_get_first_child(panel->priv->box);
             iter != NULL; iter = gtk_widget_get_next_sibling(iter))
        {
            const LuxePanelPluginInit *iter_init = PLUGIN_CLASS(iter);
            if (iter_init != NULL && iter_init == init)
                return TRUE;
        }
    }
    return FALSE;
}
