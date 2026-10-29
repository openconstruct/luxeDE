/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxe-lock-request.h"
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>

gboolean luxe_lock_request(const char *program, guint timeout_ms, GCancellable *cancel, GError **error)
{
    int sockets[2];
    if (!program || !*program || !timeout_ms || timeout_ms > 60000) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid lock request");
        return FALSE;
    }
    if (g_cancellable_set_error_if_cancelled(cancel, error)) return FALSE;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot create lock readiness channel: %s", g_strerror(errno));
        return FALSE;
    }
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_take_fd(launcher, sockets[1], 3);
    g_autoptr(GSubprocess) child = g_subprocess_launcher_spawn(launcher, error, program, "--ready-fd", "3", NULL);
    g_clear_object(&launcher);
    gboolean ready = FALSE;
    if (!child) { close(sockets[0]); return FALSE; }
    const gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
    for (;;) {
        if (g_cancellable_set_error_if_cancelled(cancel, error)) break;
        gint64 remaining = deadline - g_get_monotonic_time();
        if (remaining <= 0) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Screen locking did not confirm readiness in time");
            break;
        }
        struct pollfd descriptor = {sockets[0], POLLIN, 0};
        int result = poll(&descriptor, 1, (int)MIN(100, (remaining + 999) / 1000));
        if (result < 0 && errno == EINTR) continue;
        if (result == 0) continue;
        char answer = 0;
        ssize_t count = result > 0 ? recv(sockets[0], &answer, 1, MSG_DONTWAIT) : -1;
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count == 1 && answer == 'R') ready = TRUE;
        else g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "The screen locker failed or could not confirm that the screen is locked");
        break;
    }
    close(sockets[0]);
    // GSubprocess reaps the child independently. Never terminate a live lock
    // when the request times out or its requesting window closes.
    return ready;
}

typedef struct { char *program; guint timeout_ms; } LockRequest;
static void request_free(gpointer data)
{
    LockRequest *request = data;
    g_free(request->program); g_free(request);
}
static void request_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void)source;
    LockRequest *request = data;
    GError *error = NULL;
    if (luxe_lock_request(request->program, request->timeout_ms, cancel, &error)) g_task_return_boolean(task, TRUE);
    else g_task_return_error(task, error);
}
void luxe_lock_request_async(const char *program, guint timeout_ms, GCancellable *cancel,
                             GAsyncReadyCallback callback, gpointer data)
{
    GTask *task = g_task_new(NULL, cancel, callback, data);
    LockRequest *request = g_new0(LockRequest, 1);
    request->program = g_strdup(program); request->timeout_ms = timeout_ms;
    g_task_set_task_data(task, request, request_free);
    g_task_set_return_on_cancel(task, FALSE);
    g_task_run_in_thread(task, request_worker);
    g_object_unref(task);
}
gboolean luxe_lock_request_finish(GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}
