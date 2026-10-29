/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gio/gio.h>
G_BEGIN_DECLS
/* Completion means secured and ready, never merely spawned or unlocked.
 * Cancelling a request must not kill a locker which may already hold grabs. */
gboolean luxe_lock_request(const char *program, guint timeout_ms, GCancellable *cancel, GError **error);
void luxe_lock_request_async(const char *program, guint timeout_ms, GCancellable *cancel,
                             GAsyncReadyCallback callback, gpointer data);
gboolean luxe_lock_request_finish(GAsyncResult *result, GError **error);
G_END_DECLS
