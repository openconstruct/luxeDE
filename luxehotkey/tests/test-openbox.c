/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../plugins/openbox/openbox.c"

static void test_conversions(void)
{
    const char *keys[] = { "<Primary>a", "<Control><Shift>Tab", "<Alt>F1",
        "Super_L", "Meta_L", "Hyper_L", "a", "Tab", "Escape", "BackSpace", "XF86Search" };
    const char *ob[] = { "C-a", "C-S-Tab", "A-F1",
        "Super_L", "Meta_L", "Hyper_L", "a", "Tab", "Escape", "BackSpace", "XF86Search" };
    const char *normalized[] = { "<Control>a", "<Control><Shift>Tab", "<Alt>F1",
        "Super_L", "Meta_L", "Hyper_L", "a", "Tab", "Escape", "BackSpace", "XF86Search" };
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
        char *converted = key_to_obkey(keys[i]);
        g_assert_cmpstr(converted, ==, ob[i]);
        char *back = obkey_to_key(converted);
        g_assert_cmpstr(back, ==, normalized[i]);
        g_free(converted);
        g_free(back);
    }
    g_autofree char *canonical = key_to_obkey("<Alt><Primary><Control>XF86AudioMute");
    g_assert_cmpstr(canonical, ==, "C-A-XF86AudioMute");
}

static void test_edit_sequence(void)
{
    GError *error = NULL;
    ObXmlFile *cfg = obcfg_load(NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(cfg);
    LuxeHotkeyAttr nested = { .name = "finalactions", .has_actions = TRUE, .has_value = TRUE };
    GList original = { .data = &nested };
    GList *copied = NULL;
    FmXmlFileItem *node = make_new_xml_item(cfg, &nested, &copied, FALSE);
    g_assert_true(options_equal(&original, copied));
    free_options(copied);
    fm_xml_file_item_destroy(node);
    LuxeHotkeyAttr focus = { .name = "Focus" };
    GList action = { .data = &focus };
    LuxeHotkeyGlobal global = { .actions = &action, .accel1 = "<Alt>a", .accel2 = "<Alt>b" };
    g_assert_true(obcfg_set_wm_key(cfg, &global, &error));
    /* Keep secondary, change it, add another, keep primary, then delete. */
    global.accel1 = "<Alt>b"; global.accel2 = NULL;
    g_assert_true(obcfg_set_wm_key(cfg, &global, &error));
    global.accel1 = "<Alt>c";
    g_assert_true(obcfg_set_wm_key(cfg, &global, &error));
    global.accel2 = "<Alt>d";
    g_assert_true(obcfg_set_wm_key(cfg, &global, &error));
    global.accel2 = NULL;
    g_assert_true(obcfg_set_wm_key(cfg, &global, &error));
    global.accel1 = NULL;
    g_assert_true(obcfg_set_wm_key(cfg, &global, &error));
    g_assert_null(cfg->actions);

    LuxeHotkeyApp app = { .exec = "terminal --test", .accel1 = "<Alt>a", .accel2 = "<Alt>b" };
    g_assert_true(obcfg_set_app_key(cfg, &app, &error));
    gsize before_length;
    g_autofree char *before = fm_xml_file_to_data(cfg->xml, &before_length, &error);
    g_assert_no_error(error);
    LuxeHotkeyApp invalid = { .exec = "invalid-fixture", .accel1 = "<ControlBogus>a" };
    g_assert_false(obcfg_set_app_key(cfg, &invalid, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    gsize after_length;
    g_autofree char *after = fm_xml_file_to_data(cfg->xml, &after_length, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(after_length, ==, before_length);
    g_assert_cmpmem(after, after_length, before, before_length);
    LuxeHotkeyApp duplicate_pair = { .exec = "duplicate-pair", .accel1 = "<Alt><Control>c",
                                   .accel2 = "<Primary><Alt>c" };
    g_assert_false(obcfg_set_app_key(cfg, &duplicate_pair, &error));
    g_assert_nonnull(error); g_clear_error(&error);
    LuxeHotkeyApp separate = { .exec = "separate", .accel1 = "<Alt><Control>c" };
    g_assert_true(obcfg_set_app_key(cfg, &separate, &error));
    LuxeHotkeyGlobal conflicting = { .actions = &action, .accel1 = "<Primary><Alt>c" };
    g_assert_false(obcfg_set_wm_key(cfg, &conflicting, &error));
    g_assert_nonnull(error); g_clear_error(&error);
    LuxeHotkeyApp conflicting_app = { .exec = "another", .accel1 = "<Control><Alt>c" };
    g_assert_false(obcfg_set_app_key(cfg, &conflicting_app, &error));
    g_assert_nonnull(error); g_clear_error(&error);
    separate.accel1 = NULL;
    g_assert_true(obcfg_set_app_key(cfg, &separate, &error));
    LuxeHotkeyGlobal occupied = { .actions = &action, .accel1 = "<Shift><Control>XF86AudioMute" };
    g_assert_true(obcfg_set_wm_key(cfg, &occupied, &error));
    LuxeHotkeyApp conflicts_wm = { .exec = "wm-conflict", .accel1 = "<Primary><Shift>XF86AudioMute" };
    g_assert_false(obcfg_set_app_key(cfg, &conflicts_wm, &error));
    g_assert_nonnull(error); g_clear_error(&error);
    occupied.accel1 = NULL;
    g_assert_true(obcfg_set_wm_key(cfg, &occupied, &error));
    GList *found = obcfg_get_app_keys(cfg, "terminal*", &error);
    g_assert_cmpuint(g_list_length(found), ==, 1);
    g_list_free(found);
    app.accel1 = "<Alt>b"; app.accel2 = NULL;
    g_assert_true(obcfg_set_app_key(cfg, &app, &error));
    app.accel1 = "<Alt>c";
    g_assert_true(obcfg_set_app_key(cfg, &app, &error));
    app.accel2 = "<Alt>d";
    g_assert_true(obcfg_set_app_key(cfg, &app, &error));
    app.accel2 = NULL;
    g_assert_true(obcfg_set_app_key(cfg, &app, &error));
    app.accel1 = NULL;
    g_assert_true(obcfg_set_app_key(cfg, &app, &error));
    g_assert_null(cfg->execs);
    g_assert_no_error(error);
    app.accel1 = "Super_L"; app.accel2 = "<Alt>b";
    g_assert_true(obcfg_set_app_key(cfg, &app, &error));

    gsize length;
    char *xml = fm_xml_file_to_data(cfg->xml, &length, &error);
    g_assert_no_error(error);
    g_assert_true(obcfg_validate_contents(xml, length, &error));
    g_assert_true(obcfg_write_contents(cfg->path, xml, length, &error));
    g_assert_no_error(error);
    g_free(xml);
    cfg = obcfg_load(cfg, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(g_list_length(cfg->execs), ==, 1);
    g_assert_cmpstr(((LuxeHotkeyApp *)cfg->execs->data)->accel1, ==, "Super_L");
    g_assert_nonnull(((LuxeHotkeyApp *)cfg->execs->data)->accel2);
    obcfg_get_wm_actions(cfg, NULL);
    for (GList *l = obcfg_get_app_options(cfg, NULL); l; l = l->next)
        g_assert_cmpstr(((LuxeHotkeyAttr *)l->data)->name, !=, "command");
    gsize committed_length = 0;
    g_autofree char *committed_xml = fm_xml_file_to_data(cfg->xml, &committed_length, &error);
    g_assert_no_error(error);
    int shared_lock = luxe_rcxml_lock_acquire(cfg->path, &error);
    g_assert_cmpint(shared_lock, >=, 0);
    g_assert_false(obcfg_commit_config(cfg, committed_xml, committed_length, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error(&error);
    close(shared_lock);
    g_assert_true(obcfg_commit_config(cfg, committed_xml, committed_length, &error));
    g_assert_no_error(error);
    g_autofree char *external_edit = g_strconcat(committed_xml, "\n<!-- external edit -->\n", NULL);
    g_assert_true(g_file_set_contents(cfg->path, external_edit, -1, &error));
    g_assert_no_error(error);
    g_assert_false(obcfg_commit_config(cfg, committed_xml, committed_length, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error(&error);
    g_autofree char *preserved_external = NULL;
    gsize preserved_external_length = 0;
    g_assert_true(g_file_get_contents(cfg->path, &preserved_external,
                                      &preserved_external_length, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(preserved_external, preserved_external_length,
                    external_edit, strlen(external_edit));
    g_assert_true(g_file_set_contents(cfg->path, committed_xml, committed_length, &error));
    g_assert_no_error(error);
    LuxeHotkeyApp pending = { .exec = "terminal --test", .accel1 = "<Control>q" };
    g_assert_true(obcfg_set_app_key(cfg, &pending, &error));
    gsize pending_length;
    g_autofree char *pending_xml = fm_xml_file_to_data(cfg->xml, &pending_length, &error);
    g_assert_no_error(error);
    g_assert_true(g_file_set_contents(cfg->path, "<broken>", -1, &error));
    g_assert_no_error(error);
    g_assert_null(obcfg_load(cfg, &error));
    g_assert_nonnull(error); g_clear_error(&error);
    gsize preserved_length;
    g_autofree char *preserved_xml = fm_xml_file_to_data(cfg->xml, &preserved_length, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(preserved_length, ==, pending_length);
    g_assert_cmpmem(preserved_xml, preserved_length, pending_xml, pending_length);
    g_assert_cmpstr(((LuxeHotkeyApp *)cfg->execs->data)->accel1, ==, "<Control>q");
    obcfg_free(cfg);
    g_assert_false(obcfg_validate_contents("<broken>", 8, &error));
    g_clear_error(&error);
}

static void test_unavailable_display(void)
{
    char *display = g_strdup(g_getenv("DISPLAY"));
    g_setenv("DISPLAY", "", TRUE);
    GError *error = NULL;
    g_assert_false(restart_openbox(&error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    if (display) g_setenv("DISPLAY", display, TRUE);
    else g_unsetenv("DISPLAY");
    g_free(display);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    char *root = g_dir_make_tmp("luxehotkey-test-XXXXXX", NULL);
    g_assert_nonnull(root);
    g_setenv("XDG_CONFIG_HOME", root, TRUE);
    g_setenv("DESKTOP_SESSION", "luxe", TRUE);
    char *directory = g_build_filename(root, "openbox", NULL);
    char *path = g_build_filename(directory, "rc.xml", NULL);
    g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
    g_assert_true(g_file_set_contents(path, "<openbox_config><keyboard/></openbox_config>", -1, NULL));
    g_test_add_func("/openbox/conversions", test_conversions);
    g_test_add_func("/openbox/edit-sequence", test_edit_sequence);
    g_test_add_func("/openbox/no-display", test_unavailable_display);
    int result = g_test_run();
    g_unlink(path); g_rmdir(directory); g_rmdir(root);
    g_free(path); g_free(directory); g_free(root);
    return result;
}
