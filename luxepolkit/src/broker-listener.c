/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxepolkit-listener.h"
#include <glib-unix.h>
#include <gio/gunixoutputstream.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

G_DEFINE_TYPE(LuxePolkitListener, luxepolkit_listener, POLKIT_AGENT_TYPE_LISTENER)
static gboolean busy;
#define REQUEST_FIELD_LIMIT (64u * 1024u)
#define REQUEST_TOTAL_LIMIT (128u * 1024u)
#define REQUEST_ITEMS_LIMIT 1024u
#ifndef POLKIT_STARTUP_TIMEOUT_MS
#define POLKIT_STARTUP_TIMEOUT_MS 15000u
#endif

/* Bound input before GVariant and its printable representation duplicate it. */
static gboolean request_field_fits(const char *value, gsize *total)
{
    if (!value) return FALSE;
    gsize length = strnlen(value, REQUEST_FIELD_LIMIT + 1);
    if (length > REQUEST_FIELD_LIMIT || length > REQUEST_TOTAL_LIMIT - *total) return FALSE;
    *total += length;
    return TRUE;
}
typedef struct {
    GSubprocess *child;
    GCancellable *cancel;
    gulong cancel_handler;
    guint kill_timer;
    guint startup_timer;
    gboolean startup_expired;
} Conversation;
typedef struct { GTask *task; gchar *text; } Writer;
typedef struct { GTask *task; char byte; } ReadyReader;

static void remove_task_source(GTask *task, guint id)
{
    GSource *source = id ? g_main_context_find_source_by_id(g_task_get_context(task), id) : NULL;
    if (source) g_source_destroy(source);
}

static gboolean startup_timeout(gpointer pointer)
{
    GTask *task = pointer;
    Conversation *c = g_task_get_task_data(task);
    if (c) {
        c->startup_timer = 0;
        c->startup_expired = TRUE;
        g_subprocess_force_exit(c->child);
    }
    return G_SOURCE_REMOVE;
}

static void startup_read(GObject *source, GAsyncResult *result, gpointer pointer)
{
    ReadyReader *reader = pointer;
    GError *error = NULL;
    gssize count = g_input_stream_read_finish(G_INPUT_STREAM(source), result, &error);
    Conversation *c = g_task_get_task_data(reader->task);
    if (c && count == 1 && reader->byte == 'R') {
        remove_task_source(reader->task, c->startup_timer);
        c->startup_timer = 0;
    } else if (c && !c->startup_expired) {
        g_subprocess_force_exit(c->child);
    }
    g_clear_error(&error);
    g_object_unref(reader->task);
    g_free(reader);
}

static gboolean kill_child(gpointer pointer)
{
    Conversation *c = pointer;
    c->kill_timer = 0;
    g_subprocess_force_exit(c->child);
    return G_SOURCE_REMOVE;
}
static gboolean cancel_on_context(gpointer pointer)
{
    GTask *task = pointer;
    Conversation *c = g_task_get_task_data(task);
    if (c && !c->kill_timer) {
        g_subprocess_send_signal(c->child, SIGTERM);
        GSource *timer = g_timeout_source_new(2000);
        g_source_set_callback(timer, kill_child, c, NULL);
        c->kill_timer = g_source_attach(timer, g_task_get_context(task));
        g_source_unref(timer);
    }
    return G_SOURCE_REMOVE;
}
static void cancelled(GCancellable *cancel, gpointer pointer)
{
    (void) cancel;
    GTask *task = pointer;
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, cancel_on_context, g_object_ref(task), g_object_unref);
    g_source_attach(source, g_task_get_context(task));
    g_source_unref(source);
}
static void request_written(GObject *source, GAsyncResult *result, gpointer pointer)
{
    Writer *w = pointer;
    GError *error = NULL;
    gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), result, NULL, &error);
    g_output_stream_close(G_OUTPUT_STREAM(source), NULL, NULL);
    Conversation *c = g_task_get_task_data(w->task);
    if (!ok && c) g_subprocess_force_exit(c->child);
    g_clear_error(&error);
    g_free(w->text);
    g_object_unref(w->task);
    g_free(w);
}
static void finished(GObject *source, GAsyncResult *result, gpointer pointer)
{
    GTask *task = pointer;
    Conversation *c = g_task_get_task_data(task);
    GError *error = NULL;
    gboolean waited = g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
    if (c->cancel_handler) g_cancellable_disconnect(c->cancel, c->cancel_handler);
    remove_task_source(task, c->kill_timer);
    remove_task_source(task, c->startup_timer);
    busy = FALSE;
    if (g_cancellable_is_cancelled(c->cancel)) {
        g_clear_error(&error);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Authentication cancelled");
    } else if (c->startup_expired) {
        g_clear_error(&error);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Authentication dialog did not start in time");
    } else if (!waited) g_task_return_error(task, error);
    else if (g_subprocess_get_successful(c->child)) {
        /* Polkit's trusted helper, not this exit status, grants authorization. */
        g_task_return_boolean(task, TRUE);
    } else if (g_subprocess_get_if_exited(c->child) && g_subprocess_get_exit_status(c->child) == 3) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
            "Cannot secure keyboard and pointer input; close conflicting menus or applications and try again");
    } else g_task_return_new_error(task, G_IO_ERROR,
        g_subprocess_get_if_exited(c->child) && g_subprocess_get_exit_status(c->child) == 2
            ? G_IO_ERROR_CANCELLED : G_IO_ERROR_FAILED,
        "Authentication dialog failed or was cancelled");
    g_task_set_task_data(task, NULL, NULL);
    g_object_unref(c->cancel);
    g_object_unref(c->child);
    g_free(c);
    g_object_unref(task);
}
static void initiate_authentication(PolkitAgentListener *listener,
    const gchar *action, const gchar *message, const gchar *icon,
    PolkitDetails *details, const gchar *cookie, GList *identities,
    GCancellable *cancel, GAsyncReadyCallback callback, gpointer user_data)
{
    GTask *task = g_task_new(listener, NULL, callback, user_data);
    if (busy || !identities || (cancel && g_cancellable_is_cancelled(cancel))) {
        g_task_return_new_error(task, G_IO_ERROR,
            cancel && g_cancellable_is_cancelled(cancel) ? G_IO_ERROR_CANCELLED : G_IO_ERROR_FAILED,
            "Authentication unavailable: busy, cancelled or no permitted identities");
        g_object_unref(task);
        return;
    }
    gsize total = 0;
    guint count = 0;
    gboolean bounded = request_field_fits(action, &total) && request_field_fits(message, &total) &&
        request_field_fits(icon ? icon : "", &total) && request_field_fits(cookie, &total);
    g_auto(GStrv) keys = polkit_details_get_keys(details);
    for (GList *l = identities; bounded && l; l = l->next) {
        g_autofree gchar *id = polkit_identity_to_string(l->data);
        bounded = ++count <= REQUEST_ITEMS_LIMIT && request_field_fits(id, &total);
    }
    for (guint i = 0; bounded && keys && keys[i]; i++)
        bounded = ++count <= REQUEST_ITEMS_LIMIT && request_field_fits(keys[i], &total) &&
            request_field_fits(polkit_details_lookup(details, keys[i]), &total);
    if (!bounded) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Authentication request too large");
        g_object_unref(task);
        return;
    }
    GVariantBuilder ids, entries;
    g_variant_builder_init(&ids, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&entries, G_VARIANT_TYPE("a{ss}"));
    for (GList *l = identities; l; l = l->next) {
        gchar *id = polkit_identity_to_string(l->data);
        g_variant_builder_add(&ids, "s", id);
        g_free(id);
    }
    for (guint i = 0; keys && keys[i]; ++i)
        g_variant_builder_add(&entries, "{ss}", keys[i], polkit_details_lookup(details, keys[i]));
    GVariant *request = g_variant_ref_sink(g_variant_new("(ssss@as@a{ss})",
        action, message, icon ? icon : "", cookie,
        g_variant_builder_end(&ids), g_variant_builder_end(&entries)));
    gchar *text = g_variant_print(request, TRUE);
    g_variant_unref(request);
    if (strlen(text) > 1024 * 1024) {
        g_free(text);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Authentication request too large");
        g_object_unref(task);
        return;
    }
    int descriptors[2];
    GError *error = NULL;
    if (!g_unix_open_pipe(descriptors, FD_CLOEXEC, &error)) {
        g_free(text);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE);
    g_subprocess_launcher_take_stdin_fd(launcher, descriptors[0]);
    gchar *parent = g_strdup_printf("%ld", (long)getpid());
    GSubprocess *child = g_subprocess_launcher_spawn(launcher,
        &error, POLKIT_DIALOG_PATH, "--parent-pid", parent, "--ready", NULL);
    g_free(parent);
    g_object_unref(launcher);
    if (!child) {
        close(descriptors[1]);
        g_free(text);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }
    Conversation *c = g_new0(Conversation, 1);
    c->child = child;
    c->cancel = cancel ? g_object_ref(cancel) : g_cancellable_new();
    g_task_set_task_data(task, c, NULL);
    busy = TRUE;
    GSource *startup = g_timeout_source_new(POLKIT_STARTUP_TIMEOUT_MS);
    g_source_set_callback(startup, startup_timeout, g_object_ref(task), g_object_unref);
    c->startup_timer = g_source_attach(startup, g_task_get_context(task));
    g_source_unref(startup);
    ReadyReader *reader = g_new0(ReadyReader, 1);
    reader->task = g_object_ref(task);
    g_input_stream_read_async(g_subprocess_get_stdout_pipe(child), &reader->byte, 1,
                              G_PRIORITY_DEFAULT, NULL, startup_read, reader);
    signal(SIGPIPE, SIG_IGN);
    GOutputStream *stream = g_unix_output_stream_new(descriptors[1], TRUE);
    Writer *writer = g_new0(Writer, 1);
    writer->task = g_object_ref(task);
    writer->text = text;
    g_output_stream_write_all_async(stream, text, strlen(text), G_PRIORITY_DEFAULT,
                                   c->cancel, request_written, writer);
    g_object_unref(stream);
    c->cancel_handler = g_cancellable_connect(c->cancel, G_CALLBACK(cancelled),
                                              g_object_ref(task), g_object_unref);
    g_subprocess_wait_async(child, NULL, finished, task);
}
static gboolean initiate_authentication_finish(PolkitAgentListener *listener,
                                               GAsyncResult *result, GError **error)
{
    (void) listener;
    return g_task_propagate_boolean(G_TASK(result), error);
}
static void luxepolkit_listener_class_init(LuxePolkitListenerClass *klass)
{
    PolkitAgentListenerClass *agent = POLKIT_AGENT_LISTENER_CLASS(klass);
    agent->initiate_authentication = initiate_authentication;
    agent->initiate_authentication_finish = initiate_authentication_finish;
}
static void luxepolkit_listener_init(LuxePolkitListener *self) { (void) self; }
PolkitAgentListener *luxepolkit_listener_new(void)
{
    return g_object_new(LUXEPOLKIT_LISTENER_TYPE, NULL);
}
