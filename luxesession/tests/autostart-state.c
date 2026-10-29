/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <glib.h>
#include <glib/gstdio.h>

gboolean update_enable_state(GKeyFile *kf, gboolean enabled,
                             const char *session_name, GError **error);
gboolean is_desktop_file_enabled(GKeyFile *kf, int *flags,
                                 const char *session_name);
gchar *lxsession_autostart_save_source(const gchar *config_dir,
                                      const gchar *desktop_id,
                                      const gchar *source_file);

static guint count_session(const char * const *values, gsize count,
                           const char *session)
{
    guint found = 0;
    for (gsize i = 0; i < count; i++)
        if (g_str_equal(values[i], session)) found++;
    return found;
}

static void assert_list(GKeyFile *kf, const char *key, const char *session,
                        guint expected)
{
    gsize count = 0;
    g_autoptr(GError) error = NULL;
    g_auto(GStrv) values = g_key_file_get_string_list(kf, "Desktop Entry",
                                                       key, &count, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(count_session((const char * const *)values, count, session),
                     ==, expected);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_autoptr(GKeyFile) kf = g_key_file_new();
    const char *not_show[] = { "X-Luxe", "OtherDesktop" };
    const char *only_show[] = { "GNOME", "OtherDesktop" };
    g_key_file_set_string_list(kf, "Desktop Entry", "NotShowIn", not_show,
                               G_N_ELEMENTS(not_show));
    g_key_file_set_string_list(kf, "Desktop Entry", "OnlyShowIn", only_show,
                               G_N_ELEMENTS(only_show));

    g_autoptr(GError) error = NULL;
    g_assert_true(update_enable_state(kf, TRUE, "X-Luxe", &error));
    g_assert_no_error(error);
    int flags = 0;
    g_assert_true(is_desktop_file_enabled(kf, &flags, "X-Luxe"));
    assert_list(kf, "NotShowIn", "X-Luxe", 0);
    assert_list(kf, "OnlyShowIn", "X-Luxe", 1);

    for (guint repeat = 0; repeat < 2; repeat++) {
        g_assert_true(update_enable_state(kf, FALSE, "X-Luxe", &error));
        g_assert_no_error(error);
        g_assert_false(is_desktop_file_enabled(kf, &flags, "X-Luxe"));
        assert_list(kf, "NotShowIn", "X-Luxe", 1);
        assert_list(kf, "OnlyShowIn", "X-Luxe", 0);
    }

    g_assert_true(update_enable_state(kf, TRUE, "X-Luxe", &error));
    g_assert_no_error(error);
    g_assert_true(is_desktop_file_enabled(kf, &flags, "X-Luxe"));
    assert_list(kf, "NotShowIn", "X-Luxe", 0);
    assert_list(kf, "OnlyShowIn", "X-Luxe", 1);

    g_autofree gchar *directory = g_dir_make_tmp("luxe-autostart-source-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_autofree gchar *source = g_build_filename(directory, "system.desktop", NULL);
    g_autofree gchar *override = g_build_filename(directory, "autostart", "app.desktop", NULL);
    g_autofree gchar *override_dir = g_path_get_dirname(override);
    g_autofree gchar *resolved = lxsession_autostart_save_source(directory, "app.desktop", source);
    g_assert_cmpstr(resolved, ==, source);
    g_clear_pointer(&resolved, g_free);
    g_mkdir_with_parents(override_dir, 0700);
    g_file_set_contents(override, "[Desktop Entry]\nName=external edit\n", -1, NULL);
    resolved = lxsession_autostart_save_source(directory, "app.desktop", source);
    g_assert_cmpstr(resolved, ==, override);
    g_remove(override);
    g_rmdir(override_dir);
    g_rmdir(directory);
    return 0;
}
