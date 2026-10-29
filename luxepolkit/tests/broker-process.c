/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
static const char *dialog_path;
#include "../src/broker-listener.c"
#include <stdio.h>
static GMainLoop *test_loop;
static gboolean ok;
static GError *failure;
static gboolean rejected;
static void busy_done(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void) unused;
    GError *error = NULL;
    g_assert_false(initiate_authentication_finish(POLKIT_AGENT_LISTENER(source), result, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error(&error);
    rejected = TRUE;
}
static void done(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void) unused;
    ok = initiate_authentication_finish(POLKIT_AGENT_LISTENER(source), result, &failure);
    g_main_loop_quit(test_loop);
}
static gboolean cancel_later(gpointer cancel)
{
    g_cancellable_cancel(cancel);
    return G_SOURCE_REMOVE;
}
static gboolean expired(gpointer unused)
{
    (void) unused;
    g_error("authentication fixture timed out");
    return G_SOURCE_REMOVE;
}
static void request(PolkitAgentListener *listener, const char *mode, gboolean expected,
                    gboolean cancel_request)
{
    g_setenv("LUXE_POLKIT_FIXTURE_MODE", mode, TRUE);
    PolkitDetails *details = polkit_details_new();
    polkit_details_insert(details, "fixture-key", "fixture-value");
    PolkitIdentity *id = polkit_unix_user_new(getuid());
    GList *ids = g_list_append(NULL, id);
    GCancellable *cancel = g_cancellable_new();
    initiate_authentication(listener, "org.luxe.fixture", "Fixture authentication", "",
        details, "fixture-cookie-not-an-authorization", ids, cancel, done, NULL);
    if (cancel_request) {
        rejected = FALSE;
        initiate_authentication(listener, "org.luxe.fixture.second", "Second", "",
            details, "cookie", ids, NULL, busy_done, NULL);
        /* The ready child may wait longer than the startup deadline. */
        g_timeout_add(1300, cancel_later, cancel);
    }
    g_main_loop_run(test_loop);
    g_assert_cmpint(ok, ==, expected);
    g_assert_false(busy);
    if (cancel_request) g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    if (cancel_request) g_assert_true(rejected);
    if (!g_strcmp0(mode, "cancel")) g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    if (!g_strcmp0(mode, "never-ready") || !g_strcmp0(mode, "no-prompt"))
        g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    if (!g_strcmp0(mode, "grab-contended")) {
        g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
        g_assert_nonnull(strstr(failure->message, "conflicting"));
    }
    g_clear_error(&failure);
    g_object_unref(cancel);
    g_list_free_full(ids, g_object_unref);
    g_object_unref(details);
    gchar *maps = NULL;
    g_assert_true(g_file_get_contents("/proc/self/maps", &maps, NULL, NULL));
    g_assert_null(strstr(maps, "libgtk-4"));
    g_free(maps);
}
int main(void)
{
    dialog_path = FIXTURE_DIALOG_PATH;
    test_loop = g_main_loop_new(NULL, FALSE);
    guint timeout = g_timeout_add_seconds(15, expired, NULL);
    PolkitAgentListener *listener = luxepolkit_listener_new();
    request(listener, "success", TRUE, FALSE);
    dialog_path = "/nonexistent/luxe-polkit-dialog";
    request(listener, "success", FALSE, FALSE);
    dialog_path = FIXTURE_DIALOG_PATH;
    request(listener, "success", TRUE, FALSE);
    request(listener, "retry", TRUE, FALSE);
    request(listener, "cancel", FALSE, FALSE);
    request(listener, "crash", FALSE, FALSE);
    request(listener, "wait", FALSE, TRUE);
    request(listener, "success", TRUE, FALSE);
    request(listener, "never-ready", FALSE, FALSE);
    request(listener, "success", TRUE, FALSE);
    request(listener, "grab-contended", FALSE, FALSE);
    request(listener, "success", TRUE, FALSE);
    request(listener, "no-prompt", FALSE, FALSE);
    request(listener, "success", TRUE, FALSE);
    /* Reject before serializing a large detail, then accept a later request. */
    PolkitDetails *oversized = polkit_details_new();
    g_autofree char *value = g_strnfill(REQUEST_FIELD_LIMIT + 1, 'x');
    polkit_details_insert(oversized, "large", value);
    PolkitIdentity *user = polkit_unix_user_new(getuid());
    GList *permitted = g_list_append(NULL, user);
    initiate_authentication(listener, "fixture", "Large", "", oversized,
        "cookie", permitted, NULL, done, NULL);
    g_main_loop_run(test_loop);
    g_assert_false(ok);
    g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_clear_error(&failure);
    g_assert_false(busy);
    g_list_free_full(permitted, g_object_unref);
    g_object_unref(oversized);
    request(listener, "success", TRUE, FALSE);
    /* Empty identities never start a child. */
    PolkitDetails *details = polkit_details_new();
    initiate_authentication(listener, "fixture", "Empty", "", details,
        "cookie", NULL, NULL, done, NULL);
    g_main_loop_run(test_loop);
    g_assert_false(ok);
    g_assert_nonnull(failure);
    g_clear_error(&failure);
    g_object_unref(details);
    g_object_unref(listener);
    g_source_remove(timeout);
    g_main_loop_unref(test_loop);
    puts("PASS: success/retry/cancel/crash/restart/missing child/busy, GTK-free parent, no identities");
    return 0;
}
