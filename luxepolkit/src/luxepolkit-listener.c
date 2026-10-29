#include "../luxe-about.h"
/*
 *      lx-polkit-listener.c
 *
 *      Copyright 2010 PCMan <pcman.tw@gmail.com>
 *      Copyright (C) 2026 Jerry Howell
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "luxepolkit-listener.h"
#include "theme-scheme.h"
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#endif
#include <glib/gi18n.h>
#include <string.h>
#include "dialog-ui.h"

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <unistd.h>

#ifdef G_ENABLE_DEBUG
#define DEBUG(...)  g_debug(__VA_ARGS__)
#else
#define DEBUG(...)
#endif

G_DEFINE_TYPE(LuxePolkitListener, luxepolkit_listener, POLKIT_AGENT_TYPE_LISTENER);
static int ready_fd = -1;

typedef struct _DlgData DlgData;
struct _DlgData
{
    GTask* result;
    GtkWidget* dlg;
    /* Only identities supplied by the authority may be selected. */
    GList* identities;
    gint selected;
    GtkWidget* id_label;
    GtkWidget* request;
    GtkWidget* request_label;
    GtkWidget* detail;
    GtkWidget* account;
    GCancellable* cancellable;
    gulong cancel_handler;
    char* cookie;
    PolkitAgentSession* session;
    gboolean response_submitted;
#ifdef GDK_WINDOWING_X11
    GdkDisplay* gdk_display;
    Display* xdisplay;
    Window xwindow;
#endif
    gboolean pointer_grabbed;
    gboolean keyboard_grabbed;
    gboolean prompt_ready;
    guint grab_retry;
    gint64 grab_deadline;
};

/* defined in luxepolkit.c */

static void dlg_data_free(DlgData* data);
static void on_dlg_response(GtkDialog* dlg, int response, DlgData* data);
static gboolean start_session(DlgData* data);
static void on_dialog_mapped(GtkWidget* dialog, DlgData* data);
static void grab_input(DlgData* data);
static void release_input(DlgData* data);
#ifndef POLKIT_GRAB_TIMEOUT_MS
#define POLKIT_GRAB_TIMEOUT_MS 3000
#endif

static void update_prompt_access(DlgData *data)
{
    gboolean enabled = data->prompt_ready && data->pointer_grabbed && data->keyboard_grabbed;
    gtk_widget_set_sensitive(data->request, enabled);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(data->dlg), GTK_RESPONSE_OK, enabled);
    if (enabled) gtk_widget_grab_focus(data->request);
    if (enabled && ready_fd >= 0) {
        const char ready = 'R';
        ssize_t written;
        do written = write(ready_fd, &ready, 1); while (written < 0 && errno == EINTR);
        close(ready_fd);
        ready_fd = -1;
    }
}

#ifdef GDK_WINDOWING_X11
/* GTK4 has no gtk_grab_add().  On the X11 session used by Luxe, use the
 * server-side pointer/keyboard grabs so a password dialog cannot be moved
 * away from or clicked through while it is active. */
static void
x_error_trap_push (GdkDisplay* display)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_x11_display_error_trap_push (display);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

static void
x_error_trap_pop (GdkDisplay* display)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_x11_display_error_trap_pop_ignored (display);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

static Window
surface_xid (GdkSurface* surface)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    return gdk_x11_surface_get_xid (surface);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

static Display*
xdisplay_from_gdk (GdkDisplay* display)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    return gdk_x11_display_get_xdisplay (display);
    G_GNUC_END_IGNORE_DEPRECATIONS
}
#endif

static void
release_input (DlgData* data)
{
#ifdef GDK_WINDOWING_X11
    if (data->xdisplay == NULL)
        return;
    x_error_trap_push (data->gdk_display);
    if (data->keyboard_grabbed)
        XUngrabKeyboard (data->xdisplay, CurrentTime);
    if (data->pointer_grabbed)
        XUngrabPointer (data->xdisplay, CurrentTime);
    XFlush (data->xdisplay);
    x_error_trap_pop (data->gdk_display);
#endif
    data->pointer_grabbed = FALSE;
    data->keyboard_grabbed = FALSE;
#ifdef GDK_WINDOWING_X11
    data->gdk_display = NULL;
    data->xdisplay = NULL;
    data->xwindow = None;
#endif
}

static void
grab_input (DlgData* data)
{
#ifdef GDK_WINDOWING_X11
    GdkSurface* surface;
    GdkDisplay* display;
    Display* xdisplay;
    Window xwindow;
    int pointer_result;
    int keyboard_result;

    if (data->pointer_grabbed || data->keyboard_grabbed || data->dlg == NULL)
        return;
    surface = gtk_native_get_surface (GTK_NATIVE (data->dlg));
    if (surface == NULL)
        return;
    display = gdk_surface_get_display (surface);
    if (!GDK_IS_X11_DISPLAY (display))
        return;
    xdisplay = xdisplay_from_gdk (display);
    xwindow = surface_xid (surface);
    if (xdisplay == NULL || xwindow == None)
        return;

    data->gdk_display = display;
    data->xdisplay = xdisplay;
    data->xwindow = xwindow;
    x_error_trap_push (display);
    pointer_result = XGrabPointer (xdisplay, xwindow, True,
                                   ButtonPressMask | ButtonReleaseMask |
                                   PointerMotionMask | LeaveWindowMask,
                                   GrabModeAsync, GrabModeAsync, xwindow,
                                   None, CurrentTime);
    keyboard_result = XGrabKeyboard (xdisplay, xwindow, True,
                                     GrabModeAsync, GrabModeAsync, CurrentTime);
    XFlush (xdisplay);
    x_error_trap_pop (display);

    /* Keep the grab all-or-nothing; a half-grabbed password dialog is worse
     * than the normal focus-only fallback. */
    data->pointer_grabbed = pointer_result == GrabSuccess;
    data->keyboard_grabbed = keyboard_result == GrabSuccess;
    if (data->pointer_grabbed && data->keyboard_grabbed)
        return;
    release_input (data);
#endif
}

static gboolean retry_input_grab(gpointer pointer)
{
    DlgData *data = pointer;
    grab_input(data);
    if (data->pointer_grabbed && data->keyboard_grabbed) {
        data->grab_retry = 0;
        gtk_widget_set_visible(data->detail, FALSE);
        update_prompt_access(data);
        return G_SOURCE_REMOVE;
    }
    if (g_get_monotonic_time() < data->grab_deadline) return G_SOURCE_CONTINUE;
    data->grab_retry = 0;
    if (data->session) {
        g_signal_handlers_disconnect_matched(data->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);
        polkit_agent_session_cancel(data->session);
    }
    g_task_return_new_error(data->result, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "Cannot secure keyboard and pointer input for authentication");
    dlg_data_free(data);
    return G_SOURCE_REMOVE;
}

static void
on_dialog_mapped (GtkWidget* dialog, DlgData* data)
{
    (void) dialog;
    grab_input (data);
    update_prompt_access(data);
    if (!data->pointer_grabbed || !data->keyboard_grabbed) {
        if (data->grab_retry) g_source_remove(data->grab_retry);
        gtk_label_set_text(GTK_LABEL(data->detail), _("Waiting to secure keyboard and pointer input…"));
        gtk_widget_set_visible(data->detail, TRUE);
        data->grab_deadline = g_get_monotonic_time() + POLKIT_GRAB_TIMEOUT_MS * 1000;
        data->grab_retry = g_timeout_add(100, retry_input_grab, data);
    }
}

static void
dlg_data_free(DlgData* data)
{
    DEBUG("dlg_data_free");
    g_task_set_task_data(data->result, NULL, NULL);
    if (data->grab_retry) g_source_remove(data->grab_retry);
    if (data->cancel_handler)
        g_cancellable_disconnect(data->cancellable, data->cancel_handler);
    release_input (data);
    if (data->dlg)
        gtk_window_destroy(GTK_WINDOW(data->dlg));

    g_object_unref(data->cancellable);
    /* NOTE: NULL on the no-identities path; the old code unref'd it
     * unconditionally (GLib-CRITICAL). */
    if (data->session) {
        g_signal_handlers_disconnect_matched(data->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);
        g_object_unref(data->session);
    }
    g_list_free_full(data->identities, g_object_unref);
    g_object_unref(data->result);
    g_free(data->cookie);
    g_free(data);
}

static void on_completed(PolkitAgentSession* session, gboolean authorized, DlgData* data)
{
    DEBUG("on_complete");
    gtk_widget_set_sensitive(data->dlg, TRUE);

    if(!authorized && !g_cancellable_is_cancelled(data->cancellable))
    {
        if (!data->response_submitted) {
            g_task_return_new_error(data->result, G_IO_ERROR, G_IO_ERROR_FAILED,
                                    "Authentication session ended before a response could be submitted");
            dlg_data_free(data);
            return;
        }
        gtk_label_set_text(GTK_LABEL(data->detail), _("Authentication failed. Please try again."));
        gtk_widget_add_css_class(data->detail, "error");
        gtk_widget_set_visible(data->detail, TRUE);
        /* initiate a new session */
        g_object_unref(data->session);
        data->session = NULL;
        gtk_editable_set_text(GTK_EDITABLE(data->request), "");
        gtk_widget_grab_focus(data->request);
        if (start_session(data))
            return;
        /* No restartable identity: complete the request deterministically
         * instead of leaving polkit's caller waiting forever. */
        g_task_return_error(data->result,
            g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, _("Authentication failed.")));
        dlg_data_free(data);
        return;
    }
    if (g_cancellable_is_cancelled(data->cancellable))
        g_task_return_new_error(data->result, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Authentication cancelled");
    else
        g_task_return_boolean(data->result, TRUE);
    dlg_data_free(data);
}

static void on_request(PolkitAgentSession* session, gchar* request, gboolean echo_on, DlgData* data)
{
    const char* msg;
    DEBUG("on_request");
    if(strcmp("Password: ", request) == 0)
        msg = _("Password");
    else
        msg = request;
    gtk_label_set_text(GTK_LABEL(data->request_label), msg);
    gtk_entry_set_visibility(GTK_ENTRY(data->request), echo_on);
    data->prompt_ready = TRUE;
    update_prompt_access(data);
    /* Session startup disables the entry while polkit prepares its prompt.
     * Restore focus when it is actually ready, including password retries. */
}

static void on_show_error(PolkitAgentSession* session, gchar* text, DlgData* data)
{
    DEBUG("on error");
    gtk_label_set_text(GTK_LABEL(data->detail), text);
    gtk_widget_add_css_class(data->detail, "error");
    gtk_widget_set_visible(data->detail, TRUE);
}

static void on_show_info(PolkitAgentSession* session, gchar* text, DlgData* data)
{
    DEBUG("on info");
    gtk_label_set_text(GTK_LABEL(data->detail), text);
    gtk_widget_remove_css_class(data->detail, "error");
    gtk_widget_set_visible(data->detail, TRUE);
}

void on_dlg_response(GtkDialog* dlg, int response, DlgData* data)
{
    DEBUG("on_response: %d", response);
    if(response == GTK_RESPONSE_OK)
    {
        if (!data->session || !gtk_widget_get_sensitive(data->request)) return;
        char* request = g_strdup(gtk_editable_get_text(GTK_EDITABLE(data->request)));
        gtk_widget_set_visible(data->detail, FALSE);
        /* Clear the entry immediately; the plaintext password must not
         * linger in the buffer for the remainder of the dialog lifetime. */
        gtk_editable_set_text(GTK_EDITABLE(data->request), "");
        gtk_widget_set_sensitive(data->request, FALSE);
        gtk_dialog_set_response_sensitive(dlg, GTK_RESPONSE_OK, FALSE);
        data->response_submitted = TRUE;
        g_autoptr(PolkitAgentSession) session = g_object_ref(data->session);
        polkit_agent_session_response(session, request);
        /* Session response may complete and free data synchronously. */
        volatile char *wipe = (volatile char *)request;
        for (gsize i = 0, length = strlen(request); i < length; i++) wipe[i] = 0;
        g_free(request);
    }
    else
        g_cancellable_cancel(data->cancellable);
}

static gboolean cancel_on_context(gpointer task)
{
    DlgData *data = g_task_get_task_data(G_TASK(task));
    if (!data) return G_SOURCE_REMOVE;
    DEBUG("on_cancelled");
    if(data->session) {
        g_autoptr(PolkitAgentSession) session = g_object_ref(data->session);
        polkit_agent_session_cancel(session);
    }
    else {
        g_task_return_new_error(data->result, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Authentication cancelled");
        dlg_data_free(data);
    }
    return G_SOURCE_REMOVE;
}

static void queue_cancelled(GCancellable *cancellable, GTask *task)
{
    /* Cancellation can originate on another thread. Attach an idle source,
     * rather than invoke directly, so teardown never runs inside disconnect. */
    GSource *source = g_idle_source_new();
    g_source_set_callback(source, cancel_on_context, g_object_ref(task), g_object_unref);
    g_source_attach(source, g_task_get_context(task));
    g_source_unref(source);
}

/* Create and start the authentication session for the selected identity.
 * Returns FALSE when no identity is selectable. */
static gboolean start_session(DlgData* data)
{
    DEBUG("start_session");
    PolkitIdentity* id = POLKIT_IDENTITY(g_list_nth_data(data->identities, data->selected));
    if(!id)
        return FALSE;
    if(data->session) /* delete old session object */
    {
        /* prevent receiving completed signal. */
        g_signal_handlers_disconnect_matched(data->session, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, data);
        polkit_agent_session_cancel(data->session);
        g_object_unref(data->session);
    }
    /* create authentication session for currently selected user */
    data->response_submitted = FALSE;
    data->prompt_ready = FALSE;
    gtk_widget_set_sensitive(data->request, FALSE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(data->dlg), GTK_RESPONSE_OK, FALSE);
    data->session = polkit_agent_session_new(id, data->cookie);
    g_signal_connect(data->session, "completed", G_CALLBACK(on_completed), data);
    g_signal_connect(data->session, "request", G_CALLBACK(on_request), data);
    g_signal_connect(data->session, "show-error", G_CALLBACK(on_show_error), data);
    g_signal_connect(data->session, "show-info", G_CALLBACK(on_show_info), data);
    g_autoptr(PolkitAgentSession) session = g_object_ref(data->session);
    polkit_agent_session_initiate(session);
    return TRUE;
}

static char *identity_name(PolkitIdentity *identity)
{
    if (POLKIT_IS_UNIX_USER(identity)) {
        struct passwd *pwd = getpwuid(polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity)));
        if (pwd) return g_strdup(pwd->pw_name);
    } else if (POLKIT_IS_UNIX_GROUP(identity)) {
        struct group *grp = getgrgid(polkit_unix_group_get_gid(POLKIT_UNIX_GROUP(identity)));
        if (grp) return g_strdup_printf(_("Group: %s"), grp->gr_name);
    }
    return polkit_identity_to_string(identity);
}

static void identity_changed(GtkCheckButton *selector, DlgData *data)
{
    if (!gtk_check_button_get_active(selector)) return;
    guint selected = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(selector), "identity-index"));
    if (selected >= g_list_length(data->identities) ||
        selected == (guint)data->selected) return;
    data->selected = selected;
    gtk_editable_set_text(GTK_EDITABLE(data->request), "");
    gtk_widget_set_visible(data->detail, FALSE);
    /* Starting can complete synchronously and free data. */
    start_session(data);
}

static void initiate_authentication(PolkitAgentListener  *listener,
                                    const gchar          *action_id,
                                    const gchar          *message,
                                    const gchar          *icon_name,
                                    PolkitDetails        *details,
                                    const gchar          *cookie,
                                    GList                *identities,
                                    GCancellable         *cancellable,
                                    GAsyncReadyCallback   callback,
                                    gpointer              user_data)
{
    luxe_gtk_init();
    /* Re-read the same preference helper used by the process owner. */
    g_autoptr(GSettings) settings = luxepolkit_theme_settings_new();
    luxepolkit_apply_color_scheme(settings);
    GtkBuilder* b = gtk_builder_new();
    GtkWidget *icon, *msg;
    GList* l;
    DlgData* data = g_new0(DlgData, 1);
    DEBUG("init_authentication");
    /* NULL cancellable: cancellation is handled manually via on_cancelled;
     * handing the auth GCancellable to the task would complete it behind
     * our back and risk a double return. */
    data->result = G_TASK(g_task_new(listener, NULL, callback, user_data));
    g_task_set_source_tag(data->result, initiate_authentication);

    data->cancellable = cancellable ? g_object_ref(cancellable) : g_cancellable_new();
    data->cookie = g_strdup(cookie);
    g_task_set_task_data(data->result, data, NULL);
    if (g_cancellable_is_cancelled(data->cancellable)) {
        g_task_return_new_error(data->result, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Authentication cancelled");
        g_object_unref(b);
        dlg_data_free(data);
        return;
    }

    /* create the dialog, load from GtkBuilder ui definition file. */
    {
        GError* ui_error = NULL;
        gtk_builder_add_from_file(b, PACKAGE_UI_DIR "/luxepolkit.ui", &ui_error);
        if(ui_error || !(data->dlg = (GtkWidget*)gtk_builder_get_object(b, "dlg")))
        {
            g_clear_error(&ui_error);
            g_object_unref(b);
            /* Fail the authentication request cleanly instead of crashing
             * on a NULL dialog; polkit's caller receives the error. */
            g_task_return_error(data->result,
                g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                            _("Cannot load the authentication dialog: %s"),
                            PACKAGE_UI_DIR "/luxepolkit.ui"));
            dlg_data_free(data);
            return;
        }
    }
    icon = (GtkWidget*)gtk_builder_get_object(b, "icon");
    msg = (GtkWidget*)gtk_builder_get_object(b, "msg");
    data->id_label = (GtkWidget*)gtk_builder_get_object(b, "label1");
    data->request = (GtkWidget*)gtk_builder_get_object(b, "request");
    data->request_label = (GtkWidget*)gtk_builder_get_object(b, "request_label");
    data->detail = GTK_WIDGET(gtk_builder_get_object(b, "detail"));
    data->account = GTK_WIDGET(gtk_builder_get_object(b, "account"));
    g_object_unref(b);

    g_signal_connect(data->dlg, "response", G_CALLBACK(on_dlg_response), data);
    g_signal_connect(data->dlg, "map", G_CALLBACK(on_dialog_mapped), data);
    gtk_window_set_modal (GTK_WINDOW (data->dlg), TRUE);
    data->cancel_handler = g_cancellable_connect(data->cancellable, G_CALLBACK(queue_cancelled),
                                                g_object_ref(data->result), g_object_unref);

    /* Action buttons (the .ui carries content only; GtkDialog has no
     * action-area child in GTK4). */
    luxepolkit_prepare_dialog(GTK_DIALOG(data->dlg));

    /* set dialog icon (GTK_STOCK_DIALOG_AUTHENTICATION is "dialog-password") */
    gtk_image_set_from_icon_name(GTK_IMAGE(icon), icon_name && *icon_name ? icon_name : "system-lock-screen-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 40);

    /* set message prompt */
    gtk_label_set_text(GTK_LABEL(msg), message);

    /* Hold the authority's permitted identities as referenced objects. */
    if( identities )
    {
        int preferred = 0, index = 0;
        for(l = identities; l; l=l->next, ++index)
        {
            PolkitIdentity* id = (PolkitIdentity*)l->data;
            if(POLKIT_IS_UNIX_USER(id)
               && polkit_unix_user_get_uid(POLKIT_UNIX_USER(id)) == getuid())
                preferred = index;
            data->identities = g_list_append(data->identities, g_object_ref(id));
        }
        data->selected = preferred;
        /* Polkit supplies the permitted identities. Prefer the logged-in user;
         * never substitute an identity that the policy did not authorize. */
        if (data->identities->next) {
            GtkWidget *choices = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            GtkCheckButton *first = NULL;
            guint choice = 0;
            for (GList *item = data->identities; item; item = item->next, choice++) {
                g_autofree char *name = identity_name(item->data);
                GtkWidget *button = gtk_check_button_new_with_label(name);
                if (first) gtk_check_button_set_group(GTK_CHECK_BUTTON(button), first);
                else first = GTK_CHECK_BUTTON(button);
                g_object_set_data(G_OBJECT(button), "identity-index", GUINT_TO_POINTER(choice));
                if (choice == (guint)preferred) gtk_check_button_set_active(GTK_CHECK_BUTTON(button), TRUE);
                gtk_box_append(GTK_BOX(choices), button);
                g_signal_connect(button, "toggled", G_CALLBACK(identity_changed), data);
            }
            gtk_widget_set_hexpand(choices, TRUE);
            gtk_box_append(GTK_BOX(gtk_widget_get_parent(data->account)), choices);
            gtk_widget_set_visible(data->account, FALSE);
        } else {
            g_autofree char *name = identity_name(data->identities->data);
            gtk_label_set_text(GTK_LABEL(data->account), name);
        }
        gtk_window_present(GTK_WINDOW(data->dlg));
        gtk_widget_grab_focus(data->request);
        if (!start_session(data))
        {
            g_task_return_error(data->result,
                g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                            _("No identities available for authentication.")));
            dlg_data_free(data);
            return;
        }
    }
    else
    {
        DEBUG("no identities list, is this an error?");
        gtk_widget_set_visible(data->id_label, FALSE);
        /* P1 #10: never complete an authentication request successfully
         * when polkit supplied no identities; fail so the caller denies. */
        g_task_return_error(data->result,
            g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                        _("No identities available for authentication.")));
        dlg_data_free(data);
        return;
    }
}

static gboolean initiate_authentication_finish(PolkitAgentListener  *listener,
                                              GAsyncResult         *res,
                                              GError              **error)
{
    (void) listener;
    DEBUG("init_authentication_finish");
    return g_task_propagate_boolean(G_TASK(res), error);
}

static void luxepolkit_listener_class_init(LuxePolkitListenerClass *klass)
{
    PolkitAgentListenerClass* pkal_class;

    pkal_class = POLKIT_AGENT_LISTENER_CLASS(klass);
    pkal_class->initiate_authentication = initiate_authentication;
    pkal_class->initiate_authentication_finish = initiate_authentication_finish;
}


static void luxepolkit_listener_init(LuxePolkitListener *self)
{
}


PolkitAgentListener *luxepolkit_listener_new(void)
{
	return g_object_new(LUXEPOLKIT_LISTENER_TYPE, NULL);
}

void luxepolkit_listener_set_ready_fd(int fd)
{
    ready_fd = fd;
}
