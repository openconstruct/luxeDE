/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Diagnostic fixture only: private GTK settings, never desktop configuration. */
#include <adwaita.h>
#include <stdio.h>

static void settle(void)
{
    gint64 until = g_get_monotonic_time() + 300000;
    while (g_get_monotonic_time() < until) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
}

static void report(guint cycle)
{
    char *contents = NULL;
    g_assert_true(g_file_get_contents("/proc/self/smaps_rollup", &contents, NULL, NULL));
    unsigned long pss = 0, anon = 0;
    char **lines = g_strsplit(contents, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        sscanf(lines[i], "Pss: %lu", &pss);
        sscanf(lines[i], "Pss_Anon: %lu", &anon);
    }
    g_print("cycle=%u pss_kib=%lu anonymous_kib=%lu\n", cycle, pss, anon);
    g_strfreev(lines);
    g_free(contents);
}

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    adw_init();
    GtkWidget *window = adw_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(box), adw_header_bar_new());
    gtk_box_append(GTK_BOX(box), gtk_entry_new());
    gtk_box_append(GTK_BOX(box), gtk_button_new_with_label("Theme retention fixture"));
    const char *icons[] = {"folder-symbolic", "dialog-warning-symbolic", "network-wireless-symbolic"};
    for (guint i = 0; i < G_N_ELEMENTS(icons); i++)
        gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icons[i]));
    adw_window_set_content(ADW_WINDOW(window), box);
    gtk_window_present(GTK_WINDOW(window));
    GtkCssProvider *bridge = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(bridge),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER);
    const char *palettes[] = {"light", "dark", "nord", "mocha", "dracula", "tokyo-night"};
    for (guint cycle = 0; cycle < 12; cycle++) {
        for (guint i = 0; i < G_N_ELEMENTS(palettes); i++) {
            g_autofree char *name = g_strdup_printf("Luxe-%s", palettes[i]);
            g_autofree char *path = g_build_filename(argv[1], "bridges", palettes[i], "colors.css", NULL);
            g_autofree char *modern_path = g_build_filename(argv[1], "bridges", palettes[i], "modern.css", NULL);
            g_autofree char *colors = NULL, *modern = NULL;
            g_assert_true(g_file_get_contents(path, &colors, NULL, NULL));
            g_assert_true(g_file_get_contents(modern_path, &modern, NULL, NULL));
            g_autofree char *css = g_strconcat(colors, "\n", modern, NULL);
            gtk_css_provider_load_from_data(bridge, css, -1);
            g_object_set(gtk_settings_get_default(), "gtk-theme-name", name,
                         "gtk-icon-theme-name", i == 0 ? "Luxe-Light" : "Luxe-Dark", NULL);
            settle();
        }
        for (guint i = 0; i < 4; i++) settle();
        report((cycle + 1) * G_N_ELEMENTS(palettes));
    }
    gtk_style_context_remove_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(bridge));
    g_object_unref(bridge);
    gtk_window_destroy(GTK_WINDOW(window));
    settle();
    return 0;
}
