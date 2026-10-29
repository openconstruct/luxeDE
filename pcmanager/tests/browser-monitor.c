/* Real model and tab regression for bounded directory-monitor refreshes. */
#include "../src/browser-gtk4.c"

static void pump_ms(guint milliseconds)
{
    gint64 end = g_get_monotonic_time() + milliseconds * 1000;
    do { while(g_main_context_iteration(NULL, FALSE)); g_usleep(1000); }
    while(g_get_monotonic_time() < end);
}
static void wait_loaded(Tab *tab)
{
    gint64 end = g_get_monotonic_time() + 5000000;
    do { pump_ms(10); } while(gtk_directory_list_is_loading(tab->directory) && g_get_monotonic_time() < end);
    g_assert_false(gtk_directory_list_is_loading(tab->directory));
    g_assert_null(gtk_directory_list_get_error(tab->directory));
}
static void changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)object; (void)pspec; ++*(guint *)data;
}
static gboolean heartbeat(gpointer data)
{
    ++*(guint *)data;
    return G_SOURCE_CONTINUE;
}
int main(int argc, char **argv)
{
    if (argc == 4 && g_str_equal(argv[1], "--clipboard-owner")) {
        gtk_init();
        g_autofree char *payload = g_strdup_printf("%s\n%s\n", argv[3], argv[2]);
        g_autoptr(GBytes) bytes = g_bytes_new(payload, strlen(payload));
        g_autoptr(GdkContentProvider) provider = gdk_content_provider_new_for_bytes("x-special/gnome-copied-files", bytes);
        GdkClipboard *clipboard = gdk_display_get_clipboard(gdk_display_get_default());
        g_assert_true(gdk_clipboard_set_content(clipboard, provider));
        g_print("READY\n"); fflush(stdout);
        g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(loop);
        return 0;
    }
    luxe_ui_init(); fm_init(NULL); pcmgr_browser_setup("monitor-test", NULL);
    g_autofree char *directory = g_dir_make_tmp("pcmanager-monitor-XXXXXX", NULL);
    g_assert_nonnull(directory);
    g_autofree char *path = g_build_filename(directory, "keep.txt", NULL);
    g_assert_true(g_file_set_contents(path, "fixture", -1, NULL));
    g_autoptr(GFile) location = g_file_new_for_path(directory);
    Browser *browser = new_browser();
    Tab *tab = add_tab(browser, location);
    gtk_window_present(GTK_WINDOW(browser));
    wait_loaded(tab);
    /* The standard external clipboard action determines copy versus move. */
    gboolean move = FALSE;
    const char *good = "cut\r\nfile:///tmp/one\r\nfile:///tmp/two\r\n";
    g_autoptr(GPtrArray) parsed = parse_copied_files((const guint8 *)good, strlen(good), &move);
    g_assert_nonnull(parsed); g_assert_true(move); g_assert_cmpuint(parsed->len, ==, 2);
    const char *bad[] = {"move\nfile:///tmp/a", "cut\n/tmp/no-uri", "cut\nfile:///tmp/a\nfile:///tmp/a", "copy\nfile:///tmp/with space"};
    for (guint i=0; i<G_N_ELEMENTS(bad); i++) {
        GPtrArray *invalid = parse_copied_files((const guint8 *)bad[i], strlen(bad[i]), &move);
        g_assert_null(invalid);
    }
    g_autofree char *source_path = g_build_filename(directory, "external-cut.txt", NULL);
    g_autofree char *destination_path = g_build_filename(directory, "destination", NULL);
    g_autofree char *moved_path = g_build_filename(destination_path, "external-cut.txt", NULL);
    g_assert_true(g_file_set_contents(source_path, "external cut", -1, NULL));
    g_assert_cmpint(g_mkdir(destination_path, 0700), ==, 0);
    g_autoptr(GFile) source_file = g_file_new_for_path(source_path);
    g_autofree char *source_uri = g_file_get_uri(source_file);
    g_autofree char *cut_payload = g_strdup_printf("cut\n%s\n", source_uri);
    g_autoptr(GBytes) cut_bytes = g_bytes_new(cut_payload, strlen(cut_payload));
    GdkContentProvider *external = gdk_content_provider_new_for_bytes("x-special/gnome-copied-files", cut_bytes);
    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(browser));
    g_assert_true(gdk_clipboard_set_content(clipboard, external));
    Prompt *external_paste = g_new0(Prompt, 1);
    external_paste->browser = g_object_ref(browser);
    external_paste->parent = g_file_new_for_path(destination_path);
    start_paste(external_paste, clipboard);
    gint64 move_deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!g_file_test(moved_path, G_FILE_TEST_EXISTS) && g_get_monotonic_time() < move_deadline) pump_ms(10);
    g_assert_true(g_file_test(moved_path, G_FILE_TEST_EXISTS));
    while (browser->operation && g_get_monotonic_time() < move_deadline) pump_ms(10);
    g_assert_null(browser->operation);
    g_assert_false(g_file_test(source_path, G_FILE_TEST_EXISTS));
    g_assert_null(gdk_clipboard_get_content(clipboard));
    gdk_clipboard_set_content(clipboard, NULL); g_object_unref(external);
    pump_ms(100);
    g_assert_cmpint(g_unlink(moved_path), ==, 0); g_assert_cmpint(g_rmdir(destination_path), ==, 0);
    /* A new clipboard owner during the async read must cancel the old paste. */
    g_assert_true(g_file_set_contents(source_path, "retain", -1, NULL));
    g_assert_cmpint(g_mkdir(destination_path, 0700), ==, 0);
    external = gdk_content_provider_new_for_bytes("x-special/gnome-copied-files", cut_bytes);
    g_assert_true(gdk_clipboard_set_content(clipboard, external));
    external_paste = g_new0(Prompt, 1);
    external_paste->browser = g_object_ref(browser);
    external_paste->parent = g_file_new_for_path(destination_path);
    start_paste(external_paste, clipboard);
    GCancellable *first_read = browser->paste_cancel;
    g_assert_nonnull(first_read);
    Prompt *duplicate = g_new0(Prompt, 1);
    duplicate->browser = g_object_ref(browser);
    duplicate->parent = g_file_new_for_path(destination_path);
    start_paste(duplicate, clipboard);
    g_assert_true(browser->paste_cancel == first_read);
    GdkContentProvider *replacement = gdk_content_provider_new_typed(G_TYPE_STRING, "new owner");
    g_assert_true(gdk_clipboard_set_content(clipboard, replacement));
    pump_ms(300);
    g_assert_null(browser->paste_cancel);
    g_assert_true(g_file_test(source_path, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(moved_path, G_FILE_TEST_EXISTS));
    g_assert_true(gdk_clipboard_get_content(clipboard) == replacement);
    gdk_clipboard_set_content(clipboard, NULL);
    g_object_unref(external); g_object_unref(replacement);
    pump_ms(100);
    g_assert_cmpint(g_unlink(source_path), ==, 0); g_assert_cmpint(g_rmdir(destination_path), ==, 0);
    /* A separate GTK process supplies the clipboard, as another file manager
     * would. This uses only the nested test X server and temporary files. */
    g_assert_true(g_file_set_contents(source_path, "cross process", -1, NULL));
    g_assert_cmpint(g_mkdir(destination_path, 0700), ==, 0);
    g_autoptr(GError) child_error = NULL;
    g_autoptr(GSubprocess) owner = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, &child_error,
        argv[0], "--clipboard-owner", source_uri, "cut", NULL);
    g_assert_no_error(child_error); g_assert_nonnull(owner);
    g_autoptr(GDataInputStream) ready = g_data_input_stream_new(g_subprocess_get_stdout_pipe(owner));
    g_autofree char *ready_line = g_data_input_stream_read_line(ready, NULL, NULL, &child_error);
    g_assert_no_error(child_error); g_assert_cmpstr(ready_line, ==, "READY");
    pump_ms(100);
    g_assert_null(gdk_clipboard_get_content(clipboard));
    external_paste = g_new0(Prompt, 1);
    external_paste->browser = g_object_ref(browser);
    external_paste->parent = g_file_new_for_path(destination_path);
    start_paste(external_paste, clipboard);
    move_deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!g_file_test(moved_path, G_FILE_TEST_EXISTS) && g_get_monotonic_time() < move_deadline) pump_ms(10);
    g_assert_true(g_file_test(moved_path, G_FILE_TEST_EXISTS));
    while (browser->operation && g_get_monotonic_time() < move_deadline) pump_ms(10);
    g_assert_null(browser->operation); g_assert_false(g_file_test(source_path, G_FILE_TEST_EXISTS));
    g_subprocess_force_exit(owner); g_subprocess_wait(owner, NULL, NULL);
    pump_ms(100);
    g_assert_cmpint(g_unlink(moved_path), ==, 0); g_assert_cmpint(g_rmdir(destination_path), ==, 0);
    g_assert_true(g_file_set_contents(source_path, "cross process copy", -1, NULL));
    g_assert_cmpint(g_mkdir(destination_path, 0700), ==, 0);
    g_clear_object(&ready); g_clear_object(&owner); g_clear_pointer(&ready_line, g_free);
    owner = g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, &child_error,
        argv[0], "--clipboard-owner", source_uri, "copy", NULL);
    g_assert_no_error(child_error); g_assert_nonnull(owner);
    ready = g_data_input_stream_new(g_subprocess_get_stdout_pipe(owner));
    ready_line = g_data_input_stream_read_line(ready, NULL, NULL, &child_error);
    g_assert_no_error(child_error); g_assert_cmpstr(ready_line, ==, "READY");
    pump_ms(100);
    external_paste = g_new0(Prompt, 1);
    external_paste->browser = g_object_ref(browser);
    external_paste->parent = g_file_new_for_path(destination_path);
    start_paste(external_paste, clipboard);
    move_deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!g_file_test(moved_path, G_FILE_TEST_EXISTS) && g_get_monotonic_time() < move_deadline) pump_ms(10);
    g_assert_true(g_file_test(moved_path, G_FILE_TEST_EXISTS));
    while (browser->operation && g_get_monotonic_time() < move_deadline) pump_ms(10);
    g_assert_null(browser->operation); g_assert_true(g_file_test(source_path, G_FILE_TEST_EXISTS));
    g_subprocess_force_exit(owner); g_subprocess_wait(owner, NULL, NULL); pump_ms(100);
    g_assert_cmpint(g_unlink(source_path), ==, 0); g_assert_cmpint(g_unlink(moved_path), ==, 0);
    g_assert_cmpint(g_rmdir(destination_path), ==, 0);
    /* Paste a copied file into Trash, but never touch the real Trash. */
    GFile *clipboard_files[] = { g_file_new_for_path(path) };
    GdkFileList *clipboard_list = gdk_file_list_new_from_array(clipboard_files, 1);
    GdkContentProvider *clipboard_provider = gdk_content_provider_new_typed(GDK_TYPE_FILE_LIST, clipboard_list);
    g_boxed_free(GDK_TYPE_FILE_LIST, clipboard_list);
    g_object_unref(clipboard_files[0]);
    g_assert_true(gdk_clipboard_set_content(clipboard, clipboard_provider));
    Prompt *paste = g_new0(Prompt, 1);
    paste->browser = g_object_ref(browser);
    paste->parent = g_file_new_for_uri("trash:///");
    gdk_clipboard_read_value_async(clipboard, GDK_TYPE_FILE_LIST, G_PRIORITY_DEFAULT, NULL, paste_ready, paste);
    pump_ms(300);
    GListModel *windows = gtk_window_get_toplevels();
    GtkDialog *confirmation = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(windows); ++i) {
        GtkWidget *window = g_list_model_get_item(windows, i);
        if (GTK_IS_MESSAGE_DIALOG(window)) confirmation = GTK_DIALOG(window);
        else g_object_unref(window);
    }
    g_assert_nonnull(confirmation);
    char *detail = NULL;
    g_object_get(confirmation, "secondary-text", &detail, NULL);
    g_assert_nonnull(strstr(detail, "original files"));
    g_free(detail);
    g_assert_true(gtk_window_get_default_widget(GTK_WINDOW(confirmation)) == gtk_dialog_get_widget_for_response(confirmation, 0));
    g_assert_null(browser->operation);
    gtk_dialog_response(confirmation, 0);
    g_object_unref(confirmation);
    pump_ms(100);
    g_assert_null(browser->operation);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
    g_assert_true(gdk_clipboard_get_content(clipboard) == clipboard_provider);
    gdk_clipboard_set_content(clipboard, NULL);
    g_object_unref(clipboard_provider);
    /* A stale window changing one preference must not rewrite another. */
    g_autofree char *prefs_dir = g_dir_make_tmp("pcmanager-prefs-XXXXXX", NULL);
    char *original_config = config_path;
    config_path = g_build_filename(prefs_dir, "preferences.conf", NULL);
    g_assert_true(g_file_set_contents(config_path, "[ui]\nshow_hidden=true\nview_mode=icon\n", -1, NULL));
    browser->show_hidden = FALSE; browser->icons = FALSE;
    save_preferences(browser, PREF_VIEW);
    g_autoptr(GKeyFile) saved = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(saved, config_path, 0, NULL));
    g_assert_true(g_key_file_get_boolean(saved, "ui", "show_hidden", NULL));
    g_autofree char *view = g_key_file_get_string(saved, "ui", "view_mode", NULL);
    g_assert_cmpstr(view, ==, "list");
    g_autofree char *prefs_lock = g_strconcat(config_path, ".lock", NULL);
    g_assert_true(g_file_test(prefs_lock, G_FILE_TEST_EXISTS));
    g_assert_cmpint(g_unlink(prefs_lock), ==, 0);
    g_assert_cmpint(symlink(config_path, prefs_lock), ==, 0);
    browser->show_hidden = FALSE;
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Cannot save PCManager preferences:*preferences lock*");
    save_preferences(browser, PREF_HIDDEN);
    g_test_assert_expected_messages();
    g_assert_true(g_key_file_get_boolean(saved, "ui", "show_hidden", NULL));
    g_assert_cmpint(g_unlink(prefs_lock), ==, 0);
    g_assert_cmpint(g_unlink(config_path), ==, 0);
    g_assert_cmpint(g_mkdir(config_path, 0700), ==, 0); /* Replacement fails: target is a directory. */
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*Cannot save PCManager preferences:*");
    save_preferences(browser, PREF_HIDDEN);
    g_test_assert_expected_messages();
    g_assert_true(gtk_widget_get_visible(browser->message));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(browser->message)), "Cannot save preferences"));
    message(browser, NULL);
    g_assert_cmpint(g_rmdir(config_path), ==, 0);
    g_assert_cmpint(g_unlink(prefs_lock), ==, 0);
    g_free(config_path); config_path = original_config;
    g_assert_cmpint(g_rmdir(prefs_dir), ==, 0);
    guint position = find_name(tab, "keep.txt");
    g_assert_cmpuint(position, !=, GTK_INVALID_LIST_POSITION);
    gtk_selection_model_select_item(GTK_SELECTION_MODEL(tab->selection), position, TRUE);
    g_autoptr(GtkDirectoryList) original = g_object_ref(tab->directory);
    for(int i = 0; i < 10; ++i) {
        folder_changed(NULL, NULL, NULL, G_FILE_MONITOR_EVENT_CHANGED, tab);
        pump_ms(50);
    }
    g_assert_true(tab->directory == original); /* No rebuild during the burst. */
    pump_ms(350); wait_loaded(tab);
    g_assert_true(tab->directory != original);
    position = find_name(tab, "keep.txt");
    g_assert_cmpuint(position, !=, GTK_INVALID_LIST_POSITION);
    g_assert_true(gtk_selection_model_is_selected(GTK_SELECTION_MODEL(tab->selection), position));
    /* A second request during a load must not replace its restore snapshot. */
    refresh(tab, TRUE);
    g_autoptr(GtkDirectoryList) loading_model = g_object_ref(tab->directory);
    g_assert_true(gtk_directory_list_is_loading(loading_model));
    refresh(tab, TRUE);
    g_assert_true(tab->directory == loading_model);
    wait_loaded(tab); pump_ms(350); wait_loaded(tab);
    position = find_name(tab, "keep.txt");
    g_assert_cmpuint(position, !=, GTK_INVALID_LIST_POSITION);
    g_assert_true(gtk_selection_model_is_selected(GTK_SELECTION_MODEL(tab->selection), position));

    Tab *other = add_tab(browser, location);
    wait_loaded(other);
    g_autoptr(GtkDirectoryList) hidden_model = g_object_ref(tab->directory);
    for(int i = 0; i < 10; ++i) {
        folder_changed(NULL, NULL, NULL, G_FILE_MONITOR_EVENT_CHANGED, tab);
        pump_ms(50);
    }
    pump_ms(350);
    g_assert_true(tab->directory == hidden_model);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(browser->notebook), 0);
    pump_ms(350); wait_loaded(tab);
    g_assert_true(tab->directory != hidden_model);
    position = find_name(tab, "keep.txt");
    g_assert_true(gtk_selection_model_is_selected(GTK_SELECTION_MODEL(tab->selection), position));

    guint rebuilds = 0;
    g_signal_connect(tab->filtered, "notify::model", G_CALLBACK(changed), &rebuilds);
    guint space_queries_before_storm = tab->space_query_count;
    guint ticks = 0, events = 0;
    guint heartbeat_source = g_timeout_add(10, heartbeat, &ticks);
    gint64 end = g_get_monotonic_time() + 2300000;
    while(g_get_monotonic_time() < end) {
        for(int i = 0; i < 10; ++i) {
            folder_changed(NULL, NULL, NULL, G_FILE_MONITOR_EVENT_CHANGED, tab);
            ++events;
        }
        pump_ms(1);
    }
    g_source_remove(heartbeat_source);
    pump_ms(350); wait_loaded(tab);
    g_assert_cmpuint(events, >, 1000);
    g_assert_cmpuint(ticks, >, 50);
    g_assert_cmpuint(rebuilds, >=, 1);
    g_assert_cmpuint(rebuilds, <=, 2);
    g_assert_cmpuint(tab->space_query_count, ==, space_queries_before_storm);
    g_signal_handlers_disconnect_by_data(tab->filtered, &rebuilds);
    /* Exercise actual monitor events as well as the direct burst above. */
    g_autofree char *added = g_build_filename(directory, "added.txt", NULL);
    g_autofree char *renamed = g_build_filename(directory, "renamed.txt", NULL);
    g_assert_true(g_file_set_contents(added, "new", -1, NULL));
    end = g_get_monotonic_time() + 5000000;
    while(find_name(tab, "added.txt") == GTK_INVALID_LIST_POSITION && g_get_monotonic_time() < end)
        pump_ms(30);
    wait_loaded(tab);
    g_assert_cmpuint(find_name(tab, "added.txt"), !=, GTK_INVALID_LIST_POSITION);
    position = find_name(tab, "keep.txt");
    g_assert_true(gtk_selection_model_is_selected(GTK_SELECTION_MODEL(tab->selection), position));
    g_assert_cmpint(g_rename(path, renamed), ==, 0);
    end = g_get_monotonic_time() + 5000000;
    while(find_name(tab, "renamed.txt") == GTK_INVALID_LIST_POSITION && g_get_monotonic_time() < end)
        pump_ms(30);
    wait_loaded(tab);
    g_assert_cmpuint(find_name(tab, "renamed.txt"), !=, GTK_INVALID_LIST_POSITION);
    g_assert_cmpuint(find_name(tab, "keep.txt"), ==, GTK_INVALID_LIST_POSITION);
    g_autoptr(GtkBitset) selected = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(tab->selection));
    g_assert_cmpuint(gtk_bitset_get_size(selected), ==, 0);
    gtk_window_destroy(GTK_WINDOW(browser)); pump_ms(100);
    fm_finalize();
    g_assert_cmpint(g_unlink(renamed), ==, 0);
    g_assert_cmpint(g_unlink(added), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    return 0;
}
