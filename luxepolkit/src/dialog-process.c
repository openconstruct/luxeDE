/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxepolkit-listener.h"
#include "theme-scheme.h"
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <errno.h>

static GMainLoop *loop;
static GCancellable *cancel;
static int status = 1;

static void sync_scheme(GSettings *settings, const char *key, gpointer unused)
{
    (void) key; (void) unused;
    luxepolkit_apply_color_scheme(settings);
}

static gboolean terminate(gpointer unused)
{
    (void) unused;
    g_cancellable_cancel(cancel);
    return G_SOURCE_CONTINUE;
}
static void completed(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void) unused;
    GError *error = NULL;
    gboolean ok = polkit_agent_listener_initiate_authentication_finish(
        POLKIT_AGENT_LISTENER(source), result, &error);
    /* No cookie/password/details are logged, even on failure. */
    status = ok ? 0 : (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ? 2 :
                       g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) ? 3 : 1);
    g_clear_error(&error);
    g_main_loop_quit(loop);
}

int main(int argc, char **argv)
{
    if ((argc != 3 && argc != 4) || g_strcmp0(argv[1], "--parent-pid") ||
        (argc == 4 && g_strcmp0(argv[3], "--ready"))) return 1;
    char *end = NULL;
    gint64 parent = g_ascii_strtoll(argv[2], &end, 10);
    if (!end || *end || parent <= 1) return 1;
    /* Close the spawn/death race, and never leave a password grab orphaned. */
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent) return 1;
#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif
    GString *input = g_string_new(NULL);
    char buffer[4096];
    ssize_t count;
    while ((count = read(STDIN_FILENO, buffer, sizeof buffer)) != 0) {
        if (count < 0) {
            if (errno == EINTR) continue;
            g_string_free(input, TRUE);
            return 1;
        }
        if (input->len + count > 1024 * 1024) {
            g_string_free(input, TRUE);
            return 1;
        }
        g_string_append_len(input, buffer, count);
    }
    close(STDIN_FILENO);
    GError *error = NULL;
    GVariant *request = g_variant_parse(G_VARIANT_TYPE("(ssssasa{ss})"),
                                       input->str, input->str + input->len, NULL, &error);
    g_string_free(input, TRUE);
    if (!request) { g_clear_error(&error); return 1; }
    const char *action, *message, *icon, *cookie;
    GVariant *ids, *entries;
    g_variant_get(request, "(&s&s&s&s@as@a{ss})", &action, &message, &icon, &cookie, &ids, &entries);
    GList *identities = NULL;
    GVariantIter iter;
    const char *value;
    g_variant_iter_init(&iter, ids);
    while (g_variant_iter_next(&iter, "&s", &value)) {
        PolkitIdentity *identity = polkit_identity_from_string(value, &error);
        if (!identity) break;
        identities = g_list_append(identities, identity);
    }
    PolkitDetails *details = polkit_details_new();
    const char *key;
    g_variant_iter_init(&iter, entries);
    while (g_variant_iter_next(&iter, "{&s&s}", &key, &value))
        polkit_details_insert(details, key, value);
    if (!error && identities && *cookie && *action) {
        loop = g_main_loop_new(NULL, FALSE);
        cancel = g_cancellable_new();
        guint signal_id = g_unix_signal_add(SIGTERM, terminate, NULL);
        PolkitAgentListener *listener = luxepolkit_listener_new();
        GSettings *settings = luxepolkit_theme_settings_new();
        if (settings != NULL) {
            luxepolkit_apply_color_scheme(settings);
            g_signal_connect(settings, "changed::color-scheme", G_CALLBACK(sync_scheme), NULL);
        }
        polkit_agent_listener_initiate_authentication(listener, action, message, icon,
            details, cookie, identities, cancel, completed, NULL);
        if (argc == 4) luxepolkit_listener_set_ready_fd(STDOUT_FILENO);
        g_main_loop_run(loop);
        g_source_remove(signal_id);
        g_object_unref(listener);
        g_clear_object(&settings);
        g_object_unref(cancel);
        g_main_loop_unref(loop);
    }
    g_clear_error(&error);
    g_object_unref(details);
    g_list_free_full(identities, g_object_unref);
    g_variant_unref(ids);
    g_variant_unref(entries);
    g_variant_unref(request);
    return status;
}
