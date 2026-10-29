/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PCMGR_FILE_OPS_GTK4_H
#define PCMGR_FILE_OPS_GTK4_H
#include <gio/gio.h>
typedef enum { PCMGR_COPY, PCMGR_MOVE, PCMGR_TRASH, PCMGR_MKDIR, PCMGR_NEW_FILE, PCMGR_RENAME, PCMGR_EMPTY_TRASH } PcmgrFileOperation;
/* Progress callback runs on the thread-default main context (safe for GTK).
 * fraction is 0.0..1.0, completed/total count top-level sources processed. */
typedef void (*PcmgrProgressCallback)(double fraction, guint completed, guint total, gpointer user_data);
/* Sources contain GFile objects. Destination is a directory for copy/move,
 * and the complete target filename for create/rename. Nothing is overwritten:
 * copy/move skip existing destinations and report them, while any other
 * per-item failure is counted and the batch continues. A summary error is
 * returned when anything was skipped or failed. */
void pcmgr_file_operation_async(PcmgrFileOperation operation, GPtrArray *sources,
    GFile *destination, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer data);
void pcmgr_file_operation_async_with_progress(PcmgrFileOperation operation, GPtrArray *sources,
    GFile *destination, GCancellable *cancellable, PcmgrProgressCallback progress,
    gpointer progress_data, GAsyncReadyCallback callback, gpointer data);
gboolean pcmgr_file_operation_finish(GAsyncResult *result, GError **error);
#endif
