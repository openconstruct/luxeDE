/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

/* SPDX-License-Identifier: GPL-2.0-or-later
 * GTK-free file operations for the GTK4 browser. Runs on a GTask worker. */
#include "file-ops-gtk4.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <glib/gstdio.h>

typedef struct {
    PcmgrFileOperation operation;
    GPtrArray *sources;
    GFile *destination;
    PcmgrProgressCallback progress;
    gpointer progress_data;
    GMainContext *context;
} Operation;

static void operation_free(gpointer data)
{
    Operation *op = data;
    g_ptr_array_unref(op->sources);
    g_clear_object(&op->destination);
    g_clear_pointer(&op->context, g_main_context_unref);
    g_free(op);
}

typedef struct {
    PcmgrProgressCallback callback;
    gpointer user_data;
    double fraction;
    guint completed;
    guint total;
} ProgressReport;

static gboolean progress_dispatch(gpointer data)
{
    ProgressReport *report = data;
    report->callback(report->fraction, report->completed, report->total, report->user_data);
    g_free(report);
    return G_SOURCE_REMOVE;
}

static void report_progress(Operation *op, double fraction, guint completed, guint total)
{
    if (!op->progress || total == 0)
        return;
    if (fraction < 0.0)
        fraction = 0.0;
    if (fraction > 1.0)
        fraction = 1.0;
    ProgressReport *report = g_new(ProgressReport, 1);
    report->callback = op->progress;
    report->user_data = op->progress_data;
    report->fraction = fraction;
    report->completed = completed;
    report->total = total;
    g_main_context_invoke(op->context, progress_dispatch, report);
}

typedef struct {
    Operation *operation;
    guint index;
    guint total;
    gint64 last_time;
    double last_fraction;
} FileCopyProgress;

static void copy_file_progress(goffset current, goffset total_bytes, gpointer data)
{
    FileCopyProgress *state = data;
    if (total_bytes <= 0 || state->total == 0)
        return;
    double fraction = (state->index + (double)current / (double)total_bytes) / (double)state->total;
    gint64 now = g_get_monotonic_time();
    if (now - state->last_time < 100000 && fraction - state->last_fraction < 0.01)
        return;
    state->last_time = now;
    state->last_fraction = fraction;
    report_progress(state->operation, fraction, state->index, state->total);
}

typedef struct { guint visited; gint64 deadline; } CleanupBudget;

#define PCMGR_MAX_STAGING_CLEANUP_ENTRIES 100000
#define PCMGR_STAGING_CLEANUP_TIMEOUT_US (10 * G_USEC_PER_SEC)

static gboolean copy_directory_metadata(GFile *source, GFile *destination,
                                        GCancellable *cancel, GError **error)
{
    const char *attributes = G_FILE_ATTRIBUTE_UNIX_MODE ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED "," G_FILE_ATTRIBUTE_TIME_ACCESS;
    g_autoptr(GFileInfo) info = g_file_query_info(source, attributes,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    if(!info) return FALSE;
    if(g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_MODE) &&
       !g_file_set_attribute_uint32(destination, G_FILE_ATTRIBUTE_UNIX_MODE,
           g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE),
           G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error)) return FALSE;
    const char *time_attrs[] = { G_FILE_ATTRIBUTE_TIME_MODIFIED, G_FILE_ATTRIBUTE_TIME_ACCESS };
    for(guint i = 0; i < G_N_ELEMENTS(time_attrs); ++i) {
        if(g_file_info_has_attribute(info, time_attrs[i]) &&
           !g_file_set_attribute_uint64(destination, time_attrs[i],
               g_file_info_get_attribute_uint64(info, time_attrs[i]),
               G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error)) return FALSE;
    }
    return TRUE;
}

static gboolean remove_tree_no_follow(GFile *file, GCancellable *cancel,
                                      CleanupBudget *budget, GError **error)
{
    g_autoptr(GError) local_error = NULL;
    if (++budget->visited > PCMGR_MAX_STAGING_CLEANUP_ENTRIES ||
        g_get_monotonic_time() >= budget->deadline) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                            "Partial-copy cleanup reached its safety limit.");
        return FALSE;
    }
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &local_error);
    if (!info) {
        if (g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            return TRUE;
        g_propagate_error(error, g_steal_pointer(&local_error));
        return FALSE;
    }
    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
        GFileEnumerator *items = g_file_enumerate_children(file,
            G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            cancel, &local_error);
        if (!items) {
            g_object_unref(info);
            g_propagate_error(error, g_steal_pointer(&local_error));
            return FALSE;
        }
        for (;;) {
            GFileInfo *child_info = g_file_enumerator_next_file(items, cancel, &local_error);
            if (!child_info)
                break;
            GFile *child = g_file_get_child(file, g_file_info_get_name(child_info));
            gboolean removed = remove_tree_no_follow(child, cancel, budget, &local_error);
            g_object_unref(child);
            g_object_unref(child_info);
            if (!removed)
                break;
        }
        if (local_error == NULL &&
            !g_file_enumerator_close(items, cancel, &local_error)) {
            /* Preserve the close failure below. */
        }
        g_object_unref(items);
        if (local_error != NULL) {
            g_object_unref(info);
            g_propagate_error(error, g_steal_pointer(&local_error));
            return FALSE;
        }
    }
    if (!g_file_delete(file, cancel, &local_error)) {
        g_object_unref(info);
        g_propagate_error(error, g_steal_pointer(&local_error));
        return FALSE;
    }
    g_object_unref(info);
    return TRUE;
}

static GFile *new_private_staging_file(GFile *parent)
{
    for (guint attempt = 0; attempt < 32; attempt++) {
        gchar *name = g_strdup_printf(".pcmanager-copy-partial-%u-%u",
                                     (guint)getpid(), g_random_int());
        GFile *candidate = g_file_get_child(parent, name);
        g_free(name);
        GFileInfo *info = g_file_query_info(candidate, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                             NULL, NULL);
        if (info == NULL)
            return candidate;
        g_object_unref(info);
        g_object_unref(candidate);
    }
    return NULL;
}

/* Never follow symlinks during a recursive operation. Directory trees are
 * assembled in a private sibling and renamed only after every child succeeds,
 * so a failed/cancelled copy never leaves a partial destination behind. */
static gboolean copy_tree_full_depth(GFile *source, GFile *destination,
                                     GCancellable *cancel, GError **error,
                                     Operation *op, guint index, guint total,
                                     guint depth)
{
    GFileInfo *info;
    gboolean directory;
    GFile *staging = NULL;
    gboolean result = FALSE;

    if (depth > 128) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Directory nesting is too deep to copy safely.");
        return FALSE;
    }
    info = g_file_query_info(source, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    if (!info)
        return FALSE;
    directory = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
    g_object_unref(info);
    if (!directory) {
        if (op && op->progress && total > 0) {
            FileCopyProgress state = { op, index, total, 0, -1.0 };
            return g_file_copy(source, destination, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                               cancel, copy_file_progress, &state, error);
        }
        return g_file_copy(source, destination, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                           cancel, NULL, NULL, error);
    }

    /* Fail early on a known conflict, but retain the no-overwrite final move
     * below: another process can create the destination during the copy. */
    if (depth == 0) {
        g_autoptr(GError) lookup_error = NULL;
        g_autoptr(GFileInfo) existing = g_file_query_info(destination,
            G_FILE_ATTRIBUTE_STANDARD_TYPE, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            cancel, &lookup_error);
        if (existing) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                                "The copy destination already exists.");
            return FALSE;
        }
        if (!g_error_matches(lookup_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            if (lookup_error) g_propagate_error(error, g_steal_pointer(&lookup_error));
            else g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                     "Could not inspect the copy destination.");
            return FALSE;
        }
    }

    GFile *parent = NULL;
    if (depth == 0) {
        parent = g_file_get_parent(destination);
        if (parent == NULL) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Copy destination has no parent.");
            return FALSE;
        }
        staging = new_private_staging_file(parent);
        if (staging == NULL) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Could not allocate a private copy directory.");
            g_object_unref(parent);
            return FALSE;
        }
        g_autofree char *local_path = g_file_get_path(staging);
        if (local_path) {
            if (cancel && g_cancellable_set_error_if_cancelled(cancel, error)) {
                g_object_unref(parent);
                g_object_unref(staging);
                return FALSE;
            }
            if (g_mkdir(local_path, 0700) != 0) {
                g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                            "Could not create the private copy directory: %s", g_strerror(errno));
                g_object_unref(parent);
                g_object_unref(staging);
                return FALSE;
            }
        } else {
            if (!g_file_make_directory(staging, cancel, error)) {
                g_object_unref(parent);
                g_object_unref(staging);
                return FALSE;
            }
            /* Remote backends may not support UNIX mode attributes; their
             * privacy guarantees still require separate backend-specific work. */
            if (!g_file_set_attribute_uint32(staging, G_FILE_ATTRIBUTE_UNIX_MODE, 0700,
                                             G_FILE_QUERY_INFO_NONE, cancel, error)) {
                g_autofree char *path = g_file_get_parse_name(staging);
                if (error != NULL && *error != NULL)
                    g_prefix_error(error, "Could not establish private remote staging at %s (the empty directory may remain): ", path);
                g_object_unref(parent);
                g_object_unref(staging);
                return FALSE;
            }
        }
    } else {
        /* This child already lives inside the top-level staging tree. */
        if (!g_file_make_directory(destination, cancel, error))
            return FALSE;
        staging = g_object_ref(destination);
    }

    GFileEnumerator *items = g_file_enumerate_children(source,
        G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    if (items != NULL) {
        result = TRUE;
        for (;;) {
            GFileInfo *child_info = g_file_enumerator_next_file(items, cancel, error);
            if (!child_info) {
                if (error != NULL && *error != NULL)
                    result = FALSE;
                break;
            }
            const char *name = g_file_info_get_name(child_info);
            GFile *child = g_file_get_child(source, name);
            GFile *target = g_file_get_child(staging, name);
            result = copy_tree_full_depth(child, target, cancel, error, NULL, 0, 0, depth + 1);
            g_object_unref(child);
            g_object_unref(target);
            g_object_unref(child_info);
            if (!result)
                break;
        }
        g_file_enumerator_close(items, NULL, NULL);
        g_object_unref(items);
    }
    if (result && depth == 0)
        result = copy_directory_metadata(source, staging, cancel, error);
    else if (result)
        result = copy_directory_metadata(source, destination, cancel, error);
    /* Keep the visible staging root private even when copying source metadata. */
    if (result && depth == 0)
        result = g_file_set_attribute_uint32(staging, G_FILE_ATTRIBUTE_UNIX_MODE, 0700,
                                              G_FILE_QUERY_INFO_NONE, cancel, error);
    if (result && depth == 0)
        result = g_file_move(staging, destination, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                             cancel, NULL, NULL, error);
    if (!result && depth == 0) {
        g_autoptr(GError) cleanup_error = NULL;
        g_autofree char *local_path = g_file_get_path(staging);
        g_autoptr(GCancellable) cleanup_cancel = g_cancellable_new();
        CleanupBudget budget = { 0, g_get_monotonic_time() + PCMGR_STAGING_CLEANUP_TIMEOUT_US };
        gboolean cleaned = local_path && remove_tree_no_follow(staging, cleanup_cancel,
                                                                 &budget, &cleanup_error);
        g_autofree char *path = g_file_get_parse_name(staging);
        if (!cleaned) {
            if (!cleanup_error)
                g_set_error_literal(&cleanup_error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "Automatic cleanup is disabled for non-local staging locations.");
            if (error != NULL && *error != NULL)
                g_prefix_error(error, "Copy failed and cleanup of %s also failed (%s): ",
                               path, cleanup_error->message);
            else
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Could not remove partial copy at %s: %s",
                            path, cleanup_error->message);
        }
    }
    g_object_unref(staging);
    g_clear_object(&parent);
    return result;
}

static gboolean copy_tree_full(GFile *source, GFile *destination,
                               GCancellable *cancel, GError **error,
                               Operation *op, guint index, guint total)
{
    return copy_tree_full_depth(source, destination, cancel, error, op, index, total, 0);
}

static gboolean transfer_full(GFile *source, GFile *destination, gboolean move,
                         GCancellable *cancel, GError **error,
                         Operation *op, guint index, guint total)
{
    if (g_file_equal(source, destination) || g_file_has_prefix(destination, source)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "A folder cannot be copied or moved into itself.");
        return FALSE;
    }
    /* Resolve existing local parent symlinks before creating anything. */
    char *src_path = g_file_get_path(source);
    GFile *parent = g_file_get_parent(destination);
    char *dst_path = parent ? g_file_get_path(parent) : NULL;
    char *src_real = src_path ? realpath(src_path, NULL) : NULL;
    char *dst_real = dst_path ? realpath(dst_path, NULL) : NULL;
    gboolean nested = src_real && dst_real &&
        (!strcmp(src_real, dst_real) || (g_str_has_prefix(dst_real, src_real) &&
          (src_real[strlen(src_real)-1] == '/' || dst_real[strlen(src_real)] == '/')));
    free(src_real); free(dst_real); g_free(src_path); g_free(dst_path); g_clear_object(&parent);
    if (nested) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "The destination points inside the source folder.");
        return FALSE;
    }
    if (!move) return copy_tree_full(source, destination, cancel, error, op, index, total);
    /* Let GIO perform supported moves. Do not recursively delete a source
     * that may have changed while a cross-filesystem copy was in progress. */
    if (op && op->progress && total > 0) {
        FileCopyProgress state = { op, index, total, 0, -1.0 };
        return g_file_move(source, destination, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                           cancel, copy_file_progress, &state, error);
    }
    return g_file_move(source, destination, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                       cancel, NULL, NULL, error);
}

/* GIO's trash backend deletes whole trashed directories and their metadata;
 * never traverse local Trash paths or follow a trashed symlink ourselves.
 * Individual failures are counted and reported as a summary so one bad
 * item cannot abort the rest; cancellation still stops immediately. */
static gboolean empty_trash(GCancellable *cancel, GError **error)
{
    GFile *root = g_file_new_for_uri("trash:///");
    GFileEnumerator *items = g_file_enumerate_children(root,
        G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    gboolean ok = items != NULL;
    if (items) {
        guint failed = 0;
        char *first_failure = NULL;
        ok = TRUE;
        for (;;) {
            GError *item_error = NULL;
            GFileInfo *info = g_file_enumerator_next_file(items, cancel, &item_error);
            if (!info) {
                if (item_error) {
                    if (g_cancellable_is_cancelled(cancel) ||
                        g_error_matches(item_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                        g_propagate_error(error, item_error);
                        ok = FALSE;
                    } else {
                        g_propagate_error(error, item_error);
                        ok = FALSE;
                        break;
                    }
                }
                if (!ok)
                    break;
                if (failed) {
                    ok = FALSE;
                    *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%u Trash item%s could not be deleted: %s",
                        failed, failed == 1 ? "" : "s",
                        first_failure ? first_failure : "");
                } else {
                    ok = !item_error;
                    g_clear_error(&item_error);
                }
                break;
            }
            if (g_cancellable_set_error_if_cancelled(cancel, &item_error)) {
                g_propagate_error(error, item_error);
                g_object_unref(info);
                ok = FALSE;
                break;
            }
            GFile *child = g_file_enumerator_get_child(items, info);
            const char *name = g_file_info_get_name(info);
            GError *delete_error = NULL;
            if (!g_file_delete(child, cancel, &delete_error)) {
                if (g_cancellable_is_cancelled(cancel) ||
                    g_error_matches(delete_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                    g_propagate_error(error, delete_error);
                    g_object_unref(child); g_object_unref(info);
                    ok = FALSE;
                    break;
                }
                failed++;
                if (!first_failure)
                    first_failure = g_strdup_printf("%s: %s", name ? name : "",
                                                    delete_error->message);
                g_clear_error(&delete_error);
            }
            g_object_unref(child); g_object_unref(info);
        }
        g_free(first_failure);
        g_file_enumerator_close(items, NULL, NULL);
        g_object_unref(items);
    }
    g_object_unref(root);
    return ok;
}

/* Batch copy/move policy: never overwrite. An existing destination is
 * skipped and reported; any other per-item failure is counted and the batch
 * continues, so one bad file cannot block the rest. Cancellation still
 * stops the batch immediately and is reported as-is. */
#define PCMGR_MAX_CONFLICT_NAMES 8
static void operation_worker(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    Operation *op = data;
    GError *error = NULL;
    gboolean ok = TRUE;
    (void)object;
    if (g_cancellable_set_error_if_cancelled(cancel, &error)) ok = FALSE;
    else if (op->operation == PCMGR_EMPTY_TRASH)
        ok = empty_trash(cancel, &error);
    else if (op->operation == PCMGR_MKDIR)
        ok = g_file_make_directory(op->destination, cancel, &error);
    else if (op->operation == PCMGR_NEW_FILE) {
        GFileOutputStream *stream = g_file_create(op->destination, G_FILE_CREATE_NONE, cancel, &error);
        ok = stream != NULL;
        if (stream) {
            ok = g_output_stream_close(G_OUTPUT_STREAM(stream), cancel, &error);
            g_object_unref(stream);
        }
    } else {
        GString *conflicts = g_string_new(NULL);
        guint skipped = 0, listed = 0, failed = 0;
        char *first_failure = NULL;
        guint total = op->sources->len;
        gboolean report = op->progress != NULL &&
            (op->operation == PCMGR_COPY || op->operation == PCMGR_MOVE ||
             op->operation == PCMGR_TRASH) && total > 0;
        if (report)
            report_progress(op, 0.0, 0, total);
        for (guint i = 0; i < op->sources->len; i++) {
            GFile *source = g_ptr_array_index(op->sources, i);
            GError *item_error = NULL;
            gboolean item_ok;
            if (g_cancellable_set_error_if_cancelled(cancel, &item_error)) {
                g_propagate_error(&error, item_error);
                ok = FALSE; break;
            }
            if (op->operation == PCMGR_TRASH)
                item_ok = g_file_trash(source, cancel, &item_error);
            else if (op->operation == PCMGR_RENAME)
                item_ok = g_file_move(source, op->destination, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                                cancel, NULL, NULL, &item_error);
            else {
                char *name = g_file_get_basename(source);
                GFile *target = g_file_get_child(op->destination, name);
                item_ok = transfer_full(source, target, op->operation == PCMGR_MOVE,
                                        cancel, &item_error, op, i, total);
                g_free(name); g_object_unref(target);
            }
            if (item_ok) {
                if (report)
                    report_progress(op, (double)(i + 1) / (double)total, i + 1, total);
                continue;
            }
            if (g_cancellable_is_cancelled(cancel) ||
                g_error_matches(item_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                g_propagate_error(&error, item_error);
                ok = FALSE; break;
            }
            char *display = g_file_get_parse_name(source);
            if ((op->operation == PCMGR_COPY || op->operation == PCMGR_MOVE) &&
                g_error_matches(item_error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                skipped++;
                if (listed < PCMGR_MAX_CONFLICT_NAMES) {
                    if (conflicts->len) g_string_append(conflicts, ", ");
                    g_string_append(conflicts, display);
                    listed++;
                }
            } else {
                failed++;
                if (!first_failure) {
                    g_prefix_error(&item_error, "%s: ", display);
                    first_failure = g_strdup(item_error->message);
                }
            }
            g_clear_error(&item_error); g_free(display);
            if (report)
                report_progress(op, (double)(i + 1) / (double)total, i + 1, total);
        }
        if (ok && (skipped || failed)) {
            ok = FALSE;
            GString *summary = g_string_new(NULL);
            if (skipped)
                g_string_append_printf(summary, "%u existing file%s skipped: %s%s", skipped,
                    skipped == 1 ? "" : "s", conflicts->str,
                    skipped > listed ? ", and more" : "");
            if (failed) {
                if (summary->len) g_string_append(summary, " ");
                g_string_append_printf(summary, "%u file%s could not be processed: %s",
                    failed, failed == 1 ? "" : "s", first_failure);
            }
            error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, summary->str);
            g_string_free(summary, TRUE);
        }
        g_string_free(conflicts, TRUE); g_free(first_failure);
    }
    if (ok) g_task_return_boolean(task, TRUE);
    else g_task_return_error(task, error ? error : g_error_new_literal(
         G_IO_ERROR, G_IO_ERROR_FAILED, "The file operation did not finish."));
}

void pcmgr_file_operation_async_with_progress(PcmgrFileOperation operation, GPtrArray *sources,
    GFile *destination, GCancellable *cancel, PcmgrProgressCallback progress,
    gpointer progress_data, GAsyncReadyCallback callback, gpointer data)
{
    Operation *op = g_new0(Operation, 1);
    op->operation = operation;
    op->sources = g_ptr_array_new_with_free_func(g_object_unref);
    if (sources) for (guint i = 0; i < sources->len; i++)
        g_ptr_array_add(op->sources, g_object_ref(g_ptr_array_index(sources, i)));
    op->destination = destination ? g_object_ref(destination) : NULL;
    op->progress = progress;
    op->progress_data = progress_data;
    op->context = g_main_context_ref_thread_default();
    if (!op->context)
        op->context = g_main_context_ref(g_main_context_default());
    GTask *task = g_task_new(NULL, cancel, callback, data);
    g_task_set_task_data(task, op, operation_free);
    /* Wait for the worker to stop before telling the UI it is safe to close. */
    g_task_set_return_on_cancel(task, FALSE);
    g_task_run_in_thread(task, operation_worker);
    g_object_unref(task);
}

void pcmgr_file_operation_async(PcmgrFileOperation operation, GPtrArray *sources,
    GFile *destination, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data)
{
    pcmgr_file_operation_async_with_progress(operation, sources, destination, cancel,
                                             NULL, NULL, callback, data);
}

gboolean pcmgr_file_operation_finish(GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}
