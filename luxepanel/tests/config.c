/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <glib.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#include "conf.h"

static char *
write_fixture(const char *name, const char *contents)
{
    char *path = g_build_filename(g_get_tmp_dir(), name, NULL);
    GError *error = NULL;
    g_assert_true(g_file_set_contents(path, contents, -1, &error));
    g_assert_no_error(error);
    return path;
}

static void
test_reads_checked_values_and_strings(void)
{
    char *path = write_fixture("luxepanel-config-valid",
        "Global {\n"
        " width=100\n"
        " name=\"Luxe Panel\"\n"
        " huge=999999999999999999999999\n"
        "}\n");
    PanelConf *config = config_new();
    config_setting_t *root;
    config_setting_t *global;
    gint width = 0;
    const char *name = NULL;
    const char *huge = NULL;

    g_assert_true(config_read_file(config, path));
    root = config_setting_get_member(config_root_setting(config), "");
    global = config_setting_get_elem(root, 0);
    g_assert_true(config_setting_lookup_int(global, "width", &width));
    g_assert_cmpint(width, ==, 100);
    g_assert_true(config_setting_lookup_string(global, "name", &name));
    g_assert_cmpstr(name, ==, "Luxe Panel");
    g_assert_true(config_setting_lookup_string(global, "huge", &huge));
    g_assert_cmpstr(huge, ==, "999999999999999999999999");
    g_assert_false(config_setting_lookup_int(global, "huge", &width));

    config_destroy(config);
    g_remove(path);
    g_free(path);
}

static void
test_unmatched_braces_do_not_crash(void)
{
    char *path = write_fixture("luxepanel-config-braces",
        "}\nGlobal {\n width=80\n}\n}\nPlugin {\n type=separator\n");
    PanelConf *config = config_new();

    /* Parsing remains best-effort for compatibility, but malformed closing
     * braces must not dereference a null parent on the next line. */
    g_assert_true(config_read_file(config, path));
    config_destroy(config);
    g_remove(path);
    g_free(path);
}

static void
test_rejects_oversized_and_special_files(void)
{
    char *path = g_build_filename(g_get_tmp_dir(), "luxepanel-config-large", NULL);
    GString *large = g_string_sized_new(1024U * 1024U + 32U);
    GError *error = NULL;
    PanelConf *config = config_new();

    for (guint i = 0; i < 1024U * 1024U + 1U; i++)
        g_string_append_c(large, '#');
    g_assert_true(g_file_set_contents(path, large->str, large->len, &error));
    g_assert_no_error(error);
    g_assert_false(config_read_file(config, path));
    g_remove(path);
    g_string_free(large, TRUE);
    g_assert_cmpint(mkfifo(path, 0600), ==, 0);
    g_assert_false(config_read_file(config, path));
    g_remove(path);
    g_free(path);
    config_destroy(config);
}

static void
test_string_serialization_roundtrip(void)
{
    char *dir = g_dir_make_tmp("luxepanel-config-roundtrip-XXXXXX", NULL);
    char *path = g_build_filename(dir, "panel", NULL);
    PanelConf *written = config_new();
    config_setting_t *group = config_group_add_subgroup(config_root_setting(written), "Global");
    g_assert_nonnull(group);
    config_group_set_string(group, "Action", "line one\nline \"two\"\\three");
    config_group_set_string(group, "Numeric", "00123");
    g_assert_true(config_write_file(written, path));

    PanelConf *readback = config_new();
    g_assert_true(config_read_file(readback, path));
    config_setting_t *read_group = config_setting_get_elem(
        config_setting_get_member(config_root_setting(readback), ""), 0);
    const char *action = NULL;
    const char *numeric = NULL;
    g_assert_true(config_setting_lookup_string(read_group, "Action", &action));
    g_assert_cmpstr(action, ==, "line one\nline \"two\"\\three");
    g_assert_true(config_setting_lookup_string(read_group, "Numeric", &numeric));
    g_assert_cmpstr(numeric, ==, "00123");

    config_destroy(written);
    config_destroy(readback);
    g_remove(path);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

static void
test_empty_config_can_be_saved(void)
{
    char *dir = g_dir_make_tmp("luxepanel-config-save-XXXXXX", NULL);
    char *path = g_build_filename(dir, "panel", NULL);
    PanelConf *config = config_new();
    gchar *contents = NULL;

    g_assert_true(config_write_file(config, path));
    g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
    g_assert_nonnull(strstr(contents, "luxepanel"));
    g_free(contents);
    config_destroy(config);
    g_remove(path);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/config/checked-values", test_reads_checked_values_and_strings);
    g_test_add_func("/config/unmatched-braces", test_unmatched_braces_do_not_crash);
    g_test_add_func("/config/input-boundaries", test_rejects_oversized_and_special_files);
    g_test_add_func("/config/string-roundtrip", test_string_serialization_roundtrip);
    g_test_add_func("/config/empty-save", test_empty_config_can_be_saved);
    return g_test_run();
}
