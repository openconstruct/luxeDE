/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

#include "frame-app.h"
#include "frame-wallpaper.h"

int
main (int argc, char **argv)
{
	int wallpaper = frame_wallpaper_main(argc, argv);
	if (wallpaper >= 0) return wallpaper;
	luxe_ui_init ();
	FrameApp *app = frame_app_new();
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
