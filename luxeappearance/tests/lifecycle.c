/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the real activation path: a second activation must present the
 * existing window, and a destroyed window must be replaceable by a fresh one. */
#define main luxeappearance_entry
#include "../src/main.c"
#undef main

static unsigned count_windows(GtkApplication* app)
{
    GList* windows = gtk_application_get_windows(app);
    /* GTK owns this list; it is transfer-none. */
    return g_list_length(windows);
}

int main(void)
{
    GtkApplication* app;
    GtkWindow* first;
    GtkWindow* second;
    GError* error = NULL;

    gtk_init();
    luxe_ui_init();
    app = GTK_APPLICATION(luxe_application_new("org.luxe.Appearance.Test",
                                               G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, &error));
    g_assert_no_error(error);
    /* on_activate() is normally called after GApplication has emitted its
     * startup signal; emit it explicitly because this test drives the private
     * callback directly instead of running the application main loop. */
    g_signal_emit_by_name(app, "startup");

    on_activate(GTK_APPLICATION(app), NULL);
    g_assert_cmpuint(count_windows(GTK_APPLICATION(app)), ==, 1);
    first = gtk_application_get_active_window(GTK_APPLICATION(app));
    g_assert_nonnull(first);
    /* Keep the old object's address reserved until the new one is created;
     * otherwise a freed allocation may legitimately be reused. */
    g_object_ref(first);

    /* A launcher/D-Bus activation must not create a second dialog or replace
     * the controls owned by the first one. */
    on_activate(GTK_APPLICATION(app), NULL);
    g_assert_cmpuint(count_windows(GTK_APPLICATION(app)), ==, 1);
    g_assert_true(gtk_application_get_active_window(GTK_APPLICATION(app)) == first);

    gtk_window_close(first);
    g_assert_cmpuint(count_windows(GTK_APPLICATION(app)), ==, 0);

    /* The destroy callback must have cleared both the application pointer
     * and the builder registry, allowing a clean subsequent activation. */
    on_activate(GTK_APPLICATION(app), NULL);
    g_assert_cmpuint(count_windows(GTK_APPLICATION(app)), ==, 1);
    second = gtk_application_get_active_window(GTK_APPLICATION(app));
    g_assert_nonnull(second);
    g_assert_true(second != first);
    gtk_window_close(second);
    g_object_unref(first);

    g_object_unref(app);
    return 0;
}
