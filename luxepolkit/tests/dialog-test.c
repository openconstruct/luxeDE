/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>
#include <unistd.h>
static PolkitAgentSession *active;
static int replies;
static gboolean complete_in_response;
static gboolean fail_initiate;
static gboolean delay_request;
static uid_t session_uid;
static guint debug_messages;
static void check_debug_log(const gchar *domain, GLogLevelFlags level, const gchar *message, gpointer data) {
    (void)domain; (void)level; (void)data;
    g_assert_null(strstr(message, "private-detail-fixture"));
    g_assert_null(strstr(message, "fixture-cookie"));
    g_assert_null(strstr(message, "fixture-response"));
    debug_messages++;
}
static void fixture_initiate(PolkitAgentSession *session) {
    g_set_object(&active, session);
    if (fail_initiate) {
        g_signal_emit_by_name(session, "completed", FALSE);
        return;
    }
    if (!delay_request) g_signal_emit_by_name(session, "request", "Password: ", FALSE);
}
static void fixture_response(PolkitAgentSession *session, const char *text) {
    g_assert_cmpstr(text, ==, "fixture-response"); ++replies;
    if (complete_in_response) g_signal_emit_by_name(session, "completed", TRUE);
}
static void fixture_cancel(PolkitAgentSession *session) {
    g_signal_emit_by_name(session, "completed", FALSE);
}
static PolkitAgentSession *fixture_new(PolkitIdentity *identity, const char *cookie) {
    session_uid = POLKIT_IS_UNIX_USER(identity) ? polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity)) : (uid_t)-1;
    return polkit_agent_session_new(identity, cookie);
}
#define polkit_agent_session_new fixture_new
#define polkit_agent_session_initiate fixture_initiate
#define polkit_agent_session_response fixture_response
#define polkit_agent_session_cancel fixture_cancel
#include "../src/luxepolkit-listener.c"
static GtkWidget *find_label(GtkWidget *widget, const char *text) {
    if (GTK_IS_LABEL(widget) && !g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)),text)) return widget;
    for (GtkWidget *child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child)) {
        GtkWidget *found=find_label(child,text);if(found)return found;
    }
    return NULL;
}
static GtkWidget *find_entry(GtkWidget *widget) {
    if(GTK_IS_ENTRY(widget))return widget;
    for(GtkWidget *child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child)) {
        GtkWidget *found=find_entry(child);if(found)return found;
    }
    return NULL;
}
static GtkWidget *find_selector(GtkWidget *widget, const char *label) {
    if (GTK_IS_CHECK_BUTTON(widget) && !g_strcmp0(gtk_check_button_get_label(GTK_CHECK_BUTTON(widget)), label)) return widget;
    for (GtkWidget *child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child)) {
        GtkWidget *found=find_selector(child, label); if(found)return found;
    }
    return NULL;
}
#ifdef XDOTOOL_PATH
static void click_widget(GtkWidget *widget, GtkWindow *window) {
    graphene_point_t origin = GRAPHENE_POINT_INIT(0, 0), position;
    g_assert_true(gtk_widget_compute_point(widget, GTK_WIDGET(window), &origin, &position));
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    Display *display = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surface));
    Window child;
    int x, y;
    g_assert_true(XTranslateCoordinates(display, gdk_x11_surface_get_xid(surface),
                                       DefaultRootWindow(display), 0, 0, &x, &y, &child));
    g_autofree char *xs = g_strdup_printf("%d", x + (int)position.x + 8);
    g_autofree char *ys = g_strdup_printf("%d", y + (int)position.y + gtk_widget_get_height(widget)/2);
    const char *argv[] = {XDOTOOL_PATH, "mousemove", "--sync", xs, ys, "click", "1", NULL};
    int status;
    g_autoptr(GError) error = NULL;
    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error));
    g_assert_no_error(error);
    g_assert_true(g_spawn_check_wait_status(status, &error));
    g_assert_no_error(error);
    for (guint i = 0; i < 100 && !gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)); i++) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}
static void keyboard_select(GtkWidget *widget) {
    g_assert_true(gtk_widget_grab_focus(widget));
    const char *argv[] = {XDOTOOL_PATH, "key", "space", NULL};
    int status;
    g_autoptr(GError) error = NULL;
    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error));
    g_assert_no_error(error);
    g_assert_true(g_spawn_check_wait_status(status, &error));
    g_assert_no_error(error);
    for (guint i = 0; i < 100 && !gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)); i++) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)));
}
#endif
static gboolean empty_done=FALSE;
static gboolean empty_ok=TRUE;
static GError *empty_err=NULL;
static void on_empty_ready(GObject *src,GAsyncResult *res,gpointer user_data) {
    (void)src;
    GError *err=NULL;
    gboolean ok=initiate_authentication_finish(POLKIT_AGENT_LISTENER(user_data),res,&err);
    empty_ok=ok;empty_err=err;empty_done=TRUE;
}
static gpointer cancel_worker(gpointer cancellable) {
    g_cancellable_cancel(cancellable);
    return NULL;
}
static void assert_input_grabs(gboolean held) {
#ifdef GDK_WINDOWING_X11
    Display *other = XOpenDisplay(NULL);
    g_assert_nonnull(other);
    Window root = DefaultRootWindow(other);
    int pointer = XGrabPointer(other, root, False, ButtonPressMask,
                               GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    int keyboard = XGrabKeyboard(other, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    g_assert_cmpint(pointer, ==, held ? AlreadyGrabbed : GrabSuccess);
    g_assert_cmpint(keyboard, ==, held ? AlreadyGrabbed : GrabSuccess);
    XUngrabPointer(other, CurrentTime);
    XUngrabKeyboard(other, CurrentTime);
    XCloseDisplay(other);
#else
    (void) held;
#endif
}
static void delayed_prompt(GtkDialog *dialog, GtkWidget *entry) {
    /* Real polkit asks asynchronously, after the disabled entry loses focus. */
    g_assert_false(gtk_widget_get_sensitive(entry));
    while (g_main_context_iteration(NULL, FALSE));
    g_signal_emit_by_name(active, "request", "Password: ", FALSE);
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(dialog));
    g_assert_true(focus == entry || (focus && gtk_widget_is_ancestor(focus, entry)));
    assert_input_grabs(TRUE);
}
static void test_grab_contention(LuxePolkitListener *listener, PolkitDetails *details,
                                 gboolean release_early, gboolean pointer_conflict)
{
    Display *other = XOpenDisplay(NULL);
    g_assert_nonnull(other);
    if (pointer_conflict)
        g_assert_cmpint(XGrabPointer(other, DefaultRootWindow(other), False, ButtonPressMask,
                                    GrabModeAsync, GrabModeAsync, None, None, CurrentTime), ==, GrabSuccess);
    else
        g_assert_cmpint(XGrabKeyboard(other, DefaultRootWindow(other), False, GrabModeAsync,
                                     GrabModeAsync, CurrentTime), ==, GrabSuccess);
    XSync(other, False);
    empty_done = FALSE; empty_ok = TRUE;
    GCancellable *cancel = g_cancellable_new();
    GList *ids = g_list_append(NULL, polkit_unix_user_new(getuid()));
    initiate_authentication(POLKIT_AGENT_LISTENER(listener), "test.grab", "Test", "", details,
                            "fixture-cookie-grab", ids, cancel, on_empty_ready, listener);
    gint64 start = g_get_monotonic_time();
    while (g_get_monotonic_time() - start < 250000) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
    g_assert_false(empty_done);
    GListModel *windows = gtk_window_get_toplevels();
    GtkWindow *dialog = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
        GtkWindow *candidate = g_list_model_get_item(windows, i);
        if (GTK_IS_DIALOG(candidate)) dialog = g_object_ref(candidate);
        g_object_unref(candidate);
    }
    g_assert_nonnull(dialog);
    GtkWidget *entry = find_entry(GTK_WIDGET(dialog));
    g_assert_nonnull(entry);
    g_assert_false(gtk_widget_get_sensitive(entry));
    /* The successful half of a failed grab must not remain held. */
    if (pointer_conflict) {
        g_assert_cmpint(XGrabKeyboard(other, DefaultRootWindow(other), False, GrabModeAsync,
                                     GrabModeAsync, CurrentTime), ==, GrabSuccess);
        XUngrabKeyboard(other, CurrentTime);
    } else {
        g_assert_cmpint(XGrabPointer(other, DefaultRootWindow(other), False, ButtonPressMask,
                                    GrabModeAsync, GrabModeAsync, None, None, CurrentTime), ==, GrabSuccess);
        XUngrabPointer(other, CurrentTime);
    }
    XSync(other, False);
    if (release_early) {
        if (pointer_conflict) XUngrabPointer(other, CurrentTime);
        else XUngrabKeyboard(other, CurrentTime);
        XSync(other, False);
        gint64 deadline = g_get_monotonic_time() + 2000000;
        while (!gtk_widget_get_sensitive(entry) && g_get_monotonic_time() < deadline) {
            while (g_main_context_iteration(NULL, FALSE));
            g_usleep(1000);
        }
        g_assert_true(gtk_widget_get_sensitive(entry));
        gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    }
    while (!empty_done) g_main_context_iteration(NULL, TRUE);
    g_assert_false(empty_ok);
    if (release_early) g_assert_error(empty_err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    else g_assert_error(empty_err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error(&empty_err);
    XUngrabKeyboard(other, CurrentTime); XUngrabPointer(other, CurrentTime); XCloseDisplay(other);
    g_object_unref(dialog);
    g_list_free_full(ids, g_object_unref);
    g_object_unref(cancel);
}
int main(void) {
    g_assert_false(gtk_is_initialized());g_assert_cmpint(getuid(), !=, 0);
    guint log_handler = g_log_set_handler(NULL, G_LOG_LEVEL_DEBUG, check_debug_log, NULL);
    LuxePolkitListener *listener=g_object_new(LUXEPOLKIT_LISTENER_TYPE,NULL);
    PolkitDetails *details=polkit_details_new();
    polkit_details_insert(details, "sensitive-fixture", "private-detail-fixture");
    delay_request = TRUE;
    for(int fallback=0;fallback<2;fallback++) {
        GCancellable *cancel=g_cancellable_new();
        GList *ids=g_list_append(NULL,polkit_unix_user_new(0));
        if(!fallback)ids=g_list_append(ids,polkit_unix_user_new(getuid()));
        initiate_authentication(POLKIT_AGENT_LISTENER(listener),"test.ui","Authentication is required to change a setting.","",details,"fixture-cookie",ids,cancel,NULL,NULL);
        g_assert_true(gtk_is_initialized());
        GListModel *windows=gtk_window_get_toplevels();
        GtkDialog *dialog=NULL;
        for(guint i=0;i<g_list_model_get_n_items(windows);i++) {
            GtkWidget *window=g_list_model_get_item(windows,i);
            if(GTK_IS_DIALOG(window))dialog=GTK_DIALOG(g_object_ref(window));
            g_object_unref(window);
        }
        g_assert_nonnull(dialog);
        const char *account=fallback?"root":g_get_user_name();
        GtkWidget *selector=find_selector(GTK_WIDGET(dialog), "root");
        GtkWidget *current=find_selector(GTK_WIDGET(dialog), g_get_user_name());
        if (fallback) { g_assert_null(selector); g_assert_nonnull(find_label(GTK_WIDGET(dialog),account)); }
        else { g_assert_nonnull(selector); g_assert_nonnull(current); g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(current))); }
        GtkWidget *entry=find_entry(GTK_WIDGET(dialog));g_assert_nonnull(entry);
        delayed_prompt(dialog, entry);
        g_assert_false(gtk_entry_get_visibility(GTK_ENTRY(entry)));
        g_assert_true(gtk_entry_get_activates_default(GTK_ENTRY(entry)));
        gtk_editable_set_text(GTK_EDITABLE(entry),"fixture-response");
        gtk_dialog_response(dialog,GTK_RESPONSE_OK);g_assert_cmpint(replies,==,fallback ? 3 : 1);
        g_assert_true(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(dialog, GTK_RESPONSE_CANCEL)));
        if (!fallback) {
            PolkitAgentSession *previous = g_object_ref(active);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(selector), TRUE);
            g_assert_true(active != previous);
            g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(entry)), ==, "");
            g_signal_emit_by_name(previous, "completed", TRUE);
            g_object_unref(previous);
            g_assert_true(GTK_IS_DIALOG(dialog));
            delayed_prompt(dialog, entry);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(current), TRUE);
            delayed_prompt(dialog, entry);
            gtk_editable_set_text(GTK_EDITABLE(entry), "fixture-response");
            gtk_dialog_response(dialog, GTK_RESPONSE_OK);
        }
        PolkitAgentSession *old=g_object_ref(active);
        g_signal_emit_by_name(old,"completed",FALSE);g_object_unref(old);
        delayed_prompt(dialog, entry);
        g_assert_nonnull(find_label(GTK_WIDGET(dialog),"Authentication failed. Please try again."));
        g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(entry)),==,"");
        g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(dialog)));
        gtk_dialog_response(dialog,GTK_RESPONSE_CANCEL);
        g_assert_true(g_cancellable_is_cancelled(cancel));
        while(g_main_context_iteration(NULL,FALSE));
        assert_input_grabs(FALSE);
        g_object_unref(dialog);g_list_free_full(ids,g_object_unref);g_object_unref(cancel);
    }
    g_assert_cmpuint(debug_messages, >, 0);
    g_log_remove_handler(NULL, log_handler);
    delay_request = FALSE;
    /* Only the alternate identity is accepted in this mock scenario. */
    {
        GCancellable *cancel = g_cancellable_new();
        GList *ids = g_list_append(NULL, polkit_unix_user_new(getuid()));
        ids = g_list_append(ids, polkit_unix_user_new(0));
        empty_done = FALSE; empty_ok = FALSE;
        initiate_authentication(POLKIT_AGENT_LISTENER(listener), "test.alternate", "Test", "", details,
                                "fixture-cookie", ids, cancel, on_empty_ready, listener);
        GListModel *windows = gtk_window_get_toplevels();
        for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
            GtkWidget *window = g_list_model_get_item(windows, i);
            if (GTK_IS_DIALOG(window)) {
                GtkWidget *alternate = find_selector(window, "root");
                g_assert_nonnull(alternate);
                while (g_main_context_iteration(NULL, FALSE));
                assert_input_grabs(TRUE);
#ifdef XDOTOOL_PATH
                click_widget(alternate, GTK_WINDOW(window));
                keyboard_select(find_selector(window, g_get_user_name()));
                g_assert_cmpuint(session_uid, ==, getuid());
                click_widget(alternate, GTK_WINDOW(window));
#else
                gtk_check_button_set_active(GTK_CHECK_BUTTON(alternate), TRUE);
#endif
                g_assert_cmpuint(session_uid, ==, 0);
                gtk_editable_set_text(GTK_EDITABLE(find_entry(window)), "fixture-response");
                complete_in_response = TRUE;
                gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_OK);
                complete_in_response = FALSE;
                g_object_unref(window);
                break;
            }
            g_object_unref(window);
        }
        while (!empty_done) g_main_context_iteration(NULL, TRUE);
        g_assert_true(empty_ok); g_assert_no_error(empty_err);
        g_list_free_full(ids, g_object_unref); g_object_unref(cancel);
    }
    test_grab_contention(listener, details, TRUE, FALSE);
    test_grab_contention(listener, details, FALSE, FALSE);
    test_grab_contention(listener, details, TRUE, TRUE);
    test_grab_contention(listener, details, FALSE, TRUE);
    /* Cancellation before setup and from another thread must complete once
     * without leaving a prompt or touching GTK outside its owning context. */
    for (int early = 0; early < 2; early++) {
        GCancellable *cancel = g_cancellable_new();
        GList *ids = g_list_append(NULL, polkit_unix_user_new(getuid()));
        empty_done = FALSE; empty_ok = TRUE;
        if (early) g_cancellable_cancel(cancel);
        initiate_authentication(POLKIT_AGENT_LISTENER(listener), "test.cancel", "Cancelled request", "", details,
                                "fixture-cookie", ids, cancel, on_empty_ready, listener);
        if (!early) g_thread_join(g_thread_new("cancel-fixture", cancel_worker, cancel));
        while (!empty_done) g_main_context_iteration(NULL, TRUE);
        g_assert_false(empty_ok);
        g_assert_error(empty_err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        g_clear_error(&empty_err);
        g_list_free_full(ids, g_object_unref); g_object_unref(cancel);
    }
    /* Exercise synchronous success inside the response callback. */
    {
        GCancellable *cancel = g_cancellable_new();
        GList *ids = g_list_append(NULL, polkit_unix_user_new(getuid()));
        empty_done = FALSE;
        initiate_authentication(POLKIT_AGENT_LISTENER(listener), "test.sync", "Synchronous request", "", details,
                                "fixture-cookie", ids, cancel, on_empty_ready, listener);
        GListModel *windows = gtk_window_get_toplevels();
        for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
            GtkWidget *window = g_list_model_get_item(windows, i);
            if (GTK_IS_DIALOG(window)) {
                gtk_editable_set_text(GTK_EDITABLE(find_entry(window)), "fixture-response");
                complete_in_response = TRUE;
                gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_OK);
                g_object_unref(window);
                break;
            }
            g_object_unref(window);
        }
        while (!empty_done) g_main_context_iteration(NULL, TRUE);
        g_assert_true(empty_ok); g_assert_no_error(empty_err);
        g_list_free_full(ids, g_object_unref); g_object_unref(cancel);
    }
    /* P1 #10: empty identities must fail, not authorize with no prompt. */
    {
        empty_done = FALSE;
        empty_ok = TRUE;
        guint before=g_list_model_get_n_items(gtk_window_get_toplevels());
        GCancellable *cancel=g_cancellable_new();
        initiate_authentication(POLKIT_AGENT_LISTENER(listener),"test.ui","Authentication is required to change a setting.","",details,"fixture-cookie-empty",NULL,cancel,on_empty_ready,listener);
        for(int i=0;i<100 && !empty_done;i++)g_main_context_iteration(NULL,TRUE);
        g_assert_true(empty_done);
        g_assert_false(empty_ok);
        g_assert_nonnull(empty_err);
        g_assert_true(g_error_matches(empty_err,G_IO_ERROR,G_IO_ERROR_FAILED));
        g_assert_nonnull(g_strrstr(empty_err->message,"identities"));
        g_clear_error(&empty_err);
        /* No dialog may be left presented for the empty request. */
        while(g_main_context_iteration(NULL,FALSE));
        g_assert_cmpuint(g_list_model_get_n_items(gtk_window_get_toplevels()),==,before);
        g_object_unref(cancel);
    }
    {
        GList *ids = g_list_append(NULL, polkit_unix_user_new(getuid()));
        fail_initiate = TRUE; empty_done = FALSE;
        initiate_authentication(POLKIT_AGENT_LISTENER(listener), "test.startup-failure", "Startup failure", "", details,
                                "fixture-cookie", ids, NULL, on_empty_ready, listener);
        while (!empty_done) g_main_context_iteration(NULL, TRUE);
        g_assert_false(empty_ok);
        g_assert_error(empty_err, G_IO_ERROR, G_IO_ERROR_FAILED);
        g_clear_error(&empty_err);
        g_list_free_full(ids, g_object_unref);
    }
    g_clear_object(&active);g_object_unref(details);g_object_unref(listener);
    g_print("PASS: permitted current user, administrator fallback, masked entry, submission, inline retry, cancellation; no real authentication invoked.\n");
    return 0;
}
