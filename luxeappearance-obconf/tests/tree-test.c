/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real XML tree mutation/apply path in a private X server. */
#define main obconf_program_main
#include "../src/main.c"
#undef main
static guint theme_finish_count;
static gchar *counted_archive_install_finish(GAsyncResult *result, GError **error)
{
    ++theme_finish_count;
    return archive_install_finish(result, error);
}
#define archive_install_finish counted_archive_install_finish
#include "../src/theme.c"
#undef archive_install_finish

typedef struct {
    GMainLoop *loop;
    gchar *name;
    GError *error;
} AsyncInstallResult;

static void on_archive_install_done(GObject *source, GAsyncResult *result,
                                    gpointer data)
{
    AsyncInstallResult *state = data;
    (void)source;
    state->name = archive_install_finish(result, &state->error);
    g_main_loop_quit(state->loop);
}

static void test_x_property_validation(void)
{
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window root_window = DefaultRootWindow(dpy);
    Atom utf8 = gdk_x11_get_xatom_by_name("UTF8_STRING");
    Atom probe = gdk_x11_get_xatom_by_name("_LUXE_OBCONF_TEST_PROPERTY");
    gulong value = 1;
    gchar invalid[] = { (gchar)0xff, 0 };
    gchar embedded[] = { 'O', 0, 'K' };
    guchar *data = NULL;
    guint count = 0;
    gchar *text = NULL;

    XChangeProperty(dpy, root_window, probe, XA_INTEGER, 32, PropModeReplace,
                    (const unsigned char *)&value, 1);
    g_assert_false(get_all(root_window, probe, XA_CARDINAL, 32, &data, &count));
    g_assert_null(data);
    g_assert_cmpuint(count, ==, 0);

    XChangeProperty(dpy, root_window, probe, utf8, 8, PropModeReplace,
                    (const unsigned char *)invalid, sizeof(invalid));
    g_assert_false(prop_get_string_utf8(root_window, probe, &text));
    g_assert_null(text);
    XChangeProperty(dpy, root_window, probe, utf8, 8, PropModeReplace,
                    (const unsigned char *)embedded, sizeof(embedded));
    g_assert_false(prop_get_string_utf8(root_window, probe, &text));
    g_assert_null(text);
    XDeleteProperty(dpy, root_window, probe);
}

static void test_async_install_after_cleanup(void)
{
    g_autofree gchar *source_parent = g_build_filename(g_get_home_dir(),
                                                         "archive-source", NULL);
    g_autofree gchar *theme_dir = g_build_filename(source_parent,
                                                    "AsyncTheme", NULL);
    g_autofree gchar *openbox_dir = g_build_filename(theme_dir,
                                                     "openbox-3", NULL);
    g_autofree gchar *themerc = g_build_filename(openbox_dir,
                                                 "themerc", NULL);
    g_autofree gchar *archive = g_build_filename(g_get_home_dir(),
                                                  "AsyncTheme.obt", NULL);
    g_autofree gchar *installed = g_build_filename(g_get_home_dir(),
                                                    ".themes", "AsyncTheme",
                                                    "openbox-3", "themerc", NULL);
    g_autofree gchar *stdout_text = NULL;
    g_autofree gchar *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    gchar *tar_argv[] = { (gchar *)"tar", (gchar *)"-czf", archive,
                          (gchar *)"-C", source_parent, (gchar *)"AsyncTheme",
                          NULL };
    gint status = 0;
    AsyncInstallResult state = { 0 };

    g_assert_cmpint(g_mkdir_with_parents(openbox_dir, 0700), ==, 0);
    g_assert_true(g_file_set_contents(themerc, "[Desktop]\n", -1, &error));
    g_assert_no_error(error);
    g_assert_true(g_spawn_sync(NULL, tar_argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, &stdout_text, &stderr_text,
                               &status, &error));
    g_assert_no_error(error);
    g_assert_true(g_spawn_check_wait_status(status, &error));
    g_assert_no_error(error);

    state.loop = g_main_loop_new(NULL, FALSE);
    archive_install_async(archive, on_archive_install_done, &state);
    /* A close/cleanup may happen while extraction is still in flight. */
    obconf_cleanup();
    g_main_loop_run(state.loop);
    g_assert_error(state.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_null(state.name);
    g_assert_false(g_file_test(installed, G_FILE_TEST_EXISTS));
    g_free(state.name);
    g_clear_error(&state.error);
    g_main_loop_unref(state.loop);
}

/* Theme scanning must skip symlinked and control-character names; the
 * skip path once freed an uninitialized auto pointer. */
static void add_skipped_theme_fixtures(void)
{
    g_autofree gchar *themes = g_build_filename(g_get_home_dir(), ".themes", NULL);
    g_autofree gchar *good = g_build_filename(themes, "Good", "openbox-3", NULL);
    g_autofree gchar *good_rc = g_build_filename(good, "themerc", NULL);
    g_autofree gchar *linked = g_build_filename(themes, "Linked", NULL);
    g_autofree gchar *bad = g_build_filename(themes, "Bad\tName", "openbox-3", NULL);

    g_assert_cmpint(g_mkdir_with_parents(good, 0700), ==, 0);
    g_assert_true(g_file_set_contents(good_rc, "[Desktop]\n", -1, NULL));
    g_assert_cmpint(symlink("Good", linked), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(bad, 0700), ==, 0);
}

static void test_repeated_theme_selection(void)
{
    g_autofree gchar *saved_name = tree_get_string("theme/name", "TheBear");
    GtkWidget *view = get_widget("theme_names");
    GtkTreeModel *model = GTK_TREE_MODEL(theme_store);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gint64 deadline = g_get_monotonic_time() + 3000000;
    GtkTreeIter iter;
    while (gtk_tree_model_iter_n_children(model, NULL) == 0 &&
           g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    g_assert_true(gtk_tree_model_get_iter_first(model, &iter));
    for (guint i = 0; i < 1000; ++i) {
        gtk_tree_selection_unselect_all(selection);
        gtk_tree_selection_select_iter(selection, &iter);
    }
    gtk_tree_selection_unselect_all(selection);
    tree_set_string("theme/name", saved_name);
}

static void test_application_lifecycle(void)
{
    g_autofree gchar *config_dir = g_build_filename(g_get_user_config_dir(),
                                                      "openbox", NULL);
    g_autofree gchar *config = g_build_filename(config_dir, "rc.xml", NULL);
    g_autoptr(GError) error = NULL;
    GtkApplication *app;
    GtkWidget *window;
    GWeakRef window_ref;
    Display *dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Window root_window = DefaultRootWindow(dpy);
    Window wm_window;
    Atom check_atom, name_atom, utf8_atom;

    g_assert_cmpint(g_mkdir_with_parents(config_dir, 0700), ==, 0);
    g_assert_true(g_file_set_contents(config,
        "<openbox_config><theme><name>Afterpiece</name>"
        "<titleLayout>NLIMC</titleLayout></theme>"
        "<desktops><number>2</number><names><name>One</name>"
        "<name>Two</name></names></desktops></openbox_config>\n",
        -1, &error));
    g_assert_no_error(error);

    wm_window = XCreateSimpleWindow(dpy, root_window, -10, -10, 1, 1, 0, 0, 0);
    check_atom = gdk_x11_get_xatom_by_name("_NET_SUPPORTING_WM_CHECK");
    name_atom = gdk_x11_get_xatom_by_name("_NET_WM_NAME");
    utf8_atom = gdk_x11_get_xatom_by_name("UTF8_STRING");
    XChangeProperty(dpy, root_window, check_atom, XA_WINDOW, 32, PropModeReplace,
                    (const unsigned char *)&wm_window, 1);
    XChangeProperty(dpy, wm_window, name_atom, utf8_atom, 8, PropModeReplace,
                    (const unsigned char *)"Openbox", 7);

    app = luxe_application_new("org.luxe.ObConf.Test", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, &error));
    g_assert_no_error(error);
    window = gtk_application_window_new(GTK_APPLICATION(app));
    g_weak_ref_init(&window_ref, G_OBJECT(window));
    g_assert_true(setup_obconf(window));
    g_assert_false(settings_dirty);
    gtk_window_present(GTK_WINDOW(window));
    for (int i = 0; i < 100; ++i) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    test_repeated_theme_selection();

    /* The entry, staged XML, disk file and X11 property agree on Unicode. */
    {
        GtkWidget *row = gtk_widget_get_first_child(get_widget("desktop_names"));
        g_assert_nonnull(row);
        GtkWidget *label = gtk_widget_get_first_child(row);
        GtkWidget *entry = gtk_widget_get_next_sibling(label);
        g_assert_true(GTK_IS_ENTRY(entry));
        g_autofree gchar *ascii = g_strnfill(256, 'a');
        gtk_editable_set_text(GTK_EDITABLE(entry), ascii);
        g_assert_cmpint(g_utf8_strlen(gtk_editable_get_text(GTK_EDITABLE(entry)), -1), ==, 255);
        g_autofree gchar *accepted_ascii = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
        on_apply_clicked(NULL, NULL);
        g_assert_false(settings_dirty);
        g_autofree gchar *ascii_saved = NULL;
        g_assert_true(g_file_get_contents(config, &ascii_saved, NULL, &error));
        g_assert_no_error(error);
        g_assert_nonnull(strstr(ascii_saved, accepted_ascii));
        Atom ascii_actual = None; int ascii_format = 0;
        unsigned long ascii_count = 0, ascii_remaining = 0;
        unsigned char *ascii_property = NULL;
        g_assert_cmpint(XGetWindowProperty(dpy, root_window,
            gdk_x11_get_xatom_by_name("_NET_DESKTOP_NAMES"), 0, 4096, False,
            utf8_atom, &ascii_actual, &ascii_format, &ascii_count, &ascii_remaining,
            &ascii_property), ==, Success);
        g_assert_cmpuint(ascii_actual, ==, utf8_atom);
        g_assert_cmpstr((const char *)ascii_property, ==, accepted_ascii);
        XFree(ascii_property);
        g_autoptr(GString) long_name = g_string_new(NULL);
        for (int i = 0; i < 255; ++i) g_string_append(long_name, "é");
        gtk_editable_set_text(GTK_EDITABLE(entry), long_name->str);
        g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(entry)), ==, long_name->str);
        xmlNodePtr names = tree_ensure_node("desktops/names", NULL);
        xmlNodePtr first = names->children;
        while (first && first->type != XML_ELEMENT_NODE) first = first->next;
        g_assert_nonnull(first);
        xmlChar *staged = xmlNodeGetContent(first);
        g_assert_cmpstr((const char *)staged, ==, long_name->str);
        xmlFree(staged);
        g_assert_true(settings_dirty);
        on_apply_clicked(NULL, NULL);
        g_assert_false(settings_dirty);
        g_autofree gchar *saved = NULL;
        g_assert_true(g_file_get_contents(config, &saved, NULL, &error));
        g_assert_no_error(error);
        xmlDocPtr saved_doc = xmlReadMemory(saved, strlen(saved), "fixture.xml", NULL, XML_PARSE_NONET);
        g_assert_nonnull(saved_doc);
        xmlNodePtr desktop_node = obt_xml_find_node(xmlDocGetRootElement(saved_doc)->children, "desktops");
        g_assert_nonnull(desktop_node);
        xmlNodePtr names_node = obt_xml_find_node(desktop_node->children, "names");
        g_assert_nonnull(names_node);
        xmlNodePtr first_name = obt_xml_find_node(names_node->children, "name");
        g_assert_nonnull(first_name);
        xmlChar *saved_name = xmlNodeGetContent(first_name);
        g_assert_cmpstr((const char *)saved_name, ==, long_name->str);
        xmlFree(saved_name); xmlFreeDoc(saved_doc);
        Atom actual = None; int format = 0;
        unsigned long nitems = 0, remaining = 0;
        unsigned char *property = NULL;
        g_assert_cmpint(XGetWindowProperty(dpy, root_window,
            gdk_x11_get_xatom_by_name("_NET_DESKTOP_NAMES"), 0, 4096, False,
            utf8_atom, &actual, &format, &nitems, &remaining, &property), ==, Success);
        g_assert_cmpuint(actual, ==, utf8_atom);
        g_assert_cmpint(format, ==, 8);
        g_assert_cmpstr((const char *)property, ==, long_name->str);
        XFree(property);
        gtk_entry_set_max_length(GTK_ENTRY(entry), 0);
        g_string_append_c(long_name, 'x');
        gtk_editable_set_text(GTK_EDITABLE(entry), long_name->str);
        g_assert_true(settings_dirty);
        on_apply_clicked(NULL, NULL);
        g_assert_true(settings_dirty);
        g_autofree gchar *after = NULL;
        g_assert_true(g_file_get_contents(config, &after, NULL, &error));
        g_assert_no_error(error);
        g_assert_cmpstr(after, ==, saved);
        GtkWindow *dialog = luxe_window_get_visible_dialog(GTK_WINDOW(window));
        g_assert_nonnull(dialog);
        luxe_dialog_close(dialog);
        g_string_truncate(long_name, long_name->len - 1);
        gtk_editable_set_text(GTK_EDITABLE(entry), long_name->str);
        on_apply_clicked(NULL, NULL);
        g_assert_false(settings_dirty);
        GtkWidget *second_row = gtk_widget_get_next_sibling(row);
        g_assert_nonnull(second_row);
        GtkWidget *second_label = gtk_widget_get_first_child(second_row);
        GtkWidget *second_entry = gtk_widget_get_next_sibling(second_label);
        gtk_editable_set_text(GTK_EDITABLE(second_entry), "");
        on_apply_clicked(NULL, NULL);
        g_assert_false(settings_dirty);
    }

    gtk_window_close(GTK_WINDOW(window));
    for (int i = 0; i < 500 && (mainwin || builder || teardown_queue); ++i) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_assert_null(mainwin);
    g_assert_null(apply_button);
    g_assert_null(builder);
    g_assert_null(teardown_queue);
    g_assert_null(g_weak_ref_get(&window_ref));
    g_weak_ref_clear(&window_ref);
    window = gtk_application_window_new(GTK_APPLICATION(app));
    g_weak_ref_init(&window_ref, G_OBJECT(window));
    g_assert_true(setup_obconf(window));
    gtk_window_present(GTK_WINDOW(window));
    for (int i = 0; i < 30; ++i) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    {
        GtkWidget *row = gtk_widget_get_first_child(get_widget("desktop_names"));
        GtkWidget *label = gtk_widget_get_first_child(row);
        GtkWidget *entry = gtk_widget_get_next_sibling(label);
        g_autoptr(GString) expected = g_string_new(NULL);
        for (int i = 0; i < 255; ++i) g_string_append(expected, "é");
        g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(entry)), ==, expected->str);
        GtkWidget *second_row = gtk_widget_get_next_sibling(row);
        GtkWidget *second_label = gtk_widget_get_first_child(second_row);
        GtkWidget *second_entry = gtk_widget_get_next_sibling(second_label);
        g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(second_entry)), ==, "");
    }
    {
        /* Cancel the real installer on close and ensure its result cannot
         * affect the editor reopened in the same process. */
        g_autofree gchar *source = g_build_filename(g_get_home_dir(), "old-theme-source", NULL);
        g_autofree gchar *theme = g_build_filename(source, "OldWindowTheme", "openbox-3", NULL);
        g_autofree gchar *themerc = g_build_filename(theme, "themerc", NULL);
        g_autofree gchar *archive = g_build_filename(g_get_home_dir(), "OldWindowTheme.obt", NULL);
        g_autofree gchar *installed = g_build_filename(g_get_home_dir(), ".themes", "OldWindowTheme", "openbox-3", "themerc", NULL);
        gchar *tar_argv[] = { (gchar *)"tar", (gchar *)"-czf", archive, (gchar *)"-C", source,
                              (gchar *)"OldWindowTheme", NULL };
        int tar_status = 0;
        g_assert_cmpint(g_mkdir_with_parents(theme, 0700), ==, 0);
        g_assert_true(g_file_set_contents(themerc, "[Desktop]\n", -1, &error));
        g_assert_no_error(error);
        g_assert_true(g_spawn_sync(NULL, tar_argv, NULL, G_SPAWN_SEARCH_PATH,
                                   NULL, NULL, NULL, NULL, &tar_status, &error));
        g_assert_no_error(error);
        g_assert_true(g_spawn_check_wait_status(tar_status, &error));
        g_assert_no_error(error);
        guint finishes_before = theme_finish_count;
        theme_install(archive);
        gtk_window_destroy(GTK_WINDOW(window));
        g_assert_null(mainwin);
        g_weak_ref_clear(&window_ref);
        window = gtk_application_window_new(GTK_APPLICATION(app));
        g_weak_ref_init(&window_ref, G_OBJECT(window));
        g_assert_true(setup_obconf(window));
        gtk_window_present(GTK_WINDOW(window));
        gint64 deadline = g_get_monotonic_time() + 8000000;
        while (theme_finish_count == finishes_before && g_get_monotonic_time() < deadline) {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(1000);
        }
        g_assert_cmpuint(theme_finish_count, ==, finishes_before + 1);
        g_assert_false(g_file_test(installed, G_FILE_TEST_EXISTS));
        for (int i = 0; i < 250; ++i) {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(1000);
        }
        g_autofree gchar *current_theme = tree_get_string("theme/name", "");
        g_assert_cmpstr(current_theme, ==, "Afterpiece");
        g_assert_true(mainwin == window);
        g_assert_nonnull(apply_button);
    }
    gtk_window_close(GTK_WINDOW(window));
    for (int i = 0; i < 500 && (mainwin || builder || teardown_queue); ++i) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_assert_null(mainwin); g_assert_null(builder); g_assert_null(teardown_queue);
    g_assert_null(g_weak_ref_get(&window_ref));
    g_weak_ref_clear(&window_ref);
    g_object_unref(app);

    XDeleteProperty(dpy, root_window, check_atom);
    XDestroyWindow(dpy, wm_window);
    XFlush(dpy);
}

static void test_close_cancel_lifetime(void)
{
    GtkWidget *window = gtk_window_new();
    GWeakRef *window_ref = g_new0(GWeakRef, 1);

    g_weak_ref_init(window_ref, G_OBJECT(window));
    mainwin = window;
    settings_dirty = TRUE;
    close_dialog_pending = TRUE;
    on_close_choice(-1, window_ref);
    g_assert_true(settings_dirty);
    g_assert_true(mainwin == window);
    g_assert_false(close_dialog_pending);
    g_object_unref(window);
    mainwin = NULL;
    settings_dirty = FALSE;
}

int main(void)
{
    g_autofree gchar *dir = NULL;
    g_autofree gchar *config = NULL;
    g_autofree gchar *loaded = NULL;
    g_autofree gchar *value = NULL;
    g_autoptr(GError) error = NULL;

    luxe_ui_init();
    add_skipped_theme_fixtures();
    test_application_lifecycle();
    test_close_cancel_lifetime();
    test_x_property_validation();
    dir = g_build_filename(g_get_user_config_dir(), "openbox", NULL);
    g_mkdir_with_parents(dir, 0700);
    config = g_build_filename(dir, "rc.xml", NULL);
    g_assert_true(g_file_set_contents(config,
        "<openbox_config><!-- preserve this comment -->"
        "<theme><name>Before</name></theme>"
        "<desktops><number>2</number><names><name>One</name><name>Two</name></names></desktops>"
        "<custom data=\"keep\"/></openbox_config>\n", -1, &error));
    g_assert_no_error(error);

    paths = obt_paths_new();
    xml_i = obt_xml_instance_new();
    obc_config_file = g_strdup(config);
    g_assert_true(obt_xml_load_file(xml_i, obc_config_file, "openbox_config"));
    g_assert_cmpint(g_stat(obc_config_file, &obc_loaded_stat), ==, 0);
    obc_loaded_stat_valid = TRUE;
    doc = obt_xml_doc(xml_i);
    root = obt_xml_root(xml_i);
    g_assert_nonnull(doc);
    g_assert_nonnull(root);

    /* A result from a previous editor must not change the current editor. */
    {
        GtkWidget *old_window = gtk_window_new();
        GtkWidget *new_window = gtk_window_new();
        g_object_ref_sink(old_window); g_object_ref_sink(new_window);
        tree_set_string("theme/name", "Current theme");
        mainwin = new_window;
        g_autoptr(GTask) result = g_task_new(NULL, NULL, NULL, NULL);
        g_task_return_pointer(result, g_strdup("Stale theme"), g_free);
        on_theme_install_finished(NULL, G_ASYNC_RESULT(result), theme_operation_new(old_window));
        g_autofree gchar *name = tree_get_string("theme/name", "");
        g_assert_cmpstr(name, ==, "Current theme");
        ThemeOperation *closed = theme_operation_new(old_window);
        gtk_window_destroy(GTK_WINDOW(old_window));
        g_object_unref(old_window); old_window = NULL;
        g_autoptr(GTask) after_close = g_task_new(NULL, NULL, NULL, NULL);
        g_task_return_pointer(after_close, g_strdup("Closed theme"), g_free);
        on_theme_install_finished(NULL, G_ASYNC_RESULT(after_close), closed);
        g_clear_pointer(&name, g_free);
        name = tree_get_string("theme/name", "");
        g_assert_cmpstr(name, ==, "Current theme");
        g_autoptr(GTask) live = g_task_new(NULL, NULL, NULL, NULL);
        g_task_return_pointer(live, g_strdup("Live theme"), g_free);
        on_theme_install_finished(NULL, G_ASYNC_RESULT(live), theme_operation_new(new_window));
        g_clear_pointer(&name, g_free);
        name = tree_get_string("theme/name", "");
        g_assert_cmpstr(name, ==, "Live theme");
        mainwin = NULL;
        gtk_window_destroy(GTK_WINDOW(new_window));
        g_object_unref(new_window);
    }

    /* The displayed text and the staged XML must agree for arbitrary input. */
    {
        struct { const char *input, *expected; } cases[] = {
            { "", "" }, { "!?", "" }, { "nnn", "N" },
            { "!n?dssLliimMcC", "NDSLIMC" }, { "éN💡Dd", "ND" },
            { "n!d?", "ND" }
        };
        GtkWidget *entry = gtk_entry_new();
        g_object_ref_sink(entry);
        g_signal_connect(entry, "changed", G_CALLBACK(on_title_layout_changed), NULL);
        for (guint i = 0; i < G_N_ELEMENTS(cases); ++i) {
            gtk_editable_set_text(GTK_EDITABLE(entry), cases[i].input);
            /* Empty-to-empty does not emit changed, so call it explicitly too. */
            on_title_layout_changed(GTK_EDITABLE(entry), NULL);
            g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(entry)), ==, cases[i].expected);
            g_autofree gchar *stored = tree_get_string("theme/titleLayout", "fallback");
            g_assert_cmpstr(stored, ==, cases[i].expected);
        }
        g_object_unref(entry);
    }

    tree_set_string("theme/name", "Luxe-Test");
    tree_set_int("desktops/number", 3);
    tree_set_bool("theme/keepBorder", TRUE);
    value = tree_get_string("theme/name", "fallback");
    g_assert_cmpstr(value, ==, "Luxe-Test");
    g_clear_pointer(&value, g_free);
    g_assert_cmpint(tree_get_int("desktops/number", 1), ==, 3);
    g_assert_true(tree_get_bool("theme/keepBorder", FALSE));

    g_assert_true(tree_apply());
    g_assert_true(g_file_get_contents(config, &loaded, NULL, &error));
    g_assert_no_error(error);
    g_assert_nonnull(strstr(loaded, "<name>Luxe-Test</name>"));
    g_assert_nonnull(strstr(loaded, "<titleLayout>ND</titleLayout>"));
    g_assert_nonnull(strstr(loaded, "<number>3</number>"));
    g_assert_nonnull(strstr(loaded, "<keepBorder>yes</keepBorder>"));
    g_assert_nonnull(strstr(loaded, "preserve this comment"));
    g_assert_nonnull(strstr(loaded, "<custom data=\"keep\"/>"));
    g_assert_cmpint(g_stat(config, &obc_loaded_stat), ==, 0);
    obc_loaded_stat_valid = TRUE;

    /* A second editor's write is detected rather than silently clobbered. */
    g_assert_true(g_file_set_contents(config,
        "<openbox_config><theme><name>External</name></theme></openbox_config>\n",
        -1, NULL));
    g_assert_false(tree_apply());

    /* A symlink at the configured path is rejected, not followed. */
    {
        g_autofree gchar *real = g_strdup_printf("%s.real", config);
        gchar *contents = NULL;
        g_assert_cmpint(g_rename(config, real), ==, 0);
        g_assert_cmpint(symlink("rc.xml.real", config), ==, 0);
        g_assert_true(g_file_get_contents(real, &contents, NULL, NULL));
        g_clear_pointer(&contents, g_free);
        g_assert_false(tree_apply());
        g_assert_true(g_file_get_contents(real, &contents, NULL, NULL));
        g_clear_pointer(&contents, g_free);
        g_unlink(config);
        g_assert_cmpint(g_rename(real, config), ==, 0);
    }

    test_async_install_after_cleanup();
    obconf_cleanup();
    g_assert_null(xml_i);
    g_assert_null(obc_config_file);
    return 0;
}
