#include "../src/browser-gtk4.c"
#include <glib/gstdio.h>
#include <gio/gdesktopappinfo.h>
static void pump(void) {
    gint64 end = g_get_monotonic_time() + 300000;
    while (g_get_monotonic_time() < end) { while (g_main_context_iteration(NULL, FALSE)); g_usleep(1000); }
}
static void loaded(Tab *tab) {
    gint64 end = g_get_monotonic_time() + 5000000;
    do { pump(); } while (gtk_directory_list_is_loading(tab->directory) && g_get_monotonic_time() < end);
    g_assert_false(gtk_directory_list_is_loading(tab->directory)); g_assert_null(gtk_directory_list_get_error(tab->directory));
}
static void act(Browser *b, const char *name) { g_action_group_activate_action(G_ACTION_GROUP(b->actions), name, NULL); pump(); }
int main(int argc, char **argv) {
    gtk_init();
    char *desktop_path = g_build_filename(g_get_user_data_dir(), "applications/pcmanager-test.desktop", NULL);
    GDesktopAppInfo *testapp = g_desktop_app_info_new_from_filename(desktop_path); g_free(desktop_path);
    g_assert_nonnull(testapp); g_assert_true(g_app_info_set_as_default_for_type(G_APP_INFO(testapp), "text/plain", NULL)); g_object_unref(testapp);
    fm_init(NULL); pcmgr_browser_setup("test", NULL);
    const char *paths[] = { argv[1], NULL }; pcmgr_browser_open(paths, NULL, TRUE); pump();
    Browser *b = browsers->data; Tab *tab = current_tab(b); loaded(tab);
    g_assert_cmpuint(pcmgr_browser_count(), ==, 1);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(tab->sorted)), ==, 3);
    act(b, "hidden"); g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(tab->sorted)), ==, 4);
    act(b, "hidden"); act(b, "list");
    gtk_editable_set_text(GTK_EDITABLE(b->search), "alpha"); pump();
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(tab->sorted)), ==, 1);
    gtk_selection_model_select_item(GTK_SELECTION_MODEL(tab->selection), 0, TRUE); act(b, "copy");
    GFile *target = g_file_get_child(tab->location, "target"); navigate(tab, target, TRUE); loaded(tab);
    act(b, "paste");
    while (b->operation) pump(); loaded(tab);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(tab->sorted)), ==, 1);
    act(b, "back"); loaded(tab); g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(tab->sorted)), ==, 3);
    act(b, "new-tab"); loaded(current_tab(b)); g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(b->notebook)), ==, 2);
    act(b, "close"); pump(); g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(b->notebook)), ==, 1);
    act(b, "icons");
    gtk_editable_set_text(GTK_EDITABLE(b->search), "alpha"); pump();
    gtk_selection_model_select_item(GTK_SELECTION_MODEL(tab->selection), 0, TRUE);
    act(b, "open-with");
    GListModel *windows = gtk_window_get_toplevels(); GtkWidget *chooser = NULL;
    for (guint i=0; i<g_list_model_get_n_items(windows); i++) {
        GtkWidget *w = g_list_model_get_item(windows, i);
        if (GTK_IS_APP_CHOOSER_DIALOG(w)) chooser = w; else g_object_unref(w);
    }
    g_assert_nonnull(chooser);
    GAppInfo *chosen = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(chooser));
    g_assert_nonnull(chosen); g_assert_cmpstr(g_app_info_get_name(chosen), ==, "PCManager Test Opener"); g_object_unref(chosen);
    gtk_dialog_response(GTK_DIALOG(chooser), GTK_RESPONSE_OK); g_object_unref(chooser); pump();
    char *opened = NULL;
    g_assert_true(g_file_get_contents(g_getenv("PCMGR_TEST_OPENED_FILE"), &opened, NULL, NULL));
    g_assert_true(g_str_has_suffix(opened, "/alpha.txt\n")); g_free(opened);
    gtk_editable_set_text(GTK_EDITABLE(b->search), ""); pump();
    /* Rendered UI remains visible briefly for the runner's screenshot. */
    for (int i=0; i<10; i++) pump();
    gtk_window_close(GTK_WINDOW(b)); pump(); g_assert_cmpuint(pcmgr_browser_count(), ==, 0);
    g_object_unref(target); fm_finalize(); g_print("PASS: GTK loading, filtering, selection, clipboard copy/paste, navigation, tabs, views, close lifecycle\n"); return 0;
}
