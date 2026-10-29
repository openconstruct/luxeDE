/*
 * gtk-run.c: Little application launcher
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) pcman.tw(AT)gmail.com
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2014 Andriy Grytsenko <andrej@rep.kiev.ua>
 *
 * This file is a part of LXPanel project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <glib/gi18n.h>
#include <string.h>
#include <unistd.h>

#include "misc.h"
#include "private.h"
#include "luxe-dialogs-gtk.h"
#ifndef DISABLE_MENU
#include <menu-cache.h>
#endif
#include <libfm/fm.h>

#include "gtk-compat.h"
#include "run-completion.h"

static GtkWidget* win = NULL; /* the run dialog */
#ifndef DISABLE_MENU
static MenuCache* menu_cache = NULL;
static GSList* app_list = NULL; /* all known apps in menu cache */
static gpointer reload_notify_id = NULL;
#endif

typedef struct _ThreadData
{
    gint cancel; /* atomic: is the loading cancelled */
    GSList* files; /* all executable files found */
    GtkEntry* entry; /* referenced entry, owned by this job */
}ThreadData;

static ThreadData* thread_data = NULL; /* current run-dialog job */
static GSList* thread_jobs = NULL; /* jobs still owned by a worker/idle callback */
static GtkEntry* pending_completion_entry = NULL; /* latest dialog waiting for the one worker */
static void setup_auto_complete(GtkEntry *entry);

#ifndef DISABLE_MENU
static MenuCacheApp* match_app_by_exec_depth(const char* exec, guint depth)
{
    GSList* l;
    MenuCacheApp* ret = NULL;
    char* exec_path;
    const char* pexec;
    int path_len, exec_len, len;

    if (exec == NULL || *exec == '\0' || depth > 8)
        return NULL;
    exec_path = g_find_program_in_path(exec);
    if (exec_path == NULL)
        return NULL;

    path_len = strlen(exec_path);
    exec_len = strlen(exec);

    for (l = app_list; l; l = l->next)
    {
        MenuCacheApp* app = MENU_CACHE_APP(l->data);
        const char* app_exec = menu_cache_app_get_exec(app);
        if (app_exec == NULL)
            continue;

        if (g_path_is_absolute(app_exec))
        {
            pexec = exec_path;
            len = path_len;
        }
        else
        {
            pexec = exec;
            len = exec_len;
        }

        if (strncmp(app_exec, pexec, (size_t)len) == 0)
        {
            if (app_exec[len] == '\0')
            {
                ret = app;
                break;
            }
            if (app_exec[len] == ' ')
            {
                if (app_exec[len + 1] == '%' &&
                    app_exec[len + 2] != '\0' &&
                    strchr("FfUu", app_exec[len + 2]) != NULL)
                {
                    ret = app;
                    break;
                }
                ret = app;
            }
        }
    }

    /* Follow only a bounded number of symlinks.  A user-controlled PATH can
     * otherwise form a cycle and recurse until the panel stack is exhausted. */
    if (ret == NULL && depth < 8 &&
        g_file_test(exec_path, G_FILE_TEST_IS_SYMLINK))
    {
        char target[512];
        len = readlink(exec_path, target, sizeof(target) - 1);
        if (len > 0)
        {
            target[len] = '\0';
            ret = match_app_by_exec_depth(target, depth + 1);
            if (ret == NULL)
            {
                char* basename = g_path_get_basename(target);
                char* locate = g_find_program_in_path(basename);
                if (locate != NULL && strcmp(locate, target) == 0)
                    ret = match_app_by_exec_depth(basename, depth + 1);
                g_free(locate);
                g_free(basename);
            }
        }
    }

    g_free(exec_path);
    return ret;
}

static MenuCacheApp* match_app_by_exec(const char* exec)
{
    return match_app_by_exec_depth(exec, 0);
}
#endif

static void setup_auto_complete_with_data(ThreadData* data)
{
    GtkListStore* store;
    if (data == NULL || data->entry == NULL || !GTK_IS_ENTRY(data->entry))
        return;
    GSList *l;
    GtkEntryCompletion* comp = gtk_entry_completion_new();
    gtk_entry_completion_set_minimum_key_length( comp, 2 );
    gtk_entry_completion_set_inline_completion( comp, TRUE );
    gtk_entry_completion_set_popup_set_width( comp, TRUE );
    gtk_entry_completion_set_popup_single_match( comp, FALSE );
    store = gtk_list_store_new( 1, G_TYPE_STRING );

    for( l = data->files; l; l = l->next )
    {
        const char *name = (const char*)l->data;
        GtkTreeIter it;
        gtk_list_store_append( store, &it );
        gtk_list_store_set( store, &it, 0, name, -1 );
    }

    gtk_entry_completion_set_model( comp, (GtkTreeModel*)store );
    g_object_unref( store );
    gtk_entry_completion_set_text_column( comp, 0 );
    gtk_entry_set_completion( (GtkEntry*)data->entry, comp );

    /* trigger entry completion */
    gtk_entry_completion_complete(comp);
    g_object_unref( comp );
}

static void thread_data_free(ThreadData* data)
{
    if (data == NULL)
        return;
    g_slist_foreach(data->files, (GFunc)g_free, NULL);
    g_slist_free(data->files);
    g_clear_object(&data->entry);
    g_slice_free(ThreadData, data);
}

static gboolean on_thread_finished(ThreadData* data)
{
    /* Use this job's own state.  A previous dialog can finish after a new
     * dialog has opened; consulting the global pointer mixed the two jobs
     * and could attach old results to the new entry. */
    if (g_atomic_int_get(&data->cancel) == FALSE &&
        data->entry != NULL && GTK_IS_ENTRY(data->entry))
        setup_auto_complete_with_data(data);
    thread_jobs = g_slist_remove(thread_jobs, data);
    if (thread_data == data)
        thread_data = NULL;
    if (pending_completion_entry != NULL) {
        GtkEntry *entry=g_steal_pointer(&pending_completion_entry);
        setup_auto_complete(entry);
        g_object_unref(entry);
    }
    return G_SOURCE_REMOVE;
}

static gpointer thread_func(ThreadData* data)
{
    data->files = luxepanel_scan_path(g_getenv("PATH"),&data->cancel);
    /* install an idle handler to transfer results to the main context */
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)on_thread_finished,
                    data, (GDestroyNotify)thread_data_free);
#if GLIB_CHECK_VERSION(2, 32, 0)
    g_thread_unref(g_thread_self());
#endif

    return NULL;
}

static void setup_auto_complete( GtkEntry* entry )
{
    if(thread_jobs != NULL) {
        g_set_object(&pending_completion_entry,entry);
        return;
    }
    gboolean cache_is_available = FALSE;
    /* FIXME: consider saving the list of commands as on-disk cache. */
    if( cache_is_available )
    {
        /* load cached program list */
    }
    else
    {
        ThreadData *data;
#if GLIB_CHECK_VERSION(2, 32, 0)
        GThread *thread;
#endif

        /* Keep every worker tied to its own entry.  A canceled worker may
         * outlive a closed dialog and a new dialog may be opened meanwhile. */
        data = g_slice_new0(ThreadData);
        data->entry = g_object_ref(entry);
        thread_jobs = g_slist_prepend(thread_jobs, data);
        thread_data = data;
#if GLIB_CHECK_VERSION(2, 32, 0)
        thread = g_thread_new("gtk-run-autocomplete", (GThreadFunc)thread_func, data);
        /* We do not retain loader_thread_id; GLib 2.32 can crash if a
         * GThread is unreffed while creation is still in progress. */
#else
        if (!g_thread_create((GThreadFunc)thread_func, data, FALSE, NULL))
        {
            thread_jobs = g_slist_remove(thread_jobs, data);
            if (thread_data == data)
                thread_data = NULL;
            thread_data_free(data);
        }
#endif
#if GLIB_CHECK_VERSION(2, 32, 0)
        if (thread == NULL)
        {
            thread_jobs = g_slist_remove(thread_jobs, data);
            if (thread_data == data)
                thread_data = NULL;
            thread_data_free(data);
        }
#endif
    }
}

#ifndef DISABLE_MENU
static void reload_apps(MenuCache* cache, gpointer user_data)
{
    g_debug("reload apps!");
    if(app_list)
    {
        g_slist_foreach(app_list, (GFunc)menu_cache_item_unref, NULL);
        g_slist_free(app_list);
    }
    app_list = menu_cache_list_all_apps(cache);
}
#endif

/* fm_launch_command_simple lives in libfm-gtk (dropped); minimal local
 * replacement on core libfm + GAppInfo. */
static void show_launch_error(const char* msg)
{
    luxe_show_error(NULL, msg, NULL);
}

static gboolean launch_command(const char* cmd)
{
    GAppInfo* appinfo;
    GAppLaunchContext* ctx;
    GError* error = NULL;
    gboolean ok;

    appinfo = fm_app_info_create_from_commandline(cmd, NULL, G_APP_INFO_CREATE_NONE, &error);
    if( ! appinfo )
    {
        show_launch_error(error ? error->message : _("Command not found"));
        g_clear_error(&error);
        return FALSE;
    }
    {
        GdkDisplay *display = gdk_display_get_default();
        if (display == NULL)
        {
            show_launch_error(_("Unable to access the display"));
            g_clear_error(&error);
            g_object_unref(appinfo);
            return FALSE;
        }
        ctx = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(display));
    }
    if (ctx == NULL)
    {
        show_launch_error(_("Unable to create a launch context"));
        g_object_unref(appinfo);
        return FALSE;
    }
    ok = fm_app_info_launch(appinfo, NULL, ctx, &error);
    g_object_unref(ctx);
    g_object_unref(appinfo);
    if( ! ok )
    {
        show_launch_error(error ? error->message : _("Failed to launch command"));
        g_clear_error(&error);
    }
    return ok;
}

static void release_run_menu_cache(void)
{
#ifndef DISABLE_MENU
    g_slist_foreach(app_list, (GFunc)menu_cache_item_unref, NULL);
    g_slist_free(app_list);
    app_list = NULL;

    if (menu_cache != NULL)
    {
        if (reload_notify_id != NULL)
            menu_cache_remove_reload_notify(menu_cache, reload_notify_id);
        reload_notify_id = NULL;
        menu_cache_unref(menu_cache);
        menu_cache = NULL;
    }
#endif
}

static void cancel_run_dialog(gpointer dialog)
{
    GtkWidget *widget = GTK_WIDGET(dialog);

    if (win != widget)
        return;
    win = NULL;
    if (thread_data != NULL)
        g_atomic_int_set(&thread_data->cancel, TRUE);
    g_clear_object(&pending_completion_entry);
    release_run_menu_cache();
}

static void on_run_dialog_destroy(GtkWidget *dialog, gpointer user_data)
{
    (void)user_data;
    cancel_run_dialog(dialog);
}

static void on_response( GtkDialog* dlg, gint response, gpointer user_data )
{
    GtkEntry* entry = (GtkEntry*)user_data;
    if (dlg == NULL || entry == NULL)
        return;
    if( G_LIKELY(response == GTK_RESPONSE_OK) )
    {
        if (!launch_command(gtk_editable_get_text(GTK_EDITABLE(entry))))
        {
            g_signal_stop_emission_by_name( dlg, "response" );
            return;
        }
    }

    cancel_run_dialog(dlg);
    gtk_window_destroy( GTK_WINDOW(dlg) );
}

#ifndef DISABLE_MENU
#define RUN_ICON_SIZE 32
static void on_entry_changed( GtkEntry* entry, GtkImage* img )
{
    const char* str = gtk_editable_get_text(GTK_EDITABLE(entry));
    MenuCacheApp* app = NULL;
    if( str && *str )
        app = match_app_by_exec(str);

    if( app )
    {
        const char *name = menu_cache_item_get_icon(MENU_CACHE_ITEM(app));
        GdkPixbuf* pix = luxepanel_load_icon(name ? name : "application-x-executable",
                                            RUN_ICON_SIZE, RUN_ICON_SIZE, TRUE);
        if( pix )
        {
            GdkPaintable* pt = GDK_PAINTABLE(gdk_texture_new_for_pixbuf(pix));
            gtk_image_set_from_paintable(img, pt);
            g_object_unref(pt);
            g_object_unref(pix);
        }
        else
            gtk_image_set_from_icon_name(img, "application-x-executable");
    }
    else
    {
        gtk_image_set_from_icon_name(img, "application-x-executable");
    }
}
#endif

static void activate_window(GtkWindow* toplevel_window)
{
    /* Calling gtk_window_present() alone has no source indication, so
     * if a WM supporting _NET_ACTIVE_WINDOW is running, activate the
     * window with a direct-user-action source indication.
     * See EWMH spec, source indication part:
     * http://standards.freedesktop.org/wm-spec/wm-spec-latest.html#sourceindication
     */
    GtkWidget* widget = GTK_WIDGET(toplevel_window);
    GdkSurface* surface;
    Window* supported;
    int nitems = 0, i;
    gboolean net_active = FALSE;

    /* show the window first */
    gtk_window_present(toplevel_window);

    if (gdk_display_get_default() == NULL ||
        !GDK_IS_X11_DISPLAY(gdk_display_get_default()))
        return;
    surface = gtk_native_get_surface(GTK_NATIVE(widget));
    if( ! surface )
        return;
    supported = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_SUPPORTED, XA_ATOM, &nitems);
    if( supported )
    {
        for( i = 0; i < nitems; ++i )
        {
            if( supported[i] == a_NET_ACTIVE_WINDOW )
            {
                net_active = TRUE;
                break;
            }
        }
        XFree(supported);
    }
    if( net_active )
    {
        /* then, activate it */
        Xclimsg(gdk_x11_surface_get_xid(surface), a_NET_ACTIVE_WINDOW, 2,
                /* source indication: 2 represents direct user actions. */
                gdk_x11_get_server_time(surface),
                None /* currently active window */, 0, 0);
    }
}

void gtk_run()
{
    GtkWidget *entry, *hbox, *img, *dlg_vbox;

    if(!win)
    {
        win = gtk_dialog_new( );
        gtk_window_set_title( (GtkWindow*)win, _("Run") );
        gtk_dialog_add_button( (GtkDialog*)win, _("Cancel"), GTK_RESPONSE_CANCEL );
        gtk_dialog_add_button( (GtkDialog*)win, _("OK"), GTK_RESPONSE_OK );
        gtk_dialog_set_default_response( (GtkDialog*)win, GTK_RESPONSE_OK );
        entry = gtk_entry_new();

        gtk_entry_set_activates_default( (GtkEntry*)entry, TRUE );
        dlg_vbox = gtk_dialog_get_content_area((GtkDialog*)win);
        hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 2 );
        img = gtk_image_new_from_icon_name( "application-x-executable" );
        gtk_image_set_pixel_size( GTK_IMAGE(img), RUN_ICON_SIZE );
        gtk_widget_set_margin_top( GTK_WIDGET(dlg_vbox), 8 );
        gtk_widget_set_margin_bottom( GTK_WIDGET(dlg_vbox), 8 );
        gtk_widget_set_margin_start( GTK_WIDGET(dlg_vbox), 12 );
        gtk_widget_set_margin_end( GTK_WIDGET(dlg_vbox), 12 );
        gtk_widget_set_valign( img, GTK_ALIGN_CENTER );
        gtk_box_append( GTK_BOX(hbox), img );
        gtk_widget_set_margin_start( entry, 4 );
        gtk_widget_set_margin_end( entry, 4 );
        gtk_widget_set_hexpand( entry, TRUE );
        gtk_box_append( GTK_BOX(hbox), entry );
        gtk_widget_set_margin_top( hbox, 8 );
        gtk_box_append( GTK_BOX(dlg_vbox), gtk_label_new(_("Enter the command you want to execute:")) );
        gtk_box_append( GTK_BOX(dlg_vbox), hbox );
        g_signal_connect( win, "response", G_CALLBACK(on_response), entry );
        g_signal_connect( win, "destroy", G_CALLBACK(on_run_dialog_destroy), NULL );
        gtk_window_set_default_size( (GtkWindow*)win, 360, -1 );
        setup_auto_complete( (GtkEntry*)entry );
        gtk_window_present( (GtkWindow*)win );

#ifndef DISABLE_MENU
        g_signal_connect(entry ,"changed", G_CALLBACK(on_entry_changed), img);
#endif

#ifndef DISABLE_MENU
        /* get all apps */
#ifndef MENU_CACHE_CHECK_VERSION
#define MENU_CACHE_CHECK_VERSION(a,b,c) 0
#endif
#if MENU_CACHE_CHECK_VERSION(0, 6, 1)
        menu_cache = menu_cache_lookup_sync(g_getenv("XDG_MENU_PREFIX") ? "applications.menu" : "lxde-applications.menu" );
        if( menu_cache )
        {
#else
        /* SF bug #689: menu_cache_lookup_sync() was fail-prone before 0.6.1 */
        menu_cache = menu_cache_lookup(g_getenv("XDG_MENU_PREFIX") ? "applications.menu" : "lxde-applications.menu" );
        if( menu_cache )
        {
            menu_cache_reload(menu_cache);
#endif
            app_list = menu_cache_list_all_apps(menu_cache);
            reload_notify_id = menu_cache_add_reload_notify(menu_cache, reload_apps, NULL);
        }
#endif
    }

    activate_window(GTK_WINDOW(win));
}

/* vim: set sw=4 et sts=4 ts=4 : */
