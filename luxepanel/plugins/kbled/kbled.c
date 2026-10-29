/*
 * Copyright (C) 2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Marty Jack <martyj19@comcast.net>
 *               2012 Michael Rawson <michaelrawson76@gmail.com>
 *               2012 Julien Lavergne <julien.lavergne@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "plugin.h"
#include "misc.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/x11/gdkx.h>
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

#include <X11/XKBlib.h>

#include "icon-grid.h"

static const char * on_icons_theme[] = {
    "capslock-on",
    "numlock-on",
    "scrllock-on"
};

static const char * off_icons_theme[] = {
    "capslock-off",
    "numlock-off",
    "scrllock-off"
};

static int xkb_event_base = 0;
static int xkb_error_base = 0;

/* Private context for keyboard LED plugin. */
typedef struct {
    config_setting_t *settings;
    GtkWidget *indicator_image[3];		/* Image for each indicator */
    unsigned int current_state;			/* Current LED state, bit encoded */
    gboolean visible[3];			/* True if control is visible (per user configuration) */
    Display * event_dpy;			/* private X connection */
    GSource * event_source;			/* event pump */
} KeyboardLEDPlugin;

static void kbled_update_image(KeyboardLEDPlugin * kl, int i, unsigned int state);
static void kbled_update_display(KeyboardLEDPlugin * kl, unsigned int state);
static void kbled_destructor(gpointer user_data);

/* Update image to correspond to current state. */
static void kbled_update_image(KeyboardLEDPlugin * kl, int i, unsigned int state)
{
    luxepanel_image_change_icon(kl->indicator_image[i],
                              (state ? on_icons_theme[i] : off_icons_theme[i]),
                              NULL);
}

/* Redraw after Xkb event or initialization. */
static void kbled_update_display(KeyboardLEDPlugin * kl, unsigned int new_state)
{
    int i;
    for (i = 0; i < 3; i++)
    {
        /* If the control changed state, redraw it. */
        int current_is_lit = kl->current_state & (1 << i);
        int new_is_lit = new_state & (1 << i);
        if (current_is_lit != new_is_lit)
            kbled_update_image(kl, i, new_is_lit);
    }

    /* Save new state. */
    kl->current_state = new_state;
}

/* Private X connection event pump (GDK4 has no event filters). */
typedef struct {
    GSource source;
    GPollFD poll_fd;
    KeyboardLEDPlugin * kl;
} KbledSource;

static gboolean kbled_source_check(GSource * source)
{
    KbledSource * src = (KbledSource *) source;

    if (!(src->poll_fd.revents & G_IO_IN) || src->kl->event_dpy == NULL)
        return FALSE;
    return XPending(src->kl->event_dpy) > 0;
}

static gboolean kbled_source_dispatch(GSource * source, GSourceFunc cb, gpointer data)
{
    KbledSource * src = (KbledSource *) source;
    Display * dpy = src->kl->event_dpy;
    XEvent ev;

    while (dpy != NULL && XPending(dpy) > 0)
    {
        XNextEvent(dpy, &ev);
        /* Look for XkbIndicatorStateNotify events and update the display. */
        if (ev.xany.type == xkb_event_base + XkbEventCode)
        {
            XkbEvent * xkbev = (XkbEvent *) &ev;
            if (xkbev->any.xkb_type == XkbIndicatorStateNotify)
                kbled_update_display(src->kl, xkbev->indicators.state);
        }
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs kbled_source_funcs = {
    NULL,
    kbled_source_check,
    kbled_source_dispatch,
    NULL
};

/* Plugin constructor. */
static GtkWidget *kbled_constructor(LuxePanel *panel, config_setting_t *settings)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    KeyboardLEDPlugin * kl = g_new0(KeyboardLEDPlugin, 1);
    GtkWidget *p;
    int i;
    unsigned int current_state;
    GdkDisplay *display = gdk_display_get_default();
    Display *xdisplay;

    if (display == NULL || !GDK_IS_X11_DISPLAY(display))
    {
        g_free(kl);
        return NULL;
    }
    xdisplay = gdk_x11_display_get_xdisplay(display);
    if (xdisplay == NULL)
    {
        g_free(kl);
        return NULL;
    }

    kl->settings = settings;
    kl->visible[0] = FALSE;
    kl->visible[1] = TRUE;
    kl->visible[2] = TRUE;

    /* Load parameters from the configuration file. */
    if (config_setting_lookup_int(settings, "ShowCapsLock", &i))
        kl->visible[0] = i != 0;
    if (config_setting_lookup_int(settings, "ShowNumLock", &i))
        kl->visible[1] = i != 0;
    if (config_setting_lookup_int(settings, "ShowScrollLock", &i))
        kl->visible[2] = i != 0;

    /* Allocate top level widget and set into Plugin widget pointer. */
    p = panel_icon_grid_new(panel_get_orientation(panel),
                            panel_get_icon_size(panel),
                            panel_get_icon_size(panel),
                            1, 0, panel_get_height(panel));
    luxepanel_plugin_set_data(p, kl, kbled_destructor);

    /* Then allocate three images for the three indications, but make them visible only when the configuration requests. */
    for (i = 0; i < 3; i++)
    {
        kl->indicator_image[i] = luxepanel_image_new_for_icon(panel, off_icons_theme[i], -1, NULL);
        panel_icon_grid_add(PANEL_ICON_GRID(p), kl->indicator_image[i]);
        gtk_widget_set_visible(kl->indicator_image[i], kl->visible[i]);
    }

    /* Initialize Xkb extension if not yet done. */
    if (xkb_event_base == 0)
    {
        int opcode;
        int maj = XkbMajorVersion;
        int min = XkbMinorVersion;
        if (!XkbLibraryVersion(&maj, &min) ||
            !XkbQueryExtension(xdisplay, &opcode, &xkb_event_base, &xkb_error_base, &maj, &min))
        {
            g_object_unref(p);
            return NULL;
        }
    }

    /* Private connection + pump: GDK4 has no event filters. */
    kl->event_dpy = XOpenDisplay(NULL);
    if (kl->event_dpy != NULL)
    {
        KbledSource * src;
        int opcode;
        int maj2 = XkbMajorVersion;
        int min2 = XkbMinorVersion;
        if (XkbQueryExtension(kl->event_dpy, &opcode, &xkb_event_base, &xkb_error_base, &maj2, &min2))
        {
            src = (KbledSource *) g_source_new(&kbled_source_funcs, sizeof(KbledSource));
            src->kl = kl;
            src->poll_fd.fd = ConnectionNumber(kl->event_dpy);
            src->poll_fd.events = G_IO_IN;
            g_source_add_poll((GSource *) src, &src->poll_fd);
            g_source_attach((GSource *) src, NULL);
            kl->event_source = (GSource *) src;

            if (!XkbSelectEvents(kl->event_dpy, XkbUseCoreKbd,
                                 XkbIndicatorStateNotifyMask,
                                 XkbIndicatorStateNotifyMask))
            {
                g_source_destroy(kl->event_source);
                g_source_unref(kl->event_source);
                kl->event_source = NULL;
                XCloseDisplay(kl->event_dpy);
                kl->event_dpy = NULL;
                g_object_unref(p);
                return NULL;
            }
        }
        else
        {
            XCloseDisplay(kl->event_dpy);
            kl->event_dpy = NULL;
            g_object_unref(p);
            return NULL;
        }
    }
    else
    {
        g_object_unref(p);
        return NULL;
    }

    /* Use the private connection for state queries too. */
    xdisplay = kl->event_dpy;

    /* Get current indicator state and update display.
     * Force current state to differ in all bits so a full redraw will occur. */
    if (!XkbGetIndicatorState(xdisplay, XkbUseCoreKbd, &current_state))
    {
        g_object_unref(p);
        return NULL;
    }
    kl->current_state = ~ current_state;
    kbled_update_display(kl, current_state);

    /* Show the widget. */
    return p;
}

/* Plugin destructor. */
static void kbled_destructor(gpointer user_data)
{
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) user_data;

    /* Remove the event pump. */
    if (kl->event_source != NULL)
    {
        g_source_destroy(kl->event_source);
        g_source_unref(kl->event_source);
        kl->event_source = NULL;
    }
    if (kl->event_dpy != NULL)
    {
        XCloseDisplay(kl->event_dpy);
        kl->event_dpy = NULL;
    }
    g_free(kl);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static gboolean kbled_apply_configuration(gpointer user_data)
{
    KeyboardLEDPlugin * kl = luxepanel_plugin_get_data(user_data);
    int i;

    for (i = 0; i < 3; i++)
        gtk_widget_set_visible(kl->indicator_image[i], kl->visible[i]);
    config_group_set_int(kl->settings, "ShowCapsLock", kl->visible[0]);
    config_group_set_int(kl->settings, "ShowNumLock", kl->visible[1]);
    config_group_set_int(kl->settings, "ShowScrollLock", kl->visible[2]);
    return FALSE;
}

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *kbled_configure(LuxePanel *panel, GtkWidget *p)
{
    KeyboardLEDPlugin * kl = luxepanel_plugin_get_data(p);
    GtkWidget * dlg = luxepanel_generic_config_dlg(_("Keyboard LED"),
        panel, kbled_apply_configuration, p,
        _("Show CapsLock"), &kl->visible[0], CONF_TYPE_BOOL,
        _("Show NumLock"), &kl->visible[1], CONF_TYPE_BOOL,
        _("Show ScrollLock"), &kl->visible[2], CONF_TYPE_BOOL,
        NULL);
    gtk_widget_set_size_request(GTK_WIDGET(dlg), 200, -1);	/* Improve geometry */
    return dlg;
}

/* Callback when panel configuration changes. */
static void kbled_panel_configuration_changed(LuxePanel *panel, GtkWidget *p)
{
    /* Set orientation into the icon grid. */
    KeyboardLEDPlugin * kl = luxepanel_plugin_get_data(p);

    panel_icon_grid_set_geometry(PANEL_ICON_GRID(p), panel_get_orientation(panel),
                                 panel_get_icon_size(panel),
                                 panel_get_icon_size(panel),
                                 1, 0, panel_get_height(panel));

    /* Do a full redraw. */
    int current_state = kl->current_state;
    kl->current_state = ~ kl->current_state;
    kbled_update_display(kl, current_state);
}

FM_DEFINE_MODULE(luxepanel_gtk, kbled)

/* Plugin descriptor. */
LuxePanelPluginInit fm_module_init_luxepanel_gtk = {
    .name = N_("Keyboard LED"),
    .description = N_("Indicators for CapsLock, NumLock, and ScrollLock keys"),

    .new_instance = kbled_constructor,
    .config = kbled_configure,
    .reconfigure = kbled_panel_configuration_changed
};
