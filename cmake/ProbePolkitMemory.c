/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic only. Run under RunNestedX.py, never as a real auth agent.
 * Reuse the existing production-listener fixture's mocked polkit session.
 */
#define main existing_dialog_test_main
#include "../luxepolkit/tests/dialog-test.c"
#undef main
#include <malloc.h>

static void settle(guint milliseconds)
{
    gint64 end = g_get_monotonic_time() + milliseconds * 1000;
    do {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(10000);
    } while (g_get_monotonic_time() < end);
}

static void finalized_dialog(gpointer counter, GObject *object)
{
    (void)object;
    ++*(guint *)counter;
}

static void sample(const char *phase, guint cycle, guint finalized)
{
    g_autofree char *text = NULL;
    gsize pss = 0, anon = 0, rss = 0;
    g_assert_true(g_file_get_contents("/proc/self/smaps_rollup", &text, NULL, NULL));
    g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
    for (guint i = 0; lines[i]; ++i) {
        if (g_str_has_prefix(lines[i], "Pss:")) pss = g_ascii_strtoull(lines[i] + 4, NULL, 10);
        if (g_str_has_prefix(lines[i], "Pss_Anon:")) anon = g_ascii_strtoull(lines[i] + 9, NULL, 10);
        if (g_str_has_prefix(lines[i], "Rss:")) rss = g_ascii_strtoull(lines[i] + 4, NULL, 10);
    }
    guint windows = gtk_is_initialized() ? g_list_model_get_n_items(gtk_window_get_toplevels()) : 0;
    g_print("{\"phase\":\"%s\",\"cycle\":%u,\"pss_kib\":%zu,\"anon_kib\":%zu,"
            "\"rss_kib\":%zu,\"windows\":%u,\"finalized\":%u}\n",
            phase, cycle, pss, anon, rss, windows, finalized);
}

int main(int argc, char **argv)
{
    g_assert_cmpint(getuid(), !=, 0);
    g_assert_cmpint(argc, ==, 2);
    g_assert_true(!strcmp(argv[1], "default") || !strcmp(argv[1], "cairo"));
    if (!strcmp(argv[1], "default")) g_unsetenv("GSK_RENDERER");
    else g_setenv("GSK_RENDERER", "cairo", TRUE);
    g_setenv("GSK_DEBUG", "renderer", TRUE);
    g_setenv("GTK_THEME", "Luxe-tokyo-night", TRUE);
    /* No real authentication, credentials, policy or desktop settings used. */
    guint finalized = 0;
    LuxePolkitListener *listener = g_object_new(LUXEPOLKIT_LISTENER_TYPE, NULL);
    PolkitDetails *details = polkit_details_new();
    sample("before-gtk", 0, finalized);
    complete_in_response = TRUE;
    for (guint cycle = 1; cycle <= 6; ++cycle) {
        GCancellable *cancel = g_cancellable_new();
        GList *ids = g_list_append(NULL, polkit_unix_user_new(getuid()));
        empty_done = FALSE;
        empty_ok = FALSE;
        initiate_authentication(POLKIT_AGENT_LISTENER(listener), "fixture.memory",
            "Private memory diagnostic", "", details, "fixture-cookie", ids,
            cancel, on_empty_ready, listener);
        GListModel *windows = gtk_window_get_toplevels();
        GtkDialog *dialog = NULL;
        for (guint i = 0; i < g_list_model_get_n_items(windows); ++i) {
            GtkWidget *window = g_list_model_get_item(windows, i);
            if (GTK_IS_DIALOG(window)) dialog = GTK_DIALOG(g_object_ref(window));
            g_object_unref(window);
        }
        g_assert_nonnull(dialog);
        g_object_weak_ref(G_OBJECT(dialog), finalized_dialog, &finalized);
        settle(500);
        sample("mapped", cycle, finalized);
        gtk_editable_set_text(GTK_EDITABLE(find_entry(GTK_WIDGET(dialog))), "fixture-response");
        gtk_dialog_response(dialog, GTK_RESPONSE_OK);
        while (!empty_done) g_main_context_iteration(NULL, TRUE);
        g_assert_true(empty_ok);
        g_assert_no_error(empty_err);
        g_object_unref(dialog);
        g_list_free_full(ids, g_object_unref);
        g_object_unref(cancel);
        g_clear_object(&active);
        settle(2000);
        g_assert_cmpuint(finalized, ==, cycle);
        g_assert_cmpuint(g_list_model_get_n_items(gtk_window_get_toplevels()), ==, 0);
        sample("closed", cycle, finalized);
    }
    /* Attribute retained free libc heap only in this disposable process.
     * This is not a proposed production trim or global allocator policy. */
    malloc_trim(0);
    settle(100);
    sample("diagnostic-trim", 6, finalized);
    g_object_unref(details);
    g_object_unref(listener);
    return 0;
}
