/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include "luxe-gtk-ui.h"
#include <string.h>
static int verify_application_run(GApplication *application, int argc, char **argv);
#define main matinee_production_main
#define g_application_run verify_application_run
#include "../matinee.c"
#undef main
#undef g_application_run

static const char *expected_renderer, *expected_disable, *expected_debug;
static int verify_application_run(GApplication *application, int argc, char **argv)
{
    (void)argc; (void)argv;
    // This checks initialization by the real production main, before the
    // test invokes GtkApplication startup or creates any window itself.
    g_assert_nonnull(gdk_display_get_default());
    g_assert_cmpstr(g_getenv("GSK_RENDERER"), ==, expected_renderer);
    g_assert_cmpstr(g_getenv("GDK_DISABLE"), ==, expected_disable);
    g_assert_cmpstr(g_getenv("GDK_DEBUG"), ==, expected_debug);
    g_assert_true(g_application_register(application, NULL, NULL));
    g_application_activate(application);
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(application));
    g_assert_nonnull(window);
    g_assert_true(GSK_IS_CAIRO_RENDERER(gtk_native_get_renderer(GTK_NATIVE(window))));
    gtk_window_destroy(window);
    return 0;
}
int main(int argc, char **argv)
{
    const char *mode = argc == 2 ? argv[1] : "unset";
    if (!strcmp(mode, "empty")) expected_renderer = expected_disable = expected_debug = "";
    else if (!strcmp(mode, "explicit")) {
        expected_renderer = "gl"; expected_disable = "vulkan"; expected_debug = "";
    }
    const char *names[] = {"GSK_RENDERER", "GDK_DISABLE", "GDK_DEBUG"};
    const char *values[] = {expected_renderer, expected_disable, expected_debug};
    for (guint i = 0; i < G_N_ELEMENTS(names); ++i) {
        if (values[i]) g_setenv(names[i], values[i], TRUE);
        else g_unsetenv(names[i]);
    }
    char *arguments[] = {argv[0], NULL};
    return matinee_production_main(1, arguments);
}
