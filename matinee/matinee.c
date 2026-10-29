/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <locale.h>

#include "matinee-app.h"

GResource *matinee_get_resource (void);

int
main (int argc, char *argv[])
{
	MatineeApp *app;
	int status;

	/* Templates are loaded from the application's GResource. */
	g_resources_register (matinee_get_resource ());

	setlocale (LC_ALL, "");
	bindtextdomain ("matinee", LOCALEDIR);
	bind_textdomain_codeset ("matinee", "UTF-8");
	textdomain ("matinee");

	g_set_application_name ("Matinee");
	g_set_prgname ("matinee");
	luxe_ui_init ();

	app = matinee_app_new ();
	status = g_application_run (G_APPLICATION (app), argc, argv);
	g_object_unref (app);
	return status;
}
