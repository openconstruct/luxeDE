/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib.h>
#include <signal.h>
#include <unistd.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#endif

#include "types.h"
#include "interface.h"
#include "functions.h"

GtkWidget *main_window;
GtkApplication *luxetask_app;

GArray *task_array;
gint tasks;
uid_t own_uid;

gchar *config_file;

gboolean show_user_tasks;
gboolean show_root_tasks;
gboolean show_other_tasks;
gboolean show_full_path;

gboolean show_cached_as_free; /* Show memory used Cache as free memory */

gboolean full_view;


gint win_width;
gint win_height;
gint refresh_interval;
guint rID;

int page_size;

static void on_activate (GtkApplication *app, gpointer data)
{
    (void) data;
    if (main_window) {
        gtk_window_present(GTK_WINDOW(main_window));
        return;
    }

    task_array = g_array_new (FALSE, FALSE, sizeof (struct task));
    tasks = 0;

    main_window = create_main_window (app);
    g_object_add_weak_pointer(G_OBJECT(main_window), (gpointer *)&main_window);
    gtk_window_present (GTK_WINDOW (main_window));

    if (!refresh_task_list())
    {
        g_application_quit (G_APPLICATION (app));
        return;
    }

    rID = g_timeout_add_seconds (refresh_interval, refresh_task_list_cb, NULL);
}

int main (int argc, char *argv[])
{

#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    own_uid = getuid();

    config_file = g_build_filename (g_get_user_config_dir (), "luxetask.conf", NULL);
    load_config ();

    /* Load configuration and page size before the first process snapshot. */
    page_size = sysconf(_SC_PAGESIZE) >> 10;

    luxe_ui_init ();
    luxetask_app = GTK_APPLICATION (luxe_application_new ("org.luxe.Task",
                                      G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect (luxetask_app, "activate", G_CALLBACK (on_activate), NULL);
    int status = g_application_run (G_APPLICATION (luxetask_app), argc, argv);
    if (main_window) gtk_window_destroy(GTK_WINDOW(main_window));
    if (rID) g_source_remove(rID);
    g_clear_pointer(&task_array, g_array_unref);
    cleanup_task_view();
    g_clear_pointer(&config_file, g_free);
    g_clear_object (&luxetask_app);

    return status;
}
