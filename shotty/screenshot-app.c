/* Copyright (C) 2020 Tom Zaluckij. Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>

#include "screenshot-app.h"
#include "luxe-about.h"
#include "screenshot-menu.h"
#include "actions-menu.h"

struct _ScreenshotApp
{
	GtkApplication parent;
};

G_DEFINE_TYPE (ScreenshotApp,
               screenshot_app,
               GTK_TYPE_APPLICATION);

static void
show_about (GSimpleAction *action,
            GVariant      *parameter,
            gpointer       data)
{
	(void) action;
	(void) parameter;
	GtkWindow *parent;

	// Create and show the About dialog (shared Luxe helper)
	parent = gtk_application_get_active_window (GTK_APPLICATION (data));
	luxe_show_about_full (parent,
	                      "Shotty",
	                      VERSION,
	                      _("A simple GTK4 screenshot application for the Luxe desktop."),
	                      _("translator-credits"));
}

static void
quit_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       data)
{
	(void) action;
	(void) parameter;
	g_application_quit (G_APPLICATION (data));
}

static GActionEntry app_entries[] =
{
	{ .name = "about", .activate = show_about },
	{ .name = "quit", .activate = quit_activated }
};

static void
screenshot_app_activate (GApplication *app)
{
	// Activate parent class
	G_APPLICATION_CLASS (screenshot_app_parent_class)->activate (app);

	// Create and activate the screenshot menu window
	GList *windows = gtk_application_get_windows (GTK_APPLICATION (app));
	for (GList *item = windows; item; item = item->next) {
		if (gtk_widget_get_visible (item->data)) {
			gtk_window_present (GTK_WINDOW (item->data));
			return;
		}
	}
	for (GList *item = windows; item; item = item->next) {
		/* A hidden capture is still in progress. A viewer handoff is not. */
		if (!ACTIONS_IS_MENU (item->data) || !actions_menu_is_handoff (ACTIONS_MENU (item->data)))
			return;
	}
	gtk_window_present (GTK_WINDOW (screenshot_menu_new (SCREENSHOT_APP (app))));
}

static void
screenshot_app_startup (GApplication *app)
{
	// Startup parent class
	G_APPLICATION_CLASS (screenshot_app_parent_class)->startup (app);
	g_autoptr(GError) cache_error = NULL;
	if (!shotty_cleanup_handoff_cache (&cache_error))
		g_warning ("Cannot clean expired Shotty handoffs: %s", cache_error->message);

	// Add action entries (GActionEntry) to action map (GActionMap)
	g_action_map_add_action_entries (G_ACTION_MAP (app),
	                                 app_entries,
	                                 G_N_ELEMENTS (app_entries),
	                                 app);

	// Add quit key accelerators
	const gchar *quit_accels[2] = { "<Ctrl>Q", NULL };
	gtk_application_set_accels_for_action (GTK_APPLICATION (app),
	                                       "app.quit",
	                                       quit_accels);
}

static void
screenshot_app_init (ScreenshotApp *app)
{
	(void) app;
}

static void
screenshot_app_shutdown (GApplication *app)
{
	GList *windows;
	while ((windows = gtk_application_get_windows (GTK_APPLICATION (app))) != NULL)
		gtk_window_destroy (GTK_WINDOW (windows->data));
	G_APPLICATION_CLASS (screenshot_app_parent_class)->shutdown (app);
}

static void
screenshot_app_class_init (ScreenshotAppClass *class)
{
	// Assign GApplication entry points
	G_APPLICATION_CLASS (class)->startup = screenshot_app_startup;
	G_APPLICATION_CLASS (class)->activate = screenshot_app_activate;
	G_APPLICATION_CLASS (class)->shutdown = screenshot_app_shutdown;
}

ScreenshotApp *
screenshot_app_new (void)
{
	return g_object_new (screenshot_app_get_type (),
	                     "application-id",
	                     "org.luxe.Shotty",
	                     "flags",
	                     G_APPLICATION_DEFAULT_FLAGS, NULL);
}
