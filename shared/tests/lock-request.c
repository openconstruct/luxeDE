/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../luxe-lock-request.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

static char *program;
static gpointer cancel_later(gpointer data)
{
    g_usleep(20000);
    g_cancellable_cancel(data);
    return NULL;
}
static void check(const char *mode, guint timeout, gboolean expected, int code)
{
    g_setenv("LUXE_TEST_LOCK_MODE", mode, TRUE);
    g_autoptr(GError) error = NULL;
    gint64 start = g_get_monotonic_time();
    gboolean ready = luxe_lock_request(program, timeout, NULL, &error);
    g_assert_cmpint(ready, ==, expected);
    if (expected) {
        g_assert_no_error(error);
        // The fake locker deliberately remains alive after acknowledging.
        g_assert_cmpint(g_get_monotonic_time() - start, <, 500000);
    } else g_assert_error(error, G_IO_ERROR, code);
}
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--ready-fd") && !strcmp(argv[2], "3")) {
        const char *mode = g_getenv("LUXE_TEST_LOCK_MODE");
        if (!g_strcmp0(mode, "fail")) return 1;
        if (!g_strcmp0(mode, "delay")) g_usleep(100000);
        char reply = !g_strcmp0(mode, "wrong") ? 'X' : 'R';
        (void)send(3, &reply, 1, MSG_NOSIGNAL);
        close(3);
        g_usleep(600000);
        return 0;
    }
    program = argc == 2 ? argv[1] : argv[0];
    check("ready", 1000, TRUE, 0);
    check("delay", 1000, TRUE, 0);
    check("fail", 1000, FALSE, G_IO_ERROR_FAILED);
    check("wrong", 1000, FALSE, G_IO_ERROR_FAILED);
    check("delay", 20, FALSE, G_IO_ERROR_TIMED_OUT);
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_autoptr(GError) error = NULL;
    g_assert_false(luxe_lock_request(program, 1000, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    g_cancellable_reset(cancel);
    g_setenv("LUXE_TEST_LOCK_MODE", "delay", TRUE);
    GThread *thread = g_thread_new("cancel-lock-request", cancel_later, cancel);
    g_assert_false(luxe_lock_request(program, 1000, cancel, &error));
    g_thread_join(thread);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    g_assert_false(luxe_lock_request("/nonexistent/luxe-locker", 1000, NULL, &error));
    g_assert_nonnull(error);
    g_usleep(800000); // Allow bounded fake children to finish and be reaped.
    return 0;
}
