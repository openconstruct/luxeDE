/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include "renderer-init.h"

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    if(g_str_equal(argv[1], "--child")) {
        const char *keys[] = {"GSK_RENDERER", "GDK_DISABLE", "GDK_DEBUG"};
        for (guint i = 0; i < G_N_ELEMENTS(keys); ++i)
            g_print("%s\n", g_getenv(keys[i]) ? g_getenv(keys[i]) : "<unset>");
        return 0;
    }
    const char *original = g_str_equal(argv[1], "unset") ? NULL : argv[1];
    gboolean modern = gtk_check_version(4, 16, 0) == NULL;
    const char *disable = NULL, *debug = NULL;
    if (g_str_equal(argv[1], "empty")) disable = debug = "";
    if (g_str_equal(argv[1], "flags")) {
        disable = modern ? "dmabuf" : NULL;
        debug = "selection";
    }
    if (g_str_equal(argv[1], "all")) {
        if (modern) disable = "all";
        else debug = "all";
    }
    if (g_str_equal(argv[1], "except")) {
        if (modern) disable = "ALL:GL;vulkan,dmabuf";
        else debug = "ALL:gl-disable;vulkan-disable,selection";
    }
    if (!g_str_equal(argv[1], "ngl")) original = NULL;
    if (disable) g_setenv("GDK_DISABLE", disable, TRUE);
    else g_unsetenv("GDK_DISABLE");
    if (debug) g_setenv("GDK_DEBUG", debug, TRUE);
    else g_unsetenv("GDK_DEBUG");
    if(original) g_setenv("GSK_RENDERER", original, TRUE);
    else g_unsetenv("GSK_RENDERER");
    luxepanel_init_gtk();
    luxepanel_init_gtk(); /* repeated calls must not modify the environment */
    g_assert_cmpstr(g_getenv("GSK_RENDERER"), ==, original);
    g_assert_cmpstr(g_getenv("GDK_DISABLE"), ==, disable);
    g_assert_cmpstr(g_getenv("GDK_DEBUG"), ==, debug);
    g_autoptr(GError) graphics_error = NULL;
    g_assert_false(gdk_display_prepare_gl(gdk_display_get_default(), &graphics_error));
    g_assert_nonnull(graphics_error);
    for(guint i = 0; i < 3; i++) {
        GtkWidget *window = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(window), gtk_label_new("Renderer trial"));
        gtk_widget_realize(window);
        GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(window));
        g_assert_true(GSK_IS_CAIRO_RENDERER(renderer));
        g_print("PASS: recreated window %u uses %s\n", i, G_OBJECT_TYPE_NAME(renderer));
        gtk_window_destroy(GTK_WINDOW(window));
    }
    char *child[] = {argv[0], "--child", NULL};
    g_autofree char *output = NULL;
    g_autoptr(GError) error = NULL;
    gint status = -1;
    g_assert_true(g_spawn_sync(NULL, child, NULL, 0, NULL, NULL, &output, NULL, &status, &error));
    g_assert_no_error(error);
    g_assert_true(g_spawn_check_wait_status(status, &error));
    g_assert_no_error(error);
    g_autofree char *expected = g_strdup_printf("%s\n%s\n%s\n",
        original ? original : "<unset>", disable ? disable : "<unset>", debug ? debug : "<unset>");
    g_assert_cmpstr(output, ==, expected);
    g_print("PASS: child inherits original renderer environment, not the panel's trial override\n");
    return 0;
}
