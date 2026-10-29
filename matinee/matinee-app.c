/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* The application object.  One window, and a single-instance boundary so a
 * second launch (including a desktop file open) hands its files to the running
 * instance instead of starting a second player. */

#include "matinee-app.h"
#include "matinee-window.h"

struct _MatineeApp
{
	GtkApplication parent_instance;
	MatineeWindow *window;
};

G_DEFINE_TYPE (MatineeApp, matinee_app, GTK_TYPE_APPLICATION)

static void
set_window_accelerators (MatineeApp *app)
{
	for (gsize i = 0; i < matinee_shortcuts_count; i++)
		gtk_application_set_accels_for_action (GTK_APPLICATION (app), matinee_shortcuts[i].action,
		                                       (const char *[]) { matinee_shortcuts[i].accelerator, NULL });
}

static void
matinee_app_activate (GApplication *application)
{
	MatineeApp *app = MATINEE_APP (application);

	if (app->window == NULL)
	{
		app->window = matinee_window_new (GTK_APPLICATION (application));
		/* GtkApplication owns the toplevel; do not unref its borrowed pointer. */
		g_object_add_weak_pointer (G_OBJECT (app->window), (gpointer *) &app->window);
		set_window_accelerators (app);
	}
	gtk_window_present (GTK_WINDOW (app->window));
}

static void
matinee_app_open (GApplication *application, GFile **files, int n_files, const char *hint)
{
	MatineeApp *app = MATINEE_APP (application);

	(void) hint;
	matinee_app_activate (application);
	if (n_files <= 0)
		return;
	{
		g_autoptr(GPtrArray) paths = g_ptr_array_new_with_free_func (g_free);
		guint unsupported = 0;
		for (int i = 0; i < n_files; i++) {
			char *path = g_file_get_path (files[i]);
			if (path != NULL) g_ptr_array_add (paths, path);
			else unsupported++;
		}
		if (paths->len > 0) {
			g_ptr_array_add (paths, NULL);
			matinee_window_open_files (app->window, (const char * const *) paths->pdata);
		}
		if (unsupported > 0) {
			g_autofree char *message = g_strdup_printf (
				"%u non-local file(s) could not be opened. Matinee currently opens local files only.",
				unsupported);
			GtkWindow *dialog = luxe_choice_dialog_new ("Some files could not be opened", message);
			luxe_choice_add_response (GTK_DIALOG (dialog), "close", "Close");
			luxe_dialog_present (dialog, GTK_WIDGET (app->window));
		}
	}
}

static void
matinee_app_dispose (GObject *object)
{
	MatineeApp *app = MATINEE_APP (object);
	if (app->window != NULL)
	{
		g_object_remove_weak_pointer (G_OBJECT (app->window), (gpointer *) &app->window);
		app->window = NULL;
	}
	G_OBJECT_CLASS (matinee_app_parent_class)->dispose (object);
}

static void
matinee_app_class_init (MatineeAppClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

	object_class->dispose = matinee_app_dispose;
	application_class->activate = matinee_app_activate;
	application_class->open = matinee_app_open;
}

static void
matinee_app_init (MatineeApp *app)
{
	(void) app;
}

MatineeApp *
matinee_app_new (void)
{
	return g_object_new (MATINEE_TYPE_APP,
	                     "application-id", "org.luxe.Matinee",
	                     "flags", G_APPLICATION_HANDLES_OPEN,
	                     NULL);
}
