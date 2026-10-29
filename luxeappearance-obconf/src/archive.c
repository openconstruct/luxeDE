/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Archive helpers for ObConf. Installation is delegated to the shipped,
 * auditable shell helper asynchronously; creation uses a local tar invocation
 * only after the source tree has been checked for links and special files. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "archive.h"
#include "main.h"
#include "../luxe-dialogs.h"
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <linux/fs.h>
#endif

#define gtk_msg(type, fmt, ...) \
    do { \
        gchar *_arch_msg = g_strdup_printf(fmt, ##__VA_ARGS__); \
        if ((type) == GTK_MESSAGE_ERROR) \
            luxe_show_error(mainwin ? GTK_WINDOW(mainwin) : NULL, _arch_msg, NULL); \
        else \
            luxe_show_message(mainwin ? GTK_WINDOW(mainwin) : NULL, _arch_msg, NULL); \
        g_free(_arch_msg); \
    } while (0)

static gboolean valid_theme_name(const gchar *name)
{
    const gchar *p;
    if (!name || !*name || strchr(name, '/') ||
        g_strcmp0(name, ".") == 0 || g_strcmp0(name, "..") == 0 ||
        !g_utf8_validate(name, -1, NULL))
        return FALSE;
    for (p = name; *p; ++p) {
        if ((guchar)*p < 0x20 || *p == 0x7f)
            return FALSE;
    }
    return TRUE;
}

static GCancellable *archive_cancel;
static GCancellable *install_cancel;

typedef struct {
    GTask *task;
    GCancellable *cancel;
    GSubprocess *process;
    gulong cancel_handler;
} InstallRun;

static void install_run_free(InstallRun *run)
{
    if (run->cancel_handler) g_cancellable_disconnect(run->cancel, run->cancel_handler);
    if (install_cancel == run->cancel) g_clear_object(&install_cancel);
    g_clear_object(&run->task);
    g_clear_object(&run->cancel);
    g_clear_object(&run->process);
    g_free(run);
}

static void cancel_install_process(GCancellable *cancel, gpointer data)
{
    (void)cancel;
    g_subprocess_send_signal(G_SUBPROCESS(data), SIGTERM);
}

static void archive_install_worker(GObject *source, GAsyncResult *result,
                                  gpointer user_data)
{
    InstallRun *run = user_data;
    GTask *task = run->task;
    g_autofree gchar *stdout_text = NULL;
    g_autofree gchar *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    gchar *name;

    if (!g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result,
                                               &stdout_text, &stderr_text,
                                               &error)) {
        g_task_return_error(task, g_steal_pointer(&error));
        install_run_free(run);
        return;
    }
    if (!g_spawn_check_wait_status(g_subprocess_get_status(G_SUBPROCESS(source)),
                                    &error)) {
        if (stderr_text && *stderr_text)
            g_prefix_error(&error, "%s: ", stderr_text);
        g_task_return_error(task, g_steal_pointer(&error));
        install_run_free(run);
        return;
    }

    name = stdout_text ? g_strstrip(stdout_text) : NULL;
    if (!valid_theme_name(name)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                _("The theme installer returned an invalid theme name."));
    } else {
        g_task_return_pointer(task, g_strdup(name), g_free);
    }
    install_run_free(run);
}

void archive_install_async(const gchar *path, GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_autofree gchar *script = g_build_filename(INSTALL_SCRIPT_DIR,
                                                 "install-theme.sh", NULL);
    GTask *task = g_task_new(NULL, NULL, callback, user_data);
    GSubprocess *subprocess;
    GError *error = NULL;
    const gchar *argv[] = { script, path, NULL };

    if (!path || !*path) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                _("No Openbox theme archive was selected."));
        g_object_unref(task);
        return;
    }
    if (!g_file_test(script, G_FILE_TEST_IS_EXECUTABLE)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                _("The Openbox theme installer is missing from this installation."));
        g_object_unref(task);
        return;
    }
    if (install_cancel) g_cancellable_cancel(install_cancel);
    g_clear_object(&install_cancel);
    install_cancel = g_cancellable_new();
    subprocess = g_subprocess_newv(argv,
                                   G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                   G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                   &error);
    if (!subprocess) {
        g_task_return_error(task, g_steal_pointer(&error));
        g_object_unref(task);
        g_clear_object(&install_cancel);
        return;
    }
    InstallRun *run = g_new0(InstallRun, 1);
    run->task = task;
    run->cancel = g_object_ref(install_cancel);
    run->process = subprocess;
    run->cancel_handler = g_cancellable_connect(run->cancel,
        G_CALLBACK(cancel_install_process), g_object_ref(subprocess), g_object_unref);
    g_subprocess_communicate_utf8_async(subprocess, NULL, run->cancel,
                                       archive_install_worker, run);
}

gchar *archive_install_finish(GAsyncResult *result, GError **error)
{
    return g_task_propagate_pointer(G_TASK(result), error);
}

typedef struct { gchar *source; gchar *destination; GCancellable *cancel; GWeakRef window; } ArchiveJob;
typedef struct { guint entries; guint depth; guint64 bytes; } TreeBudget;

static void archive_job_free(ArchiveJob *job)
{
    g_free(job->source); g_free(job->destination);
    g_clear_object(&job->cancel);
    g_weak_ref_clear(&job->window); g_free(job);
}

static gboolean validate_tree_bounded(const gchar *path, guint depth,
                                      TreeBudget *budget, GCancellable *cancel,
                                      GError **error)
{
    GDir *dir;
    const gchar *entry;
    if (depth > 32 || g_cancellable_is_cancelled(cancel)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                            depth > 32 ? "Theme directory nesting exceeds 32 levels" : "Archive creation cancelled");
        return FALSE;
    }
    dir = g_dir_open(path, 0, error);
    if (!dir) return FALSE;
    while ((entry = g_dir_read_name(dir))) {
        g_autofree gchar *child = g_build_filename(path, entry, NULL);
        GStatBuf st;
        if (++budget->entries > 8192) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Theme contains more than 8192 entries");
            g_dir_close(dir); return FALSE;
        }
        if ((budget->entries & 63u) == 0 && g_cancellable_is_cancelled(cancel)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Archive creation cancelled");
            g_dir_close(dir); return FALSE;
        }
        if (lstat(child, &st) != 0) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot inspect %s: %s", child, g_strerror(errno));
            g_dir_close(dir); return FALSE;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!validate_tree_bounded(child, depth + 1, budget, cancel, error)) { g_dir_close(dir); return FALSE; }
        } else if (S_ISREG(st.st_mode)) {
            if (st.st_size < 0 || (guint64)st.st_size > 268435456u - budget->bytes) {
                g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Theme exceeds the 256 MiB archive input limit");
                g_dir_close(dir); return FALSE;
            }
            budget->bytes += (guint64)st.st_size;
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Theme contains a link or special file: %s", child);
            g_dir_close(dir); return FALSE;
        }
    }
    g_dir_close(dir);
    return TRUE;
}

static void archive_cancel_subprocess(GCancellable *cancellable, gpointer data)
{
    GSubprocess *process = data;
    (void)cancellable;
    g_subprocess_send_signal(process, SIGTERM);
}

static void archive_create_worker(GTask *task, gpointer source, gpointer task_data,
                                  GCancellable *cancel)
{
    ArchiveJob *job = task_data;
    TreeBudget budget = {0};
    struct stat st;
    g_autofree gchar *name = g_path_get_basename(job->source);
    g_autofree gchar *parent = g_path_get_dirname(job->source);
    g_autofree gchar *glob = NULL;
    g_autofree gchar *template = NULL;
    g_autofree gchar *tmp = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) process = NULL;
    gulong cancel_handler = 0;
    gint fd;
    (void)source;
    if (!valid_theme_name(name) || lstat(job->source, &st) != 0 || !S_ISDIR(st.st_mode)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid Openbox theme directory"); return;
    }
    g_autofree gchar *themerc = g_build_filename(job->source, "openbox-3", "themerc", NULL);
    if (lstat(themerc, &st) != 0 || !S_ISREG(st.st_mode) ||
        !validate_tree_bounded(job->source, 0, &budget, cancel, &error)) {
        if (!error) g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Directory is not an Openbox theme");
        g_task_return_error(task, g_steal_pointer(&error)); return;
    }
    g_autofree gchar *out_dir = g_path_get_dirname(job->destination);
    template = g_strdup_printf("%s/.obconf-theme-XXXXXX", out_dir);
    fd = g_mkstemp(template);
    if (fd < 0 || close(fd) != 0) {
        if (fd >= 0) g_unlink(template);
        g_task_return_new_error(task, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot create temporary archive: %s", g_strerror(errno)); return;
    }
    tmp = g_strdup(template);
    glob = g_build_filename(name, "openbox-3", NULL);
    const gchar *argv_const[] = { "timeout", "--signal=TERM", "--kill-after=2s", "30s", "tar", "-czf", tmp, "-C", parent, "--", glob, NULL };
    process = g_subprocess_newv(argv_const, G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error);
    if (!process) goto failed;
    cancel_handler = g_cancellable_connect(cancel, G_CALLBACK(archive_cancel_subprocess), g_object_ref(process), g_object_unref);
    if (!g_subprocess_wait_check(process, cancel, &error)) goto failed;
    if (cancel_handler) { g_cancellable_disconnect(cancel, cancel_handler); cancel_handler = 0; }
    /* link(2) publishes a regular file atomically and never replaces a target. */
    if (link(tmp, job->destination) != 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot publish archive without replacing an existing file: %s", g_strerror(errno));
        goto failed;
    }
    g_unlink(tmp);
    g_task_return_pointer(task, g_strdup(job->destination), g_free);
    return;
failed:
    if (cancel_handler) g_cancellable_disconnect(cancel, cancel_handler);
    if (tmp) g_unlink(tmp);
    if (error) g_task_return_error(task, g_steal_pointer(&error));
    else g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to create theme archive");
}

static void archive_create_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    ArchiveJob *job = data;
    g_autoptr(GError) error = NULL;
    g_autofree gchar *dest = g_task_propagate_pointer(G_TASK(result), &error);
    g_autoptr(GObject) window = g_weak_ref_get(&job->window);
    (void)source;
    if (window && window == G_OBJECT(mainwin) && xml_i) {
        if (dest) gtk_msg(GTK_MESSAGE_INFO, _("\"%s\" was successfully created"), dest);
        else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            gtk_msg(GTK_MESSAGE_ERROR, _("Cannot create theme archive: %s"), error ? error->message : _("unknown error"));
    }
    archive_job_free(job);
    if (archive_cancel == job->cancel) g_clear_object(&archive_cancel);
}

void archive_cancel_pending(void)
{
    if (archive_cancel) g_cancellable_cancel(archive_cancel);
    if (install_cancel) g_cancellable_cancel(install_cancel);
}

void archive_create(const gchar *path)
{
    if (!path || !mainwin || !xml_i) return;
    if (archive_cancel) g_cancellable_cancel(archive_cancel);
    g_clear_object(&archive_cancel);
    archive_cancel = g_cancellable_new();
    ArchiveJob *job = g_new0(ArchiveJob, 1);
    job->source = g_canonicalize_filename(path, NULL);
    g_autofree gchar *cwd = g_get_current_dir();
    g_autofree gchar *name = g_path_get_basename(path);
    g_autofree gchar *filename = g_strdup_printf("%s.obt", name);
    job->destination = g_build_filename(cwd, filename, NULL);
    job->cancel = g_object_ref(archive_cancel);
    g_weak_ref_init(&job->window, G_OBJECT(mainwin));
    GTask *task = g_task_new(NULL, archive_cancel, archive_create_finished, job);
    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, archive_create_worker);
    g_object_unref(task);
}
