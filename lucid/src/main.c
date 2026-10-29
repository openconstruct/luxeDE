#include "../luxe-about.h"
/* Lucid GTK4 frontend for the gedit 48.1 fork.
 * Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 * The modeline parser is adapted from gedit; this frontend replaces its
 * GTK3/Tepl UI and integrates the selected plugin functions directly.
 */
#include "config.h"
#include "state.h"
#include "modeline-parser.h"
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <gtksourceview/gtksource.h>
#include <vte/vte.h>
#include <libspelling.h>
#include <math.h>
#include <string.h>

typedef struct Window Window;
typedef struct Document Document;
typedef struct SessionResult SessionResult;
struct SessionResult {
    GKeyFile *state;
    GPtrArray *omissions;
    gboolean busy;
};
struct Document {
    gint refs;
    Window *w;
    GtkSourceBuffer *buffer;
    GtkSourceFile *file;
    GtkWidget *view, *page, *label;
    GtkSourceSearchContext *search, *highlight;
    SpellingTextBufferAdapter *spelling;
    GCancellable *cancel;
    GFile *save_target;
    char *title;
    gboolean busy, closed, close_after_save, close_prompt;
    guint modeline_timer;
    int restore_line, restore_column;
};
struct Window {
    GtkApplication *app;
    GtkWidget *window, *notebook, *status, *recovery_warning, *searchbar, *find, *replace, *matches;
    GtkWidget *sidebar, *folder_entry, *file_list, *terminal_box, *terminal, *terminal_status;
    GtkWidget *paned, *vertical;
    GtkDirectoryList *directory;
    GFile *sidebar_folder;
    GSettings *settings;
    GtkSourceSearchSettings *search_settings;
    GtkCssProvider *font_css;
    char *data_dir, *state_dir, *sessions_dir, *session_file;
    gboolean closing, quit_pending, restoring, terminal_started, session_dirty;
    gboolean snapshot_again, quit_when_saved, named_save_running;
    GCancellable *window_cancel;
    guint snapshot_timer, snapshot_generation, snapshot_running;
    gint64 last_edit;
};
static Window win;
#ifdef LUCID_TEST
static guint test_errors;
static guint test_document_preference_calls;
#endif
/* Live per-cursor occurrence highlighting is skipped for very large files. */
#define LUCID_OCCURRENCE_MAX_CHARS 500000
/* Recovery snapshots: keystroke-quiescence debounce plus size/count bounds. */
#ifndef LUCID_SNAPSHOT_QUIESCE_US
#define LUCID_SNAPSHOT_QUIESCE_US (6 * G_TIME_SPAN_SECOND)
#endif
#ifndef LUCID_SNAPSHOT_EDIT_DELAY_MS
#define LUCID_SNAPSHOT_EDIT_DELAY_MS 6000
#endif
#ifndef LUCID_SNAPSHOT_METADATA_DELAY_MS
#define LUCID_SNAPSHOT_METADATA_DELAY_MS 30000
#endif
#ifndef LUCID_SNAPSHOT_RETRY_DELAY_MS
#define LUCID_SNAPSHOT_RETRY_DELAY_MS 3000
#endif
#define LUCID_SNAPSHOT_MAX_DOCS 100
#define LUCID_SNAPSHOT_MAX_CHARS_PER_DOC 262144
#define LUCID_SNAPSHOT_MAX_BYTES_TOTAL (1024 * 1024)
static Document *current(Window *w);
static void update_status(Window *w);
static void apply_preferences(Window *w);
static void snapshot(Window *w,gboolean force);
static gboolean snapshot_tick(gpointer data);
static void open_file(Window *w, GFile *file);
static Document *new_document(Window *w);
static void request_close(Document *doc);
static void quit_next(Window *w);
static void save_document(Document *doc, gboolean save_as);
static void set_folder(Window *w, GFile *folder);
static void pin_dialog_folder(Window *w, GtkFileDialog *dialog, GFile *file);
static void restore_session(Window *w, const char *path);
static void finish_close(Window *w);
static SessionResult *collect_session(Window *w,const char *name);
static void session_result_free(SessionResult *result);
static Document *doc_ref(Document *d) { g_atomic_int_inc(&d->refs); return d; }
static void doc_unref(Document *d)
{
    if (!g_atomic_int_dec_and_test(&d->refs)) return;
    if(d->modeline_timer)g_source_remove(d->modeline_timer);
    g_clear_object(&d->spelling); g_clear_object(&d->highlight); g_clear_object(&d->search);
    g_clear_object(&d->cancel); g_clear_object(&d->file); g_clear_object(&d->save_target);
    g_clear_object(&d->view); g_clear_object(&d->buffer); g_free(d->title); g_free(d);
}
static void error_message(Window *w, const char *title, const char *message)
{
    if (w->closing) return;
#ifdef LUCID_TEST
    test_errors++;g_test_message("%s: %s",title,message);return;
#endif
    GtkWindow *dialog = luxe_choice_dialog_new(title, message);
    luxe_choice_add_response(GTK_DIALOG(dialog), "close", "Close");
    luxe_dialog_present(dialog, w->window);
}
static void margins(GtkWidget *widget, int n)
{
    gtk_widget_set_margin_start(widget,n);gtk_widget_set_margin_end(widget,n);
    gtk_widget_set_margin_top(widget,n);gtk_widget_set_margin_bottom(widget,n);
}
static GtkWidget *button(const char *icon, const char *tooltip, const char *action)
{
    GtkWidget *b=gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(b,tooltip);
    if(action)gtk_actionable_set_action_name(GTK_ACTIONABLE(b),action);
    return b;
}
static Document *current(Window *w)
{
    int n=gtk_notebook_get_current_page(GTK_NOTEBOOK(w->notebook));
    GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),n);
    return page?g_object_get_data(G_OBJECT(page),"document"):NULL;
}
static char *document_name(Document *d)
{
    GFile *location=gtk_source_file_get_location(d->file);
    return location?g_file_get_basename(location):g_strdup(d->title?d->title:"Untitled");
}
static void update_label(Document *d)
{
    if(d->closed)return;
    g_autofree char *name=document_name(d);
    g_autofree char *label=g_strdup_printf("%s%s%s",gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(d->buffer))?"• ":"",name,d->busy?" …":"");
    if(g_strcmp0(gtk_label_get_text(GTK_LABEL(d->label)),label)!=0)gtk_label_set_text(GTK_LABEL(d->label),label);
    GFile *file=gtk_source_file_get_location(d->file);
    g_autofree char *uri=file?g_file_get_parse_name(file):g_strdup(name);
    gtk_widget_set_tooltip_text(d->label,uri);
    if(current(d->w)==d){g_autofree char *title=g_strconcat(label," — Lucid",NULL);gtk_window_set_title(GTK_WINDOW(d->w->window),title);}
}
static void update_status(Window *w)
{
    Document *d=current(w);if(!d||d->closed)return;
    GtkTextIter pos;gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(d->buffer),&pos,gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(d->buffer)));
    GtkSourceLanguage *lang=gtk_source_buffer_get_language(d->buffer);
    const GtkSourceEncoding *encoding=gtk_source_file_get_encoding(d->file);
    const char *newline=gtk_source_file_get_newline_type(d->file)==GTK_SOURCE_NEWLINE_TYPE_CR_LF?"CRLF":gtk_source_file_get_newline_type(d->file)==GTK_SOURCE_NEWLINE_TYPE_CR?"CR":"LF";
    g_autofree char *text=g_strdup_printf("Ln %d, Col %d    %s    %s · %s%s",gtk_text_iter_get_line(&pos)+1,gtk_source_view_get_visual_column(GTK_SOURCE_VIEW(d->view),&pos)+1,lang?gtk_source_language_get_name(lang):"Plain Text",encoding?gtk_source_encoding_get_charset(encoding):"UTF-8",newline,d->busy?"    Working…":"");
    if(g_strcmp0(gtk_label_get_text(GTK_LABEL(w->status)),text)!=0)gtk_label_set_text(GTK_LABEL(w->status),text);
    update_label(d);
    int count=gtk_source_search_context_get_occurrences_count(d->search);
    g_autofree char *result=count<0?g_strdup("Searching…"):g_strdup_printf("%d matches",count);
    if(g_strcmp0(gtk_label_get_text(GTK_LABEL(w->matches)),result)!=0)gtk_label_set_text(GTK_LABEL(w->matches),result);
}
static void arm_snapshot(Window *w,guint delay_ms)
{
    if(!w->closing&&!w->restoring&&!w->snapshot_timer)
        w->snapshot_timer=g_timeout_add(delay_ms,snapshot_tick,w);
}
/* Tab order, active tab and caret positions are metadata. Keep them dirty for
 * the next content snapshot or explicit close/save, but never wake an idle
 * editor just to reread every modified buffer. */
static void mark_dirty(Window *w){w->session_dirty=TRUE;w->snapshot_generation++;}
static void mark_edited(Window *w){
    mark_dirty(w);w->last_edit=g_get_monotonic_time();
    if(!w->closing&&!w->restoring){
        if(w->snapshot_timer){g_source_remove(w->snapshot_timer);w->snapshot_timer=0;}
        arm_snapshot(w,LUCID_SNAPSHOT_EDIT_DELAY_MS);
    }
}
static void modified(GtkTextBuffer *buffer,Document *d){(void)buffer;mark_edited(d->w);update_label(d);}
static gboolean modeline_timeout(gpointer data)
{
    Document *d=data;d->modeline_timer=0;
    if(!d->closed)modeline_parser_apply_modeline(GTK_SOURCE_VIEW(d->view));
    return G_SOURCE_REMOVE;
}
static void text_changed(GtkTextBuffer *buffer,Document *d)
{
    (void)buffer;mark_edited(d->w);
    if(!d->busy&&!d->closed&&!d->modeline_timer)d->modeline_timer=g_timeout_add(180,modeline_timeout,d);
}
static void cursor_changed(GtkTextBuffer *buffer,GtkTextIter *iter,GtkTextMark *mark,Document *d)
{
    (void)iter;
    if(d->closed || (mark!=gtk_text_buffer_get_insert(buffer) && mark!=gtk_text_buffer_get_selection_bound(buffer)))return;
    update_status(d->w);
    GtkTextIter a,b;
    GtkSourceSearchSettings *settings=gtk_source_search_context_get_settings(d->highlight);
    /* Live occurrence highlighting is skipped (silently) for very large
     * files, where a per-keystroke search would stall the UI. */
    if(!gtk_widget_get_visible(d->w->searchbar)&&gtk_text_buffer_get_char_count(buffer)<=LUCID_OCCURRENCE_MAX_CHARS&&gtk_text_buffer_get_selection_bounds(buffer,&a,&b)&&gtk_text_iter_get_line(&a)==gtk_text_iter_get_line(&b)&&gtk_text_iter_get_offset(&b)-gtk_text_iter_get_offset(&a)<200){
        g_autofree char *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE);
        gtk_source_search_settings_set_search_text(settings,text);
    }else gtk_source_search_settings_set_search_text(settings,NULL);
}
static void search_count(GObject *o,GParamSpec *p,Document *d){(void)o;(void)p;if(!d->closed)update_status(d->w);}
static GtkSourceStyleScheme *scheme_for_palette(GtkSourceStyleSchemeManager *manager,const char *palette)
{
    if(!palette||!*palette)return NULL;
    const char *const *ids=gtk_source_style_scheme_manager_get_scheme_ids(manager);
    for(guint i=0;ids&&ids[i];i++){
        GtkSourceStyleScheme *scheme=gtk_source_style_scheme_manager_get_scheme(manager,ids[i]);
        if(scheme&&g_strcmp0(gtk_source_style_scheme_get_metadata(scheme,"luxe-palette"),palette)==0)return scheme;
    }
    return NULL;
}
static GtkSourceStyleScheme *effective_scheme(Window *w)
{
    GtkSourceStyleSchemeManager *manager=gtk_source_style_scheme_manager_get_default();
    g_autofree char *saved=g_settings_get_string(w->settings,"theme");
    const char *id=saved;
    if(g_settings_get_boolean(w->settings,"follow-desktop-theme")){
        g_autofree char *gtk_theme=NULL;
        g_object_get(gtk_settings_get_default(),"gtk-theme-name",&gtk_theme,NULL);
        if(gtk_theme&&g_str_has_prefix(gtk_theme,"Luxe-")){
            GtkSourceStyleScheme *matched=scheme_for_palette(manager,gtk_theme+5);
            if(matched)return matched;
        }
        gboolean dark=FALSE;
        g_object_get(gtk_settings_get_default(),"gtk-application-prefer-dark-theme",&dark,NULL);
        id=dark?"Adwaita-dark":"Adwaita";
    }
    GtkSourceStyleScheme *scheme=gtk_source_style_scheme_manager_get_scheme(manager,id);
    if(!scheme)scheme=gtk_source_style_scheme_manager_get_scheme(manager,"Adwaita");
    return scheme;
}
static void apply_document_preference(Document *d,const char *key)
{
#ifdef LUCID_TEST
    test_document_preference_calls++;
#endif
    Window *w=d->w;GtkSourceView *v=GTK_SOURCE_VIEW(d->view);
    if(!key||g_str_equal(key,"theme")||g_str_equal(key,"follow-desktop-theme"))
        gtk_source_buffer_set_style_scheme(d->buffer,effective_scheme(w));
    if(!key||g_str_equal(key,"line-numbers"))gtk_source_view_set_show_line_numbers(v,g_settings_get_boolean(w->settings,"line-numbers"));
    if(!key||g_str_equal(key,"tab-width"))gtk_source_view_set_tab_width(v,g_settings_get_uint(w->settings,"tab-width"));
    if(!key||g_str_equal(key,"insert-spaces"))gtk_source_view_set_insert_spaces_instead_of_tabs(v,g_settings_get_boolean(w->settings,"insert-spaces"));
    if(!key||g_str_equal(key,"show-right-margin"))gtk_source_view_set_show_right_margin(v,g_settings_get_boolean(w->settings,"show-right-margin"));
    if(!key||g_str_equal(key,"right-margin-position"))gtk_source_view_set_right_margin_position(v,g_settings_get_uint(w->settings,"right-margin-position"));
    if(!key||g_str_equal(key,"wrap-mode"))gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(v),g_settings_get_enum(w->settings,"wrap-mode"));
    if(!key||g_str_equal(key,"spell-check"))spelling_text_buffer_adapter_set_enabled(d->spelling,g_settings_get_boolean(w->settings,"spell-check"));
    if(key&&(g_str_equal(key,"tab-width")||g_str_equal(key,"insert-spaces")))modeline_parser_apply_modeline(v);
    if(!key){gtk_source_view_set_auto_indent(v,TRUE);gtk_source_view_set_indent_on_tab(v,TRUE);gtk_source_view_set_highlight_current_line(v,TRUE);}
}
static void apply_document_preferences(Document *d){apply_document_preference(d,NULL);}
static void close_clicked(GtkButton *b,Document *d){(void)b;request_close(d);}
static Document *new_document(Window *w)
{
    Document *d=g_new0(Document,1);d->refs=1;d->w=w;mark_dirty(w);
    d->buffer=gtk_source_buffer_new(NULL);d->file=gtk_source_file_new();d->cancel=g_cancellable_new();
    gtk_source_buffer_set_implicit_trailing_newline(d->buffer,FALSE);
    d->view=g_object_ref_sink(gtk_source_view_new_with_buffer(d->buffer));
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(d->view),10);gtk_text_view_set_right_margin(GTK_TEXT_VIEW(d->view),10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(d->view),8);gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(d->view),8);
    gtk_widget_add_css_class(d->view,"lucid-editor");
    d->search=gtk_source_search_context_new(d->buffer,w->search_settings);
    GtkSourceSearchSettings *hs=gtk_source_search_settings_new();
    gtk_source_search_settings_set_case_sensitive(hs,TRUE);
    d->highlight=gtk_source_search_context_new(d->buffer,hs);g_object_unref(hs);
    d->spelling=spelling_text_buffer_adapter_new(d->buffer,spelling_checker_get_default());
    gtk_widget_insert_action_group(d->view,"spelling",G_ACTION_GROUP(d->spelling));
    gtk_text_view_set_extra_menu(GTK_TEXT_VIEW(d->view),spelling_text_buffer_adapter_get_menu_model(d->spelling));
    d->page=gtk_scrolled_window_new();gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(d->page),d->view);
    GtkWidget *tab=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);d->label=gtk_label_new("Untitled");
    gtk_label_set_ellipsize(GTK_LABEL(d->label),PANGO_ELLIPSIZE_END);gtk_label_set_max_width_chars(GTK_LABEL(d->label),28);gtk_label_set_width_chars(GTK_LABEL(d->label),16);
    GtkWidget *close=button("window-close-symbolic","Close document",NULL);gtk_widget_add_css_class(close,"flat");
    gtk_box_append(GTK_BOX(tab),d->label);gtk_box_append(GTK_BOX(tab),close);
    g_object_set_data_full(G_OBJECT(d->page),"document",d,(GDestroyNotify)doc_unref);
    int n=gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook),d->page,tab);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(w->notebook),d->page,TRUE);
    g_signal_connect(close,"clicked",G_CALLBACK(close_clicked),d);
    g_signal_connect(d->buffer,"modified-changed",G_CALLBACK(modified),d);
    g_signal_connect(d->buffer,"changed",G_CALLBACK(text_changed),d);
    g_signal_connect(d->buffer,"mark-set",G_CALLBACK(cursor_changed),d);
    g_signal_connect(d->search,"notify::occurrences-count",G_CALLBACK(search_count),d);
    apply_document_preferences(d);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook),n);
    gtk_widget_grab_focus(d->view);update_status(w);return d;
}
static void guess_language(Document *d)
{
    GFile *file=gtk_source_file_get_location(d->file);
    g_autofree char *name=file?g_file_get_basename(file):NULL;
    GtkTextIter a,b;gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(d->buffer),&a);b=a;gtk_text_iter_forward_chars(&b,4096);
    g_autofree char *sample=gtk_text_buffer_get_text(GTK_TEXT_BUFFER(d->buffer),&a,&b,TRUE);
    gboolean uncertain;
    g_autofree char *type=g_content_type_guess(name,(const guchar*)sample,strlen(sample),&uncertain);
    GtkSourceLanguage *language=gtk_source_language_manager_guess_language(gtk_source_language_manager_get_default(),name,type);
    gtk_source_buffer_set_language(d->buffer,language);
    modeline_parser_apply_modeline(GTK_SOURCE_VIEW(d->view));
}
static void set_busy(Document *d,gboolean busy)
{
    d->busy=busy;if(d->closed)return;
    gtk_text_view_set_editable(GTK_TEXT_VIEW(d->view),!busy);update_status(d->w);
}
static void loaded(GObject *loader,GAsyncResult *result,gpointer data)
{
    Document *d=data;GError *error=NULL;
    gboolean ok=gtk_source_file_loader_load_finish(GTK_SOURCE_FILE_LOADER(loader),result,&error);
    if(!d->closed){
        set_busy(d,FALSE);
        if(ok){
            guess_language(d);
            GtkTextIter pos;gtk_text_buffer_get_iter_at_line_offset(GTK_TEXT_BUFFER(d->buffer),&pos,MIN(d->restore_line,gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(d->buffer))-1),0);
            int len=gtk_text_iter_get_chars_in_line(&pos);gtk_text_iter_set_line_offset(&pos,MIN(d->restore_column,MAX(0,len-1)));
            gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(d->buffer),&pos);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(d->view),&pos,0.1,FALSE,0,0);
        }else{
            gtk_source_file_set_location(d->file,NULL);
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(d->buffer),"",0);gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(d->buffer),FALSE);
            /* Refresh the failed tab itself: when it is in the background,
             * update_status() only refreshes the current tab, leaving a stale
             * filename/busy label behind. */
            update_label(d);
            error_message(d->w,"Could not open file",error->message);
        }
        update_status(d->w);
    }
    g_clear_error(&error);g_object_unref(loader);doc_unref(d);
}
static Document *load_document(Window *w,GFile *file)
{
    Document *d=new_document(w);gtk_source_file_set_location(d->file,file);
    GtkSourceFileLoader *loader=gtk_source_file_loader_new(d->buffer,d->file);
    set_busy(d,TRUE);gtk_source_file_loader_load_async(loader,G_PRIORITY_DEFAULT,d->cancel,NULL,NULL,NULL,loaded,doc_ref(d));
    return d;
}
static void open_file(Window *w,GFile *file)
{
    for(int i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));i++){
        GtkWidget *p=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),i);Document *d=g_object_get_data(G_OBJECT(p),"document");
        GFile *f=gtk_source_file_get_location(d->file);
        if(f&&g_file_equal(f,file)){gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook),i);return;}
    }
    load_document(w,file);
}
static void open_selected(GObject *dialog,GAsyncResult *res,gpointer data)
{
    Window *w=data;GError *error=NULL;GListModel *files=gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(dialog),res,&error);
    if(files){if(!w->closing)for(guint i=0;i<g_list_model_get_n_items(files);i++){GFile *file=g_list_model_get_item(files,i);open_file(w,file);g_object_unref(file);}g_object_unref(files);}
    else if(error&&!g_error_matches(error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_DISMISSED))error_message(w,"Could not open files",error->message);
    g_clear_error(&error);g_object_unref(dialog);
}
static void remove_document(Document *d)
{
    Window *w=d->w;if(d->closed||w->closing)return;
    mark_dirty(w);d->closed=TRUE;d->close_prompt=FALSE;g_cancellable_cancel(d->cancel);
    g_signal_handlers_disconnect_by_data(d->buffer,d);g_signal_handlers_disconnect_by_data(d->search,d);
    int n=gtk_notebook_page_num(GTK_NOTEBOOK(w->notebook),d->page);
    gtk_notebook_remove_page(GTK_NOTEBOOK(w->notebook),n);
    if(w->quit_pending)quit_next(w);else if(!gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook)))new_document(w);
    update_status(w);
}
static void saved(GObject *saver,GAsyncResult *result,gpointer data)
{
    Document *d=data;GError *error=NULL;gboolean ok=gtk_source_file_saver_save_finish(GTK_SOURCE_FILE_SAVER(saver),result,&error);
    if(!d->closed){
        set_busy(d,FALSE);
        if(ok){guess_language(d);update_status(d->w);if(d->close_after_save){d->close_after_save=FALSE;if(d->w->quit_pending)quit_next(d->w);else remove_document(d);}else snapshot(d->w,TRUE);}
        else{d->close_after_save=FALSE;d->w->quit_pending=FALSE;error_message(d->w,"Could not save file",error->message);}
    }
    g_clear_error(&error);g_object_unref(saver);doc_unref(d);
}
static void save_to(Document *d,GFile *target)
{
    GtkSourceFileSaver *saver=gtk_source_file_saver_new_with_target(d->buffer,d->file,target);
    const GtkSourceEncoding *encoding=gtk_source_file_get_encoding(d->file);
    if(encoding)gtk_source_file_saver_set_encoding(saver,encoding);
    gtk_source_file_saver_set_newline_type(saver,gtk_source_file_get_newline_type(d->file));
    set_busy(d,TRUE);gtk_source_file_saver_save_async(saver,G_PRIORITY_DEFAULT,d->cancel,NULL,NULL,NULL,saved,doc_ref(d));
}
static void overwrite_answer(GObject *dialog,GAsyncResult *res,gpointer data)
{
    Document *d=data;const char *answer=luxe_choice_choose_finish(GTK_DIALOG(dialog),res);
    if(!d->closed){
        set_busy(d,FALSE);
        if(g_strcmp0(answer,"replace")==0)save_to(d,d->save_target);
        else{d->close_after_save=FALSE;d->w->quit_pending=FALSE;}
    }
    g_clear_object(&d->save_target);doc_unref(d);
}
static void save_selected(GObject *dialog,GAsyncResult *res,gpointer data)
{
    Document *d=data;GError *error=NULL;GFile *file=gtk_file_dialog_save_finish(GTK_FILE_DIALOG(dialog),res,&error);
    if(!d->closed){
        set_busy(d,FALSE);
        if(file){
            /* GtkSourceFileSaver only refuses to overwrite a file whose
             * mtime it recorded itself; a first-time Save As onto an
             * existing document would replace it silently. */
            GFile *location=gtk_source_file_get_location(d->file);
            if(g_file_query_exists(file,NULL)&&(!location||!g_file_equal(file,location))){
                g_set_object(&d->save_target,file);
                g_autofree char *name=g_file_get_basename(file);
                g_autofree char *heading=g_strdup_printf("Replace “%s”?",name);
                GtkWindow *prompt=luxe_choice_dialog_new(heading,"An existing document has that name. Replacing it will overwrite its contents.");
                luxe_choice_add_responses(GTK_DIALOG(prompt),"cancel","Cancel","replace","Replace",NULL);
                luxe_choice_set_close_response(GTK_DIALOG(prompt),"cancel");
                luxe_choice_set_default_response(GTK_DIALOG(prompt),"cancel");
                luxe_choice_set_response_appearance(GTK_DIALOG(prompt),"replace",LUXE_RESPONSE_DESTRUCTIVE);
                set_busy(d,TRUE);
                luxe_choice_choose(GTK_DIALOG(prompt),GTK_WINDOW(d->w->window),d->cancel,overwrite_answer,doc_ref(d));
            }
            else save_to(d,file);
        }
        else {d->close_after_save=FALSE;d->w->quit_pending=FALSE;if(error&&!g_error_matches(error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_DISMISSED))error_message(d->w,"Could not save file",error->message);}
    }
    g_clear_object(&file);g_clear_error(&error);g_object_unref(dialog);doc_unref(d);
}
static void save_document(Document *d,gboolean save_as)
{
    if(d->busy||d->closed)return;
    GFile *file=gtk_source_file_get_location(d->file);
    if(file&&!save_as){save_to(d,file);return;}
    GtkFileDialog *dialog=gtk_file_dialog_new();gtk_file_dialog_set_title(dialog,"Save document");
    pin_dialog_folder(d->w,dialog,file);
    if(file)gtk_file_dialog_set_initial_file(dialog,file);else gtk_file_dialog_set_initial_name(dialog,"Untitled.txt");
    set_busy(d,TRUE);gtk_file_dialog_save(dialog,GTK_WINDOW(d->w->window),d->cancel,save_selected,doc_ref(d));
}
static void close_answer(GObject *dialog,GAsyncResult *res,gpointer data)
{
    Document *d=data;const char *answer=luxe_choice_choose_finish(GTK_DIALOG(dialog),res);
    if(!d->closed){
        d->close_prompt=FALSE;
        if(g_strcmp0(answer,"discard")==0){
            if(d->w->quit_pending){gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(d->buffer),FALSE);quit_next(d->w);}
            else{remove_document(d);/* Drop discarded edits from recovery. */mark_dirty(d->w);snapshot(d->w,TRUE);}
        }
        else if(g_strcmp0(answer,"save")==0){d->close_after_save=TRUE;save_document(d,FALSE);}
        else d->w->quit_pending=FALSE;
    }
    else d->close_prompt=FALSE;
    doc_unref(d);
}
static void request_close(Document *d)
{
    if(d->closed||d->w->closing||d->close_prompt)return;
    if(d->busy){d->w->quit_pending=FALSE;error_message(d->w,"File operation in progress","Wait for the document to finish opening or saving before closing it.");return;}
    if(!gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(d->buffer))){remove_document(d);return;}
    g_autofree char *name=document_name(d);
    g_autofree char *message=g_strdup_printf("Unsaved changes to “%s” will be lost if discarded.",name);
    GtkWindow *dialog=luxe_choice_dialog_new("Save changes?",message);
    luxe_choice_add_responses(GTK_DIALOG(dialog),"cancel","Cancel","discard","Discard","save","Save",NULL);
    luxe_choice_set_close_response(GTK_DIALOG(dialog),"cancel");
    luxe_choice_set_default_response(GTK_DIALOG(dialog),"cancel");
    luxe_choice_set_response_appearance(GTK_DIALOG(dialog),"discard",LUXE_RESPONSE_DESTRUCTIVE);
    luxe_choice_set_response_appearance(GTK_DIALOG(dialog),"save",LUXE_RESPONSE_SUGGESTED);
    d->close_prompt=TRUE;
    luxe_choice_choose(GTK_DIALOG(dialog),GTK_WINDOW(d->w->window),d->cancel,close_answer,doc_ref(d));
}
static void quit_next(Window *w)
{
    if(w->closing||!w->notebook||!w->window)return;
    for(int i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));i++) {
        GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),i);
        Document *d=g_object_get_data(G_OBJECT(page),"document");
        if(d->busy){w->quit_pending=FALSE;error_message(w,"File operation in progress","Wait for files to finish opening or saving before quitting.");return;}
        if(gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(d->buffer))) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook),i);request_close(d);return;
        }
    }
    w->quit_pending=FALSE;w->quit_when_saved=TRUE;
    gtk_widget_set_sensitive(w->window,FALSE);
    snapshot(w,TRUE);
    if(!w->snapshot_running&&!w->session_dirty)finish_close(w);
}
static void finish_close(Window *w)
{
    int width=gtk_widget_get_width(w->window),height=gtk_widget_get_height(w->window);
    if(width>200&&height>200){g_settings_set_int(w->settings,"window-width",width);g_settings_set_int(w->settings,"window-height",height);}
    w->closing=TRUE;gtk_window_destroy(GTK_WINDOW(w->window));
}
static gboolean close_window(GtkWindow *window,Window *w)
{
    (void)window;if(w->quit_pending)return TRUE;
    w->quit_pending=TRUE;quit_next(w);return TRUE;
}

static void session_omission(SessionResult *result,Document *d,const char *reason)
{
    g_autofree char *title=document_name(d);
    g_ptr_array_add(result->omissions,g_strdup_printf("%s — %s",title,reason));
}
static SessionResult *collect_session(Window *w,const char *name)
{
    SessionResult *result=g_new0(SessionResult,1);
    result->omissions=g_ptr_array_new_with_free_func(g_free);
    result->state=g_key_file_new();
    GKeyFile *state=result->state;
    g_key_file_set_integer(state,"Session","version",1);
    g_key_file_set_string(state,"Session","name",name);
    int count=0,active=0;gsize total=0;
    for(int i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));i++){
        GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),i);Document *d=g_object_get_data(G_OBJECT(page),"document");
        if(d->busy){result->busy=TRUE;g_clear_pointer(&result->state,g_key_file_unref);return result;}
        GFile *file=gtk_source_file_get_location(d->file);gboolean dirty=gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(d->buffer));
        if(!file&&!dirty)continue;
        if(count>=LUCID_SNAPSHOT_MAX_DOCS){session_omission(result,d,"tab limit reached");continue;}
        /* Bound recovery size: an oversized dirty buffer is stored as a
         * location-only entry (reopened from disk) or skipped when untitled,
         * so one huge file cannot balloon every snapshot. */
        g_autofree char *text=NULL;
        if(dirty){
            if(gtk_text_buffer_get_char_count(GTK_TEXT_BUFFER(d->buffer))>LUCID_SNAPSHOT_MAX_CHARS_PER_DOC){
                session_omission(result,d,file?"unsaved contents exceed the per-document recovery limit; only the file location is saved":"unsaved contents exceed the per-document recovery limit; untitled contents are omitted");
                if(!file)continue;
                dirty=FALSE;
            }else{
                GtkTextIter ta,tb;gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(d->buffer),&ta,&tb);text=gtk_text_buffer_get_text(GTK_TEXT_BUFFER(d->buffer),&ta,&tb,TRUE);
                if(total+strlen(text)>LUCID_SNAPSHOT_MAX_BYTES_TOTAL){
                    session_omission(result,d,file?"unsaved contents exceed the total recovery limit; only the file location is saved":"unsaved contents exceed the total recovery limit; untitled contents are omitted");
                    if(!file)continue;
                    g_clear_pointer(&text,g_free);dirty=FALSE;
                }
            }
        }
        g_autofree char *group=g_strdup_printf("Document %d",count++);
        if(d==current(w))active=count-1;
        g_autofree char *uri=file?g_file_get_uri(file):g_strdup("");
        g_autofree char *title=document_name(d);
        g_key_file_set_string(state,group,"uri",uri);g_key_file_set_string(state,group,"title",title);
        GtkTextIter pos;gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(d->buffer),&pos,gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(d->buffer)));
        g_key_file_set_integer(state,group,"line",gtk_text_iter_get_line(&pos));g_key_file_set_integer(state,group,"column",gtk_text_iter_get_line_offset(&pos));
        g_key_file_set_boolean(state,group,"modified",dirty);
        if(dirty){g_key_file_set_string(state,group,"text",text);total+=strlen(text);}
    }
    if(count>0)active=CLAMP(active,0,count-1);
    g_key_file_set_integer(state,"Session","count",count);g_key_file_set_integer(state,"Session","active",active);
    g_key_file_set_boolean(state,"Session","complete",result->omissions->len==0);
    return result;
}
static void session_result_free(SessionResult *result)
{
    if(!result)return;
    g_clear_pointer(&result->state,g_key_file_unref);
    g_clear_pointer(&result->omissions,g_ptr_array_unref);
    g_free(result);
}
static char *session_omission_message(GPtrArray *omissions)
{
    if(!omissions||!omissions->len)return NULL;
    GString *message=g_string_new("Recovery is incomplete. Affected tabs: ");
    guint shown=MIN(omissions->len,8u);
    for(guint i=0;i<shown;i++){
        if(i)g_string_append(message,"; ");
        g_string_append(message,g_ptr_array_index(omissions,i));
    }
    if(omissions->len>shown)g_string_append_printf(message,"; and %u more",omissions->len-shown);
    return g_string_free(message,FALSE);
}
static void show_session_omissions(Window *w,GPtrArray *omissions)
{
    if(!w->recovery_warning)return;
    g_autofree char *message=session_omission_message(omissions);
    if(message){
        gtk_label_set_text(GTK_LABEL(w->recovery_warning),message);
        gtk_widget_set_visible(w->recovery_warning,TRUE);
    }else{
        gtk_label_set_text(GTK_LABEL(w->recovery_warning),"Recovery is incomplete: some tabs or unsaved contents exceed its limits. Save those files.");
        gtk_widget_set_visible(w->recovery_warning,FALSE);
    }
}
typedef struct {GKeyFile *state;char *path,*preserve_dir;guint generation;gboolean limited;} Snapshot;
static void snapshot_done(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)object;
    Window *w=data;GError *error=NULL;
    gssize written=g_task_propagate_int(G_TASK(result),&error);
    w->snapshot_running=FALSE;
    if(written<0){
        if(w->quit_when_saved&&!w->closing){w->quit_when_saved=FALSE;gtk_widget_set_sensitive(w->window,TRUE);error_message(w,"Could not save recovery",error->message);}
        else g_printerr("Session recovery: %s\n",error->message);
        w->snapshot_again=FALSE;
        if(!w->closing&&!w->quit_pending)arm_snapshot(w,LUCID_SNAPSHOT_METADATA_DELAY_MS);
    }
    else if((guint)written==w->snapshot_generation){
        w->session_dirty=FALSE;
        if(w->snapshot_timer){g_source_remove(w->snapshot_timer);w->snapshot_timer=0;}
        Snapshot *snap=g_task_get_task_data(G_TASK(result));
        if(!w->closing&&w->recovery_warning&&!snap->limited)show_session_omissions(w,NULL);
    }
    g_clear_error(&error);
    if(!w->closing&&(w->snapshot_again||w->quit_when_saved)){
        w->snapshot_again=FALSE;
        if(w->session_dirty)snapshot(w,TRUE);
        else if(w->quit_when_saved)finish_close(w);
    }
    g_application_release(G_APPLICATION(w->app));
}
static void snapshot_free(gpointer p)
{
    Snapshot *snap=p;g_key_file_unref(snap->state);g_free(snap->path);g_free(snap->preserve_dir);g_free(snap);
}
static void snapshot_write(GTask *task,gpointer,gpointer data,GCancellable *cancel)
{
    (void)cancel;
    Snapshot *snap=data;GError *error=NULL;
    if(snap->limited&&!lucid_state_preserve_existing(snap->path,snap->preserve_dir,&error)){
        g_task_return_error(task,error);return;
    }
    if(lucid_state_write(snap->state,snap->path,&error))g_task_return_int(task,(gssize)snap->generation);
    else g_task_return_error(task,error);
}
static void snapshot(Window *w,gboolean force)
{
    if(w->closing||w->restoring||w->quit_pending||!w->session_dirty)return;
    /* Keystroke quiescence: periodic ticks wait until edits have settled so
     * typing never triggers heavy serialize+fsync cycles. Explicit saves,
     * discards and quit still snapshot immediately. At most one write runs
     * at a time via snapshot_running/snapshot_generation. */
    if(!force&&g_get_monotonic_time()-w->last_edit<LUCID_SNAPSHOT_QUIESCE_US)return;
    if(w->snapshot_running){w->snapshot_again=TRUE;return;}
    SessionResult *collection=collect_session(w,"Last session");
    if(collection->busy){
        /* Busy documents: leave quit state alone when quitting — the
         * completing save/load calls snapshot(TRUE) and drives quit forward.
         * For a stuck insensitive window, the completion path re-enables it
         * on error via snapshot_done. */
        session_result_free(collection);
        return;
    }
    gboolean limited=collection->omissions->len>0;
    if(collection->omissions->len)show_session_omissions(w,collection->omissions);
    GKeyFile *state=g_steal_pointer(&collection->state);
    session_result_free(collection);
    guint generation=w->snapshot_generation;
    Snapshot *snap=g_new(Snapshot,1);
    snap->state=state;snap->path=g_strdup(w->session_file);snap->preserve_dir=g_strdup(w->sessions_dir);snap->generation=generation;snap->limited=limited;
    w->snapshot_running=TRUE;
    /* Serialization, fsync and rename run on the worker; only buffer reads
     * touched the UI thread above. A text change during the write bumps the
     * generation, keeps session_dirty set, and the next tick snapshots again. */
    g_application_hold(G_APPLICATION(w->app));
    GTask *task=g_task_new(w->app,NULL,snapshot_done,w);
    g_task_set_task_data(task,snap,(GDestroyNotify)snapshot_free);
    g_task_run_in_thread(task,snapshot_write);
    g_object_unref(task);
}
static gboolean snapshot_tick(gpointer data){
    Window *w=data;w->snapshot_timer=0;snapshot(w,FALSE);
    if(w->session_dirty&&!w->snapshot_running&&!w->quit_pending)arm_snapshot(w,LUCID_SNAPSHOT_RETRY_DELAY_MS);
    return G_SOURCE_REMOVE;
}
static void restore_session_state(Window *w, GKeyFile *state)
{
    gboolean was_restoring = w->restoring;
    w->restoring=TRUE;
    /* Skip locations that are already open (restoring twice, or an entry
     * that matches a pre-existing document): open_file's dedup cannot be
     * used here because recovered dirty documents intentionally have no
     * location, so track restored URIs explicitly. */
    GHashTable *restored=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
    int count=g_key_file_get_integer(state,"Session","count",NULL),first=gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook)),opened=0;
    /* Active entry tracked as an exact notebook page: session indexes shift
     * when entries are skipped (already-open tabs, invalid text), so
     * first+active would land on the wrong tab after close/switch edges. */
    int active=g_key_file_get_integer(state,"Session","active",NULL),active_page=-1;
    for(int i=0;i<count;i++){
        g_autofree char *group=g_strdup_printf("Document %d",i);
        g_autofree char *uri=g_key_file_get_string(state,group,"uri",NULL);
        gboolean dirty=g_key_file_get_boolean(state,group,"modified",NULL);
        if(!dirty&&uri&&*uri){
            if(g_hash_table_contains(restored,uri)){
                if(i==active){
                    for(int p=0;p<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));p++){
                        GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),p);Document *open=g_object_get_data(G_OBJECT(page),"document");
                        GFile *f=gtk_source_file_get_location(open->file);
                        if(f){g_autofree char *open_uri=g_file_get_uri(f);if(!strcmp(open_uri,uri)){active_page=p;break;}}
                    }
                }
                continue;
            }
            gboolean already=FALSE;int already_page=-1;
            for(int p=0;p<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));p++){
                GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),p);Document *open=g_object_get_data(G_OBJECT(page),"document");
                GFile *f=gtk_source_file_get_location(open->file);
                if(f){g_autofree char *open_uri=g_file_get_uri(f);if(!strcmp(open_uri,uri)){already=TRUE;already_page=p;break;}}
            }
            if(already){if(i==active)active_page=already_page;continue;}
            g_hash_table_add(restored,g_strdup(uri));
        }
        Document *d;
        if(dirty){
            g_autofree char *text=g_key_file_get_string(state,group,"text",NULL);
            if(!text||!g_utf8_validate(text,-1,NULL))continue;
            d=new_document(w);
            g_autofree char *title=g_key_file_get_string(state,group,"title",NULL);
            d->title=g_strdup_printf("Recovered — %s",title?title:"Untitled");
            // Restored edits are separate documents: a changed disk file must
            // never be silently overwritten by an old session snapshot.
            gtk_text_buffer_set_text(GTK_TEXT_BUFFER(d->buffer),text,-1);
            if(uri&&*uri){GFile *f=g_file_new_for_uri(uri);gtk_source_file_set_location(d->file,f);guess_language(d);gtk_source_file_set_location(d->file,NULL);g_object_unref(f);}
            gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(d->buffer),TRUE);
        }else if(uri&&*uri){GFile *file=g_file_new_for_uri(uri);d=load_document(w,file);g_object_unref(file);}
        else continue;
        if(i==active)active_page=gtk_notebook_page_num(GTK_NOTEBOOK(w->notebook),d->page);
        opened++;
        d->restore_line=MAX(0,g_key_file_get_integer(state,group,"line",NULL));
        d->restore_column=MAX(0,g_key_file_get_integer(state,group,"column",NULL));
        if(dirty){GtkTextIter pos;gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(d->buffer),&pos,MIN(d->restore_line,gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(d->buffer))-1));gtk_text_iter_set_line_offset(&pos,MIN(d->restore_column,MAX(0,gtk_text_iter_get_chars_in_line(&pos)-1)));gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(d->buffer),&pos);update_label(d);}
    }
    g_hash_table_unref(restored);
    if(active_page>=0)gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook),CLAMP(active_page,0,gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook))-1));
    else if(opened)gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook),MIN(first+MAX(0,active),gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook))-1));
    w->restoring=was_restoring;
}
static void restore_session(Window *w,const char *path)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) state = lucid_state_read(path, &error);
    if (!state) {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) error_message(w, "Could not restore session", error->message);
        return;
    }
    restore_session_state(w, state);
}
typedef struct {
    GKeyFile *state;
    char *path,*preserve_dir;
    GPtrArray *omissions;
    GWeakRef dialog;
} NamedSave;
static void named_save_free(gpointer data)
{
    NamedSave *save=data;g_clear_pointer(&save->state,g_key_file_unref);g_free(save->path);g_free(save->preserve_dir);
    g_clear_pointer(&save->omissions,g_ptr_array_unref);g_weak_ref_clear(&save->dialog);g_free(save);
}
static void named_save_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
    (void)source;(void)cancel;NamedSave *save=data;GError *error=NULL;
    if(save->omissions->len&&!lucid_state_preserve_existing(save->path,save->preserve_dir,&error)){
        g_task_return_error(task,error);return;
    }
    if(lucid_state_write(save->state,save->path,&error))g_task_return_boolean(task,TRUE);
    else g_task_return_error(task,error);
}
static void named_save_done(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)object;Window *w=data;GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
    NamedSave *save=g_task_get_task_data(G_TASK(result));
    w->named_save_running=FALSE;
    if(!w->closing){
        if(!ok)error_message(w,"Could not save session",error->message);
        else{
            g_autoptr(GtkWindow) dialog=g_weak_ref_get(&save->dialog);
            if(dialog)luxe_dialog_close(dialog);
            if(save->omissions->len){g_autofree char *message=session_omission_message(save->omissions);error_message(w,"Session saved with limits",message);}
        }
    }
    g_clear_error(&error);g_application_release(G_APPLICATION(w->app));
}
static void save_session_clicked(GtkButton *button,gpointer data)
{
    (void)button;GObject *dialog=data;Window *w=g_object_get_data(dialog,"window");
    GtkWidget *entry=g_object_get_data(dialog,"entry");const char *name=gtk_editable_get_text(GTK_EDITABLE(entry));
    if(!*name||w->named_save_running)return;
    SessionResult *collection=collect_session(w,name);
    if(collection->busy){session_result_free(collection);error_message(w,"Files are still loading","Wait for file operations to finish, then save the session.");return;}
    NamedSave *save=g_new0(NamedSave,1);save->state=g_steal_pointer(&collection->state);
    save->path=lucid_session_path(w->sessions_dir,name);save->preserve_dir=g_strdup(w->sessions_dir);save->omissions=g_ptr_array_ref(collection->omissions);
    g_weak_ref_init(&save->dialog,G_OBJECT(dialog));session_result_free(collection);
    GTask *task=g_task_new(w->app,NULL,named_save_done,w);g_task_set_task_data(task,save,named_save_free);
    w->named_save_running=TRUE;g_application_hold(G_APPLICATION(w->app));g_task_run_in_thread(task,named_save_worker);g_object_unref(task);
}
typedef struct {char *path;GWeakRef dialog;} SessionRestore;
static void session_restore_free(gpointer data)
{
    SessionRestore *restore=data;g_free(restore->path);g_weak_ref_clear(&restore->dialog);g_free(restore);
}
static void session_restore_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
    (void)source;(void)cancel;SessionRestore *restore=data;GError *error=NULL;
    GKeyFile *state=lucid_state_read(restore->path,&error);
    if(state)g_task_return_pointer(task,state,(GDestroyNotify)g_key_file_unref);
    else g_task_return_error(task,error);
}
static void session_restore_done(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)object;Window *w=data;GError *error=NULL;
    GKeyFile *state=g_task_propagate_pointer(G_TASK(result),&error);
    SessionRestore *restore=g_task_get_task_data(G_TASK(result));
    if(!w->closing){
        if(!state)error_message(w,"Could not restore session",error->message);
        else{
            restore_session_state(w,state);
            g_autoptr(GtkWindow) dialog=g_weak_ref_get(&restore->dialog);
            if(dialog)luxe_dialog_close(dialog);
        }
    }
    g_clear_pointer(&state,g_key_file_unref);g_clear_error(&error);g_application_release(G_APPLICATION(w->app));
}
static void restore_session_clicked(GtkButton *button,gpointer data)
{
    Window *w=data;SessionRestore *restore=g_new0(SessionRestore,1);
    restore->path=g_strdup(g_object_get_data(G_OBJECT(button),"path"));
    g_weak_ref_init(&restore->dialog,G_OBJECT(g_object_get_data(G_OBJECT(button),"dialog")));
    gtk_widget_set_sensitive(GTK_WIDGET(button),FALSE);
    GTask *task=g_task_new(w->app,NULL,session_restore_done,w);g_task_set_task_data(task,restore,session_restore_free);
    g_application_hold(G_APPLICATION(w->app));g_task_run_in_thread(task,session_restore_worker);g_object_unref(task);
}
#define LUCID_SESSION_LIST_MAX 256
#define LUCID_SESSION_SCAN_MAX 4096
typedef struct {
    gint refs;
    Window *window;
    GWeakRef dialog, list;
    GCancellable *cancel;
    GPtrArray *paths;
    guint next, loaded;
    gboolean truncated;
} SessionList;
static SessionList *session_list_ref(SessionList *list){g_atomic_int_inc(&list->refs);return list;}
static void session_list_unref(gpointer data)
{
    SessionList *list=data;if(!g_atomic_int_dec_and_test(&list->refs))return;
    g_weak_ref_clear(&list->dialog);g_weak_ref_clear(&list->list);g_clear_object(&list->cancel);
    g_clear_pointer(&list->paths,g_ptr_array_unref);g_free(list);
}
static gint session_path_compare(gconstpointer a,gconstpointer b){return g_strcmp0(*(char *const *)a,*(char *const *)b);}
static void session_list_worker(GTask *task,gpointer source,gpointer data,GCancellable *cancel)
{
    (void)source;const char *path=data;GError *error=NULL;
    if(g_cancellable_set_error_if_cancelled(cancel,&error)){g_task_return_error(task,error);return;}
    g_autoptr(GKeyFile) state=lucid_state_read(path,&error);
    if(!state){g_task_return_error(task,error);return;}
    g_autofree char *title=g_key_file_get_string(state,"Session","name",NULL);
    const char *base=strrchr(path,G_DIR_SEPARATOR);base=base?base+1:path;
    if(g_str_has_prefix(base,"recovered-")){g_free(g_steal_pointer(&title));title=g_strdup_printf("Recovered edits — %.12s",base+10);}
    else if(g_str_has_prefix(base,"recovery-after-error-")){g_free(g_steal_pointer(&title));title=g_strdup("Recovery after a startup error");}
    if(!title)title=g_strdup(base);
    g_task_return_pointer(task,g_steal_pointer(&title),g_free);
}
static void session_list_next(SessionList *list);
static void session_list_done(GObject *object,GAsyncResult *result,gpointer data)
{
    (void)object;SessionList *context=data;GError *error=NULL;
    g_autofree char *title=g_task_propagate_pointer(G_TASK(result),&error);
    const char *path=g_task_get_task_data(G_TASK(result));
    g_autoptr(GtkWindow) dialog=g_weak_ref_get(&context->dialog);
    g_autoptr(GtkWidget) box=g_weak_ref_get(&context->list);
    if(title&&dialog&&box){
        GtkWidget *first=gtk_widget_get_first_child(box);
        if(context->loaded==0&&first)gtk_box_remove(GTK_BOX(box),first);
        GtkWidget *button=gtk_button_new_with_label(title);
        g_autofree char *path_copy=g_strdup(path);
        gtk_widget_set_tooltip_text(button,path_copy);
        g_object_set_data_full(G_OBJECT(button),"path",g_steal_pointer(&path_copy),g_free);
        g_object_set_data(G_OBJECT(button),"dialog",dialog);
        g_signal_connect(button,"clicked",G_CALLBACK(restore_session_clicked),context->window);
        gtk_box_append(GTK_BOX(box),button);context->loaded++;
    }
    g_clear_error(&error);
    session_list_next(context);
    session_list_unref(context);
}
static void session_list_next(SessionList *list)
{
    if(g_cancellable_is_cancelled(list->cancel))return;
    if(list->next>=list->paths->len){
        g_autoptr(GtkWindow) dialog=g_weak_ref_get(&list->dialog);
        g_autoptr(GtkWidget) box=g_weak_ref_get(&list->list);
        if(dialog&&box&&list->loaded==0){
            GtkWidget *label=gtk_widget_get_first_child(box);
            if(label)gtk_label_set_text(GTK_LABEL(label),"No saved sessions yet.");
        }
        if(dialog&&box&&list->truncated)gtk_box_append(GTK_BOX(box),gtk_label_new("Session list limited to the first 256 entries."));
        return;
    }
    const char *path=g_ptr_array_index(list->paths,list->next++);
    GTask *task=g_task_new(NULL,list->cancel,session_list_done,session_list_ref(list));
    g_task_set_task_data(task,g_strdup(path),g_free);g_task_run_in_thread(task,session_list_worker);g_object_unref(task);
}
static void sessions_dialog_closed(GtkWidget *dialog,SessionList *list)
{
    (void)dialog;g_cancellable_cancel(list->cancel);
}
static void sessions_dialog(Window *w)
{
    GtkWindow *dialog=luxe_dialog_new();gtk_window_set_title(dialog,"Sessions");
    luxe_dialog_set_width(dialog,440);luxe_dialog_set_height(dialog,360);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);margins(box,18);luxe_window_set_content(dialog,box);
    GtkWidget *hint=gtk_label_new("Save your open tabs as a named session.\nRestoring opens them alongside your current tabs.");gtk_label_set_wrap(GTK_LABEL(hint),TRUE);gtk_box_append(GTK_BOX(box),hint);
    GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8),*entry=gtk_entry_new(),*save=gtk_button_new_with_label("Save session");
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry),"Session name");gtk_widget_set_hexpand(entry,TRUE);gtk_box_append(GTK_BOX(row),entry);gtk_box_append(GTK_BOX(row),save);gtk_box_append(GTK_BOX(box),row);
    g_object_set_data(G_OBJECT(dialog),"window",w);g_object_set_data(G_OBJECT(dialog),"entry",entry);g_signal_connect(save,"clicked",G_CALLBACK(save_session_clicked),dialog);
    GtkWidget *scroll=gtk_scrolled_window_new(),*list_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_widget_set_vexpand(scroll,TRUE);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),list_box);gtk_box_append(GTK_BOX(box),scroll);
    gtk_box_append(GTK_BOX(list_box),gtk_label_new("Loading saved sessions…"));
    SessionList *list=g_new0(SessionList,1);list->refs=1;list->window=w;list->cancel=g_cancellable_new();
    list->paths=g_ptr_array_new_with_free_func(g_free);g_weak_ref_init(&list->dialog,G_OBJECT(dialog));g_weak_ref_init(&list->list,G_OBJECT(list_box));
    GDir *dir=g_dir_open(w->sessions_dir,0,NULL);const char *name;guint scanned=0;
    while(dir&&scanned<LUCID_SESSION_SCAN_MAX&&list->paths->len<LUCID_SESSION_LIST_MAX&&(name=g_dir_read_name(dir))){
        scanned++;if(g_str_has_suffix(name,".session"))g_ptr_array_add(list->paths,g_build_filename(w->sessions_dir,name,NULL));
    }
    if(dir){if(list->paths->len==LUCID_SESSION_LIST_MAX||scanned==LUCID_SESSION_SCAN_MAX)list->truncated=g_dir_read_name(dir)!=NULL;g_dir_close(dir);}
    g_ptr_array_sort(list->paths,session_path_compare);
    g_object_set_data_full(G_OBJECT(dialog),"session-list",list,session_list_unref);
    g_signal_connect(dialog,"unrealize",G_CALLBACK(sessions_dialog_closed),list);
    luxe_dialog_present(dialog,w->window);session_list_next(list);
}
static void color_chosen(GObject *dialog,GAsyncResult *res,gpointer data)
{
    Document *d=data;GError *error=NULL;GdkRGBA *color=gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(dialog),res,&error);
    if(color&&!d->closed&&!d->busy){
        g_autofree char *hex=g_strdup_printf("#%02x%02x%02x",(int)(color->red*255+0.5),(int)(color->green*255+0.5),(int)(color->blue*255+0.5));
        GtkTextBuffer *b=GTK_TEXT_BUFFER(d->buffer);gtk_text_buffer_begin_user_action(b);gtk_text_buffer_delete_selection(b,TRUE,TRUE);gtk_text_buffer_insert_at_cursor(b,hex,-1);gtk_text_buffer_end_user_action(b);
    }
    if(color)gdk_rgba_free(color);
    g_clear_error(&error);g_object_unref(dialog);doc_unref(d);
}
static void pick_color(Document *d)
{
    if(d->busy)return;
    GtkColorDialog *dialog=gtk_color_dialog_new();gtk_color_dialog_set_title(dialog,"Insert color");gtk_color_dialog_set_with_alpha(dialog,FALSE);
    GdkRGBA initial;gdk_rgba_parse(&initial,"#89b4fa");GtkTextIter a,b;
    if(gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(d->buffer),&a,&b)){g_autofree char *text=gtk_text_buffer_get_text(GTK_TEXT_BUFFER(d->buffer),&a,&b,FALSE);gdk_rgba_parse(&initial,text);}
    gtk_color_dialog_choose_rgba(dialog,GTK_WINDOW(d->w->window),&initial,d->cancel,color_chosen,doc_ref(d));
}
static void terminal_spawned(VteTerminal *terminal,GPid pid,GError *error,gpointer data)
{
    (void)pid;Window *w=data;if(w->closing||!terminal)return;
    if(error){w->terminal_started=FALSE;gtk_label_set_text(GTK_LABEL(w->terminal_status),error->message);}
    else gtk_label_set_text(GTK_LABEL(w->terminal_status),"Terminal");
}
static void terminal_exited(VteTerminal *terminal,int status,Window *w)
{
    (void)terminal;(void)status;w->terminal_started=FALSE;
    if(!w->closing)gtk_label_set_text(GTK_LABEL(w->terminal_status),"Shell exited — use Restart to open another shell");
}
static void start_terminal(Window *w)
{
    if(w->terminal_started)return;
    Document *d=current(w);GFile *file=d?gtk_source_file_get_location(d->file):NULL;
    GFile *parent=file?g_file_get_parent(file):NULL;g_autofree char *cwd=parent?g_file_get_path(parent):NULL;g_clear_object(&parent);
    const char *shell=g_getenv("SHELL");if(!shell||!g_path_is_absolute(shell)||!g_file_test(shell,G_FILE_TEST_IS_EXECUTABLE))shell="/bin/sh";
    char *argv[]={(char*)shell,NULL};w->terminal_started=TRUE;
    vte_terminal_spawn_async(VTE_TERMINAL(w->terminal),VTE_PTY_DEFAULT,cwd?cwd:g_get_home_dir(),argv,NULL,G_SPAWN_DEFAULT,NULL,NULL,NULL,-1,w->window_cancel,terminal_spawned,w);
}
static void terminal_restart(GtkButton *b,Window *w){(void)b;start_terminal(w);gtk_widget_grab_focus(w->terminal);}
static void terminal_hide(GtkButton *b,Window *w){(void)b;gtk_widget_set_visible(w->terminal_box,FALSE);}
static void terminal_copy(GtkButton *b,Window *w){(void)b;vte_terminal_copy_clipboard_format(VTE_TERMINAL(w->terminal),VTE_FORMAT_TEXT);}
static void terminal_paste(GtkButton *b,Window *w){(void)b;vte_terminal_paste_clipboard(VTE_TERMINAL(w->terminal));}
static gboolean terminal_key(GtkEventControllerKey *controller,guint key,guint code,GdkModifierType mods,Window *w)
{
    (void)controller;(void)code;
    if((mods&(GDK_CONTROL_MASK|GDK_SHIFT_MASK))==(GDK_CONTROL_MASK|GDK_SHIFT_MASK)){
        if(key==GDK_KEY_C||key==GDK_KEY_c){terminal_copy(NULL,w);return TRUE;}
        if(key==GDK_KEY_V||key==GDK_KEY_v){terminal_paste(NULL,w);return TRUE;}
    }return FALSE;
}

static void search_changed(GtkEditable *entry,Window *w)
{
    gtk_source_search_settings_set_search_text(w->search_settings,gtk_editable_get_text(entry));update_status(w);
}
static void find_next(Window *w,gboolean backwards)
{
    Document *d=current(w);if(!d||d->busy)return;
    GtkTextIter pos,a,b;gboolean wrapped;
    GtkTextBuffer *buffer=GTK_TEXT_BUFFER(d->buffer);
    if(gtk_text_buffer_get_selection_bounds(buffer,&a,&b))pos=backwards?a:b;
    else gtk_text_buffer_get_iter_at_mark(buffer,&pos,gtk_text_buffer_get_insert(buffer));
    gboolean found=backwards?gtk_source_search_context_backward(d->search,&pos,&a,&b,&wrapped):gtk_source_search_context_forward(d->search,&pos,&a,&b,&wrapped);
    if(found){gtk_text_buffer_select_range(buffer,&a,&b);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(d->view),&a,0.1,FALSE,0,0);}
    update_status(w);
}
static void search_activate(GtkEntry *entry,Window *w){(void)entry;find_next(w,FALSE);}
static void replace_text(Window *w,gboolean all)
{
    Document *d=current(w);if(!d||d->busy||!*gtk_editable_get_text(GTK_EDITABLE(w->find)))return;
    GError *error=NULL;const char *replacement=gtk_editable_get_text(GTK_EDITABLE(w->replace));
    if(all)gtk_source_search_context_replace_all(d->search,replacement,-1,&error);
    else{
        GtkTextIter a,b;
        if(gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(d->buffer),&a,&b)&&gtk_source_search_context_get_occurrence_position(d->search,&a,&b)>0)
            gtk_source_search_context_replace(d->search,&a,&b,replacement,-1,&error);
        find_next(w,FALSE);
    }
    if(error){error_message(w,"Replace failed",error->message);g_clear_error(&error);}
}
static void show_search(Window *w)
{
    gtk_widget_set_visible(w->searchbar,TRUE);Document *d=current(w);
    if(d){GtkTextIter a,b;if(gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(d->buffer),&a,&b)&&gtk_text_iter_get_line(&a)==gtk_text_iter_get_line(&b)){
        g_autofree char *text=gtk_text_buffer_get_text(GTK_TEXT_BUFFER(d->buffer),&a,&b,FALSE);gtk_editable_set_text(GTK_EDITABLE(w->find),text);}}
    search_changed(GTK_EDITABLE(w->find),w);
    if(d)gtk_source_search_settings_set_search_text(gtk_source_search_context_get_settings(d->highlight),NULL);
    gtk_widget_grab_focus(w->find);
}
static void hide_search(Window *w)
{
    gtk_widget_set_visible(w->searchbar,FALSE);gtk_source_search_settings_set_search_text(w->search_settings,NULL);
    Document *d=current(w);if(d)gtk_widget_grab_focus(d->view);
}
static gboolean window_key(GtkEventControllerKey *controller,guint key,guint code,GdkModifierType mods,Window *w)
{
    (void)controller;(void)code;(void)mods;
    if(key==GDK_KEY_Escape&&gtk_widget_get_visible(w->searchbar)){hide_search(w);return TRUE;}return FALSE;
}
static void file_setup(GtkSignalListItemFactory *factory,GtkListItem *item,gpointer data)
{
    (void)factory;(void)data;GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);margins(row,5);
    GtkWidget *icon=gtk_image_new(),*label=gtk_label_new(NULL);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);gtk_widget_set_hexpand(label,TRUE);
    gtk_box_append(GTK_BOX(row),icon);gtk_box_append(GTK_BOX(row),label);gtk_list_item_set_child(item,row);
}
static void file_bind(GtkSignalListItemFactory *factory,GtkListItem *item,gpointer data)
{
    (void)factory;(void)data;GFileInfo *info=gtk_list_item_get_item(item);GtkWidget *row=gtk_list_item_get_child(item),*icon=gtk_widget_get_first_child(row),*label=gtk_widget_get_next_sibling(icon);
    gtk_image_set_from_gicon(GTK_IMAGE(icon),g_file_info_get_icon(info));gtk_label_set_text(GTK_LABEL(label),g_file_info_get_display_name(info));
}
static int file_compare(gconstpointer a,gconstpointer b,gpointer data)
{
    (void)data;gboolean ad=g_file_info_get_file_type((GFileInfo*)a)==G_FILE_TYPE_DIRECTORY,bd=g_file_info_get_file_type((GFileInfo*)b)==G_FILE_TYPE_DIRECTORY;
    if(ad!=bd)return ad?-1:1;
    return g_utf8_collate(g_file_info_get_display_name((GFileInfo*)a),g_file_info_get_display_name((GFileInfo*)b));
}
static gboolean file_visible(gpointer item,gpointer data){(void)data;return !g_file_info_get_is_hidden(item);}
static GFile *sidebar_info_file(GFileInfo *info, GFile *parent)
{
    GObject *attribute = g_file_info_get_attribute_object(info, "standard::file");
    if (G_IS_FILE(attribute)) return G_FILE(g_object_ref(attribute));
    const char *name = g_file_info_get_name(info);
    return parent && name ? g_file_get_child(parent, name) : NULL;
}
static void file_activated(GtkListView *view,guint position,Window *w)
{
    if(w->closing)return;
    GListModel *model=G_LIST_MODEL(gtk_list_view_get_model(view));
    if(position>=g_list_model_get_n_items(model))return;
    GFileInfo *info=g_list_model_get_item(model,position);if(!info)return;
    g_autoptr(GFile) file=sidebar_info_file(info,gtk_directory_list_get_file(w->directory));
    if(file){if(g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY)set_folder(w,file);else open_file(w,file);}g_object_unref(info);
}
static void set_folder(Window *w,GFile *folder)
{
    g_set_object(&w->sidebar_folder,folder);
    if(gtk_widget_get_visible(w->sidebar))gtk_directory_list_set_file(w->directory,folder);
    g_autofree char *name=g_file_get_parse_name(folder);gtk_editable_set_text(GTK_EDITABLE(w->folder_entry),name);
}
static void set_sidebar_visible(Window *w,gboolean visible)
{
    gtk_widget_set_visible(w->sidebar,visible);
    if(visible){
        gtk_directory_list_set_monitored(w->directory,TRUE);
        if(!gtk_directory_list_get_file(w->directory)&&w->sidebar_folder)gtk_directory_list_set_file(w->directory,w->sidebar_folder);
    }else{
        gtk_directory_list_set_monitored(w->directory,FALSE);
        if(gtk_directory_list_get_file(w->directory))gtk_directory_list_set_file(w->directory,NULL);
    }
}
static void folder_entered(GtkEntry *entry,Window *w)
{
    GFile *folder=g_file_parse_name(gtk_editable_get_text(GTK_EDITABLE(entry)));set_folder(w,folder);g_object_unref(folder);
}
static void folder_up(GtkButton *b,Window *w)
{
    (void)b;GFile *file=gtk_directory_list_get_file(w->directory),*parent=file?g_file_get_parent(file):NULL;if(parent){set_folder(w,parent);g_object_unref(parent);}
}
static void folder_error(GObject *object,GParamSpec *pspec,Window *w)
{
    (void)object;(void)pspec;const GError *error=gtk_directory_list_get_error(w->directory);if(error)error_message(w,"Could not list folder",error->message);
}
static void apply_font_preferences(Window *w)
{
    g_autofree char *font=g_settings_get_string(w->settings,"font");PangoFontDescription *desc=pango_font_description_from_string(font);
    const char *family=pango_font_description_get_family(desc);g_autofree char *escaped=g_strescape(family?family:"Monospace",NULL);
    double size=(double)pango_font_description_get_size(desc)/PANGO_SCALE;if(size<6)size=12;
    char number[G_ASCII_DTOSTR_BUF_SIZE];g_ascii_dtostr(number,sizeof number,size);
    g_autofree char *css=g_strdup_printf("textview.lucid-editor { font-family: \"%s\"; font-size: %spt; font-weight: %d; }",escaped,number,pango_font_description_get_weight(desc));
    gtk_css_provider_load_from_string(w->font_css,css);vte_terminal_set_font(VTE_TERMINAL(w->terminal),desc);pango_font_description_free(desc);
}
static void apply_theme_preferences(Window *w)
{
    GtkSourceStyleScheme *scheme=effective_scheme(w);
    if(scheme){
        /* Libadwaita escalates GtkSettings:gtk-application-prefer-dark-theme
         * to a fatal warning; GtkSettings:color-scheme is the supported
         * API. The editor scheme variant still drives the chrome, as before. */
        const char *variant=gtk_source_style_scheme_get_metadata(scheme,"variant");luxe_style_set_color_scheme(luxe_style_settings(),g_strcmp0(variant,"dark")==0?LUXE_COLOR_SCHEME_FORCE_DARK:LUXE_COLOR_SCHEME_FORCE_LIGHT);
        GtkSourceStyle *style=gtk_source_style_scheme_get_style(scheme,"text");char *fg=NULL,*bg=NULL;
        if(style){g_object_get(style,"foreground",&fg,"background",&bg,NULL);GdkRGBA f,b;if(fg&&bg&&gdk_rgba_parse(&f,fg)&&gdk_rgba_parse(&b,bg)){
            GdkRGBA palette[16];gsize count=0;const char *colors=gtk_source_style_scheme_get_metadata(scheme,"terminal-palette");
            if(colors){g_auto(GStrv) values=g_strsplit(colors," ",-1);for(guint i=0;values[i]&&count<16;i++){if(gdk_rgba_parse(&palette[count],values[i]))count++;}}
            vte_terminal_set_colors(VTE_TERMINAL(w->terminal),&f,&b,count==16?palette:NULL,count==16?16:0);
        }g_free(fg);g_free(bg);}
    }
}
static void apply_preferences(Window *w)
{
    apply_font_preferences(w);
    apply_theme_preferences(w);
    set_sidebar_visible(w,g_settings_get_boolean(w->settings,"sidebar"));
    for(int i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));i++){
        GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),i);Document *d=g_object_get_data(G_OBJECT(page),"document");apply_document_preferences(d);
    }
    update_status(w);
}
static void preference_changed(GSettings *settings,const char *key,Window *w){
    if(w->closing)return;
    if(g_str_equal(key,"sidebar")){set_sidebar_visible(w,g_settings_get_boolean(settings,"sidebar"));return;}
    if(g_str_equal(key,"window-width")||g_str_equal(key,"window-height"))return;
    if(g_str_equal(key,"font")){apply_font_preferences(w);return;}
    if(g_str_equal(key,"theme")||g_str_equal(key,"follow-desktop-theme"))apply_theme_preferences(w);
    if(g_str_equal(key,"theme")||g_str_equal(key,"follow-desktop-theme")||
       g_str_equal(key,"line-numbers")||g_str_equal(key,"tab-width")||
       g_str_equal(key,"insert-spaces")||g_str_equal(key,"show-right-margin")||
       g_str_equal(key,"right-margin-position")||g_str_equal(key,"wrap-mode")||
       g_str_equal(key,"spell-check")){
        for(int i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));i++){
            GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),i);
            apply_document_preference(g_object_get_data(G_OBJECT(page),"document"),key);
        }
    }
}
static void desktop_theme_changed(GObject *settings,GParamSpec *property,Window *w)
{
    (void)settings;(void)property;
    if(!w->closing&&w->settings&&g_settings_get_boolean(w->settings,"follow-desktop-theme")){
        apply_theme_preferences(w);
        for(int i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));i++){
            GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),i);
            apply_document_preference(g_object_get_data(G_OBJECT(page),"document"),"follow-desktop-theme");
        }
    }
}
static GtkWidget *preference_row(GtkWidget *box,const char *title,GtkWidget *control)
{
    GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,16),*label=gtk_label_new(title);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_widget_set_hexpand(label,TRUE);gtk_box_append(GTK_BOX(row),label);gtk_box_append(GTK_BOX(row),control);gtk_box_append(GTK_BOX(box),row);return control;
}
static void theme_selected(GObject *dropdown,GParamSpec *pspec,Window *w)
{
    (void)pspec;GPtrArray *ids=g_object_get_data(dropdown,"ids");guint i=gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));if(i<ids->len)g_settings_set_string(w->settings,"theme",g_ptr_array_index(ids,i));
}
static void font_selected(GObject *button,GParamSpec *pspec,Window *w)
{
    (void)pspec;
    const PangoFontDescription *desc=gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(button));
    g_autofree char *font=pango_font_description_to_string(desc);g_settings_set_string(w->settings,"font",font);
}
static void tab_width_changed(GtkSpinButton *spin,Window *w){g_settings_set_uint(w->settings,"tab-width",gtk_spin_button_get_value_as_int(spin));}
static void wrap_toggled(GtkCheckButton *check,Window *w){g_settings_set_enum(w->settings,"wrap-mode",gtk_check_button_get_active(check)?GTK_WRAP_WORD_CHAR:GTK_WRAP_NONE);}
static void preferences(Window *w)
{
    if(w->closing)return;
    GtkWindow *dialog=luxe_dialog_new();gtk_window_set_title(dialog,"Lucid Preferences");
    luxe_dialog_set_width(dialog,530);luxe_dialog_set_height(dialog,460);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,14);margins(box,20);luxe_window_set_content(dialog,box);
    GtkStringList *names=gtk_string_list_new(NULL);GPtrArray *ids=g_ptr_array_new_with_free_func(g_free);guint selected=0;
    g_autofree char *theme=g_settings_get_string(w->settings,"theme");GtkSourceStyleSchemeManager *manager=gtk_source_style_scheme_manager_get_default();const char *const *schemes=gtk_source_style_scheme_manager_get_scheme_ids(manager);
    for(int i=0;schemes[i];i++){
        if(!g_str_has_prefix(schemes[i],"lucid-"))continue;
        GtkSourceStyleScheme *scheme=gtk_source_style_scheme_manager_get_scheme(manager,schemes[i]);
        if(g_str_equal(theme,schemes[i]))selected=ids->len;
        gtk_string_list_append(names,gtk_source_style_scheme_get_name(scheme));g_ptr_array_add(ids,g_strdup(schemes[i]));
    }
    GtkWidget *themes=gtk_drop_down_new(G_LIST_MODEL(names),NULL);gtk_drop_down_set_enable_search(GTK_DROP_DOWN(themes),TRUE);gtk_drop_down_set_selected(GTK_DROP_DOWN(themes),selected);g_object_set_data_full(G_OBJECT(themes),"ids",ids,(GDestroyNotify)g_ptr_array_unref);
    GtkWidget *follow=gtk_check_button_new_with_label("Follow desktop theme");
    g_settings_bind(w->settings,"follow-desktop-theme",follow,"active",G_SETTINGS_BIND_DEFAULT);
    gtk_box_append(GTK_BOX(box),follow);
    g_object_bind_property(follow,"active",themes,"sensitive",G_BINDING_SYNC_CREATE|G_BINDING_INVERT_BOOLEAN);
    preference_row(box,"Color scheme",themes);g_signal_connect(themes,"notify::selected",G_CALLBACK(theme_selected),w);
    g_autofree char *font=g_settings_get_string(w->settings,"font");GtkFontDialog *fd=gtk_font_dialog_new();GtkWidget *font_button=gtk_font_dialog_button_new(fd);PangoFontDescription *desc=pango_font_description_from_string(font);gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_button),desc);pango_font_description_free(desc);preference_row(box,"Editor font",font_button);g_signal_connect(font_button,"notify::font-desc",G_CALLBACK(font_selected),w);
    GtkWidget *tabs=gtk_spin_button_new_with_range(1,32,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(tabs),g_settings_get_uint(w->settings,"tab-width"));preference_row(box,"Tab width",tabs);g_signal_connect(tabs,"value-changed",G_CALLBACK(tab_width_changed),w);
    const char *keys[]={"insert-spaces","line-numbers","show-right-margin","spell-check","restore-session",NULL};
    const char *labels[]={"Use spaces for indentation","Show line numbers","Show right margin","Check spelling","Restore tabs on startup"};
    for(int i=0;keys[i];i++){GtkWidget *check=gtk_check_button_new_with_label(labels[i]);g_settings_bind(w->settings,keys[i],check,"active",G_SETTINGS_BIND_DEFAULT);gtk_box_append(GTK_BOX(box),check);}
    GtkWidget *wrap=gtk_check_button_new_with_label("Wrap long lines");gtk_check_button_set_active(GTK_CHECK_BUTTON(wrap),g_settings_get_enum(w->settings,"wrap-mode")!=GTK_WRAP_NONE);g_signal_connect(wrap,"toggled",G_CALLBACK(wrap_toggled),w);gtk_box_append(GTK_BOX(box),wrap);
    GtkWidget *note=gtk_label_new("Changes apply immediately. Recovered unsaved documents\nare opened separately so disk files stay safe.");gtk_label_set_wrap(GTK_LABEL(note),TRUE);gtk_widget_add_css_class(note,"dim-label");gtk_box_append(GTK_BOX(box),note);luxe_dialog_present(dialog,w->window);
}
static void language_selected(GObject *dropdown,GParamSpec *pspec,Document *d)
{
    (void)pspec;if(d->closed)return;GPtrArray *ids=g_object_get_data(dropdown,"ids");guint i=gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if(i<ids->len){const char *id=g_ptr_array_index(ids,i);gtk_source_buffer_set_language(d->buffer,*id?gtk_source_language_manager_get_language(gtk_source_language_manager_get_default(),id):NULL);update_status(d->w);}
}
static void release_doc_data(gpointer data,GClosure *closure){(void)closure;doc_unref(data);}
static void choose_language(Document *d)
{
    if(d->closed||d->w->closing)return;
    GtkWindow *dialog=luxe_dialog_new();gtk_window_set_title(dialog,"Syntax Highlighting");
    luxe_dialog_set_width(dialog,360);
    GtkStringList *names=gtk_string_list_new((const char*[]){"Plain Text",NULL});GPtrArray *ids=g_ptr_array_new_with_free_func(g_free);g_ptr_array_add(ids,g_strdup(""));
    GtkSourceLanguageManager *manager=gtk_source_language_manager_get_default();const char *const *languages=gtk_source_language_manager_get_language_ids(manager);GtkSourceLanguage *current_lang=gtk_source_buffer_get_language(d->buffer);guint selected=0;
    for(int i=0;languages[i];i++){
        GtkSourceLanguage *lang=gtk_source_language_manager_get_language(manager,languages[i]);if(gtk_source_language_get_hidden(lang))continue;
        if(lang==current_lang)selected=ids->len;
        gtk_string_list_append(names,gtk_source_language_get_name(lang));g_ptr_array_add(ids,g_strdup(languages[i]));
    }
    GtkWidget *dropdown=gtk_drop_down_new(G_LIST_MODEL(names),NULL);gtk_drop_down_set_enable_search(GTK_DROP_DOWN(dropdown),TRUE);gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown),selected);g_object_set_data_full(G_OBJECT(dropdown),"ids",ids,(GDestroyNotify)g_ptr_array_unref);
    g_signal_connect_data(dropdown,"notify::selected",G_CALLBACK(language_selected),doc_ref(d),release_doc_data,0);margins(dropdown,20);luxe_window_set_content(dialog,dropdown);luxe_dialog_present(dialog,d->w->window);
}
static void statistics(Document *d)
{
    GtkTextIter a,b;GtkTextBuffer *buffer=GTK_TEXT_BUFFER(d->buffer);gboolean selected=gtk_text_buffer_get_selection_bounds(buffer,&a,&b);if(!selected)gtk_text_buffer_get_bounds(buffer,&a,&b);
    g_autofree char *text=gtk_text_buffer_get_text(buffer,&a,&b,TRUE);guint words=0,lines=1;gboolean in_word=FALSE;
    for(const char *p=text;*p;p=g_utf8_next_char(p)){gunichar c=g_utf8_get_char(p);if(c=='\n')lines++;gboolean word=g_unichar_isalnum(c)||c=='_';if(word&&!in_word)words++;in_word=word;}
    if(!*text)lines=0;
    g_autofree char *message=g_strdup_printf("%u words\n%u lines\n%ld characters\n%zu bytes (UTF-8)",words,lines,g_utf8_strlen(text,-1),strlen(text));error_message(d->w,selected?"Selection Statistics":"Document Statistics",message);
}
static void zoom(Window *w,int delta)
{
    g_autofree char *font=g_settings_get_string(w->settings,"font");PangoFontDescription *desc=pango_font_description_from_string(font);int size=pango_font_description_get_size(desc)/PANGO_SCALE;
    pango_font_description_set_size(desc,CLAMP(size+delta,6,48)*PANGO_SCALE);g_autofree char *next=pango_font_description_to_string(desc);g_settings_set_string(w->settings,"font",next);pango_font_description_free(desc);
}
static void go_line_clicked(GtkButton *button,gpointer data)
{
    (void)button;GtkWindow *dialog=data;Document *d=g_object_get_data(G_OBJECT(dialog),"document");GtkWidget *spin=g_object_get_data(G_OBJECT(dialog),"spin");
    if(!d->closed){GtkTextIter pos;gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(d->buffer),&pos,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin))-1);gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(d->buffer),&pos);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(d->view),&pos,0.1,FALSE,0,0);gtk_widget_grab_focus(d->view);}luxe_dialog_close(dialog);
}
static void go_line(Document *d)
{
    if(d->closed||d->w->closing)return;
    GtkWindow *dialog=luxe_dialog_new();gtk_window_set_title(dialog,"Go to Line");
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8),*spin=gtk_spin_button_new_with_range(1,gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(d->buffer)),1),*go=gtk_button_new_with_label("Go");margins(box,18);gtk_box_append(GTK_BOX(box),spin);gtk_box_append(GTK_BOX(box),go);luxe_window_set_content(dialog,box);
    g_object_set_data_full(G_OBJECT(dialog),"document",doc_ref(d),(GDestroyNotify)doc_unref);g_object_set_data(G_OBJECT(dialog),"spin",spin);g_signal_connect(go,"clicked",G_CALLBACK(go_line_clicked),dialog);luxe_dialog_present(dialog,d->w->window);
}

static void pin_dialog_folder(Window *w,GtkFileDialog *dialog,GFile *file)
{
    /* Pin the dialog to an explicit folder. Adw startup activates
     * xdg-desktop-portal (Settings portal), which flips GtkFileDialog
     * to the out-of-process backend; without an explicit folder that
     * backend opens in its own cwd instead of ours. Prefer the current
     * file's folder, falling back to the process working directory. */
    GFile *folder=NULL;
    if(file)folder=g_file_get_parent(file);
    if(!folder){
        Document *d=current(w);
        GFile *location=d?gtk_source_file_get_location(d->file):NULL;
        if(location)folder=g_file_get_parent(location);
    }
    if(!folder)folder=g_file_new_for_path(".");
    gtk_file_dialog_set_initial_folder(dialog,folder);
    g_object_unref(folder);
}

static void action(GSimpleAction *action,GVariant *parameter,gpointer data)
{
    (void)parameter;Window *w=data;const char *name=g_action_get_name(G_ACTION(action));Document *d=current(w);
    if(g_str_equal(name,"new")){new_document(w);return;}
    if(g_str_equal(name,"open")){GtkFileDialog *dialog=gtk_file_dialog_new();gtk_file_dialog_set_title(dialog,"Open files");pin_dialog_folder(w,dialog,NULL);gtk_file_dialog_open_multiple(dialog,GTK_WINDOW(w->window),w->window_cancel,open_selected,w);return;}
    if(g_str_equal(name,"quit")){close_window(GTK_WINDOW(w->window),w);return;}
    if(g_str_equal(name,"preferences")){preferences(w);return;}
    if(g_str_equal(name,"sessions")){sessions_dialog(w);return;}
    if(g_str_equal(name,"sidebar")){g_settings_set_boolean(w->settings,"sidebar",!g_settings_get_boolean(w->settings,"sidebar"));return;}
    if(g_str_equal(name,"terminal")){gboolean visible=!gtk_widget_get_visible(w->terminal_box);gtk_widget_set_visible(w->terminal_box,visible);if(visible){start_terminal(w);gtk_widget_grab_focus(w->terminal);}return;}
    if(g_str_equal(name,"about")) {
        GtkWindow *dialog = luxe_about_new();
        static const char *authors[] = {"Luxe contributors", "gedit contributors", "Sébastien Wilmet", "Paolo Borelli", "Ignacio Casal Quinteiro", "Jesse van den Kieboom", "Paolo Maggi", "Alex Roberts", "Chema Celorio", "Evan Lawrence", "Federico Mena Quintero", "Garrett Regier", "James Willcox", "Sébastien Lafargue", "Steve Frécinaux", NULL};
        static const char *documenters[] = {"Daniel Neel", "Eric Baudais", "Jim Campbell", "Sun GNOME Documentation Team", NULL};
        g_object_set(dialog,
                     "program-name", "Lucid",
                     "version", LUCID_VERSION,
                     "comments", "A GTK4 text editor for Luxe, forked from gedit.",
                     "copyright", "Copyright 1998-2024 – the gedit team\nCopyright (C) 2026 Jerry Howell",
                     "logo-icon-name", "org.luxe.Lucid",
                     "website", "https://github.com/openconstruct/luxeDE",
                     NULL);
        gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
        gtk_about_dialog_set_documenters(GTK_ABOUT_DIALOG(dialog), documenters);
        luxe_about_credits(GTK_ABOUT_DIALOG(dialog));
        luxe_dialog_present(dialog, GTK_WIDGET(w->window));
        return;
    }
    if(!d)return;
    GtkTextBuffer *buffer=GTK_TEXT_BUFFER(d->buffer);
    if(g_str_equal(name,"close")){request_close(d);return;}
    if(g_str_equal(name,"save")){save_document(d,FALSE);return;}
    if(g_str_equal(name,"save-as")){save_document(d,TRUE);return;}
    if(g_str_equal(name,"find")){show_search(w);return;}
    if(g_str_equal(name,"hide-search")){hide_search(w);return;}
    if(g_str_equal(name,"next")){find_next(w,FALSE);return;}
    if(g_str_equal(name,"previous")){find_next(w,TRUE);return;}
    if(g_str_equal(name,"zoom-in")){zoom(w,1);return;}
    if(g_str_equal(name,"zoom-out")){zoom(w,-1);return;}
    if(g_str_equal(name,"language")){choose_language(d);return;}
    if(g_str_equal(name,"statistics")){statistics(d);return;}
    if(g_str_equal(name,"go-line")){go_line(d);return;}
    if(d->busy)return;
    if(g_str_equal(name,"replace")){replace_text(w,FALSE);return;}
    if(g_str_equal(name,"replace-all")){replace_text(w,TRUE);return;}
    if(g_str_equal(name,"color")){pick_color(d);return;}
    if(g_str_equal(name,"undo")){if(gtk_text_buffer_get_can_undo(buffer))gtk_text_buffer_undo(buffer);return;}
    if(g_str_equal(name,"redo")){if(gtk_text_buffer_get_can_redo(buffer))gtk_text_buffer_redo(buffer);return;}
    if(g_str_equal(name,"select-all")){GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);gtk_text_buffer_select_range(buffer,&a,&b);return;}
    GdkClipboard *clipboard=gtk_widget_get_clipboard(d->view);
    if(g_str_equal(name,"copy")){gtk_text_buffer_copy_clipboard(buffer,clipboard);return;}
    if(g_str_equal(name,"cut")){gtk_text_buffer_cut_clipboard(buffer,clipboard,TRUE);return;}
    if(g_str_equal(name,"paste")){gtk_text_buffer_paste_clipboard(buffer,clipboard,NULL,TRUE);return;}
    if(g_str_equal(name,"date")){
        GDateTime *now=g_date_time_new_now_local();g_autofree char *text=g_date_time_format(now,"%Y-%m-%d %H:%M");g_date_time_unref(now);
        gtk_text_buffer_begin_user_action(buffer);gtk_text_buffer_delete_selection(buffer,TRUE,TRUE);gtk_text_buffer_insert_at_cursor(buffer,text,-1);gtk_text_buffer_end_user_action(buffer);return;
    }
    if(g_str_equal(name,"sort")){
        GtkTextIter a,b;if(!gtk_text_buffer_get_selection_bounds(buffer,&a,&b))gtk_text_buffer_get_bounds(buffer,&a,&b);
        gtk_source_buffer_sort_lines(d->buffer,&a,&b,GTK_SOURCE_SORT_FLAGS_NONE,0);return;
    }
}
static void switched(GObject *notebook,GParamSpec *pspec,Window *w)
{
    (void)notebook;(void)pspec;mark_dirty(w);update_status(w);
}
static void reordered(GtkNotebook *notebook,GtkWidget *page,guint position,Window *w)
{
    (void)notebook;(void)page;(void)position;mark_dirty(w);
}
static void add_section(GMenu *bar,const char *title,const char *const *labels,const char *const *actions)
{
    GMenu *menu=g_menu_new();for(int i=0;labels[i];i++){g_autofree char *name=g_strconcat("win.",actions[i],NULL);g_menu_append(menu,labels[i],name);}g_menu_append_section(bar,title,G_MENU_MODEL(menu));g_object_unref(menu);
}
static void window_unrealized(GtkWidget *widget,Window *w)
{
    (void)widget;
    w->closing=TRUE;
    w->quit_pending=FALSE;w->quit_when_saved=FALSE;w->snapshot_again=FALSE;
    if(w->snapshot_timer){g_source_remove(w->snapshot_timer);w->snapshot_timer=0;}
    if(w->directory)gtk_directory_list_set_monitored(w->directory,FALSE);
    g_cancellable_cancel(w->window_cancel);
    if(!w->notebook)return;
    for(int i=0;i<gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook));i++){
        GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->notebook),i);
        Document *d=page?g_object_get_data(G_OBJECT(page),"document"):NULL;
        if(!d)continue;
        d->closed=TRUE;d->close_prompt=FALSE;
        g_signal_handlers_disconnect_by_data(d->buffer,d);
        g_signal_handlers_disconnect_by_data(d->search,d);
        g_cancellable_cancel(d->cancel);
    }
}
static void cleanup_window(Window *w)
{
    if(w->snapshot_timer){g_source_remove(w->snapshot_timer);w->snapshot_timer=0;}
    if(w->settings)g_signal_handlers_disconnect_by_data(w->settings,w);
    if(gtk_settings_get_default())g_signal_handlers_disconnect_by_data(gtk_settings_get_default(),w);
    if(w->directory){g_signal_handlers_disconnect_by_data(w->directory,w);gtk_directory_list_set_monitored(w->directory,FALSE);}
    if(w->font_css&&gdk_display_get_default())gtk_style_context_remove_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(w->font_css));
    g_clear_object(&w->settings);g_clear_object(&w->directory);g_clear_object(&w->sidebar_folder);g_clear_object(&w->search_settings);
    g_clear_object(&w->font_css);g_clear_object(&w->window_cancel);
    w->window=NULL;w->notebook=NULL;w->status=NULL;w->recovery_warning=NULL;w->searchbar=NULL;w->find=NULL;w->replace=NULL;w->matches=NULL;
    w->sidebar=NULL;w->folder_entry=NULL;w->file_list=NULL;w->terminal_box=NULL;w->terminal=NULL;w->terminal_status=NULL;
    w->paned=NULL;w->vertical=NULL;
    g_clear_pointer(&w->data_dir,g_free);g_clear_pointer(&w->state_dir,g_free);
    g_clear_pointer(&w->sessions_dir,g_free);g_clear_pointer(&w->session_file,g_free);
    modeline_parser_shutdown();
}
static void setup_window(Window *w,GtkApplication *app)
{
    w->window_cancel=g_cancellable_new();
    w->app=app;w->settings=g_settings_new("org.luxe.Lucid");w->search_settings=gtk_source_search_settings_new();gtk_source_search_settings_set_wrap_around(w->search_settings,TRUE);
#ifdef LUCID_TEST
    w->data_dir=g_strdup(g_getenv("LUCID_DATA_DIR")?g_getenv("LUCID_DATA_DIR"):LUCID_DATA_DIR);
#else
    w->data_dir=g_strdup(LUCID_DATA_DIR);
#endif
    w->state_dir=g_build_filename(g_get_user_state_dir(),"lucid",NULL);w->sessions_dir=g_build_filename(w->state_dir,"sessions",NULL);w->session_file=g_build_filename(w->state_dir,"recovery.session",NULL);
    g_autofree char *styles=g_build_filename(w->data_dir,"styles",NULL);gtk_source_style_scheme_manager_prepend_search_path(gtk_source_style_scheme_manager_get_default(),styles);
    modeline_parser_init(w->data_dir);
    w->window=gtk_application_window_new(GTK_APPLICATION(app));gtk_window_set_title(GTK_WINDOW(w->window),"Lucid");gtk_window_set_icon_name(GTK_WINDOW(w->window),"org.luxe.Lucid");gtk_window_set_default_size(GTK_WINDOW(w->window),g_settings_get_int(w->settings,"window-width"),g_settings_get_int(w->settings,"window-height"));
    g_object_add_weak_pointer(G_OBJECT(w->window),(gpointer *)&w->window);
    g_signal_connect(w->window,"unrealize",G_CALLBACK(window_unrealized),w);
    const char *actions[]={"new","open","save","save-as","close","quit","undo","redo","cut","copy","paste","select-all","find","hide-search","next","previous","replace","replace-all","zoom-in","zoom-out","language","statistics","go-line","date","sort","color","terminal","sidebar","sessions","preferences","about",NULL};
    for(int i=0;actions[i];i++){GSimpleAction *a=g_simple_action_new(actions[i],NULL);g_signal_connect(a,"activate",G_CALLBACK(action),w);g_action_map_add_action(G_ACTION_MAP(w->window),G_ACTION(a));g_object_unref(a);}
    const char *accels[][2]={{"new","<Primary>n"},{"open","<Primary>o"},{"save","<Primary>s"},{"save-as","<Primary><Shift>s"},{"close","<Primary>w"},{"quit","<Primary>q"},{"find","<Primary>f"},{"go-line","<Primary>l"},{"next","<Primary>g"},{"previous","<Primary><Shift>g"},{"preferences","<Primary>comma"},{"terminal","F4"},{"sidebar","F9"},{"zoom-in","<Primary>plus"},{"zoom-out","<Primary>minus"},{NULL,NULL}};
    for(int i=0;accels[i][0];i++){g_autofree char *name=g_strconcat("win.",accels[i][0],NULL);gtk_application_set_accels_for_action(GTK_APPLICATION(app),name,(const char*[]){accels[i][1],NULL});}
    GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    /* GtkApplicationWindow rejects a classic titlebar child; the header bar
     * lives in an GtkBox top bar instead (pilot pattern). */
    GtkWidget *view=luxe_toolbar_new();luxe_toolbar_set_content(GTK_BOX(view),root);
    GtkWidget *header=gtk_header_bar_new();
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header),button("document-new-symbolic","New document (Ctrl+N)","win.new"));gtk_header_bar_pack_start(GTK_HEADER_BAR(header),button("document-open-symbolic","Open files (Ctrl+O)","win.open"));gtk_header_bar_pack_start(GTK_HEADER_BAR(header),button("document-save-symbolic","Save (Ctrl+S)","win.save"));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header),button("edit-find-symbolic","Find and replace (Ctrl+F)","win.find"));gtk_header_bar_pack_end(GTK_HEADER_BAR(header),button("utilities-terminal-symbolic","Terminal (F4)","win.terminal"));
    GMenu *menu=g_menu_new();
    add_section(menu,"File",(const char*[]){"New","Open…","Save","Save As…","Sessions…","Close","Quit",NULL},(const char*[]){"new","open","save","save-as","sessions","close","quit"});
    add_section(menu,"Edit",(const char*[]){"Undo","Redo","Cut","Copy","Paste","Select All","Preferences…",NULL},(const char*[]){"undo","redo","cut","copy","paste","select-all","preferences"});
    add_section(menu,"View",(const char*[]){"File Browser","Terminal","Larger Text","Smaller Text",NULL},(const char*[]){"sidebar","terminal","zoom-in","zoom-out"});
    add_section(menu,"Search",(const char*[]){"Find and Replace…","Find Next","Find Previous","Go to Line…",NULL},(const char*[]){"find","next","previous","go-line"});
    add_section(menu,"Tools",(const char*[]){"Syntax Highlighting…","Document Statistics…","Sort Lines","Insert Date/Time","Insert Color…",NULL},(const char*[]){"language","statistics","sort","date","color"});
    add_section(menu,"Help",(const char*[]){"About Lucid",NULL},(const char*[]){"about"});
    GtkWidget *hamburger=gtk_menu_button_new();gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(hamburger),"open-menu-symbolic");gtk_widget_set_tooltip_text(hamburger,"Main menu");gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(hamburger),G_MENU_MODEL(menu));g_object_unref(menu);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header),hamburger);
    luxe_toolbar_add_top_bar(GTK_BOX(view),header);
    /* The header bar must be registered before the view becomes the window
     * content, or luxe_window_set_content() finds no titlebar to attach. */
    luxe_window_set_content(GTK_WINDOW(w->window),view);
    w->searchbar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);margins(w->searchbar,6);gtk_widget_set_visible(w->searchbar,FALSE);gtk_box_append(GTK_BOX(root),w->searchbar);
    w->find=gtk_entry_new();w->replace=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(w->find),"Find");gtk_entry_set_placeholder_text(GTK_ENTRY(w->replace),"Replace with");gtk_widget_set_hexpand(w->find,TRUE);gtk_widget_set_hexpand(w->replace,TRUE);gtk_box_append(GTK_BOX(w->searchbar),w->find);gtk_box_append(GTK_BOX(w->searchbar),w->replace);
    gtk_box_append(GTK_BOX(w->searchbar),button("go-up-symbolic","Previous match","win.previous"));gtk_box_append(GTK_BOX(w->searchbar),button("go-down-symbolic","Next match","win.next"));
    GtkWidget *replace=gtk_button_new_with_label("Replace"),*all=gtk_button_new_with_label("All");gtk_actionable_set_action_name(GTK_ACTIONABLE(replace),"win.replace");gtk_actionable_set_action_name(GTK_ACTIONABLE(all),"win.replace-all");gtk_box_append(GTK_BOX(w->searchbar),replace);gtk_box_append(GTK_BOX(w->searchbar),all);
    w->matches=gtk_label_new("");gtk_box_append(GTK_BOX(w->searchbar),w->matches);gtk_box_append(GTK_BOX(w->searchbar),button("window-close-symbolic","Close search (Escape)","win.hide-search"));
    g_signal_connect(w->find,"changed",G_CALLBACK(search_changed),w);g_signal_connect(w->find,"activate",G_CALLBACK(search_activate),w);
    w->paned=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_widget_set_vexpand(w->paned,TRUE);gtk_box_append(GTK_BOX(root),w->paned);
    w->sidebar=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_widget_set_visible(w->sidebar,g_settings_get_boolean(w->settings,"sidebar"));gtk_widget_set_size_request(w->sidebar,180,-1);gtk_paned_set_start_child(GTK_PANED(w->paned),w->sidebar);gtk_paned_set_resize_start_child(GTK_PANED(w->paned),FALSE);gtk_paned_set_shrink_start_child(GTK_PANED(w->paned),FALSE);gtk_paned_set_position(GTK_PANED(w->paned),230);
    GtkWidget *folderbar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*up=button("go-up-symbolic","Parent folder",NULL);w->folder_entry=gtk_entry_new();gtk_widget_set_hexpand(w->folder_entry,TRUE);gtk_editable_set_width_chars(GTK_EDITABLE(w->folder_entry),10);gtk_box_append(GTK_BOX(folderbar),up);gtk_box_append(GTK_BOX(folderbar),w->folder_entry);gtk_box_append(GTK_BOX(w->sidebar),folderbar);g_signal_connect(up,"clicked",G_CALLBACK(folder_up),w);g_signal_connect(w->folder_entry,"activate",G_CALLBACK(folder_entered),w);
    w->directory=gtk_directory_list_new("standard::name,standard::display-name,standard::type,standard::icon,standard::is-hidden,standard::file",NULL);gtk_directory_list_set_monitored(w->directory,gtk_widget_get_visible(w->sidebar));g_signal_connect(w->directory,"notify::error",G_CALLBACK(folder_error),w);
    GtkFilterListModel *filtered=gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(w->directory)),GTK_FILTER(gtk_custom_filter_new(file_visible,NULL,NULL)));
    GtkSortListModel *sorted=gtk_sort_list_model_new(G_LIST_MODEL(filtered),GTK_SORTER(gtk_custom_sorter_new(file_compare,NULL,NULL)));
    GtkListItemFactory *factory=gtk_signal_list_item_factory_new();g_signal_connect(factory,"setup",G_CALLBACK(file_setup),w);g_signal_connect(factory,"bind",G_CALLBACK(file_bind),w);
    w->file_list=gtk_list_view_new(GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(sorted))),factory);g_signal_connect(w->file_list,"activate",G_CALLBACK(file_activated),w);
    GtkWidget *files_scroll=gtk_scrolled_window_new();gtk_widget_set_vexpand(files_scroll,TRUE);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(files_scroll),w->file_list);gtk_box_append(GTK_BOX(w->sidebar),files_scroll);
    GFile *home=g_file_new_for_path(g_get_home_dir());set_folder(w,home);g_object_unref(home);
    w->vertical=gtk_paned_new(GTK_ORIENTATION_VERTICAL);gtk_paned_set_end_child(GTK_PANED(w->paned),w->vertical);gtk_paned_set_shrink_end_child(GTK_PANED(w->paned),FALSE);
    w->notebook=gtk_notebook_new();gtk_notebook_set_scrollable(GTK_NOTEBOOK(w->notebook),TRUE);gtk_paned_set_start_child(GTK_PANED(w->vertical),w->notebook);gtk_paned_set_resize_end_child(GTK_PANED(w->vertical),FALSE);gtk_paned_set_shrink_start_child(GTK_PANED(w->vertical),FALSE);
    w->terminal_box=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_set_size_request(w->terminal_box,-1,170);gtk_paned_set_end_child(GTK_PANED(w->vertical),w->terminal_box);gtk_widget_set_visible(w->terminal_box,FALSE);
    GtkWidget *terminal_bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);margins(terminal_bar,4);w->terminal_status=gtk_label_new("Terminal");gtk_label_set_xalign(GTK_LABEL(w->terminal_status),0);gtk_label_set_ellipsize(GTK_LABEL(w->terminal_status),PANGO_ELLIPSIZE_END);gtk_widget_set_hexpand(w->terminal_status,TRUE);gtk_box_append(GTK_BOX(terminal_bar),w->terminal_status);
    GtkWidget *restart=gtk_button_new_with_label("Restart"),*copy=button("edit-copy-symbolic","Copy (Ctrl+Shift+C)",NULL),*paste=button("edit-paste-symbolic","Paste (Ctrl+Shift+V)",NULL),*hide=button("window-close-symbolic","Hide terminal",NULL);gtk_box_append(GTK_BOX(terminal_bar),copy);gtk_box_append(GTK_BOX(terminal_bar),paste);gtk_box_append(GTK_BOX(terminal_bar),restart);gtk_box_append(GTK_BOX(terminal_bar),hide);gtk_box_append(GTK_BOX(w->terminal_box),terminal_bar);
    g_signal_connect(restart,"clicked",G_CALLBACK(terminal_restart),w);g_signal_connect(copy,"clicked",G_CALLBACK(terminal_copy),w);g_signal_connect(paste,"clicked",G_CALLBACK(terminal_paste),w);g_signal_connect(hide,"clicked",G_CALLBACK(terminal_hide),w);
    w->terminal=vte_terminal_new();vte_terminal_set_scrollback_lines(VTE_TERMINAL(w->terminal),10000);gtk_widget_set_vexpand(w->terminal,TRUE);gtk_box_append(GTK_BOX(w->terminal_box),w->terminal);g_signal_connect(w->terminal,"child-exited",G_CALLBACK(terminal_exited),w);
    GtkEventController *terminal_keys=gtk_event_controller_key_new();g_signal_connect(terminal_keys,"key-pressed",G_CALLBACK(terminal_key),w);gtk_widget_add_controller(w->terminal,terminal_keys);
    w->recovery_warning=gtk_label_new("Recovery is incomplete: some tabs or unsaved contents exceed its limits. Save those files.");
    gtk_label_set_wrap(GTK_LABEL(w->recovery_warning),TRUE);gtk_label_set_xalign(GTK_LABEL(w->recovery_warning),0);
    gtk_widget_add_css_class(w->recovery_warning,"error");margins(w->recovery_warning,6);
    gtk_widget_set_visible(w->recovery_warning,FALSE);gtk_box_append(GTK_BOX(root),w->recovery_warning);
    w->status=gtk_label_new("");gtk_label_set_xalign(GTK_LABEL(w->status),0);margins(w->status,6);gtk_box_append(GTK_BOX(root),w->status);
    w->font_css=gtk_css_provider_new();gtk_style_context_add_provider_for_display(gtk_widget_get_display(w->window),GTK_STYLE_PROVIDER(w->font_css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_signal_connect(w->settings,"changed",G_CALLBACK(preference_changed),w);
    g_signal_connect(gtk_settings_get_default(),"notify::gtk-theme-name",G_CALLBACK(desktop_theme_changed),w);
    g_signal_connect(w->notebook,"notify::page",G_CALLBACK(switched),w);g_signal_connect(w->notebook,"page-reordered",G_CALLBACK(reordered),w);g_signal_connect(w->window,"close-request",G_CALLBACK(close_window),w);
    GtkEventController *keys=gtk_event_controller_key_new();g_signal_connect(keys,"key-pressed",G_CALLBACK(window_key),w);gtk_widget_add_controller(w->window,keys);
    apply_preferences(w);
}
static void initialize_window(Window *w, GtkApplication *app)
{
    w->restoring = TRUE;
    setup_window(w, app);
    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) state = lucid_state_read(w->session_file, &error);
    if (state) {
        g_auto(GStrv) groups = g_key_file_get_groups(state, NULL);
        gboolean has_edits = FALSE;
        for (guint i = 0; groups[i]; ++i)
            has_edits |= g_str_has_prefix(groups[i], "Document ") && g_key_file_has_key(state, groups[i], "text", NULL);
        if (has_edits) {
            g_autofree char *preserved = lucid_state_preserve(state, w->sessions_dir, &error);
            if (preserved && !g_settings_get_boolean(w->settings, "restore-session"))
                error_message(w, "Previous unsaved edits preserved", "Automatic session restoration is off. Open Sessions to recover earlier edits. Your requested files can still be opened normally.");
        }
        if (g_settings_get_boolean(w->settings, "restore-session")) restore_session_state(w, state);
    } else if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) g_clear_error(&error);
    if (error) {
        // Retain an unreadable/unpreserved original byte-for-byte. New edits
        // get a separate live recovery record, also discoverable in Sessions.
        g_autofree char *uuid = g_uuid_string_random();
        g_autofree char *name = g_strconcat("recovery-after-error-", uuid, ".session", NULL);
        g_free(w->session_file);
        w->session_file = g_build_filename(w->sessions_dir, name, NULL);
        g_autofree char *detail = g_strdup_printf("%s\nThe previous recovery was left untouched. New recovery will use %s", error->message, w->session_file);
        error_message(w, "Previous recovery needs attention", detail);
    }
    w->restoring = FALSE;
    if(w->session_dirty)arm_snapshot(w,LUCID_SNAPSHOT_METADATA_DELAY_MS);
}
static void activate(GtkApplication *app,gpointer data)
{
    Window *w=data;if(w->closing)return;if(!w->window){initialize_window(w,app);if(!gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->notebook)))new_document(w);}gtk_window_present(GTK_WINDOW(w->window));
}
static void application_open(GApplication *app,GFile **files,int count,const char *hint,gpointer data)
{
    (void)hint;Window *w=data;if(w->closing)return;if(!w->window)initialize_window(w,GTK_APPLICATION(app));for(int i=0;i<count;i++)open_file(w,files[i]);gtk_window_present(GTK_WINDOW(w->window));
}
int main(int argc,char **argv)
{
    if(argc==2&&g_str_equal(argv[1],"--version")){g_print("Lucid %s (GTK4; gedit 48.1 fork)\n",LUCID_VERSION);return 0;}
    gtk_source_init();luxe_ui_init();g_set_application_name("Lucid");
    GtkApplication *app=luxe_application_new("org.luxe.Lucid",G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app,"activate",G_CALLBACK(activate),&win);g_signal_connect(app,"open",G_CALLBACK(application_open),&win);
    int status=g_application_run(G_APPLICATION(app),argc,argv);
    cleanup_window(&win);
    g_object_unref(app);return status;
}
