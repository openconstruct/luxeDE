/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* Integration test of the actual Apply persistence path in an isolated HOME. */
#include "luxe-gtk-ui.h"
#include <glib/gstdio.h>
#include <errno.h>
static const char *deny_unlink_path;
static const char *deny_unlink_path2;
static int fixture_unlink(const char *path)
{
    if ((deny_unlink_path && g_str_equal(path, deny_unlink_path)) ||
        (deny_unlink_path2 && g_str_equal(path, deny_unlink_path2))) { errno = EACCES; return -1; }
    return g_unlink(path);
}
static GListModel *chooser_fixture;
static char *last_alert_detail;
static gboolean inject_openbox_conflict;
static const char *external_openbox = "<openbox_config><theme><name>External user edit that must survive</name></theme></openbox_config>\n";
static gboolean fixture_replace(GFile *file, const char *contents, gsize length, const char *etag,
                                gboolean backup, GFileCreateFlags flags, char **new_etag,
                                GCancellable *cancel, GError **error)
{
    if (inject_openbox_conflict) {
        g_autofree char *path = g_file_get_path(file);
        if (path && g_str_has_suffix(path, "/openbox/rc.xml")) {
            inject_openbox_conflict = FALSE;
            g_assert_true(g_file_set_contents(path, external_openbox, -1, NULL));
        }
    }
    return g_file_replace_contents(file, contents, length, etag, backup, flags, new_etag, cancel, error);
}
static GListModel *fixture_files(GtkFileChooser *chooser)
{ (void)chooser; return g_object_ref(chooser_fixture); }
static void report_save_alert(GtkAlertDialog *dialog, GtkWindow *parent)
{
    g_free(last_alert_detail);
    last_alert_detail = g_strdup(gtk_alert_dialog_get_detail(dialog));
    g_printerr("Apply diagnostic: %s\n", gtk_alert_dialog_get_detail(dialog));
    gtk_alert_dialog_show(dialog, parent);
}
#define gtk_alert_dialog_show report_save_alert
#define gtk_file_chooser_get_files fixture_files
#define g_unlink fixture_unlink
#define g_file_replace_contents fixture_replace
#include "../src/main-dlg.c"
#undef g_file_replace_contents
#undef g_unlink
#undef gtk_file_chooser_get_files
#undef gtk_alert_dialog_show
gboolean under_luxesession = FALSE;

static void wait_for_wallpaper_apply(void)
{
    gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while(wallpaper_apply_pending && g_get_monotonic_time() < deadline)
        g_main_context_iteration(NULL, TRUE);
    g_assert_false(wallpaper_apply_pending);
}

static void test_wallpaper_follow(GtkWindow *window)
{
    g_autofree char *bin = g_dir_make_tmp("luxeappearance-frame-XXXXXX", NULL);
    g_autofree char *command = g_build_filename(bin, "frame", NULL);
    g_autofree char *script = g_strdup(
        "#!/bin/sh\n"
        "[ \"$1\" = --set-wallpaper ] || exit 2\n"
        "[ \"${FAKE_FRAME_FAIL:-0}\" = 0 ] || { echo 'fixture failure' >&2; exit 7; }\n"
        "mkdir -p \"$XDG_CONFIG_HOME/luxe\"\n"
        "printf '[Wallpaper]\\nsource=%s\\n' \"$2\" > \"$XDG_CONFIG_HOME/luxe/wallpaper.ini\"\n");
    g_autofree char *old_path = g_strdup(g_getenv("PATH"));
    g_autofree char *new_path = g_strconcat(bin, ":", old_path ? old_path : "/usr/bin:/bin", NULL);
    g_autofree char *wallpaper = NULL;
    g_autofree char *state_path = appearance_state_path();
    g_autofree char *frame_path = g_build_filename(g_get_user_config_dir(), "luxe", "wallpaper.ini", NULL);
    g_autofree char *contents = NULL;
    g_autoptr(GKeyFile) state = g_key_file_new();
    const LuxeTheme *theme = luxe_theme_find("Luxe-tokyo-night");
    g_assert_nonnull(theme);
    wallpaper = luxe_theme_wallpaper(theme);
    g_assert_true(g_file_test(wallpaper, G_FILE_TEST_IS_REGULAR));
    g_assert_true(g_file_set_contents(command, script, -1, NULL));
    g_assert_cmpint(g_chmod(command, 0755), ==, 0);
    g_setenv("PATH", new_path, TRUE);
    g_setenv("FAKE_FRAME_FAIL", "0", TRUE);
    g_free(gtk_theme_name);
    gtk_theme_name = g_strdup("Luxe-tokyo-night");
    gtk_check_button_set_active(theme_wallpaper, TRUE);
    apply_preset_wallpaper();
    wait_for_wallpaper_apply();
    g_assert_true(g_file_get_contents(frame_path, &contents, NULL, NULL));
    g_assert_true(g_key_file_load_from_data(state, contents, -1, G_KEY_FILE_NONE, NULL));
    g_autofree char *frame_source = g_key_file_get_string(state, "Wallpaper", "source", NULL);
    g_assert_cmpstr(frame_source, ==, wallpaper);
    g_assert_true(frame_wallpaper_source_matches(wallpaper));
    g_assert_true(g_file_set_contents(frame_path, "[Wallpaper]\nsource=/manual/user-choice.png\n", -1, NULL));
    g_assert_false(frame_wallpaper_source_matches(wallpaper));
    g_autofree char *restored_frame = g_strdup_printf("[Wallpaper]\nsource=%s\n", wallpaper);
    g_assert_true(g_file_set_contents(frame_path, restored_frame, -1, NULL));
    g_clear_pointer(&contents, g_free);
    g_key_file_remove_group(state, "Appearance", NULL);
    g_key_file_set_boolean(state, "Appearance", "wallpaper-follow", TRUE);
    g_key_file_set_string(state, "Appearance", "last-wallpaper", wallpaper);
    gsize length = 0;
    contents = g_key_file_to_data(state, &length, NULL);
    g_assert_true(lxsession_atomic_write_file(state_path, contents, length, NULL));
    g_clear_pointer(&contents, g_free);

    g_setenv("FAKE_FRAME_FAIL", "1", TRUE);
    main_dlg_widget = NULL; /* Avoid opening a modal error dialog in this failure fixture. */
    apply_preset_wallpaper();
    wait_for_wallpaper_apply();
    main_dlg_widget = GTK_WIDGET(window);
    g_assert_true(g_file_get_contents(state_path, &contents, NULL, NULL));
    g_assert_true(g_key_file_load_from_data(state, contents, -1, G_KEY_FILE_NONE, NULL));
    g_autofree char *last = g_key_file_get_string(state, "Appearance", "last-wallpaper", NULL);
    g_assert_cmpstr(last, ==, wallpaper);
    if(old_path) g_setenv("PATH", old_path, TRUE);
    g_unsetenv("FAKE_FRAME_FAIL");
    g_print("PASS: wallpaper following records Frame state only after a successful apply.\n");
}

static gboolean icon_row(const char *name, const char *display)
{
    GtkTreeIter it;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(icon_theme_list), &it);
    guint matches = 0;
    while(valid) {
        g_autofree char *row = NULL, *label = NULL;
        gtk_tree_model_get(GTK_TREE_MODEL(icon_theme_list), &it,
                           COL_NAME, &row, COL_DISP_NAME, &label, -1);
        if(g_strcmp0(row, name) == 0) {
            matches++;
            if(display) g_assert_cmpstr(label, ==, display);
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(icon_theme_list), &it);
    }
    g_assert_cmpuint(matches, <=, 1);
    return matches == 1;
}

static gboolean theme_scan_idle_ran;
static gboolean mark_theme_scan_idle(gpointer data)
{
    (void)data;
    theme_scan_idle_ran = TRUE;
    return G_SOURCE_REMOVE;
}

static void test_icon_themes(void)
{
    const struct { const char *name, *metadata; } fixtures[] = {
        {"TestInstalledPack", "Name=Installed pack\n"},
        {"TestVisible", "Name=Visible\nHidden=false\n"},
        {"TestHidden", "Name=Hidden\nHidden=true\n"},
        {"TestActiveHidden", "Name=Active hidden\nHidden=true\n"},
        {"TestMalformed", "Name=Malformed\nHidden=perhaps\n"},
        {"TestUnnamed", ""},
        {"Adwaita", "Name=Adwaita\nHidden=true\n"}
    };
    for(guint i = 0; i < G_N_ELEMENTS(fixtures); i++) {
        g_autofree char *dir = g_build_filename(g_get_user_data_dir(), "icons", fixtures[i].name, NULL);
        g_autofree char *path = g_build_filename(dir, "index.theme", NULL);
        g_autofree char *contents = g_strconcat("[Icon Theme]\nDirectories=scalable/apps\n",
            fixtures[i].metadata, "[scalable/apps]\nSize=48\nType=Scalable\n", NULL);
        g_assert_cmpint(g_mkdir_with_parents(dir, 0700), ==, 0);
        g_assert_true(g_file_set_contents(path, contents, -1, NULL));
    }
    /* A legacy duplicate must not hide the user-data pack or create two rows. */
    g_autofree char *legacy = g_build_filename(g_get_home_dir(), ".icons", "TestInstalledPack", NULL);
    g_autofree char *legacy_index = g_build_filename(legacy, "index.theme", NULL);
    g_assert_cmpint(g_mkdir_with_parents(legacy, 0700), ==, 0);
    g_assert_true(g_file_set_contents(legacy_index,
        "[Icon Theme]\nName=Legacy duplicate\nDirectories=scalable/apps\n", -1, NULL));
    g_free(icon_theme_name); icon_theme_name = g_strdup("TestInstalledPack");
    load_all_themes_now(); main_dlg_ensure_themes_loaded();
    g_assert_true(icon_row("TestInstalledPack", "Installed pack"));
    g_assert_true(icon_row("TestSystemPack", "System pack"));
    g_assert_true(icon_row("TestVisible", "Visible"));
    g_assert_true(icon_row("TestUnnamed", "TestUnnamed"));
    g_assert_true(icon_row("Adwaita", "Adwaita"));
    g_assert_false(icon_row("TestHidden", NULL));
    g_assert_false(icon_row("TestActiveHidden", NULL));
    g_assert_false(icon_row("TestMalformed", NULL));
    g_assert_cmpstr(icon_theme_name, ==, "TestInstalledPack");
    /* Refresh retains the active hidden choice without exposing all fallbacks. */
    g_free(icon_theme_name); icon_theme_name = g_strdup("TestActiveHidden");
    load_all_themes_now(); main_dlg_ensure_themes_loaded();
    g_assert_true(icon_row("TestActiveHidden", "Active hidden"));
    g_assert_false(icon_row("TestHidden", NULL));
    g_assert_false(icon_row("TestMalformed", NULL));
    g_assert_cmpstr(icon_theme_name, ==, "TestActiveHidden");
    refresh_icon_and_cursor_lists();
    main_dlg_ensure_themes_loaded();
    g_assert_true(icon_row("TestActiveHidden", "Active hidden"));
    g_assert_true(icon_row("TestInstalledPack", "Installed pack"));
    g_assert_true(icon_row("TestSystemPack", "System pack"));
    g_assert_cmpstr(icon_theme_name, ==, "TestActiveHidden");
    g_print("PASS: optional Hidden, stock Adwaita, active selection, refresh and duplicate icon packs.\n");
}

static void test_presets(GtkWidget *window)
{
    g_autofree char *css_path = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "gtk.css", NULL);
    g_autofree char *bridge_path = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "luxe-colors.css", NULL);
    g_autofree char *state_path = appearance_state_path();
    const char *custom = "/* my custom CSS */\n.my-class { font-weight: bold; }\n";
    g_assert_true(g_file_set_contents(css_path, custom, -1, NULL));

    for(guint i = 0; i < G_N_ELEMENTS(luxe_themes); i++) {
        on_preset_clicked(NULL, (gpointer)&luxe_themes[i]);
        on_apply_clicked(GTK_BUTTON(apply_button), NULL);
        g_assert_false(settings_dirty);
        g_autofree char *css = NULL, *colors = NULL;
        g_assert_true(g_file_get_contents(css_path, &css, NULL, NULL));
        g_assert_nonnull(strstr(css, custom));
        g_assert_true(g_file_get_contents(bridge_path, &colors, NULL, NULL));
        g_assert_nonnull(strstr(colors, luxe_themes[i].bg));
        g_autoptr(GKeyFile) state = g_key_file_new();
        g_assert_true(g_key_file_load_from_file(state, state_path, 0, NULL));
        g_autofree char *id = g_key_file_get_string(state, "Appearance", "theme", NULL);
        g_assert_cmpstr(id, ==, luxe_themes[i].id);
        g_autoptr(GSettings) interface = g_settings_new("org.gnome.desktop.interface");
        g_autofree char *global = g_settings_get_string(interface, "gtk-theme");
        g_assert_cmpstr(global, ==, gtk_theme_name);
        g_autofree char *obpath = openbox_config_path();
        g_autofree char *xml = NULL;
        g_assert_true(g_file_get_contents(obpath, &xml, NULL, NULL));
        g_assert_nonnull(strstr(xml, gtk_theme_name));
    }
    gtk_toggle_button_set_active(color_mode_light, TRUE);
    g_assert_cmpstr(gtk_theme_name, ==, "Luxe-light");
    gtk_toggle_button_set_active(color_mode_dark, TRUE);
    g_assert_cmpstr(gtk_theme_name, ==, "Luxe-tokyo-night");
    on_preset_clicked(NULL, (gpointer)luxe_theme_find("Luxe-nord"));
    main_dlg_handle_close(window); /* Cancel preview, no file change. */
    g_assert_cmpstr(gtk_theme_name, ==, "Luxe-tokyo-night");
    g_assert_cmpstr(last_dark_theme, ==, "Luxe-tokyo-night");

    /* A late state-write failure must restore GTK, Openbox and both CSS files. */
    const char *relative[] = {"gtk-4.0/settings.ini", "gtk-3.0/settings.ini", "openbox/rc.xml", "gtk-4.0/gtk.css", "gtk-4.0/luxe-colors.css"};
    char *before[G_N_ELEMENTS(relative)] = {0};
    for(guint i = 0; i < G_N_ELEMENTS(relative); i++) {
        g_autofree char *path = g_build_filename(g_get_user_config_dir(), relative[i], NULL);
        g_assert_true(g_file_get_contents(path, &before[i], NULL, NULL));
    }
    g_autofree char *state_before = NULL;
    g_assert_true(g_file_get_contents(state_path, &state_before, NULL, NULL));
    g_assert_true(g_file_set_contents(state_path, "[broken state", -1, NULL));
    on_preset_clicked(NULL, (gpointer)luxe_theme_find("Luxe-nord"));
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_true(settings_dirty);
    for(guint i = 0; i < G_N_ELEMENTS(relative); i++) {
        g_autofree char *path = g_build_filename(g_get_user_config_dir(), relative[i], NULL);
        g_autofree char *after = NULL;
        g_assert_true(g_file_get_contents(path, &after, NULL, NULL));
        g_assert_cmpstr(after, ==, before[i]); g_free(before[i]);
    }
    g_assert_true(g_file_set_contents(state_path, state_before, -1, NULL));

    g_autofree char *journal_path = apply_journal_path();
    g_assert_false(g_file_test(journal_path, G_FILE_TEST_EXISTS));

    /* A crash journal can restore previous files and global preferences. */
    GArray *targets = g_array_new(FALSE, TRUE, sizeof(ApplyTarget));
    g_autoptr(GError) error = NULL;
    ApplyTarget *saved = snapshot_file(css_path, &error);
    g_assert_nonnull(saved);
    g_array_append_val(targets, *saved); g_free(saved);
    g_assert_true(write_apply_journal(targets, &error));
    g_assert_true(g_file_set_contents(css_path, "/* interrupted */", -1, NULL));
    g_autoptr(GKeyFile) journal = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(journal, journal_path, 0, NULL));
    g_key_file_set_string(journal, "Interface", "color-scheme", "not-a-valid-mode");
    gsize journal_len;
    g_autofree char *invalid = g_key_file_to_data(journal, &journal_len, NULL);
    g_assert_true(g_file_set_contents(journal_path, invalid, journal_len, NULL));
    g_assert_false(recover_apply_journal(&error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_autofree char *still_interrupted = NULL;
    g_assert_true(g_file_get_contents(css_path, &still_interrupted, NULL, NULL));
    g_assert_cmpstr(still_interrupted, ==, "/* interrupted */");
    g_unlink(journal_path);
    g_assert_true(write_apply_journal(targets, &error));
    g_assert_true(recover_apply_journal(&error));
    g_assert_no_error(error);
    g_autofree char *restored = NULL;
    g_assert_true(g_file_get_contents(css_path, &restored, NULL, NULL));
    g_assert_cmpstr(restored, ==, g_array_index(targets, ApplyTarget, 0).contents);
    free_apply_targets(targets); g_array_unref(targets);

    /* An ETag conflict must not cause the generic rollback to overwrite the
     * intervening edit that caused the conflict. */
    g_autofree char *obpath = openbox_config_path();
    g_autofree char *ob_before = NULL;
    g_assert_true(g_file_get_contents(obpath, &ob_before, NULL, NULL));
    on_preset_clicked(NULL, (gpointer)luxe_theme_find("Luxe-nord"));
    inject_openbox_conflict = TRUE;
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_false(inject_openbox_conflict);
    g_assert_true(settings_dirty);
    g_autofree char *ob_after = NULL;
    g_assert_true(g_file_get_contents(obpath, &ob_after, NULL, NULL));
    g_assert_cmpstr(ob_after, ==, external_openbox);
    g_assert_nonnull(strstr(last_alert_detail, "Openbox configuration could not be saved"));
    gboolean may_have_written = TRUE;
    g_assert_false(write_openbox_theme_config(ob_before, strlen(ob_before), TRUE, TRUE,
                                               &may_have_written, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
    g_assert_false(may_have_written);
    g_clear_error(&error);
    g_autofree char *still_external = NULL;
    g_assert_true(g_file_get_contents(obpath, &still_external, NULL, NULL));
    g_assert_cmpstr(still_external, ==, external_openbox);
    g_assert_true(g_file_set_contents(obpath, ob_before, -1, NULL));
    g_assert_false(journal_target_allowed("/tmp/not-an-appearance-file"));
    g_assert_false(journal_target_allowed("/etc/passwd"));

    /* A failed removal of a newly created target must preserve recovery
     * evidence and be safely retryable, including callers without GError. */
    g_assert_cmpint(g_unlink(css_path), ==, 0);
    targets = g_array_new(FALSE, TRUE, sizeof(ApplyTarget));
    saved = snapshot_file(css_path, &error);
    g_assert_nonnull(saved); g_assert_false(saved->existed);
    g_array_append_val(targets, *saved); g_free(saved);
    g_assert_true(write_apply_journal(targets, &error));
    g_assert_true(g_file_set_contents(css_path, "created during apply", -1, NULL));
    deny_unlink_path = css_path;
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*could not restore*Permission denied*");
    g_assert_false(recover_apply_journal(&error));
    g_test_assert_expected_messages();
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_nonnull(strstr(error->message, css_path));
    g_clear_error(&error);
    g_assert_true(g_file_test(journal_path, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(css_path, G_FILE_TEST_EXISTS));
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*could not restore*Permission denied*");
    g_assert_false(recover_apply_journal(NULL));
    g_test_assert_expected_messages();
    g_assert_true(g_file_test(journal_path, G_FILE_TEST_EXISTS));
    deny_unlink_path = NULL;
    g_assert_true(recover_apply_journal(&error));
    g_assert_no_error(error);
    g_assert_false(g_file_test(css_path, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(journal_path, G_FILE_TEST_EXISTS));
    free_apply_targets(targets); g_array_unref(targets);
    g_assert_true(g_file_set_contents(css_path, custom, -1, NULL));

    /* Exercise the actual Apply failure path, not just recovery: a late
     * malformed state causes rollback after creating a previously absent
     * GTK settings file, whose removal is deliberately denied. */
    g_autofree char *settings40 = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
    g_autofree char *saved40 = NULL;
    g_assert_true(g_file_get_contents(settings40, &saved40, NULL, NULL));
    g_assert_cmpint(g_unlink(settings40), ==, 0);
    g_assert_true(g_file_set_contents(state_path, "[broken state", -1, NULL));
    deny_unlink_path = settings40;
    on_preset_clicked(NULL, (gpointer)luxe_theme_find("Luxe-nord"));
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*could not restore*Permission denied*");
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_test_assert_expected_messages();
    g_assert_nonnull(strstr(last_alert_detail, "broken state"));
    g_assert_nonnull(strstr(last_alert_detail, "rollback incomplete"));
    g_assert_nonnull(strstr(last_alert_detail, settings40));
    g_assert_true(g_file_test(journal_path, G_FILE_TEST_EXISTS));
    deny_unlink_path = NULL;
    g_assert_true(recover_apply_journal(&error));
    g_assert_no_error(error);
    g_assert_false(g_file_test(journal_path, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(settings40, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_set_contents(settings40, saved40, -1, NULL));
    g_assert_true(g_file_set_contents(state_path, state_before, -1, NULL));

    /* Aggregate failures without marking either target restored; retry the
     * same transaction object after the injected failures are removed. */
    g_autofree char *one = g_build_filename(g_get_user_config_dir(), "rollback-one", NULL);
    g_autofree char *two = g_build_filename(g_get_user_config_dir(), "rollback-two", NULL);
    g_assert_true(g_file_set_contents(one, "one", -1, NULL));
    g_assert_true(g_file_set_contents(two, "two", -1, NULL));
    targets = g_array_new(FALSE, TRUE, sizeof(ApplyTarget));
    ApplyTarget a = {.path=g_strdup(one), .written=TRUE};
    ApplyTarget b = {.path=g_strdup(two), .written=TRUE};
    g_array_append_val(targets, a); g_array_append_val(targets, b);
    deny_unlink_path = one; deny_unlink_path2 = two;
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*could not restore*rollback-two*");
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*could not restore*rollback-one*");
    g_assert_false(rollback_targets(targets, &error));
    g_test_assert_expected_messages();
    g_assert_nonnull(strstr(error->message, one)); g_assert_nonnull(strstr(error->message, two));
    g_clear_error(&error);
    g_assert_true(g_array_index(targets, ApplyTarget, 0).written);
    g_assert_true(g_array_index(targets, ApplyTarget, 1).written);
    deny_unlink_path = NULL; deny_unlink_path2 = NULL;
    g_assert_true(rollback_targets(targets, &error)); g_assert_no_error(error);
    g_assert_false(g_file_test(one, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(two, G_FILE_TEST_EXISTS));
    free_apply_targets(targets); g_array_unref(targets);

    /* A previously existing target whose atomic replacement fails must also
     * remain retryable. A directory obstruction is confined to the fixture. */
    g_assert_cmpint(g_mkdir(one, 0700), ==, 0);
    targets = g_array_new(FALSE, TRUE, sizeof(ApplyTarget));
    ApplyTarget c = {.path=g_strdup(one), .contents=g_strdup("original"), .len=8, .existed=TRUE, .written=TRUE};
    g_array_append_val(targets, c);
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*could not restore*rollback-one*");
    g_assert_false(rollback_targets(targets, &error));
    g_test_assert_expected_messages();
    g_assert_nonnull(error); g_clear_error(&error);
    g_assert_true(g_array_index(targets, ApplyTarget, 0).written);
    g_assert_cmpint(g_rmdir(one), ==, 0);
    g_assert_true(rollback_targets(targets, &error)); g_assert_no_error(error);
    g_autofree char *restored_one = NULL;
    g_assert_true(g_file_get_contents(one, &restored_one, NULL, NULL));
    g_assert_cmpstr(restored_one, ==, "original");
    g_assert_cmpint(g_unlink(one), ==, 0);
    free_apply_targets(targets); g_array_unref(targets);

    /* Final symlinks and unmanaged bridge files are not replaced. */
    g_autofree char *target = g_build_filename(g_get_user_config_dir(), "keep.css", NULL);
    g_assert_true(g_file_set_contents(target, "keep", -1, NULL));
    g_assert_cmpint(g_unlink(css_path), ==, 0);
    g_assert_cmpint(symlink(target, css_path), ==, 0);
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_true(settings_dirty);
    g_assert_true(g_file_test(css_path, G_FILE_TEST_IS_SYMLINK));
    g_unlink(css_path); g_unlink(target);
    g_assert_true(g_file_set_contents(css_path, restored, -1, NULL));
    g_assert_true(g_file_set_contents(bridge_path, "/* not yours */", -1, NULL));
    g_assert_false(luxe_theme_write_bridge(luxe_theme_find("Luxe-nord"), &error));
    g_clear_error(&error);
    /* Repair only this private fixture's bridge. */
    g_unlink(bridge_path);
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_false(settings_dirty);

    g_free(gtk_theme_name); gtk_theme_name = g_strdup("HighContrast");
    mark_dirty(); on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_false(settings_dirty);
    g_autofree char *external = NULL;
    g_assert_true(g_file_get_contents(css_path, &external, NULL, NULL));
    g_assert_cmpstr(external, ==, custom);
    /* Leave a clean light preset for the following out-of-process probe. */
    sync_color_mode_selector();
    gtk_toggle_button_set_active(color_mode_light, TRUE);
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_false(settings_dirty);
    /* Capture the actual chooser, not an image-generation concept. */
    gtk_window_present(GTK_WINDOW(window));
    gint64 until = g_get_monotonic_time() + 250000;
    while(g_get_monotonic_time() < until) {
        while(g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    GdkPaintable *paintable = gtk_widget_paintable_new(window);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), gtk_widget_get_width(window), gtk_widget_get_height(window));
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    GdkTexture *texture = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(window)), node, NULL);
    g_assert_true(gdk_texture_save_to_png(texture, LUXE_TEST_SCREENSHOT));
    g_object_unref(texture); gsk_render_node_unref(node); g_object_unref(paintable);
    g_print("PASS: six global presets, CSS preservation, remembered mode, Cancel, late rollback, crash recovery, symlinks and external themes.\n");
}
static void file_finalized(gpointer data, GObject *object)
{ (void)object; *(gboolean *)data = TRUE; }
static void test_chooser_ownership(void)
{
    g_autofree char *installer = g_build_filename(PACKAGE_DATA_DIR, "luxeappearance", "install-icon-theme.sh", NULL);
    g_assert_false(g_file_test(installer, G_FILE_TEST_EXISTS));
    const char *uris[] = {"file:///nonexistent-luxe-test-theme.tar", "https://example.invalid/theme.tar"};
    for (guint i = 0; i < G_N_ELEMENTS(uris); ++i) {
        GListStore *files = g_list_store_new(G_TYPE_OBJECT);
        GFile *file = g_file_new_for_uri(uris[i]);
        gboolean finalized = FALSE;
        g_object_weak_ref(G_OBJECT(file), file_finalized, &finalized);
        g_list_store_append(files, file); g_object_unref(file);
        chooser_fixture = G_LIST_MODEL(files);
        GtkFileChooserNative *dialog = gtk_file_chooser_native_new("Fixture", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, "Open", "Cancel");
        on_install_response(GTK_NATIVE_DIALOG(dialog), GTK_RESPONSE_ACCEPT, NULL);
        g_assert_false(finalized); // The chooser model still owns its reference.
        g_object_unref(files);
        g_assert_true(finalized);  // Callback retained no extra reference.
    }
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new("Fixture", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, "Open", "Cancel");
    chooser_fixture = NULL;
    on_install_response(GTK_NATIVE_DIALOG(dialog), GTK_RESPONSE_CANCEL, NULL);
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--chooser-ownership")) {
        luxe_ui_init(); test_chooser_ownership(); return 0;
    }
    gtk_init();
    /* libadwaita must be live in-process so GtkBuilder can resolve the
     * GtkApplicationWindow in the installed luxeappearance.ui. */
    luxe_ui_init();
    GtkWidget* window = create_dlg();
    main_dlg_init(window);
    {
        GtkToggleButton* dark = GTK_TOGGLE_BUTTON(lookup_widget(window, "color_mode_dark"));
        GtkToggleButton* light = GTK_TOGGLE_BUTTON(lookup_widget(window, "color_mode_light"));
        g_assert_nonnull(dark); g_assert_nonnull(light);
        gtk_toggle_button_set_active(dark, TRUE);
        g_assert_cmpstr(gtk_theme_name, ==, "Luxe-dark");
        g_assert_cmpstr(icon_theme_name, ==, "Luxe-Dark");
        gtk_toggle_button_set_active(light, TRUE);
        g_assert_cmpstr(gtk_theme_name, ==, "Luxe-light");
        g_assert_cmpstr(icon_theme_name, ==, "Luxe-Light");
    }
    g_free(rc_file); rc_file = g_build_filename(g_get_user_config_dir(), "gtkrc-test", NULL);
    g_free(gtk_theme_name); gtk_theme_name = g_strdup("Adwaita-dark");
    g_free(icon_theme_name); icon_theme_name = g_strdup("Adwaita");
    {
        g_autofree char *path = g_build_filename(g_get_user_config_dir(), "openbox", "rc.xml", NULL);
        g_autofree char *after = NULL;
        const char *commented = "<openbox_config><!-- ignored: <theme><name>Wrong</name></theme> -->\n<theme><!-- comment -->\n<name>Original</name></theme></openbox_config>\n";
        g_autoptr(GError) error = NULL;
        g_autofree char *oversized = g_malloc0(1024 * 1024 + 2);
        memset(oversized, 'x', 1024 * 1024 + 1);
        g_assert_true(g_file_set_contents(path, oversized, 1024 * 1024 + 1, NULL));
        g_autoptr(GFile) oversized_file = g_file_new_for_path(path);
        g_autofree char *read_data = NULL, *read_etag = NULL;
        gsize read_length = 0;
        g_assert_false(read_openbox_config(oversized_file, &read_data, &read_length, &read_etag, &error));
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
        g_assert_null(read_data); g_clear_error(&error);
        g_assert_true(g_file_set_contents(path, commented, -1, &error));
        g_assert_no_error(error);
        g_assert_true(write_openbox_theme_config(NULL, 0, TRUE, FALSE, NULL, &error));
        g_assert_no_error(error);
        g_clear_error(&error);
        g_assert_true(g_file_get_contents(path, &after, NULL, &error));
        g_assert_no_error(error);
        g_assert_nonnull(strstr(after, "<!-- ignored: <theme><name>Wrong</name></theme> -->"));
        g_assert_nonnull(strstr(after, "<!-- comment -->"));
        g_assert_nonnull(strstr(after, "<name>Luxe-tokyo-night</name>"));
        /* A matching template still has to create the missing user file. */
        g_assert_cmpint(g_unlink(path), ==, 0);
        g_free(gtk_theme_name); gtk_theme_name = g_strdup("Luxe-tokyo-night");
        g_assert_true(write_openbox_theme_config(NULL, 0, FALSE, FALSE, NULL, &error));
        g_assert_no_error(error);
        g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
        g_autofree char *created = NULL;
        g_assert_true(g_file_get_contents(path, &created, NULL, &error));
        g_assert_no_error(error);
        g_assert_nonnull(strstr(created, "<name>Luxe-tokyo-night</name>"));
        g_free(gtk_theme_name); gtk_theme_name = g_strdup("Adwaita-dark");
        g_assert_true(g_file_set_contents(path, "<openbox_config><theme><name>Original</name></theme></openbox_config>\n", -1, &error));
        g_assert_no_error(error);
    }
    {
        GError* openbox_error = NULL;
        gboolean openbox_ok = write_openbox_theme_config(NULL, 0, FALSE, FALSE, NULL, &openbox_error);
        g_assert_no_error(openbox_error);
        g_assert_true(openbox_ok);
    }
    settings_dirty = TRUE;
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_false(settings_dirty);
    GKeyFile* key = g_key_file_new();
    char* path = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
    g_assert_true(g_key_file_load_from_file(key, path, 0, NULL));
    char* value = g_key_file_get_string(key, "Settings", "gtk-theme-name", NULL);
    g_assert_cmpstr(value, ==, "Adwaita-dark"); g_free(value);
    g_assert_cmpint(g_key_file_get_integer(key, "Settings", "gtk-double-click-time", NULL), ==, 333);
    g_assert_true(g_key_file_get_boolean(key, "Settings", "gtk-application-prefer-dark-theme", NULL));
    {
        char* openbox = g_build_filename(g_get_user_config_dir(), "openbox", "rc.xml", NULL);
        char* xml = NULL;
        g_assert_true(g_file_get_contents(openbox, &xml, NULL, NULL));
        g_assert_nonnull(strstr(xml, "<name>Luxe-tokyo-night</name>"));
        g_free(xml); g_free(openbox);
    }
    g_free(path);
    path = g_build_filename(g_get_user_config_dir(), "gtk-3.0", "settings.ini", NULL);
    g_assert_true(g_key_file_load_from_file(key, path, 0, NULL)); g_free(path);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lookup_widget(window, "color_mode_dark")), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lookup_widget(window, "color_mode_light")), TRUE);
    under_luxesession = TRUE; g_free(session_name); session_name = g_strdup("TestSession");
    settings_dirty = TRUE;
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    path = g_build_filename(g_get_user_config_dir(), "luxesession", session_name, "desktop.conf", NULL);
    g_assert_true(g_key_file_load_from_file(key, path, 0, NULL));
    value = g_key_file_get_string(key, "GTK", "sNet/ThemeName", NULL);
    g_assert_cmpstr(value, ==, "Luxe-light"); g_free(value); g_free(path);
    {
        char* openbox = g_build_filename(g_get_user_config_dir(), "openbox", "rc.xml", NULL);
        char* xml = NULL;
        g_assert_true(g_file_get_contents(openbox, &xml, NULL, NULL));
        g_assert_nonnull(strstr(xml, "<name>Luxe-light</name>"));
        g_free(xml); g_free(openbox);
    }
    /* A system-theme panel must be restarted after Apply so GTK4 reloads its
     * CSS. Verify the real command is issued only inside LuxeSession, using
     * a private luxepanelctl stand-in rather than starting a desktop panel. */
    {
        char* bindir = g_dir_make_tmp("luxeappearance-panelctl-XXXXXX", NULL);
        char* command = g_build_filename(bindir, "luxepanelctl", NULL);
        char* log = g_build_filename(bindir, "restart.log", NULL);
        /* A stand-in that records the argument. PATH is prepended rather than
         * replaced so unrelated helpers stay reachable, and the real
         * luxepanelctl can never be selected. */
        char* script = g_strdup("#!/bin/sh\nprintf '%s' \"$1\" > \"$LUXEAPPEARANCE_PANEL_RESTART_LOG\"\n");
        char* result = NULL;
        gint attempts = 0;
        g_assert_true(g_file_set_contents(command, script, -1, NULL));
        g_assert_cmpint(g_chmod(command, 0700), ==, 0);
        {
            const char* old_path = g_getenv("PATH");
            char* new_path = g_strdup_printf("%s%s%s", bindir,
                                             old_path ? ":" : "",
                                             old_path ? old_path : "");
            g_setenv("PATH", new_path, TRUE);
            g_free(new_path);
        }
        g_setenv("LUXEAPPEARANCE_PANEL_RESTART_LOG", log, TRUE);
        g_setenv("_LUXESESSION_PID", "test-session", TRUE);
        under_luxesession = TRUE;
        reload_luxepanel_theme();
        while(!g_file_test(log, G_FILE_TEST_EXISTS) && attempts++ < 50)
        {
            g_main_context_iteration(NULL, FALSE);
            g_usleep(10000);
        }
        g_assert_true(g_file_get_contents(log, &result, NULL, NULL));
        g_assert_cmpstr(result, ==, "restart");
        /* Wallpaper following is opt-in and uses a local path via argv,
         * never a shell or the live PCManager instance. */
        g_autofree char *pcmanager = g_build_filename(bindir, "frame", NULL);
        g_autofree char *wallpaper_log = g_build_filename(bindir, "wallpaper.log", NULL);
        g_assert_true(g_file_set_contents(pcmanager,
            "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$LUXE_TEST_WALLPAPER_LOG\"\n", -1, NULL));
        g_assert_cmpint(g_chmod(pcmanager, 0700), ==, 0);
        g_setenv("LUXE_TEST_WALLPAPER_LOG", wallpaper_log, TRUE);
        apply_preset_wallpaper();
        g_assert_false(g_file_test(wallpaper_log, G_FILE_TEST_EXISTS));
        gtk_check_button_set_active(theme_wallpaper, TRUE);
        on_apply_clicked(GTK_BUTTON(apply_button), NULL);
        g_assert_false(settings_dirty);
        attempts = 0;
        while(!g_file_test(wallpaper_log, G_FILE_TEST_EXISTS) && attempts++ < 50) {
            g_main_context_iteration(NULL, FALSE); g_usleep(10000);
        }
        g_autofree char *wallpaper_result = NULL;
        g_autofree char *wallpaper = luxe_theme_wallpaper(luxe_theme_find(gtk_theme_name));
        g_assert_true(g_str_has_suffix(wallpaper, ".png"));
        g_autofree char *expected = g_strdup_printf("--set-wallpaper\n%s\n", wallpaper);
        g_assert_true(g_file_get_contents(wallpaper_log, &wallpaper_result, NULL, NULL));
        g_assert_cmpstr(wallpaper_result, ==, expected);
        gtk_check_button_set_active(theme_wallpaper, FALSE);
        on_apply_clicked(GTK_BUTTON(apply_button), NULL);
        g_unsetenv("LUXE_TEST_WALLPAPER_LOG");
        g_unlink(pcmanager); g_unlink(wallpaper_log);
        g_unsetenv("_LUXESESSION_PID");
        g_unsetenv("LUXEAPPEARANCE_PANEL_RESTART_LOG");
        g_free(result); g_free(script);
        g_unlink(command); g_unlink(log);
        g_rmdir(bindir);
        g_free(command); g_free(log); g_free(bindir);
    }
    /* UTF-8-preserving gtkrc quoting: non-ASCII must survive verbatim. */
    {
        char* e = rc_escape("Caf\xc3\xa9 \"Quoted\" \\ Test");
        g_assert_true(strstr(e, "Caf\xc3\xa9") != NULL);
        g_assert_true(strstr(e, "\\\"Quoted\\\"") != NULL);
        g_assert_true(strstr(e, "\\\\") != NULL);
        g_assert_true(strstr(e, "\\303") == NULL);
        g_free(e);
    }
    {
        char* tmp = g_build_filename(g_get_user_config_dir(), "gtkrc-utf8-test", NULL);
        GError* werr = NULL;
        char* old_theme = g_strdup(gtk_theme_name);
        char* contents = NULL;
        g_free(gtk_theme_name); gtk_theme_name = g_strdup("Caf\xc3\xa9-Th\xc3\xa8me");
        g_assert_true(write_rc_file(tmp, &werr));
        g_assert_true(g_file_get_contents(tmp, &contents, NULL, &werr));
        g_assert_true(strstr(contents, "Caf\xc3\xa9-Th\xc3\xa8me") != NULL);
        g_assert_true(strstr(contents, "\\303") == NULL);
        g_free(contents); g_unlink(tmp); g_free(tmp);
        g_free(gtk_theme_name); gtk_theme_name = old_theme;
    }
    /* Standalone gtkrc must be preserved: only the Luxe keys are rewritten,
     * never the whole file. Previously Apply replaced ~/.gtkrc-2.0 outright
     * and destroyed unrelated bindings, includes and comments. */
    {
        char* tmp = g_build_filename(g_get_user_config_dir(), "gtkrc-preserve-test", NULL);
        char* before = NULL, *after = NULL;
        GError* werr = NULL;
        const char* original =
            "# my gtkrc\n"
            "gtk-font-name=\"Sans 12\"\n"
            "gtk-binding-key <Shift>Delete {\n"
            "  \"delete-or-chars-forward\" ()\n"
            "}\n"
            "include \"~/.gtkrc-extra\"\n"
            "gtk-theme-name=\"Raleigh\"\n";
        g_assert_true(g_file_set_contents(tmp, original, -1, NULL));
        g_assert_true(write_rc_file(tmp, &werr));
        g_assert_no_error(werr);
        g_assert_true(g_file_get_contents(tmp, &after, NULL, &werr));
        /* Unrelated lines survive byte-for-byte. */
        g_assert_nonnull(strstr(after, "# my gtkrc"));
        g_assert_nonnull(strstr(after, "gtk-binding-key <Shift>Delete"));
        g_assert_nonnull(strstr(after, "delete-or-chars-forward"));
        g_assert_nonnull(strstr(after, "include \"~/.gtkrc-extra\""));
        /* The Luxe-managed keys are updated in place, not duplicated. */
        g_assert_nonnull(strstr(after, "gtk-font-name="));
        g_assert_nonnull(strstr(after, "gtk-icon-theme-name="));
        g_assert_true(strstr(after, "gtk-theme-name=\"Raleigh\"") == NULL);
        g_assert_nonnull(strstr(after, "gtk-theme-name=\"Luxe-light\""));
        g_free(after);
        g_assert_true(g_file_get_contents(tmp, &before, NULL, NULL));
        /* A second Apply must be idempotent. */
        g_assert_true(write_rc_file(tmp, &werr));
        after = NULL;
        g_assert_true(g_file_get_contents(tmp, &after, NULL, NULL));
        g_assert_cmpstr(before, ==, after);
        g_free(before); g_free(after);
        g_unlink(tmp); g_free(tmp);
    }
    /* A malformed (NUL-containing) gtkrc must be refused, not truncated. */
    {
        char* tmp = g_build_filename(g_get_user_config_dir(), "gtkrc-nul-test", NULL);
        char data[32] = { 'g', 'k', 't', 'c', 0, 'x' };
        GError* werr = NULL;
        g_assert_true(g_file_set_contents(tmp, data, (gssize)sizeof(data), NULL));
        g_assert_false(write_rc_file(tmp, &werr));
        g_assert_nonnull(werr);
        g_clear_error(&werr);
        g_unlink(tmp); g_free(tmp);
    }
    /* A symlinked standalone gtkrc must be refused rather than silently
     * converted into a regular file at the link path. */
    {
        char* real = g_build_filename(g_get_user_config_dir(), "gtkrc-real", NULL);
        char* link = g_build_filename(g_get_user_config_dir(), "gtkrc-link", NULL);
        GError* werr = NULL;
        g_assert_true(g_file_set_contents(real,
            "# keep this target\n gtk-theme-name=\"Raleigh\"\n", -1, NULL));
        g_unlink(link);
        g_assert_cmpint(symlink("gtkrc-real", link), ==, 0);
        g_assert_false(write_rc_file(link, &werr));
        g_assert_error(werr, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
        g_clear_error(&werr);
        char* target = NULL;
        g_assert_true(g_file_get_contents(real, &target, NULL, NULL));
        g_assert_nonnull(strstr(target, "keep this target"));
        g_free(target);
        g_unlink(link); g_unlink(real);
        g_free(real); g_free(link);
    }
    /* rc_escape must encode every C0 control, not just \n, \r and \t. */
    {
        char raw[] = { 'a', 0x01, 0x08, 'b', 0 };
        char* e = rc_escape(raw);
        g_assert_nonnull(strstr(e, "\\x01"));
        g_assert_nonnull(strstr(e, "\\x08"));
        g_free(e);
    }
    /* DESKTOP_SESSION is a path component: traversal must never be accepted. */
    {
        g_assert_false(valid_session_name(".."));
        g_assert_false(valid_session_name("../../evil"));
        g_assert_false(valid_session_name("a/b"));
        g_assert_false(valid_session_name(""));
        g_assert_false(valid_session_name(NULL));
        g_assert_true(valid_session_name("LXDE"));
        g_assert_true(valid_session_name("X-Luxe"));
        g_setenv("DESKTOP_SESSION", "../../escape", TRUE);
        g_clear_pointer(&session_name, g_free);
        session_name = default_session_name();
        g_assert_cmpstr(session_name, ==, "LXDE");
        g_setenv("DESKTOP_SESSION", "X-Luxe", TRUE);
        g_clear_pointer(&session_name, g_free);
        session_name = default_session_name();
        g_assert_cmpstr(session_name, ==, "X-Luxe");
    }
    /* A symlinked Openbox rc.xml must be refused rather than followed. */
    {
        char* obdir = g_build_filename(g_get_user_config_dir(), "openbox", NULL);
        char* real = g_build_filename(obdir, "rc-real.xml", NULL);
        char* link = g_build_filename(obdir, "rc.xml", NULL);
        GError* werr = NULL;
        g_assert_true(g_file_set_contents(real,
            "<openbox_config><theme><name>Luxe-tokyo-night</name></theme></openbox_config>", -1, NULL));
        g_unlink(link);
        g_assert_cmpint(symlink("rc-real.xml", link), ==, 0);
        g_assert_false(write_openbox_theme_config(NULL, 0, FALSE, FALSE, NULL, &werr));
        g_assert_error(werr, G_FILE_ERROR, G_FILE_ERROR_ISDIR);
        g_clear_error(&werr);
        /* The link target is untouched. */
        char* rc = NULL;
        g_assert_true(g_file_get_contents(real, &rc, NULL, NULL));
        g_assert_nonnull(strstr(rc, "Luxe-tokyo-night"));
        g_free(rc);
        g_unlink(link); g_unlink(real);
        g_free(obdir); g_free(real); g_free(link);
    }
    /* gtk-toolbar-style belongs only in the GTK3 file: GTK4 rejects it. */
    {
        GError* werr = NULL;
        g_assert_true(write_settings_file("gtk-4.0", &werr));
        g_assert_true(write_settings_file("gtk-3.0", &werr));
        char* p40 = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
        char* p30 = g_build_filename(g_get_user_config_dir(), "gtk-3.0", "settings.ini", NULL);
        GKeyFile* k40 = g_key_file_new();
        GKeyFile* k30 = g_key_file_new();
        g_assert_true(g_key_file_load_from_file(k40, p40, 0, NULL));
        g_assert_true(g_key_file_load_from_file(k30, p30, 0, NULL));
        g_assert_false(g_key_file_has_key(k40, "Settings", "gtk-toolbar-style", NULL));
        g_assert_true(g_key_file_has_key(k30, "Settings", "gtk-toolbar-style", NULL));
        g_key_file_unref(k40); g_key_file_unref(k30);
        g_free(p40); g_free(p30);
    }
    /* Rollback: corrupt gtk-3.0 so its write fails; gtk-4.0 must be restored. */
    {
        char* p40 = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
        char* p30 = g_build_filename(g_get_user_config_dir(), "gtk-3.0", "settings.ini", NULL);
        char* before40 = NULL;
        char* before30 = NULL;
        char* after40 = NULL;
        g_assert_true(g_file_get_contents(p40, &before40, NULL, NULL));
        g_assert_true(g_file_get_contents(p30, &before30, NULL, NULL));
        g_assert_true(g_file_set_contents(p30, "[\nnot valid [[[", -1, NULL));
        under_luxesession = FALSE;
        g_free(rc_file); rc_file = g_build_filename(g_get_user_config_dir(), "gtkrc-test", NULL);
        settings_dirty = TRUE;
        on_apply_clicked(GTK_BUTTON(apply_button), NULL);
        g_assert_true(settings_dirty);
        g_assert_true(g_file_get_contents(p40, &after40, NULL, NULL));
        g_assert_cmpstr(before40, ==, after40);
        g_free(before40); g_free(after40);
        /* Repair the corrupted file, then a normal write must succeed again. */
        g_assert_true(g_file_set_contents(p30, before30, -1, NULL));
        g_free(before30);
        {
            GError* rerr = NULL;
            g_assert_true(write_settings_file("gtk-3.0", &rerr));
            g_clear_error(&rerr);
        }
        g_free(p40); g_free(p30);
        settings_dirty = FALSE;
    }
    /* A failed write must not clear dirty state or pretend Apply succeeded. */
    path = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
    g_unlink(path); g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
    settings_dirty = TRUE;
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_true(settings_dirty);
    g_rmdir(path); g_free(path);
    settings_dirty = FALSE;
    GError* error = NULL;
    g_assert_true(write_settings_file("gtk-4.0", &error));
    /* Theme enumeration is deferred to an idle callback; force completion. */
    main_dlg_ensure_themes_loaded();
    /* Only themes with GTK4 CSS plus GTK4's built-ins belong in the picker. */
    GtkTreeIter it; gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(gtk_theme_list), &it);
    gboolean dark_found = FALSE;
    while(valid) {
        char* name = NULL; gtk_tree_model_get(GTK_TREE_MODEL(gtk_theme_list), &it, COL_NAME, &name, -1);
        if(g_strcmp0(name, "Adwaita-dark") == 0) dark_found = TRUE;
        g_assert_cmpstr(name, !=, "TestGtk2Only"); g_free(name);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(gtk_theme_list), &it);
    }
    g_assert_true(dark_found);
    /* Delay only the test worker and prove the GTK main loop processes an
     * independent idle callback while filesystem enumeration is in flight. */
    theme_scan_idle_ran = FALSE;
    g_setenv("LUXE_THEME_SCAN_TEST_DELAY_MS", "250", TRUE);
    load_all_themes_now();
    g_idle_add(mark_theme_scan_idle, NULL);
    main_dlg_ensure_themes_loaded();
    g_unsetenv("LUXE_THEME_SCAN_TEST_DELAY_MS");
    g_assert_true(theme_scan_idle_ran);
    test_icon_themes();
    g_key_file_unref(key);
    /* Finish with one clean Apply over a settings.ini that still carries an
     * unrelated key. The out-of-process probe then verifies a real persisted
     * state rather than the torn-down fixtures left by the failure cases. */
    path = g_build_filename(g_get_user_config_dir(), "gtk-4.0", "settings.ini", NULL);
    g_assert_true(g_file_set_contents(path,
        "[Settings]\ngtk-double-click-time=333\n", -1, NULL));
    settings_dirty = TRUE;
    on_apply_clicked(GTK_BUTTON(apply_button), NULL);
    g_assert_false(settings_dirty);
    g_free(path);
    test_presets(window);
    test_wallpaper_follow(GTK_WINDOW(window));
    g_print("PASS: GTK3/4 and session persistence, unrelated keys preserved, failure reported, GTK4 themes listed.\n");
    return 0;
}
