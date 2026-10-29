/* Copyright (C) 2020 Tom Zaluckij. Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

#include "screenshot-app.h"

int
main (int argc, char **argv)
{
	luxe_ui_init ();
	g_autoptr(ScreenshotApp) app = screenshot_app_new ();
	return g_application_run (G_APPLICATION (app), argc, argv);
}
