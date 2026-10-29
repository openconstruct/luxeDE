/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>

#include "frame-app.h"
#include "luxe-about.h"
#include "frame-window.h"

struct _FrameApp
{
	GtkApplication parent;
};

G_DEFINE_TYPE (FrameApp,
               frame_app,
               GTK_TYPE_APPLICATION);

static void
show_about (GSimpleAction *action G_GNUC_UNUSED,
            GVariant      *parameter G_GNUC_UNUSED,
            gpointer       data)
{
	GtkWindow *parent;

	// Create and show the About dialog (shared Luxe helper)
	parent = gtk_application_get_active_window (GTK_APPLICATION (data));
	luxe_show_about_full (parent,
	                      "Frame",
	                      VERSION,
	                      _("A fast, light image viewer for the Luxe desktop."),
	                      _("translator-credits"));
}

static void
quit_activated (GSimpleAction *action G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       data)
{
	g_application_quit (G_APPLICATION (data));
}

static GActionEntry app_entries[] =
{
	{ .name = "about", .activate = show_about },
	{ .name = "quit", .activate = quit_activated }
};

static void
frame_app_activate (GApplication *app)
{
	GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));
	FrameWindow *win = windows ? FRAME_WINDOW(windows->data) : frame_window_new(GTK_APPLICATION(app));
	gtk_window_present(GTK_WINDOW(win));
}

static void
frame_app_open (GApplication  *app,
                GFile        **files,
                gint           n_files,
                const gchar   *hint)
{
	(void)hint;
	frame_app_activate(app);
	if (n_files > 0 && files != NULL && files[0] != NULL)
		frame_window_open(FRAME_WINDOW(gtk_application_get_windows(GTK_APPLICATION(app))->data), files[0]);
}

static void
frame_app_startup (GApplication *app)
{
	// Startup parent class
	G_APPLICATION_CLASS (frame_app_parent_class)->startup (app);

	// Add action entries (GActionEntry) to action map (GActionMap)
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 app_entries,
	                                 G_N_ELEMENTS (app_entries),
	                                 app);

	// Add key accelerators
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.quit",
	                                       (const gchar *const []) { "<Ctrl>Q", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.open",
	                                       (const gchar *const []) { "<Ctrl>O", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.info",
	                                       (const gchar *const []) { "i", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.trash",
	                                       (const gchar *const []) { "Delete", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.rotate::cw",
	                                       (const gchar *const []) { "r", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.rotate::ccw",
	                                       (const gchar *const []) { "<Shift>R", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.fullscreen",
	                                       (const gchar *const []) { "f", "F11", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.slideshow",
	                                       (const gchar *const []) { "s", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.wallpaper",
	                                       (const gchar *const []) { "w", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.nav::next",
	                                       (const gchar *const []) { "Right", "n", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.nav::prev",
	                                       (const gchar *const []) { "Left", "p", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.zoom::in",
	                                       (const gchar *const []) { "plus", "equal", "KP_Add", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.zoom::out",
	                                       (const gchar *const []) { "minus", "KP_Subtract", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.zoom::fit",
	                                       (const gchar *const []) { "0", "z", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.zoom::one",
	                                       (const gchar *const []) { "1", NULL });
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "win.escape",
	                                       (const gchar *const []) { "Escape", NULL });
}

static void
frame_app_init (FrameApp *app G_GNUC_UNUSED)
{
}

static void frame_app_shutdown(GApplication *app)
{
	while (gtk_application_get_windows(GTK_APPLICATION(app)))
		gtk_window_destroy(gtk_application_get_windows(GTK_APPLICATION(app))->data);
	G_APPLICATION_CLASS(frame_app_parent_class)->shutdown(app);
}

static void
frame_app_class_init (FrameAppClass *class)
{
	// Assign GApplication entry points
	G_APPLICATION_CLASS (class)->startup = frame_app_startup;
	G_APPLICATION_CLASS (class)->activate = frame_app_activate;
	G_APPLICATION_CLASS (class)->open = frame_app_open;
	G_APPLICATION_CLASS (class)->shutdown = frame_app_shutdown;
}

FrameApp *
frame_app_new (void)
{
	return g_object_new (frame_app_get_type (),
	                     "application-id",
	                     "org.luxe.Frame",
	                     "flags",
	                     G_APPLICATION_HANDLES_OPEN, NULL);
}
