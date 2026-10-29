/* Integration tests run only in an isolated display/config/state directory. */
#define LUCID_TEST
#define LUCID_SNAPSHOT_QUIESCE_US 100000
#define LUCID_SNAPSHOT_EDIT_DELAY_MS 100
#define LUCID_SNAPSHOT_METADATA_DELAY_MS 200
#define LUCID_SNAPSHOT_RETRY_DELAY_MS 40
#define main lucid_application_main
#include "../src/main.c"
#undef main
#include <glib/gstdio.h>
static void wait_document(Document *d)
{
    gint64 deadline=g_get_monotonic_time()+15*G_TIME_SPAN_SECOND;
    while(d->busy&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_false(d->busy);
}
static char *contents(Document *d){GtkTextIter a,b;gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(d->buffer),&a,&b);return gtk_text_buffer_get_text(GTK_TEXT_BUFFER(d->buffer),&a,&b,TRUE);}
static void sidebar_file_ownership(void)
{
    g_autoptr(GFileInfo) info = g_file_info_new();
    g_autoptr(GFile) parent = g_file_new_for_path("/tmp");
    g_file_info_set_name(info, "lucid-test-file");
    for (int i = 0; i < 100; i++) {
        GFile *weak = sidebar_info_file(info, parent);
        g_assert_nonnull(weak);
        g_object_add_weak_pointer(G_OBJECT(weak), (gpointer *)&weak);
        g_object_unref(weak);
        g_assert_null(weak);
    }
    g_autoptr(GFile) existing = g_file_get_child(parent, "other");
    g_file_info_set_attribute_object(info, "standard::file", G_OBJECT(existing));
    GFile *owned = sidebar_info_file(info, parent);
    g_assert_true(owned == existing);
    g_object_unref(owned);
    g_assert_true(g_file_info_get_attribute_object(info, "standard::file") == G_OBJECT(existing));
}
typedef enum { RECOVERY_ON, RECOVERY_OFF, RECOVERY_MALFORMED, RECOVERY_CONFLICT, RECOVERY_ACTIVATE, RECOVERY_CLEAN } RecoveryMode;
static void startup_recovery(RecoveryMode mode)
{
    gboolean restore = mode == RECOVERY_ON || mode == RECOVERY_CONFLICT || mode == RECOVERY_ACTIVATE || mode == RECOVERY_CLEAN;
    gboolean malformed = mode == RECOVERY_MALFORMED;
    g_autofree char *lost = g_strdup_printf("UNSAVED previous-process recovery text (%d)", mode);
    g_autofree char *state_dir = g_build_filename(g_get_user_state_dir(), "lucid", NULL);
    g_autofree char *recovery = g_build_filename(state_dir, "recovery.session", NULL);
    g_autoptr(GKeyFile) state = g_key_file_new();
    g_key_file_set_integer(state, "Session", "version", 1);
    g_key_file_set_integer(state, "Session", "count", 1);
    g_key_file_set_boolean(state, "Document 0", "modified", TRUE);
    g_key_file_set_string(state, "Document 0", "text", lost);
    g_assert_true(lucid_state_write(state, recovery, NULL));
    if (malformed) g_assert_true(g_file_set_contents(recovery, "broken previous recovery\n", -1, NULL));
    g_autofree char *conflict = NULL;
    if (mode == RECOVERY_CONFLICT) {
        g_autofree char *sessions = g_build_filename(state_dir, "sessions", NULL);
        conflict = lucid_state_preserve(state, sessions, NULL);
        g_assert_nonnull(conflict);
        g_autoptr(GKeyFile) other = lucid_state_read(conflict, NULL);
        g_key_file_set_string(other, "Document 0", "text", "Existing archive must not be replaced");
        g_assert_true(lucid_state_write(other, conflict, NULL));
    }
    g_autoptr(GSettings) settings = g_settings_new("org.luxe.Lucid");
    g_settings_set_boolean(settings, "restore-session", restore);
    g_autofree char *directory = g_dir_make_tmp("lucid-startup-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "requested.txt", NULL);
    g_assert_true(g_file_set_contents(path, "requested file", -1, NULL));
    g_autoptr(GFile) file = g_file_new_for_path(path);
    if (mode == RECOVERY_CLEAN) {
        g_autofree char *uri = g_file_get_uri(file);
        g_key_file_remove_key(state, "Document 0", "text", NULL);
        g_key_file_set_boolean(state, "Document 0", "modified", FALSE);
        g_key_file_set_string(state, "Document 0", "uri", uri);
        g_assert_true(lucid_state_write(state, recovery, NULL));
    }
    GtkApplication *app = luxe_application_new("org.luxe.Lucid.StartupRecoveryTest", G_APPLICATION_NON_UNIQUE);
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    Window window = {0};
    GFile *files[] = {file};
    if (mode == RECOVERY_ACTIVATE) activate(app, &window);
    application_open(G_APPLICATION(app), files, 1, "", &window);
    gboolean visible = FALSE;
    for (int i = 0; i < gtk_notebook_get_n_pages(GTK_NOTEBOOK(window.notebook)); ++i) {
        Document *d = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(window.notebook), i)), "document");
        wait_document(d);
        g_autofree char *text = contents(d);
        if (!strcmp(text, lost)) visible = TRUE;
    }
    g_assert_cmpint(visible, ==, restore && !malformed && mode != RECOVERY_CLEAN);
    g_assert_cmpint(gtk_notebook_get_n_pages(GTK_NOTEBOOK(window.notebook)), ==, visible ? 2 : 1);
    window.last_edit = 0;
    snapshot(&window, TRUE);
    while (window.snapshot_running) g_main_context_iteration(NULL, TRUE);
    if (malformed) {
        g_autofree char *text = NULL;
        g_assert_true(g_file_get_contents(recovery, &text, NULL, NULL));
        g_assert_cmpstr(text, ==, "broken previous recovery\n");
        g_assert_cmpstr(window.session_file, !=, recovery);
    } else if (mode == RECOVERY_CONFLICT) {
        g_assert_cmpstr(window.session_file, !=, recovery);
        g_autoptr(GKeyFile) original = lucid_state_read(recovery, NULL);
        g_autofree char *text = g_key_file_get_string(original, "Document 0", "text", NULL);
        g_assert_cmpstr(text, ==, lost);
        g_autoptr(GKeyFile) other = lucid_state_read(conflict, NULL);
        g_autofree char *other_text = g_key_file_get_string(other, "Document 0", "text", NULL);
        g_assert_cmpstr(other_text, ==, "Existing archive must not be replaced");
    } else if (mode != RECOVERY_CLEAN) {
        gboolean retained = FALSE;
        GDir *dir = g_dir_open(window.sessions_dir, 0, NULL);
        const char *name;
        while (dir && (name = g_dir_read_name(dir))) {
            g_autofree char *saved_path = g_build_filename(window.sessions_dir, name, NULL);
            g_autoptr(GKeyFile) saved = lucid_state_read(saved_path, NULL);
            if (!saved) continue;
            g_autofree char *text = g_key_file_get_string(saved, "Document 0", "text", NULL);
            if (!g_strcmp0(text, lost)) retained = TRUE;
        }
        if (dir) g_dir_close(dir);
        g_assert_true(retained);
    }
    g_autoptr(GKeyFile) current_state = lucid_state_read(window.session_file, NULL);
    g_assert_nonnull(current_state);
    window.closing = TRUE;
    gtk_window_destroy(GTK_WINDOW(window.window));
    while (window.snapshot_running) g_main_context_iteration(NULL, TRUE);
    cleanup_window(&window); g_object_unref(app);
    g_settings_set_boolean(settings, "restore-session", TRUE);
    test_errors = 0;
    g_unlink(path); g_rmdir(directory);
}
static void recovery_case(RecoveryMode mode)
{
    if (g_test_subprocess()) { startup_recovery(mode); return; }
    // Lucid is a single-window process; each startup scenario must receive a
    // fresh GtkApplication registration and independent process state.
    g_test_trap_subprocess(NULL, 15 * G_TIME_SPAN_SECOND, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed();
}
static void recovery_open_on(void) { recovery_case(RECOVERY_ON); }
static void recovery_open_off(void) { recovery_case(RECOVERY_OFF); }
static void recovery_malformed(void) { recovery_case(RECOVERY_MALFORMED); }
static void recovery_conflict(void) { recovery_case(RECOVERY_CONFLICT); }
static void recovery_activate(void) { recovery_case(RECOVERY_ACTIVATE); }
static void recovery_clean(void) { recovery_case(RECOVERY_CLEAN); }
static void answer_close(int response)
{
    for(int n=0;n<10;n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    GtkWindow *dialog=luxe_window_get_visible_dialog(GTK_WINDOW(win.window));
    g_assert_true(GTK_IS_DIALOG(dialog));
    g_object_ref(dialog);
    const char *answers[]={"cancel","discard","save"};
    luxe_choice_respond(GTK_DIALOG(dialog),answers[response]);
    g_object_unref(dialog);
    for(int n=0;n<10;n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
}
static GtkMenuButton *find_menu_button(GtkWidget *widget)
{
    if(GTK_IS_MENU_BUTTON(widget))return GTK_MENU_BUTTON(widget);
    for(GtkWidget *child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child)){
        GtkMenuButton *found=find_menu_button(child);if(found)return found;
    }
    return NULL;
}
static gboolean menu_has_action(GMenuModel *model,const char *wanted)
{
    for(int i=0;i<g_menu_model_get_n_items(model);i++){
        GVariant *action=g_menu_model_get_item_attribute_value(model,i,"action",G_VARIANT_TYPE_STRING);
        gboolean match=action&&g_str_equal(g_variant_get_string(action,NULL),wanted);
        if(action)g_variant_unref(action);
        if(match)return TRUE;
        GMenuModel *section=g_menu_model_get_item_link(model,i,G_MENU_LINK_SECTION);
        if(section){gboolean in_section=menu_has_action(section,wanted);g_object_unref(section);if(in_section)return TRUE;}
    }
    return FALSE;
}
static void header_bar_present(void)
{
    // The header bar carries the toolbar and the main menu (Preferences, About).
    // It is only attached when it is added to the toolbar view before the view
    // becomes the window content; a missing titlebar leaves a bare editor.
    GtkWidget *titlebar=gtk_window_get_titlebar(GTK_WINDOW(win.window));
    g_assert_nonnull(titlebar);
    g_assert_true(GTK_IS_HEADER_BAR(titlebar));
    GtkMenuButton *menu=find_menu_button(titlebar);
    g_assert_nonnull(menu);
    GMenuModel *model=gtk_menu_button_get_menu_model(menu);
    g_assert_nonnull(model);
    g_assert_true(menu_has_action(model,"win.preferences"));
    g_assert_true(menu_has_action(model,"win.about"));
}
static void integration(void)
{
    GtkApplication *app=luxe_application_new("org.luxe.Lucid.Test",G_APPLICATION_NON_UNIQUE);
    GError *error=NULL;g_assert_true(g_application_register(G_APPLICATION(app),NULL,&error));g_assert_no_error(error);
    setup_window(&win,app);
    g_assert_false(gtk_widget_get_visible(win.sidebar));
    g_assert_null(gtk_directory_list_get_file(win.directory));
    g_assert_false(gtk_directory_list_get_monitored(win.directory));
    gtk_window_present(GTK_WINDOW(win.window));
    header_bar_present();
    const char *const *ids=gtk_source_style_scheme_manager_get_scheme_ids(gtk_source_style_scheme_manager_get_default());int count=0;
    for(int i=0;ids[i];i++){if(!g_str_has_prefix(ids[i],"lucid-"))continue;GtkSourceStyleScheme *scheme=gtk_source_style_scheme_manager_get_scheme(gtk_source_style_scheme_manager_get_default(),ids[i]);g_assert_nonnull(scheme);g_assert_nonnull(gtk_source_style_scheme_get_style(scheme,"text"));g_assert_nonnull(gtk_source_style_scheme_get_style(scheme,"def:keyword"));count++;}
    g_assert_cmpint(count,==,22);
    char *dir=g_dir_make_tmp("lucid-editor-XXXXXX",&error);g_assert_no_error(error);char *path=g_build_filename(dir,"café test.c",NULL);
    g_assert_true(g_file_set_contents(path,"int main(void) {\r\n    return 0;\r\n}",-1,&error));g_assert_no_error(error);
    GFile *file=g_file_new_for_path(path);Document *d=load_document(&win,file);wait_document(d);g_assert_cmpuint(test_errors,==,0);
    // The Ubuntu 24.04 libspelling 0.2 adapter must retain the same editor
    // integration as newer libspelling: enable/disable, actions and context menu.
    g_assert_nonnull(d->spelling);
    g_assert_true(G_IS_ACTION_GROUP(d->spelling));
    g_assert_nonnull(spelling_text_buffer_adapter_get_menu_model(d->spelling));
    g_settings_set_boolean(win.settings,"spell-check",FALSE);
    g_assert_false(spelling_text_buffer_adapter_get_enabled(d->spelling));
    g_settings_set_boolean(win.settings,"spell-check",TRUE);
    g_assert_true(spelling_text_buffer_adapter_get_enabled(d->spelling));
    g_assert_cmpstr(gtk_source_language_get_id(gtk_source_buffer_get_language(d->buffer)),==,"c");
    g_assert_cmpint(gtk_source_file_get_newline_type(d->file),==,GTK_SOURCE_NEWLINE_TYPE_CR_LF);
    gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(d->buffer),"// hi\n",-1);save_document(d,FALSE);wait_document(d);g_assert_cmpuint(test_errors,==,0);
    char *disk=NULL;g_file_get_contents(path,&disk,NULL,&error);g_assert_no_error(error);g_assert_cmpstr(disk,==,"// hi\r\nint main(void) {\r\n    return 0;\r\n}");g_free(disk);
    // Save As retains the old file and moves the document to the new path.
    char *other=g_build_filename(dir,"saved.c",NULL);GFile *target=g_file_new_for_path(other);save_to(d,target);wait_document(d);g_assert_cmpuint(test_errors,==,0);g_assert_true(g_file_equal(gtk_source_file_get_location(d->file),target));g_assert_true(g_file_test(path,G_FILE_TEST_EXISTS));
    // External changes must not be silently overwritten.
    gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(d->buffer),"LOCAL",-1);
    g_file_set_contents(other,"EXTERNAL CHANGES",-1,&error);g_assert_no_error(error);
    g_assert_true(g_file_set_attribute_uint64(target,G_FILE_ATTRIBUTE_TIME_MODIFIED,g_get_real_time()/G_USEC_PER_SEC+5,G_FILE_QUERY_INFO_NONE,NULL,&error));g_assert_no_error(error);
    save_document(d,FALSE);wait_document(d);g_assert_cmpuint(test_errors,==,1);
    g_file_get_contents(other,&disk,NULL,&error);g_assert_no_error(error);g_assert_cmpstr(disk,==,"EXTERNAL CHANGES");g_free(disk);g_assert_true(gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(d->buffer)));
    // Session snapshot captures unsaved text. Restore must not retain a writable
    // association with a potentially newer disk file.
    SessionResult *collection=collect_session(&win,"Development");g_assert_false(collection->busy);GKeyFile *state=g_steal_pointer(&collection->state);session_result_free(collection);g_assert_nonnull(state);char *session=lucid_session_path(dir,"Development");g_assert_true(lucid_state_write(state,session,&error));g_assert_no_error(error);g_key_file_unref(state);
    g_autofree char *before=contents(d);restore_session(&win,session);Document *recovered=current(&win);g_assert_true(recovered!=d);g_assert_null(gtk_source_file_get_location(recovered->file));g_autofree char *after=contents(recovered);g_assert_cmpstr(before,==,after);g_assert_true(gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(recovered->buffer)));
    // Search/replace is undoable and the upstream modeline parser still works.
    spelling_text_buffer_adapter_set_enabled(recovered->spelling,FALSE);
    GString *long_modeline=g_string_new("# vim: set ts=2 sw=2 et:");
    while(long_modeline->len<9000)g_string_append_c(long_modeline,'x');
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(recovered->buffer),long_modeline->str,-1);
    gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(recovered->view),4);
    modeline_parser_apply_modeline(GTK_SOURCE_VIEW(recovered->view));
    g_assert_cmpuint(gtk_source_view_get_tab_width(GTK_SOURCE_VIEW(recovered->view)),==,4);
    g_string_prepend(long_modeline,"\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(recovered->buffer),long_modeline->str,-1);
    modeline_parser_apply_modeline(GTK_SOURCE_VIEW(recovered->view));
    g_assert_cmpuint(gtk_source_view_get_tab_width(GTK_SOURCE_VIEW(recovered->view)),==,4);
    g_string_free(long_modeline,TRUE);
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(recovered->buffer),"# vim: set ts=2 sw=2 et:\nhello hello\n",-1);modeline_parser_apply_modeline(GTK_SOURCE_VIEW(recovered->view));g_assert_cmpuint(gtk_source_view_get_tab_width(GTK_SOURCE_VIEW(recovered->view)),==,2);
    gtk_editable_set_text(GTK_EDITABLE(win.find),"hello");gtk_editable_set_text(GTK_EDITABLE(win.replace),"world");replace_text(&win,TRUE);g_autofree char *replaced=contents(recovered);g_assert_nonnull(strstr(replaced,"world world"));gtk_text_buffer_undo(GTK_TEXT_BUFFER(recovered->buffer));g_autofree char *undone=contents(recovered);g_assert_nonnull(strstr(undone,"hello hello"));
    // Desktop-follow is the default; all six palettes change the editor and
    // embedded terminal without replacing the user's stored custom scheme.
    g_assert_true(g_settings_get_boolean(win.settings,"follow-desktop-theme"));
    g_autofree char *saved_scheme=g_settings_get_string(win.settings,"theme");
    const char *palettes[]={"light","dark","nord","mocha","dracula","tokyo-night"};
    GtkSourceStyleSchemeManager *scheme_manager=gtk_source_style_scheme_manager_get_default();
    for(guint i=0;i<G_N_ELEMENTS(palettes);i++){
        g_autofree char *gtk_theme=g_strconcat("Luxe-",palettes[i],NULL);
        g_object_set(gtk_settings_get_default(),"gtk-theme-name",gtk_theme,NULL);
        while(g_main_context_iteration(NULL,FALSE));
        GtkSourceStyleScheme *scheme=scheme_for_palette(scheme_manager,palettes[i]);g_assert_nonnull(scheme);
        g_assert_cmpstr(gtk_source_style_scheme_get_id(gtk_source_buffer_get_style_scheme(recovered->buffer)),==,gtk_source_style_scheme_get_id(scheme));
        g_autofree char *still_saved=g_settings_get_string(win.settings,"theme");
        g_assert_cmpstr(still_saved,==,saved_scheme);
    }
    g_settings_set_boolean(win.settings,"follow-desktop-theme",FALSE);
    // Every bundled custom scheme still applies to a real buffer when opted out.
    for(int i=0;ids[i];i++)if(g_str_has_prefix(ids[i],"lucid-")){g_settings_set_string(win.settings,"theme",ids[i]);g_assert_cmpstr(gtk_source_style_scheme_get_id(gtk_source_buffer_get_style_scheme(recovered->buffer)),==,ids[i]);}
    guint preference_calls_before_font=test_document_preference_calls;
    g_settings_set_string(win.settings,"font","Monospace 13");
    g_assert_cmpuint(test_document_preference_calls,==,preference_calls_before_font);
    g_assert_cmpuint(gtk_source_view_get_tab_width(GTK_SOURCE_VIEW(recovered->view)),==,2);
    g_settings_set_boolean(win.settings,"line-numbers",FALSE);
    g_assert_false(gtk_source_view_get_show_line_numbers(GTK_SOURCE_VIEW(recovered->view)));
    g_assert_cmpuint(gtk_source_view_get_tab_width(GTK_SOURCE_VIEW(recovered->view)),==,2);
    g_settings_set_uint(win.settings,"tab-width",6);
    g_assert_cmpuint(gtk_source_view_get_tab_width(GTK_SOURCE_VIEW(recovered->view)),==,2);
    g_assert_cmpuint(test_errors,==,1);
    // Cancel keeps edits and the tab; Discard closes only the requested tab.
    Document *closing=doc_ref(new_document(&win));
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(closing->buffer),"keep these edits",-1);
    request_close(closing);answer_close(0);g_assert_false(closing->closed);g_assert_true(gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(closing->buffer)));
    request_close(closing);answer_close(1);g_assert_true(closing->closed);doc_unref(closing);
    // Save from the close prompt completes the async write before closing.
    closing=doc_ref(load_document(&win,file));wait_document(closing);
    gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(closing->buffer),"SAVED BEFORE CLOSE\n",-1);
    request_close(closing);answer_close(2);wait_document(closing);g_assert_true(closing->closed);
    g_file_get_contents(path,&disk,NULL,&error);g_assert_no_error(error);g_assert_true(g_str_has_prefix(disk,"SAVED BEFORE CLOSE\r\n"));g_free(disk);doc_unref(closing);
    g_assert_cmpuint(test_errors,==,1);
    // Normal dialogs use Adwaita, not plain GtkWindow.
    preferences(&win);
    for(int n=0;n<20&&!luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    {GtkWindow *dlg=luxe_window_get_visible_dialog(GTK_WINDOW(win.window));g_assert_true(GTK_IS_WINDOW(dlg));g_assert_true(GTK_IS_WINDOW(dlg));luxe_dialog_close(dlg);}
    for(int n=0;n<20&&luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    g_assert_cmpint(g_mkdir_with_parents(win.sessions_dir,0700),==,0);
    for(int i=0;i<300;i++){g_autofree char *name=g_strdup_printf("test-session-%03d.session",i);g_autofree char *saved=g_build_filename(win.sessions_dir,name,NULL);g_assert_true(g_file_set_contents(saved,"[Session]\nversion=1\ncount=0\nactive=0\nname=Fixture\n",-1,&error));g_assert_no_error(error);}
    sessions_dialog(&win);
    for(int n=0;n<20&&!luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    GtkWindow *dlg=luxe_window_get_visible_dialog(GTK_WINDOW(win.window));g_assert_true(GTK_IS_WINDOW(dlg));
    SessionList *session_list=g_object_get_data(G_OBJECT(dlg),"session-list");g_assert_nonnull(session_list);
    g_assert_cmpuint(session_list->paths->len,==,LUCID_SESSION_LIST_MAX);g_assert_true(session_list->truncated);
    gint64 list_deadline=g_get_monotonic_time()+2*G_TIME_SPAN_SECOND;
    while(!session_list->loaded&&g_get_monotonic_time()<list_deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    SessionList *list_hold=session_list_ref(session_list);luxe_dialog_close(dlg);
    g_assert_true(g_cancellable_is_cancelled(list_hold->cancel));
    gint64 settle_deadline=g_get_monotonic_time()+150000;
    while(g_get_monotonic_time()<settle_deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    guint loaded_after_close=list_hold->loaded;g_usleep(50000);while(g_main_context_iteration(NULL,FALSE));g_assert_cmpuint(list_hold->loaded,==,loaded_after_close);session_list_unref(list_hold);
    for(int n=0;n<20&&luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    sessions_dialog(&win);
    for(int n=0;n<20&&!luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    dlg=luxe_window_get_visible_dialog(GTK_WINDOW(win.window));g_assert_true(GTK_IS_WINDOW(dlg));
    GtkWidget *session_entry=g_object_get_data(G_OBJECT(dlg),"entry");gtk_editable_set_text(GTK_EDITABLE(session_entry),"Async save fixture");
    g_autofree char *failed_save_path=lucid_session_path(win.sessions_dir,"Async save fixture");g_assert_cmpint(g_mkdir(failed_save_path,0700),==,0);
    guint errors_before_save=test_errors;save_session_clicked(NULL,G_OBJECT(dlg));
    gint64 save_deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while(win.named_save_running&&g_get_monotonic_time()<save_deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_false(win.named_save_running);g_assert_cmpuint(test_errors,==,errors_before_save+1);g_assert_true(luxe_window_get_visible_dialog(GTK_WINDOW(win.window))==dlg);
    g_assert_cmpint(g_rmdir(failed_save_path),==,0);save_session_clicked(NULL,G_OBJECT(dlg));save_deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while(win.named_save_running&&g_get_monotonic_time()<save_deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_false(win.named_save_running);g_assert_true(g_file_test(failed_save_path,G_FILE_TEST_IS_REGULAR));
    for(int n=0;n<20&&luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    g_assert_null(luxe_window_get_visible_dialog(GTK_WINDOW(win.window)));
    g_remove(failed_save_path);
    for(int i=0;i<300;i++){g_autofree char *name=g_strdup_printf("test-session-%03d.session",i);g_autofree char *saved=g_build_filename(win.sessions_dir,name,NULL);g_assert_cmpint(g_remove(saved),==,0);}
    Document *lang_doc=current(&win);g_assert_nonnull(lang_doc);
    choose_language(lang_doc);
    for(int n=0;n<20&&!luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    {GtkWindow *dlg=luxe_window_get_visible_dialog(GTK_WINDOW(win.window));g_assert_true(GTK_IS_WINDOW(dlg));luxe_dialog_close(dlg);}
    for(int n=0;n<20&&luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    go_line(lang_doc);
    for(int n=0;n<20&&!luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    {GtkWindow *dlg=luxe_window_get_visible_dialog(GTK_WINDOW(win.window));g_assert_true(GTK_IS_WINDOW(dlg));luxe_dialog_close(dlg);}
    for(int n=0;n<20&&luxe_window_get_visible_dialog(GTK_WINDOW(win.window));n++){while(g_main_context_iteration(NULL,FALSE));g_usleep(10000);}
    // Sidebar lists real files and activates them.
    {char *sub=g_build_filename(dir,"sidebar",NULL);g_mkdir_with_parents(sub,0700);
    char *inner=g_build_filename(sub,"inner.txt",NULL);g_assert_true(g_file_set_contents(inner,"sidebar\n",-1,&error));g_assert_no_error(error);
    GFile *folder=g_file_new_for_path(sub);set_folder(&win,folder);
    g_assert_null(gtk_directory_list_get_file(win.directory));
    guint preference_calls=test_document_preference_calls;
    g_settings_set_boolean(win.settings,"sidebar",TRUE);
    g_assert_cmpuint(test_document_preference_calls,==,preference_calls);
    g_assert_true(gtk_directory_list_get_monitored(win.directory));
    g_assert_true(g_file_equal(gtk_directory_list_get_file(win.directory),folder));
    gint64 deadline=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;guint n_items=0;
    while(g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));if(g_list_model_get_n_items(G_LIST_MODEL(win.directory))>0){n_items=g_list_model_get_n_items(G_LIST_MODEL(win.directory));break;}g_usleep(10000);}
    g_assert_cmpuint(n_items,>,0);
    // GtkDirectoryList must expose standard::file for activation.
    {g_autofree char *attrs=NULL;g_object_get(win.directory,"attributes",&attrs,NULL);g_assert_nonnull(attrs);g_assert_nonnull(strstr(attrs,"standard::file"));}
    g_settings_set_boolean(win.settings,"sidebar",FALSE);
    g_assert_null(gtk_directory_list_get_file(win.directory));
    g_assert_false(gtk_directory_list_get_monitored(win.directory));
    char *hidden_change=g_build_filename(sub,"created-while-hidden.txt",NULL);
    g_assert_true(g_file_set_contents(hidden_change,"new",-1,NULL));
    g_settings_set_boolean(win.settings,"sidebar",TRUE);
    g_assert_true(g_file_equal(gtk_directory_list_get_file(win.directory),folder));
    gboolean found_hidden_change=FALSE;deadline=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;
    while(!found_hidden_change&&g_get_monotonic_time()<deadline){
        while(g_main_context_iteration(NULL,FALSE));
        for(guint i=0;i<g_list_model_get_n_items(G_LIST_MODEL(win.directory));i++){
            GFileInfo *info=g_list_model_get_item(G_LIST_MODEL(win.directory),i);
            if(g_strcmp0(g_file_info_get_name(info),"created-while-hidden.txt")==0)found_hidden_change=TRUE;
            g_object_unref(info);
        }
        g_usleep(10000);
    }
    g_assert_true(found_hidden_change);
    g_settings_set_boolean(win.settings,"sidebar",FALSE);g_object_unref(folder);
    g_assert_cmpuint(test_document_preference_calls,==,preference_calls);
    g_assert_cmpint(g_unlink(hidden_change),==,0);g_free(hidden_change);
    g_free(sub);g_free(inner);}
    // Oversized dirty buffers are reported, not silently dropped.
    {Document *big=doc_ref(new_document(&win));
    gtk_source_file_set_location(big->file,file);
    spelling_text_buffer_adapter_set_enabled(big->spelling,FALSE);
    GString *huge=g_string_new(NULL);for(int i=0;i<300000;i++)g_string_append_c(huge,'x');
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(big->buffer),huge->str,-1);g_string_free(huge,TRUE);
    SessionResult *big_result=collect_session(&win,"Big");g_assert_nonnull(big_result->state);g_assert_cmpuint(big_result->omissions->len,==,1);
    g_autofree char *big_message=session_omission_message(big_result->omissions);g_assert_nonnull(strstr(big_message,"per-document recovery limit"));g_assert_nonnull(strstr(big_message,"only the file location is saved"));
    session_result_free(big_result);
    while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    /* A limited snapshot must preserve the prior recoverable text before
     * replacing the single recovery file with a location-only entry. */
    g_autoptr(GKeyFile) prior=g_key_file_new();
    g_key_file_set_integer(prior,"Session","version",1);g_key_file_set_integer(prior,"Session","count",1);
    g_key_file_set_boolean(prior,"Document 0","modified",TRUE);g_key_file_set_string(prior,"Document 0","text","PRIOR RECOVERABLE TEXT");
    g_assert_true(lucid_state_write(prior,win.session_file,&error));g_assert_no_error(error);
    snapshot(&win,TRUE);g_assert_true(gtk_widget_get_visible(win.recovery_warning));
    while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    gboolean prior_preserved=FALSE;GDir *preserved_dir=g_dir_open(win.sessions_dir,0,NULL);const char *preserved_name;
    while(preserved_dir&&(preserved_name=g_dir_read_name(preserved_dir))){g_autofree char *preserved_path=g_build_filename(win.sessions_dir,preserved_name,NULL);g_autoptr(GKeyFile) saved=lucid_state_read(preserved_path,NULL);if(saved){g_autofree char *saved_text=g_key_file_get_string(saved,"Document 0","text",NULL);if(g_strcmp0(saved_text,"PRIOR RECOVERABLE TEXT")==0)prior_preserved=TRUE;}}
    if(preserved_dir)g_dir_close(preserved_dir);g_assert_true(prior_preserved);
    mark_edited(&win);snapshot(&win,TRUE);while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    g_autofree char *bounded_backup=g_build_filename(win.sessions_dir,"recovery-before-limited.session",NULL);
    g_assert_true(g_file_test(bounded_backup,G_FILE_TEST_IS_REGULAR));
    remove_document(big);doc_unref(big);
    char *valid_recovery=win.session_file;win.session_file=g_strdup(dir); /* A directory cannot be the recovery file. */
    snapshot(&win,TRUE);while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    g_assert_true(gtk_widget_get_visible(win.recovery_warning));
    g_free(win.session_file);win.session_file=valid_recovery;
    snapshot(&win,TRUE);while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    g_assert_false(gtk_widget_get_visible(win.recovery_warning));}
    // Aggregate limits name each affected untitled tab and reason.
    {GPtrArray *large_docs=g_ptr_array_new();g_autofree char *text=g_strnfill(250000,'y');
    for(int i=0;i<5;i++){Document *d=doc_ref(new_document(&win));spelling_text_buffer_adapter_set_enabled(d->spelling,FALSE);gtk_text_buffer_set_text(GTK_TEXT_BUFFER(d->buffer),text,-1);g_ptr_array_add(large_docs,d);}
    SessionResult *aggregate=collect_session(&win,"Aggregate");g_assert_nonnull(aggregate->state);g_assert_cmpuint(aggregate->omissions->len,==,1);
    g_autofree char *aggregate_message=session_omission_message(aggregate->omissions);g_assert_nonnull(strstr(aggregate_message,"total recovery limit"));g_assert_nonnull(strstr(aggregate_message,"untitled contents are omitted"));session_result_free(aggregate);
    for(guint i=0;i<large_docs->len;i++){Document *d=g_ptr_array_index(large_docs,i);remove_document(d);doc_unref(d);}g_ptr_array_unref(large_docs);snapshot(&win,TRUE);while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);}
    /* Settled sessions have no recurring timer; metadata stays pending until
     * content changes or an explicit close/save snapshot. */
    g_assert_cmpuint(win.snapshot_timer,==,0);
    mark_dirty(&win);g_assert_cmpuint(win.snapshot_timer,==,0);
    for(int i=0;i<20;i++)mark_dirty(&win);
    g_assert_cmpuint(win.snapshot_timer,==,0);
    Document *cursor_doc=current(&win);g_assert_nonnull(cursor_doc);win.session_dirty=FALSE;guint cursor_generation=win.snapshot_generation;
    GtkTextIter cursor_pos;gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(cursor_doc->buffer),&cursor_pos);
    cursor_changed(GTK_TEXT_BUFFER(cursor_doc->buffer),&cursor_pos,gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(cursor_doc->buffer)),cursor_doc);
    g_assert_false(win.session_dirty);g_assert_cmpuint(win.snapshot_generation,==,cursor_generation);g_assert_cmpuint(win.snapshot_timer,==,0);
    mark_edited(&win);g_assert_cmpuint(win.snapshot_timer,>,0);
    snapshot(&win,TRUE);while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    g_assert_cmpuint(win.snapshot_timer,==,0);
    mark_edited(&win);
    gint64 timer_deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while((win.session_dirty||win.snapshot_running||win.snapshot_timer)&&g_get_monotonic_time()<timer_deadline){
        while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);
    }
    g_assert_false(win.session_dirty);g_assert_false(win.snapshot_running);g_assert_cmpuint(win.snapshot_timer,==,0);
    Document *busy_doc=current(&win);g_assert_nonnull(busy_doc);busy_doc->busy=TRUE;
    mark_edited(&win);gint64 busy_until=g_get_monotonic_time()+250000;
    while(g_get_monotonic_time()<busy_until){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_true(win.session_dirty);g_assert_cmpuint(win.snapshot_timer,>,0);
    SessionResult *busy_result=collect_session(&win,"Busy");g_assert_true(busy_result->busy);g_assert_null(busy_result->state);session_result_free(busy_result);
    busy_doc->busy=FALSE;timer_deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while((win.session_dirty||win.snapshot_running||win.snapshot_timer)&&g_get_monotonic_time()<timer_deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_false(win.session_dirty);g_assert_cmpuint(win.snapshot_timer,==,0);
    /* More than 100 eligible tabs must report a limit, not silently drop the rest. */
    {GPtrArray *extra=g_ptr_array_new();
    for(int i=0;i<101;i++){Document *d=doc_ref(new_document(&win));spelling_text_buffer_adapter_set_enabled(d->spelling,FALSE);gtk_text_buffer_set_text(GTK_TEXT_BUFFER(d->buffer),"unsaved",-1);g_ptr_array_add(extra,d);}
    SessionResult *many=collect_session(&win,"Many");
    g_assert_nonnull(many->state);g_assert_cmpuint(many->omissions->len,==,3);g_assert_cmpint(g_key_file_get_integer(many->state,"Session","count",NULL),==,100);session_result_free(many);
    snapshot(&win,TRUE);g_assert_true(gtk_widget_get_visible(win.recovery_warning));g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(win.recovery_warning)),"tab limit reached"));
    while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    for(guint i=0;i<extra->len;i++){Document *d=g_ptr_array_index(extra,i);remove_document(d);doc_unref(d);}
    g_ptr_array_unref(extra);
    snapshot(&win,TRUE);while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    g_assert_false(gtk_widget_get_visible(win.recovery_warning));}
    g_remove(session);g_remove(path);g_remove(other);g_rmdir(dir);g_free(session);g_free(path);g_free(other);g_free(dir);g_object_unref(file);g_object_unref(target);
    win.closing=TRUE;gtk_window_destroy(GTK_WINDOW(win.window));
    while(win.snapshot_running)g_main_context_iteration(NULL,TRUE);
    cleanup_window(&win);g_object_unref(app);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);gtk_init();luxe_ui_init();gtk_source_init();g_test_add_func("/editor/sidebar-file-ownership",sidebar_file_ownership);g_test_add_func("/editor/recovery-open-on",recovery_open_on);g_test_add_func("/editor/recovery-open-off",recovery_open_off);g_test_add_func("/editor/recovery-malformed",recovery_malformed);g_test_add_func("/editor/recovery-conflict",recovery_conflict);g_test_add_func("/editor/recovery-activate",recovery_activate);g_test_add_func("/editor/recovery-clean",recovery_clean);g_test_add_func("/editor/integration",integration);return g_test_run();}
