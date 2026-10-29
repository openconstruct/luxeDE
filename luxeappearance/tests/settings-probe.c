/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
/* Verify that the Apply test actually persisted the settings, and report
 * the live theme plus X selection owners. Exits non-zero when persistence
 * is missing, so the CTest cannot pass vacuously. */
#include <gtk/gtk.h>
#include <glib.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#endif

int main(void)
{
    GError *error = NULL;
    g_autofree char *path = NULL;
    g_autoptr(GKeyFile) key = NULL;
    g_autofree char *theme = NULL;
    g_autofree char *icons = NULL;
    gboolean ok = TRUE;

    gtk_init();
    g_object_get(gtk_settings_get_default(), "gtk-theme-name", &theme, NULL);
    g_print("GTK4 effective theme: %s\n", theme ? theme : "(unset)");

    /* The save test writes gtk-4.0/settings.ini; the probe runs afterwards in
     * the same XDG state and must see it. */
    path = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
    key = g_key_file_new();
    if (!g_key_file_load_from_file(key, path, 0, &error)) {
        g_printerr("FAIL: gtk-4.0/settings.ini was not persisted: %s\n",
                   error ? error->message : "unknown error");
        g_clear_error(&error);
        ok = FALSE;
    } else {
        g_autofree char *saved = g_key_file_get_string(key, "Settings", "gtk-theme-name", NULL);
        g_print("Persisted GTK theme: %s\n", saved ? saved : "(unset)");
        if (!saved || !*saved) {
            g_printerr("FAIL: persisted gtk-theme-name is empty\n");
            ok = FALSE;
        }
        /* Unknown keys must survive Apply. */
        g_autofree char *dclick = g_key_file_get_string(key, "Settings", "gtk-double-click-time", NULL);
        if (!g_strcmp0(dclick, "333"))
            g_print("Unrelated key preserved: gtk-double-click-time=%s\n", dclick);
        else {
            g_printerr("FAIL: unrelated key gtk-double-click-time was lost (got '%s')\n",
                       dclick ? dclick : "(missing)");
            ok = FALSE;
        }
    }
    if (ok)
        g_print("PASS: appearance settings persisted with unrelated keys intact\n");
    else
        g_print("FAIL: appearance settings were not persisted\n");

#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        Display *d = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        g_print("LUXESESSION owner: %lu; XSETTINGS owner: %lu\n",
                XGetSelectionOwner(d, XInternAtom(d, "_LUXESESSION", False)),
                XGetSelectionOwner(d, XInternAtom(d, "_XSETTINGS_S0", False)));
    }
#endif
    return ok ? 0 : 1;
}
