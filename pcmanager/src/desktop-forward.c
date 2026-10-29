/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Compatibility entry points only: Frame owns decoding, painting and settings. */
#include "desktop-gtk4.h"
#include <gio/gio.h>
static void child_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)data;
    g_autoptr(GError) error = NULL;
    if (!g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error))
        g_warning("Frame wallpaper command failed: %s", error->message);
}
static void launch(const char *option, const char *path)
{
    g_autoptr(GError) error = NULL;
    const char *argv[] = {"frame", option, path, NULL};
    GSubprocess *child = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
    if (!child) {
        g_warning("Cannot launch Frame wallpaper: %s", error->message);
        return;
    }
    g_subprocess_wait_check_async(child, NULL, child_finished, NULL);
    g_object_unref(child);
}
void pcmgr_desktop_preferences(void) {launch("--wallpaper-settings",NULL);}
void pcmgr_desktop_manager_init(const char *profile) {(void)profile; launch("--wallpaper-daemon",NULL);}
gboolean pcmgr_desktop_running(void) {return FALSE;}
void pcmgr_desktop_manager_finalize(void) {}
void pcmgr_desktop_reload(void) {launch("--wallpaper-daemon",NULL);}
void pcmgr_set_wallpaper(const char *file,const char *mode)
{
    if (mode && !g_str_equal(mode, "stretch")) {
        g_warning("Frame supports only stretch wallpaper mode (requested: %s)", mode);
        return;
    }
    if (file) launch("--set-wallpaper", file);
}
