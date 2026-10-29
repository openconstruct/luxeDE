/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * main.c — GTK4/libadwaita port.
 *
 * The out-of-process theme preview (luxeappearance demo argv mode with
 * GtkSocket/GtkPlug over XEMBED) is gone: XEMBED does not exist in
 * GTK4, so the preview always runs in-process now (see demo.c).
 * The temporary gtkrc-2.0 + mkstemp/unlink dance went away with it;
 * preview-before-apply is handled by live GtkSettings preview with
 * restore-on-close (see main-dlg.c).
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "luxe-gtk-ui.h"
#include <gtk/gtk.h>
#include <string.h>
#include <unistd.h>

#ifdef GDK_WINDOWING_X11
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <gdk/x11/gdkx.h>
#endif

#include "../luxe-about.h"
#include "main-dlg-ui.h"
#include "main-dlg.h"
#include "demo.h"
#include "glade-support.h"

static GtkWidget* main_dlg = NULL;
/* Weak pointer: the window clears this when it is destroyed so a late
 * callback can never present or act on freed UI. */
static gboolean actions_added = FALSE;

gboolean under_luxesession = FALSE;	/* wether luxesession-xsettings daemon is active */

static Atom luxesession_atom = 0;

static void check_luxesession()
{
    Display* dpy;

    under_luxesession = FALSE;
    if(!have_x11())
        return;

    dpy = xdisplay();
    if(!dpy)
        return;
    luxesession_atom = XInternAtom(dpy, "_LUXESESSION", True);
    if( luxesession_atom != None )
    {
        /* Do not XGrabServer for a single ownership query: a grab can block
         * behind another client and freeze the desktop. Xlib serializes the
         * request; the error trap only protects against a dead X server. */
        gdk_x11_display_error_trap_push(gdk_display_get_default());
        if( XGetSelectionOwner(dpy, luxesession_atom) )
            under_luxesession = TRUE;
        gdk_x11_display_error_trap_pop_ignored(gdk_display_get_default());
    }
    (void)luxesession_atom;
    /* The D-Bus fallback probe for older daemons is asynchronous now
     * (see start_async_session_probe): never block startup on D-Bus. */
}

/* Asynchronous settings-daemon probe: older integrated daemons own
 * XSETTINGS but omit _LUXESESSION, so ask the session bus without ever
 * blocking the UI thread. The callback re-checks the window because the
 * dialog may be gone by the time the reply arrives. */
static void session_probe_name_cb(GObject* src, GAsyncResult* res, gpointer user_data)
{
    GDBusConnection* bus = G_DBUS_CONNECTION(src);
    GVariant* reply;
    gboolean has_owner = FALSE;
    (void)user_data;
    reply = g_dbus_connection_call_finish(bus, res, NULL);
    if(reply)
    {
        g_variant_get(reply, "(b)", &has_owner);
        g_variant_unref(reply);
    }
    if(has_owner && !under_luxesession && main_dlg)
    {
        if(main_dlg_note_session_daemon())
            under_luxesession = TRUE;
    }
}

static void session_probe_bus_cb(GObject* src, GAsyncResult* res, gpointer user_data)
{
    GDBusConnection* bus;
    (void)src; (void)user_data;
    bus = g_bus_get_finish(res, NULL);
    if(!bus)
        return;
    g_dbus_connection_call(bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
        g_variant_new("(s)", "org.lxde.SessionManager"),
        G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL,
        session_probe_name_cb, NULL);
    g_object_unref(bus);
}

static void start_async_session_probe(void)
{
    if(under_luxesession)
        return;
    g_bus_get(G_BUS_TYPE_SESSION, NULL, session_probe_bus_cb, NULL);
}

static void on_window_destroyed(GtkWidget* widget, gpointer user_data);

static void on_close_clicked(GtkButton* button, gpointer user_data)
{
    GtkWindow* window = GTK_WINDOW(user_data);
    (void)button;
    on_window_destroyed(GTK_WIDGET(window), NULL);
    gtk_window_destroy(window);
}

static gboolean on_close_request(GtkWindow* window, gpointer user_data)
{
    (void)user_data;
    /* Restore the live preview and release builders before GTK's default
     * close handler destroys the window. */
    on_window_destroyed(GTK_WIDGET(window), NULL);
    return FALSE;
}

/* Teardown for the single live window: stop deferred enumeration, drop the
 * builder registry, and clear every global so a late callback can only see
 * NULL rather than a destroyed widget. GTK4's gtk_window_destroy() does not
 * reliably emit the old destroy signal, so the close-request and close-button
 * paths call this directly; it is also safe if a platform emits destroy. */
static void on_window_destroyed(GtkWidget* widget, gpointer user_data)
{
    (void)user_data;
    if(main_dlg != widget)
        return;
    main_dlg_shutdown(GTK_WIDGET(widget));
    glade_clear_builders();
    main_dlg = NULL;
}

/* Secondary commands (About) live in the header-bar hamburger menu,
 * bound to this application action; the dialog content comes from the
 * shared luxe-about.h helper (GtkAboutDialog). */
static void on_about_action(GSimpleAction* action, GVariant* parameter, gpointer user_data)
{
    GtkWindow* parent;
    (void)action; (void)parameter;
    parent = gtk_application_get_active_window(GTK_APPLICATION(user_data));
    luxe_show_about(parent, "LuxeAppearance", VERSION, "Appearance settings for Luxe.");
}

static const GActionEntry app_actions[] = {
    { "about", on_about_action, NULL, NULL, NULL },
};

static void on_activate(GtkApplication* application, gpointer user_data)
{
    GtkWidget* apply;
    GtkWidget* closebutton;
    (void)user_data;

    /* A second activation (launcher, D-Bus activation, desktop entry) must
     * present the existing window. Creating another dialog would wire the new
     * window to the first window's controls, because lookup_widget() resolves
     * names through the builder registry. */
    if(main_dlg)
    {
        gtk_window_present(GTK_WINDOW(main_dlg));
        return;
    }

    check_luxesession();

    main_dlg = create_dlg();
    if(!main_dlg)
    {
        g_application_quit(G_APPLICATION(application));
        return;
    }
    /* A weak pointer covers finalization, while the destroy callback below
     * clears the application pointer immediately when a window is closed. */
    g_object_add_weak_pointer(G_OBJECT(main_dlg), (gpointer*)&main_dlg);
    g_signal_connect(main_dlg, "destroy", G_CALLBACK(on_window_destroyed), NULL);
    gtk_window_set_application(GTK_WINDOW(main_dlg), application);
    gtk_window_set_icon_name(GTK_WINDOW(main_dlg), "org.luxe.Appearance");

    if(!actions_added)
    {
        g_action_map_add_action_entries(G_ACTION_MAP(application), app_actions,
                                        G_N_ELEMENTS(app_actions), application);
        actions_added = TRUE;
    }

    main_dlg_init(main_dlg);

    apply = lookup_widget(main_dlg, "apply");
    closebutton = lookup_widget(main_dlg, "closebutton1");
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply_clicked), NULL);
    g_signal_connect(closebutton, "clicked", G_CALLBACK(on_close_clicked), main_dlg);
    g_signal_connect(main_dlg, "close-request", G_CALLBACK(on_close_request), NULL);

    /* NOTE: GtkFontButton uses the "font-set" signal in GTK3/4
     * (was "font_set" in GTK2). */
    g_signal_connect(lookup_widget(main_dlg, "font"), "font-set",
                     G_CALLBACK(on_font_changed), NULL);
    g_signal_connect(lookup_widget(main_dlg, "install_theme"), "clicked",
                     G_CALLBACK(on_install_theme_clicked), NULL);
    g_signal_connect(lookup_widget(main_dlg, "remove_theme"), "clicked",
                     G_CALLBACK(on_remove_theme_clicked), NULL);
#if CURSOR_THEME
    g_signal_connect(lookup_widget(main_dlg, "cursor_theme_size"), "value-changed",
                     G_CALLBACK(on_cursor_size_changed), NULL);
#endif
    g_signal_connect(lookup_widget(main_dlg, "tb_style"), "changed",
                      G_CALLBACK(on_tb_style_changed), NULL);

    gtk_window_present(GTK_WINDOW(main_dlg));

    /* Probe the session bus off the critical path; the callback upgrades
     * to luxesession mode if an older daemon is found. */
    start_async_session_probe();
}

int main (int argc, char *argv[])
{
#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    luxe_ui_init();

    {
        GtkApplication* app = GTK_APPLICATION(luxe_application_new("org.luxe.Appearance",
                                                                 G_APPLICATION_DEFAULT_FLAGS));
        int status;
        g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
        status = g_application_run(G_APPLICATION(app), argc, argv);
        g_object_unref(app);
        return status;
    }
}
