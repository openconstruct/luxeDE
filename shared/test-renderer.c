/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxe-renderer.h"

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    const char *keys[] = {"GSK_RENDERER", "GDK_DISABLE", "GDK_DEBUG"};
    if (g_str_equal(argv[1], "--child")) {
        for (guint i = 0; i < G_N_ELEMENTS(keys); ++i)
            g_print("%s\n", g_getenv(keys[i]) ? g_getenv(keys[i]) : "<unset>");
        return 0;
    }
    /* Test both token vocabularies even when only one GTK runtime is present. */
    g_autofree char *modern = luxe_without_graphics("ALL:GL;vulkan,dmabuf", "gl", "vulkan");
    g_autofree char *legacy = luxe_without_graphics("ALL:gl-disable;vulkan-disable,selection", "gl-disable", "vulkan-disable");
    g_assert_cmpstr(modern, ==, "ALL,dmabuf");
    g_assert_cmpstr(legacy, ==, "ALL,selection");
    const char *values[] = {NULL, NULL, NULL};
    gboolean is_modern = gtk_check_version(4, 16, 0) == NULL;
    if (g_str_equal(argv[1], "empty")) values[0] = values[1] = values[2] = "";
    if (g_str_equal(argv[1], "ngl")) values[0] = "ngl";
    if (g_str_equal(argv[1], "flags")) {
        values[1] = is_modern ? "dmabuf" : NULL;
        values[2] = "selection";
    }
    if (g_str_equal(argv[1], "all")) values[is_modern ? 1 : 2] = "all";
    if (g_str_equal(argv[1], "except")) values[is_modern ? 1 : 2] = is_modern
        ? "ALL:GL;vulkan,dmabuf" : "ALL:gl-disable;vulkan-disable,selection";
    for (guint i = 0; i < G_N_ELEMENTS(keys); ++i) {
        if (values[i]) g_setenv(keys[i], values[i], TRUE);
        else g_unsetenv(keys[i]);
    }
    luxe_gtk_init();
    luxe_gtk_init();
    for (guint i = 0; i < G_N_ELEMENTS(keys); ++i)
        g_assert_cmpstr(g_getenv(keys[i]), ==, values[i]);
    g_autoptr(GError) error = NULL;
    g_assert_false(gdk_display_prepare_gl(gdk_display_get_default(), &error));
    g_assert_nonnull(error);
    for (guint i = 0; i < 3; ++i) {
        GtkWidget *window = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(window), gtk_entry_new());
        gtk_widget_realize(window);
        g_assert_true(GSK_IS_CAIRO_RENDERER(gtk_native_get_renderer(GTK_NATIVE(window))));
        gtk_window_destroy(GTK_WINDOW(window));
    }
    char *child[] = {argv[0], "--child", NULL};
    g_autofree char *output = NULL;
    gint status = -1;
    g_clear_error(&error);
    g_assert_true(g_spawn_sync(NULL, child, NULL, 0, NULL, NULL, &output, NULL, &status, &error));
    g_assert_no_error(error);
    g_assert_true(g_spawn_check_wait_status(status, &error));
    g_autofree char *expected = g_strdup_printf("%s\n%s\n%s\n",
        values[0] ? values[0] : "<unset>", values[1] ? values[1] : "<unset>",
        values[2] ? values[2] : "<unset>");
    g_assert_cmpstr(output, ==, expected);
    g_print("PASS: no GL, recreated Cairo windows, exact environment and child inheritance\n");
    return 0;
}
