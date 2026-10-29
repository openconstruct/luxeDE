/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "mime-defaults.h"
#include "safe-file.h"
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <string.h>

/* An existing desktop-specific user file precedes the generic user file.
 * Otherwise use XDG_CONFIG_HOME, never the deprecated data-directory file. */
static gchar *mime_defaults_path(void)
{
    g_auto(GStrv) desktops = g_strsplit(g_getenv("XDG_CURRENT_DESKTOP") ?: "", ":", -1);
    for (guint i = 0; desktops[i]; i++) {
        if (!*desktops[i] || strchr(desktops[i], '/') || strchr(desktops[i], '\\'))
            continue;
        g_autofree gchar *lower = g_ascii_strdown(desktops[i], -1);
        g_autofree gchar *name = g_strconcat(lower, "-mimeapps.list", NULL);
        gchar *path = g_build_filename(g_get_user_config_dir(), name, NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK))
            return path;
        g_free(path);
    }
    return g_build_filename(g_get_user_config_dir(), "mimeapps.list", NULL);
}

static void associate(GKeyFile *file, const gchar *group, const gchar *type,
                      const gchar *desktop_id, gboolean add)
{
    g_auto(GStrv) old = g_key_file_get_string_list(file, group, type, NULL, NULL);
    g_autoptr(GPtrArray) list = g_ptr_array_new();
    if (add)
        g_ptr_array_add(list, (gpointer)desktop_id);
    for (guint i = 0; old && old[i]; i++)
        if (*old[i] && !g_str_equal(old[i], desktop_id))
            g_ptr_array_add(list, old[i]);
    if (list->len)
        g_key_file_set_string_list(file, group, type,
                                   (const gchar * const *)list->pdata, list->len);
    else
        g_key_file_remove_key(file, group, type, NULL);
}

gboolean lxsession_set_mime_defaults(const gchar * const *types, gint count,
                                    const gchar *desktop_id, GError **error)
{
    if (!types || count <= 0 || count > 256 || !desktop_id || !*desktop_id ||
        strchr(desktop_id, '/') || strchr(desktop_id, ';') || strchr(desktop_id, '\n')) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Invalid MIME association");
        return FALSE;
    }
    g_autofree gchar *path = mime_defaults_path();
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    g_autoptr(GError) read_error = NULL;
    if (lxsession_read_bounded_file(path, 1024 * 1024, &contents, &length, &read_error)) {
        if (!g_key_file_load_from_data(file, contents, length,
                G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, error))
            return FALSE;
    } else if (!g_error_matches(read_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        g_propagate_error(error, g_steal_pointer(&read_error));
        return FALSE;
    }
    for (gint i = 0; i < count; i++) {
        if (!types[i] || !*types[i] || !strchr(types[i], '/') ||
            strchr(types[i], '\n') || strchr(types[i], '=')) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Invalid MIME type");
            return FALSE;
        }
        const gchar *value[] = {desktop_id};
        g_key_file_set_string_list(file, "Default Applications", types[i], value, 1);
        associate(file, "Added Associations", types[i], desktop_id, TRUE);
        associate(file, "Removed Associations", types[i], desktop_id, FALSE);
    }
    g_autofree gchar *result = g_key_file_to_data(file, &length, error);
    return result && lxsession_atomic_write_file(path, result, length, error);
}

gboolean lxsession_set_browser(const gchar *command, GError **error)
{
    /* Disable is not a request to erase existing URL handlers. */
    if (!command || !*command)
        return TRUE;
    g_auto(GStrv) argv = NULL;
    if (strlen(command) > 8192) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Browser command is too long");
        return FALSE;
    }
    if (!g_shell_parse_argv(command, NULL, &argv, error))
        return FALSE;
    g_autofree gchar *executable = g_find_program_in_path(argv[0]);
    if (!executable) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "Browser executable not found: %s", argv[0]);
        return FALSE;
    }
    const gchar *types[] = {"x-scheme-handler/http", "x-scheme-handler/https"};
    g_autoptr(GAppInfo) browser = NULL;
    /* Explicit selection must also find a previously removed association. */
    GList *apps = g_app_info_get_all();
    /* The catalog stores executable paths, not Exec field codes. Match the
     * executable only for a plain command; custom arguments need a launcher
     * preserving those arguments, not a different profile's desktop entry. */
    if (!argv[1]) {
        for (GList *item = apps; item; item = item->next) {
            GAppInfo *app = item->data;
            g_auto(GStrv) args = NULL;
            const gchar *commandline = g_app_info_get_commandline(app);
            gboolean plain = commandline && g_shell_parse_argv(commandline, NULL, &args, NULL);
            g_autofree gchar *candidate = plain ? g_find_program_in_path(args[0]) : NULL;
            for (guint j = 1; plain && args[j]; j++)
                plain = g_str_equal(args[j], "%u") || g_str_equal(args[j], "%U");
            if (plain && g_app_info_supports_uris(app) && candidate &&
                g_str_equal(candidate, executable)) {
                browser = g_object_ref(app);
                break;
            }
        }
    }
    g_list_free_full(apps, g_object_unref);
    if (browser)
        return lxsession_set_mime_defaults(types, 2, g_app_info_get_id(browser), error);

    /* A manually configured executable may not have a desktop entry. */
    g_autoptr(GKeyFile) launcher = g_key_file_new();
    g_key_file_set_string(launcher, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(launcher, "Desktop Entry", "Name", "Luxe default browser");
    g_key_file_set_boolean(launcher, "Desktop Entry", "NoDisplay", TRUE);
    gboolean has_uri = FALSE;
    for (guint i = 1; argv[i]; i++)
        has_uri |= g_str_equal(argv[i], "%u") || g_str_equal(argv[i], "%U");
    g_autofree gchar *exec = g_strconcat(command, has_uri ? "" : " %u", NULL);
    g_key_file_set_string(launcher, "Desktop Entry", "Exec", exec);
    g_key_file_set_string_list(launcher, "Desktop Entry", "MimeType", types, 2);
    /* A rejected MIME write must not change an older custom handler's Exec. */
    g_autofree gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, command, -1);
    g_autofree gchar *desktop_id = g_strconcat("luxe-default-browser-", hash, ".desktop", NULL);
    g_autofree gchar *path = g_build_filename(g_get_user_data_dir(), "applications",
                                             desktop_id, NULL);
    g_autofree gchar *data = g_key_file_to_data(launcher, NULL, error);
    return data && lxsession_atomic_write_file(path, data, strlen(data), error) &&
        lxsession_set_mime_defaults(types, 2, desktop_id, error);
}
