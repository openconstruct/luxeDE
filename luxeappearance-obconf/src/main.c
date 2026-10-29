/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../luxe-about.h"
#include "../luxe-dialogs.h"
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   main.c for ObConf, the configuration tool for Openbox
   Copyright (c) 2003-2008   Dana Jansens
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

/* Standalone GTK4 port. The GTK2 build was an luxeappearance plugin;
 * that host API does not exist in the GTK4 tree, so obconf is a
 * standalone GtkApplication window instead. Openbox-only by design:
 * the theme engine (obrender) and the reconfigure broadcast are X11,
 * and the app refuses to run under any other window manager. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "main.h"
#include "archive.h"
#include "theme.h"
#include "appearance.h"
#include "desktops.h"
#include "preview_update.h"
#include "tree.h"
#include <glib/gi18n-lib.h>
#include "luxe-gtk-ui.h"

#include <locale.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <X11/Xatom.h>
#include <gdk/x11/gdkx.h>
#include <gdk/x11/gdkx11property.h>

GtkWidget *mainwin = NULL;
/* Apply button of the main window; enabled by tree_set_* via
 * obconf_set_dirty(), cleared again after tree_apply(). */
GtkWidget *apply_button = NULL;

GtkBuilder* builder;
xmlDocPtr doc;
xmlNodePtr root;
RrInstance *rrinst;
gchar *obc_config_file = NULL;
GStatBuf obc_loaded_stat;
gboolean obc_loaded_stat_valid = FALSE;
ObtPaths *paths;
ObtXmlInst *xml_i;
/* Set once theme/appearance tabs are live; gates cleanup. */
static gboolean setup_done = FALSE;
static gboolean settings_dirty = FALSE;
static gboolean close_dialog_pending = FALSE;

/* Fatal startup errors quit the application after the user dismisses the
 * alert; on_activate skips its immediate quit while one is pending. */
static gboolean fatal_error_pending = FALSE;

static void obconf_cleanup(void);

static void detach_window_content(GtkWidget *widget)
{
    GtkWidget *child;

    if (!widget || !GTK_IS_WINDOW(widget))
        return;
    child = gtk_window_get_child(GTK_WINDOW(widget));
    if (child)
        gtk_window_set_child(GTK_WINDOW(widget), NULL);
}

static void on_obconf_fatal_error_dismissed(gint button_index, gpointer data)
{
    GtkWidget *window = mainwin;

    (void)button_index;
    fatal_error_pending = FALSE;
    if (window && GTK_IS_WINDOW(window)) {
        detach_window_content(window);
        obconf_cleanup();
        mainwin = NULL;
        apply_button = NULL;
        gtk_window_destroy(GTK_WINDOW(window));
    }
    if (data)
        g_application_quit(G_APPLICATION(data));
}

void obconf_error(gchar *msg, gboolean modal)
{
    GApplication *app = g_application_get_default();
    if (!msg)
        return;
    if (!modal && (!mainwin || !GTK_IS_WINDOW(mainwin))) {
        g_warning("%s", msg);
        return;
    }
    if (modal)
        fatal_error_pending = TRUE;
    luxe_ask(mainwin ? GTK_WINDOW(mainwin) : NULL, msg, NULL,
             (const char *const[]){_("OK"), NULL}, -1, 0,
             modal ? on_obconf_fatal_error_dismissed : NULL, app);
}

/* Mark the config dirty: enable Apply. Called from tree_set_*. */
void obconf_set_dirty(void)
{
    settings_dirty = TRUE;
    if (apply_button && GTK_IS_WIDGET(apply_button))
        gtk_widget_set_sensitive(apply_button, TRUE);
}

static void obconf_clear_dirty(void)
{
    settings_dirty = FALSE;
    if (apply_button && GTK_IS_WIDGET(apply_button))
        gtk_widget_set_sensitive(apply_button, FALSE);
}

static gboolean get_all(Window win, Atom prop, Atom type, gint size,
                        guchar **data, guint *num)
{
    GdkDisplay *display = gdk_display_get_default();
    Display *xdisplay;
    gboolean ret = FALSE;
    gint res;
    guchar *xdata = NULL;
    Atom ret_type;
    gint ret_size;
    gulong ret_items, bytes_left;

    if (!data || !num || !GDK_IS_X11_DISPLAY(display) ||
        (size != 8 && size != 16 && size != 32))
        return FALSE;
    *data = NULL;
    *num = 0;
    xdisplay = GDK_DISPLAY_XDISPLAY(display);

    gdk_x11_display_error_trap_push(display);
    res = XGetWindowProperty(xdisplay, win, prop, 0l, 1L << 20,
                             FALSE, type, &ret_type, &ret_size,
                             &ret_items, &bytes_left, &xdata);
    gdk_x11_display_error_trap_pop_ignored(display);
    if (res == Success) {
        if (xdata && ret_size == size && ret_items > 0 &&
            ret_items <= (1UL << 20) && bytes_left == 0 &&
            (ret_type == type || type == AnyPropertyType)) {
            guint i;

            *data = g_try_malloc_n((gsize)ret_items, (gsize)(size / 8));
            if (*data) {
                for (i = 0; i < (guint)ret_items; ++i)
                    switch (size) {
                    case 8:
                        (*data)[i] = xdata[i];
                        break;
                    case 16:
                        ((guint16 *) *data)[i] = ((guint16 *) xdata)[i];
                        break;
                    case 32:
                        ((guint32 *) *data)[i] = ((guint32 *) xdata)[i];
                        break;
                    default:
                        g_assert_not_reached(); /* unhandled size */
                    }
                *num = (guint)ret_items;
                ret = TRUE;
            }
        }
    }
    if (xdata)
        XFree(xdata);
    return ret;
}

static gboolean prop_get_string_utf8(Window win, Atom prop, gchar **ret)
{
    gchar *raw;
    gchar *str;
    guint num;

    if (!ret)
        return FALSE;
    *ret = NULL;
    if (get_all(win, prop,
                gdk_x11_get_xatom_by_name("UTF8_STRING"),
                8,(guchar**)&raw, &num))
    {
        if (memchr(raw, '\0', num)) {
            g_free(raw);
            return FALSE;
        }
        str = g_strndup(raw, num); /* the property must contain one string */
        g_free(raw);
        if (g_utf8_validate(str, -1, NULL)) {
            *ret = str;
            return TRUE;
        }
        g_free(str);
    }
    return FALSE;
}

/* Read _NET_WM_NAME of the window manager (via _NET_SUPPORTING_WM_CHECK).
 * gdk_x11_screen_get_window_manager_name() is gone in GTK4, so read the
 * properties directly; get_all/prop_get_string_utf8 above do the work. */
static gchar *get_window_manager_name(void)
{
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window root_window = DefaultRootWindow(dpy);
    guint32 *sup = NULL;
    guint num = 0;
    gchar *name = NULL;
    Atom check, wm_name;

    check = gdk_x11_get_xatom_by_name("_NET_SUPPORTING_WM_CHECK");
    if (!get_all(root_window, check, XA_WINDOW, 32, (guchar**)&sup, &num) || !num) {
        check = gdk_x11_get_xatom_by_name("_WIN_SUPPORTING_WM_CHECK");
        if (!get_all(root_window, check, XA_CARDINAL, 32, (guchar**)&sup, &num) || !num)
            return NULL;
    }
    wm_name = gdk_x11_get_xatom_by_name("_NET_WM_NAME");
    prop_get_string_utf8(sup[0], wm_name, &name);
    g_free(sup);
    return name;
}

static void on_apply_clicked(GtkButton *button, gpointer data)
{
    (void)button; (void)data;
    if (!desktops_validate() || !tree_apply()) return;
    if (!desktops_apply()) return;
    obconf_clear_dirty();
}

static void on_show_about(GSimpleAction *action, GVariant *param,
                          gpointer data)
{
    GtkWindow *parent;

    (void)action; (void)param;
    parent = gtk_application_get_active_window(GTK_APPLICATION(data));
    if (!parent && mainwin && GTK_IS_WINDOW(mainwin))
        parent = GTK_WINDOW(mainwin);
    luxe_show_about(parent, _("LuxeAppearance-ObConf"), VERSION,
                    _("Openbox window manager settings for Luxe."));
}

static const GActionEntry app_actions[] = {
    { "about", on_show_about, NULL, NULL, NULL },
};

typedef struct {
    GtkBuilder *builder;
    ObtXmlInst *xml_i;
    ObtPaths *paths;
    gchar *config_file;
} ObconfTeardown;

static GQueue *teardown_queue;
static guint teardown_source;

static void release_teardown(ObconfTeardown *state)
{
    if (!state)
        return;
    if (state->builder)
        g_clear_object(&state->builder);
    if (state->xml_i) {
        /* obt_xml_instance_unref() does not close an open document. */
        obt_xml_close(state->xml_i);
        obt_xml_instance_unref(state->xml_i);
    }
    if (state->paths)
        obt_paths_unref(state->paths);
    g_free(state->config_file);
    g_free(state);
}

static gboolean release_teardowns(gpointer data)
{
    ObconfTeardown *state;

    (void)data;
    state = g_queue_pop_head(teardown_queue);
    release_teardown(state);
    if (!g_queue_is_empty(teardown_queue))
        return G_SOURCE_CONTINUE;
    teardown_source = 0;
    g_queue_free(teardown_queue);
    teardown_queue = NULL;
    return G_SOURCE_REMOVE;
}

static void schedule_teardown(ObconfTeardown *state)
{
    if (!state)
        return;
    if (!teardown_queue)
        teardown_queue = g_queue_new();
    g_queue_push_tail(teardown_queue, state);
    if (!teardown_source)
        teardown_source = g_timeout_add(100, release_teardowns, NULL);
}

static ObconfTeardown *take_teardown_state(void)
{
    ObconfTeardown *state;

    if (!builder && !xml_i && !paths && !obc_config_file)
        return NULL;
    state = g_new0(ObconfTeardown, 1);
    state->builder = builder;
    state->xml_i = xml_i;
    state->paths = paths;
    state->config_file = obc_config_file;
    builder = NULL;
    xml_i = NULL;
    paths = NULL;
    obc_config_file = NULL;
    obc_loaded_stat_valid = FALSE;
    return state;
}

static void obconf_cleanup(void)
{
    ObconfTeardown *state;

    archive_cancel_pending();

    /* NULL-safe: also called on early setup failure. */
    if (setup_done) {
        preview_update_set_active_font(NULL);
        preview_update_set_inactive_font(NULL);
        preview_update_set_menu_header_font(NULL);
        preview_update_set_menu_item_font(NULL);
        preview_update_set_osd_active_font(NULL);
        preview_update_set_osd_inactive_font(NULL);
        preview_update_set_title_layout(NULL);
        setup_done = FALSE;
    }
    preview_update_reset();
    theme_cleanup();
    desktops_cleanup();

    if (rrinst) {
        RrInstanceFree(rrinst);
        rrinst = NULL;
    }
    /* obt_xml_doc() returns a borrowed document owned by xml_i. Do not
     * xmlFreeDoc(doc) after unrefing the instance: that was a double-free. */
    doc = NULL;
    root = NULL;
    settings_dirty = FALSE;
    close_dialog_pending = FALSE;

    /* Detach ownership from the live globals before GTK destroys the
     * window.  The actual unrefs happen after the destroy turn, when no
     * builder callback can still refer to the old XML/path objects. */
    state = take_teardown_state();
    schedule_teardown(state);
}

static void close_window_ref_free(GWeakRef *window_ref)
{
    if (window_ref) {
        g_weak_ref_clear(window_ref);
        g_free(window_ref);
    }
}

static void on_close_choice(gint button_index, gpointer data)
{
    GWeakRef *window_ref = data;
    GtkWindow *window = window_ref ? g_weak_ref_get(window_ref) : NULL;

    close_dialog_pending = FALSE;
    if (button_index < 1 || button_index > 2) {
        if (window_ref)
            close_window_ref_free(window_ref);
        if (window)
            g_object_unref(window);
        return; /* Cancel/Escape keeps the editor and pending changes. */
    }
    if (button_index == 2) {
        on_apply_clicked(GTK_BUTTON(apply_button), NULL);
        if (settings_dirty) {
            if (window_ref)
                close_window_ref_free(window_ref);
            if (window)
                g_object_unref(window);
            return; /* Apply failed or a concurrent write was detected. */
        }
    }
    if (window) {
        detach_window_content(GTK_WIDGET(window));
        obconf_cleanup();
        mainwin = NULL;
        apply_button = NULL;
        gtk_window_destroy(GTK_WINDOW(window));
        g_object_unref(window);
    }
    if (window_ref)
        close_window_ref_free(window_ref);
}

/* close-request wrapper: ask before discarding pending settings, then
 * detach the builder state before the window is destroyed.  The queued
 * teardown releases Openbox/GTK ownership after the destroy turn. */
static void on_window_destroyed(GtkWidget *widget, gpointer data)
{
    gboolean owns_window = mainwin == widget;
    gboolean fatal = fatal_error_pending;
    GApplication *app = g_application_get_default();

    (void)data;
    if (owns_window) {
        obconf_cleanup();
        mainwin = NULL;
        apply_button = NULL;
    }
    if (owns_window && fatal) {
        fatal_error_pending = FALSE;
        if (app)
            g_application_quit(app);
    }
}

static gboolean on_win_close(GtkWindow *win, gpointer data)
{
    (void)data;
    if (settings_dirty && !close_dialog_pending) {
        const char *buttons[] = {_("Cancel"), _("Discard"), _("Apply"), NULL};
        GWeakRef *window_ref = g_new0(GWeakRef, 1);
        g_weak_ref_init(window_ref, G_OBJECT(win));
        close_dialog_pending = TRUE;
        luxe_ask(GTK_WINDOW(win), _("Apply the pending Openbox settings before closing?"),
                 _("Discarding changes keeps the last saved rc.xml."),
                 buttons, 0, 0, on_close_choice, window_ref);
        return TRUE;
    }
    if (close_dialog_pending)
        return TRUE;
    detach_window_content(GTK_WIDGET(win));
    obconf_cleanup();
    mainwin = NULL;
    apply_button = NULL;
    return FALSE;
}

static gboolean setup_obconf(GtkWidget *win)
{
    gboolean exit_with_error = FALSE;
    gchar *wm_name;
    GtkWidget *content, *header, *btnbox, *toolbar_view, *menu_btn;
    GtkWidget *apply, *close;
    GMenu *menu;

    /* Publish the window before any startup diagnostic.  A fatal setup
     * error must keep this parent alive until its alert is dismissed. */
    mainwin = win;
    g_object_add_weak_pointer(G_OBJECT(win), (gpointer *)&mainwin);
    g_signal_connect(win, "destroy", G_CALLBACK(on_window_destroyed), NULL);

    /* Openbox-only: refuse any other window manager. Works on X11
     * only; under Wayland gdk_display_get_default() is not X11 and
     * GDK_DISPLAY_XDISPLAY would fail, so check first. */
    if (!GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        obconf_error(_("LuxeAppearance-ObConf requires X11 and the Openbox window manager."), TRUE);
        return FALSE;
    }

    /* detect openbox */
    wm_name = get_window_manager_name();
    if (g_strcmp0(wm_name, "Openbox")) {
        g_free(wm_name);
        obconf_error(_("Openbox does not appear to be running. LuxeAppearance-ObConf cannot be used without it."), TRUE);
        return FALSE;
    }
    g_free(wm_name);

    gtk_window_set_icon_name(GTK_WINDOW(win), "org.luxe.ObConf");

    /* gtk_builder_connect_signals() is gone in GTK4; register the
     * handler symbols explicitly with a cscope instead. */
    {
        GtkBuilderScope *scope = GTK_BUILDER_SCOPE(gtk_builder_cscope_new());
        gtk_builder_cscope_add_callback(scope, on_install_theme_clicked);
        gtk_builder_cscope_add_callback(scope, on_theme_archive_clicked);
        gtk_builder_cscope_add_callback(scope, on_font_active_font_set);
        gtk_builder_cscope_add_callback(scope, on_font_inactive_font_set);
        gtk_builder_cscope_add_callback(scope, on_font_menu_header_font_set);
        gtk_builder_cscope_add_callback(scope, on_font_menu_item_font_set);
        gtk_builder_cscope_add_callback(scope, on_font_active_display_font_set);
        gtk_builder_cscope_add_callback(scope, on_font_inactive_display_font_set);
        gtk_builder_cscope_add_callback(scope, on_title_layout_changed);
        builder = gtk_builder_new();
        gtk_builder_set_scope(builder, GTK_BUILDER_SCOPE(scope));
        g_object_unref(scope);
    }
    gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);
    g_debug(GLADEDIR"/obconf.ui");
    if (!gtk_builder_add_from_file(builder, GLADEDIR"/obconf.ui", NULL)) {
        obconf_error(_("Failed to load the obconf.ui interface file. LuxeAppearance-ObConf is probably not installed correctly."), TRUE);
        return FALSE;
    }

    content = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
    gtk_box_append(GTK_BOX(content), get_widget("obconf_vbox"));

    /* GtkApplicationWindow hosts its chrome in an GtkBox with a
     * top header bar (never as a window titlebar): Apply/Close are the
     * window actions; the hamburger carries secondary commands (About). */
    header = gtk_header_bar_new();
    btnbox = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
    apply = gtk_button_new_with_mnemonic(_("_Apply"));
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply_clicked), NULL);
    gtk_widget_set_sensitive(apply, FALSE);
    gtk_widget_add_css_class(apply, "suggested-action");
    apply_button = apply;
    g_object_add_weak_pointer(G_OBJECT(apply), (gpointer *)&apply_button);
    close = gtk_button_new_with_mnemonic(_("_Close"));
    /* gtk_window_destroy skips close-request, skipping obconf_cleanup; go
     * through the window's normal close path instead. */
    g_signal_connect_swapped(close, "clicked",
                             G_CALLBACK(gtk_window_close), win);
    gtk_box_append(GTK_BOX(btnbox), close);
    gtk_box_append(GTK_BOX(btnbox), apply);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), btnbox);

    menu = g_menu_new();
    g_menu_append(menu, _("About"), "app.about");
    menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn),
                                  "open-menu-symbolic");
    gtk_widget_set_tooltip_text(menu_btn, _("Main menu"));
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_btn);

    toolbar_view = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar_view), header);
    luxe_toolbar_set_content(GTK_BOX(toolbar_view), content);
    luxe_window_set_content(GTK_WINDOW(win),
                                       toolbar_view);

    paths = obt_paths_new();
    xml_i = obt_xml_instance_new();

    if (!obc_config_file) {
        gchar *p;

        if (prop_get_string_utf8(DefaultRootWindow(GDK_DISPLAY_XDISPLAY(gdk_display_get_default())),
                                 gdk_x11_get_xatom_by_name("_OB_CONFIG_FILE"),
                                 &p))
        {
            obc_config_file = g_filename_from_utf8(p, -1, NULL, NULL, NULL);
            g_free(p);
        }
    }

    /* _OB_CONFIG_FILE arrives from the window manager over X11, so never
     * trust it blindly as the write target: accept it only when it resolves
     * to a regular, non-symlink file inside the user's config directory.
     * Anything else is ignored so the default rc.xml path below is used. */
    if (obc_config_file) {
        gchar *config_home = g_canonicalize_filename(
            obt_paths_config_home(paths), NULL);
        gchar *home_real = realpath(config_home, NULL);
        gchar *canonical = realpath(obc_config_file, NULL);
        gsize home_len = home_real ? strlen(home_real) : 0;
        GStatBuf raw_st;
        gboolean raw_is_link = lstat(obc_config_file, &raw_st) == 0 &&
                               S_ISLNK(raw_st.st_mode);
        gboolean cfg_ok = home_real && canonical && !raw_is_link &&
                          home_len > 0 && strncmp(canonical, home_real, home_len) == 0 &&
                          canonical[home_len] == G_DIR_SEPARATOR &&
                          g_file_test(canonical, G_FILE_TEST_IS_REGULAR);
        g_free(config_home);
        g_free(home_real);
        if (!cfg_ok) {
            g_warning("Ignoring _OB_CONFIG_FILE value '%s': not a regular "
                      "file under the config directory", obc_config_file);
            g_free(canonical);
            g_free(obc_config_file);
            obc_config_file = NULL;
        } else {
            g_free(obc_config_file);
            obc_config_file = canonical;
        }
    }

    xmlIndentTreeOutput = 1;
    xmlResetLastError();
    {
        gboolean loaded = FALSE;

        /* A WM-provided path is authoritative.  Falling back to the
         * default after a failed custom load would leave the loaded
         * document paired with the wrong Apply target. */
        if (obc_config_file)
            loaded = obt_xml_load_file(xml_i, obc_config_file,
                                       "openbox_config");
        else
            loaded = obt_xml_load_config_file(xml_i, "openbox", "rc.xml",
                                              "openbox_config");
        if (!loaded) {
            obconf_error(_("Failed to load an rc.xml. Openbox is probably not installed correctly."), TRUE);
            exit_with_error = TRUE;
        } else {
            /* Remember the loaded rc.xml's modification time so Apply can
             * refuse to overwrite changes made by another settings tool. */
            gchar *loaded_path = obc_config_file;
            if (!loaded_path) {
                gchar *p2 = g_build_filename(obt_paths_config_home(paths),
                                             "openbox", "rc.xml", NULL);
                loaded_path = p2;
            }
            obc_loaded_stat_valid = (g_stat(loaded_path, &obc_loaded_stat) == 0);
            if (!obc_config_file) g_free(loaded_path);
            doc = obt_xml_doc(xml_i);
            root = obt_xml_root(xml_i);
        }
    }

    /* look for parsing errors */
    {
        xmlErrorPtr e = xmlGetLastError();
        if (e && !exit_with_error) {
            char *a = g_strdup_printf
                (_("Error while parsing the Openbox configuration file. Your configuration file is not valid XML.\n\nMessage: %s"),
                 e->message);
            obconf_error(a, TRUE);
            g_free(a);
            exit_with_error = TRUE;
        }
    }

    if (!exit_with_error) {
        rrinst = RrInstanceNew(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                               DefaultScreen(GDK_DISPLAY_XDISPLAY(gdk_display_get_default())));
        if (!rrinst) {
            obconf_error(_("Unable to initialize the Openbox theme renderer."), TRUE);
            exit_with_error = TRUE;
        }
    }
    if (!exit_with_error) {
        theme_setup_tab();
        appearance_setup_tab();
        desktops_setup_tab();
        theme_load_all();
        /* Widget/model initialization is not a user edit. */
        obconf_clear_dirty();
        setup_done = TRUE;
    }
    return !exit_with_error;
}

static void on_activate(GtkApplication *app, gpointer data)
{
    GtkWidget *win;

    (void)data;

    /* Single-instance: re-present the existing window instead of
     * opening a second one. */
    if (mainwin && GTK_IS_WINDOW(mainwin)) {
        gtk_window_present(GTK_WINDOW(mainwin));
        return;
    }

    /* X11-only from the start: gtk_init happened implicitly; bail
     * early on Wayland so GDK_DISPLAY_XDISPLAY is never touched. */
    if (!GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        g_printerr("%s\n", _("LuxeAppearance-ObConf requires X11 and the Openbox window manager."));
        return;
    }

    win = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(win), _("LuxeAppearance-ObConf"));
    gtk_window_set_default_size(GTK_WINDOW(win), 640, 480);

    if (!setup_obconf(win)) {
        if (fatal_error_pending) {
            /* Keep the parent alive until the startup alert is dismissed;
             * its callback performs the final cleanup and quits. */
            gtk_window_present(GTK_WINDOW(win));
            return;
        }
        detach_window_content(win);
        obconf_cleanup();
        mainwin = NULL;
        apply_button = NULL;
        gtk_window_destroy(GTK_WINDOW(win));
        g_application_quit(G_APPLICATION(app));
        return;
    }
    g_signal_connect(win, "close-request",
                     G_CALLBACK(on_win_close), NULL);
    gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

#ifdef ENABLE_NLS
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif

    /* X11-gated on purpose (obrender + _OB_CONTROL reconfigure). */
    gdk_set_allowed_backends("x11");

    luxe_ui_init();
    app = GTK_APPLICATION(luxe_application_new("org.luxe.ObConf",
                                              G_APPLICATION_DEFAULT_FLAGS));
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions,
                                    G_N_ELEMENTS(app_actions), app);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
