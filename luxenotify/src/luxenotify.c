/*
 * LuxeNotify - freedesktop.org notification service for Luxe
 * Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "luxenotify.h"
#include "luxe-renderer.h"
#include "notification-protocol.h"

#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#define NOTIFICATION_BUS_NAME LUXE_NOTIFY_BUS_NAME
#define NOTIFICATION_OBJECT_PATH LUXE_NOTIFY_OBJECT_PATH
#define NOTIFICATION_INTERFACE LUXE_NOTIFY_INTERFACE
#define LUXE_NOTIFICATION_INTERFACE LUXE_NOTIFY_CONTROL_INTERFACE

/* P1 #8a: bound live windows from untrusted D-Bus clients. Each live
 * notification owns a GTK4 window (+ pixbuf), so an uncapped queue lets one
 * client exhaust memory in this 24/7 daemon. When the cap is reached the
 * oldest notification is closed (reason 3 with NotificationClosed emitted,
 * matching CloseNotification semantics) to make room; replacement Notifys
 * reuse their id and never trigger eviction. */
#define MAX_LIVE_NOTIFICATIONS 32

/* NotificationClosed reasons per the freedesktop notification spec:
 * 1 = expired, 2 = dismissed by the user, 3 = closed by the app.
 * Reason 4 is undefined/reserved and must never be emitted. */
static LuxeNotification *find_notification(LuxeNotifyServer *server, guint id)
{
    for (GList *item = server->notifications; item != NULL; item = item->next) {
        LuxeNotification *notification = item->data;
        if (notification->id == id)
            return notification;
    }
    return NULL;
}

static guint allocate_notification_id(LuxeNotifyServer *server)
{
    do {
        server->next_id++;
        if (server->next_id == 0)
            server->next_id++;
    } while (find_notification(server, server->next_id) != NULL);
    return server->next_id;
}

void luxe_notify_server_action(LuxeNotifyServer *server,
                               LuxeNotification *notification,
                               const gchar *action_key)
{
    if (server->connection == NULL || notification == NULL || action_key == NULL)
        return;

    g_dbus_connection_emit_signal(server->connection,
                                  NULL,
                                  NOTIFICATION_OBJECT_PATH,
                                  NOTIFICATION_INTERFACE,
                                  "ActionInvoked",
                                  g_variant_new("(us)", notification->id, action_key),
                                  NULL);
}

void luxe_notify_server_active_count_changed(LuxeNotifyServer *server)
{
    if (server == NULL || server->connection == NULL)
        return;
    guint count = g_list_length(server->notifications);
    if (count == server->published_count)
        return;
    server->published_count = count;
    g_dbus_connection_emit_signal(server->connection,
                                  NULL,
                                  NOTIFICATION_OBJECT_PATH,
                                  LUXE_NOTIFICATION_INTERFACE,
                                  "ActiveCountChanged",
                                  g_variant_new("(u)", count),
                                  NULL);
}

void luxe_notify_server_close(LuxeNotifyServer *server,
                              LuxeNotification *notification,
                              guint reason,
                              gboolean emit_signal)
{
    if (server == NULL || notification == NULL ||
        g_list_find(server->notifications, notification) == NULL)
        return;

    guint id = notification->id;
    server->notifications = g_list_remove(server->notifications, notification);

    if (notification->timeout_id != 0) {
        g_source_remove(notification->timeout_id);
        notification->timeout_id = 0;
    }
    if (notification->icon_cancel != NULL)
        g_cancellable_cancel(notification->icon_cancel);
    if (notification->window != NULL) {
        GtkWindow *window = notification->window;
        notification->window = NULL;
        gtk_window_destroy(window);
    }

    g_clear_pointer(&notification->app_name, g_free);
    g_clear_object(&notification->icon_cancel);
    g_free(notification);

    if (emit_signal)
        luxe_notify_server_active_count_changed(server);

    if (emit_signal && server->connection != NULL) {
        g_dbus_connection_emit_signal(server->connection,
                                      NULL,
                                      NOTIFICATION_OBJECT_PATH,
                                      NOTIFICATION_INTERFACE,
                                      "NotificationClosed",
                                      g_variant_new("(uu)", id, reason),
                                      NULL);
    }
    luxe_notification_reposition_all(server);
}

static void return_capabilities(GDBusMethodInvocation *invocation)
{
    /* No "persistence": notifications live only in memory for this session,
     * so cross-session persistent storage must not be advertised. The
     * "resident" hint is still honoured for in-session lifetime. */
    g_dbus_method_invocation_return_value(invocation,
        g_variant_new("(^as)", LUXE_NOTIFY_CAPABILITIES));
}

static void handle_notify(LuxeNotifyServer *server,
                          GVariant *parameters,
                          GDBusMethodInvocation *invocation)
{
    /* Bus registration/capabilities/counts need no widgets or GDK connection.
     * Read current theme settings when the first card actually arrives. */
    if (!gtk_init_check()) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.freedesktop.Notifications.Error.Failed", "Cannot open the notification display");
        return;
    }
    luxe_gtk_init();
    const gchar *app_name;
    const gchar *app_icon;
    const gchar *summary;
    const gchar *body;
    guint replaces_id;
    gint expire_timeout;
    GVariant *actions = NULL;
    GVariant *hints = NULL;

    g_variant_get(parameters,
                  "(&su&s&s&s@as@a{sv}i)",
                  &app_name,
                  &replaces_id,
                  &app_icon,
                  &summary,
                  &body,
                  &actions,
                  &hints,
                  &expire_timeout);

    guint id = replaces_id;
    LuxeNotification *old = replaces_id == 0 ? NULL :
                            find_notification(server, replaces_id);
    /* A replacement is requested by the calling app, so the displaced
     * notification counts as closed by the app (reason 3). The signal is
     * suppressed to avoid a spurious close event for the surviving id. */
    if (old != NULL)
        luxe_notify_server_close(server, old, 3, FALSE);
    else {
        if (g_list_length(server->notifications) >= MAX_LIVE_NOTIFICATIONS) {
            luxe_notify_server_close(server,
                                     server->notifications->data, 3, TRUE);
        }
        id = allocate_notification_id(server);
    }

    LuxeNotification *notification = luxe_notification_window_new(server,
                                                                   id,
                                                                   app_name,
                                                                   app_icon,
                                                                   summary,
                                                                   body,
                                                                   actions,
                                                                   hints,
                                                                   expire_timeout);
    if (notification == NULL) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.freedesktop.Notifications.Error.Failed",
            "Unable to create the notification window");
    } else {
        server->notifications = g_list_append(server->notifications, notification);
        luxe_notify_server_active_count_changed(server);
        luxe_notification_reposition_all(server);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", id));
    }

    g_variant_unref(actions);
    g_variant_unref(hints);
}

static void method_call(GDBusConnection *connection,
                        const gchar *sender,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *method_name,
                        GVariant *parameters,
                        GDBusMethodInvocation *invocation,
                        gpointer user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    LuxeNotifyServer *server = user_data;

    if (g_str_equal(method_name, "GetCapabilities")) {
        return_capabilities(invocation);
    } else if (g_str_equal(method_name, "GetServerInformation")) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(ssss)", LUXE_NOTIFY_SERVER_NAME,
                          LUXE_NOTIFY_SERVER_VENDOR, VERSION,
                          LUXE_NOTIFY_SPEC_VERSION));
    } else if (g_str_equal(method_name, "CloseNotification")) {
        guint id = 0;
        g_variant_get(parameters, "(u)", &id);
        LuxeNotification *notification = find_notification(server, id);
        if (notification != NULL)
            luxe_notify_server_close(server, notification, 3, TRUE);
        g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_str_equal(method_name, "Notify")) {
        handle_notify(server, parameters, invocation);
    } else {
        g_dbus_method_invocation_return_error(invocation,
                                               G_DBUS_ERROR,
                                               G_DBUS_ERROR_UNKNOWN_METHOD,
                                               "Unknown notification method: %s",
                                               method_name);
    }
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = method_call,
    .get_property = NULL,
    .set_property = NULL
};

static void control_method_call(GDBusConnection *connection,
                                const gchar *sender,
                                const gchar *object_path,
                                const gchar *interface_name,
                                const gchar *method_name,
                                GVariant *parameters,
                                GDBusMethodInvocation *invocation,
                                gpointer user_data)
{
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)parameters;
    LuxeNotifyServer *server = user_data;
    if (g_str_equal(method_name, "GetActiveCount")) {
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(u)", g_list_length(server->notifications)));
    } else {
        g_dbus_method_invocation_return_error(invocation,
                                               G_DBUS_ERROR,
                                               G_DBUS_ERROR_UNKNOWN_METHOD,
                                               "Unknown Luxe notification method: %s",
                                               method_name);
    }
}

static const GDBusInterfaceVTable control_interface_vtable = {
    .method_call = control_method_call,
    .get_property = NULL,
    .set_property = NULL
};

static void bus_acquired(GDBusConnection *connection,
                         const gchar *name,
                         gpointer user_data)
{
    (void)name;
    LuxeNotifyServer *server = user_data;
    GError *error = NULL;

    server->connection = g_object_ref(connection);
    server->registration_id = g_dbus_connection_register_object(
        connection,
        NOTIFICATION_OBJECT_PATH,
        server->introspection->interfaces[0],
        &interface_vtable,
        server,
        NULL,
        &error);
    if (server->registration_id == 0) {
        g_printerr("luxenotify: cannot register D-Bus interface: %s\n",
                   error->message);
        g_clear_error(&error);
        g_main_loop_quit(server->loop);
        return;
    }
    server->control_registration_id = g_dbus_connection_register_object(
        connection,
        NOTIFICATION_OBJECT_PATH,
        server->introspection->interfaces[1],
        &control_interface_vtable,
        server,
        NULL,
        &error);
    if (server->control_registration_id == 0) {
        g_printerr("luxenotify: cannot register Luxe control interface: %s\n",
                   error->message);
        g_clear_error(&error);
        g_main_loop_quit(server->loop);
    }
}

static void name_lost(GDBusConnection *connection,
                      const gchar *name,
                      gpointer user_data)
{
    (void)connection;
    LuxeNotifyServer *server = user_data;
    g_printerr("luxenotify: notification service name is already in use: %s\n", name);
    g_main_loop_quit(server->loop);
}

static gboolean about_closed(GtkWindow *window, gpointer user_data)
{
    (void)window;
    g_main_loop_quit(user_data);
    return FALSE;
}

static int show_about(void)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    const gchar *authors[] = { "Jerry Howell", NULL };
    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Luxe Notifications");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), VERSION);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
        "A lightweight GTK4 notification service for the Luxe desktop.");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog),
        "https://github.com/openconstruct/luxeDE");
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(dialog), GTK_LICENSE_GPL_2_0);
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog),
        "Copyright © 2026 Jerry Howell");
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
    gtk_window_set_icon_name(GTK_WINDOW(dialog), "preferences-system-notifications");
    g_signal_connect(dialog, "close-request", G_CALLBACK(about_closed), loop);
    gtk_window_present(GTK_WINDOW(dialog));
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return EXIT_SUCCESS;
}

static void peer_closed(GDBusConnection *connection,gboolean remote,GError *error,gpointer loop)
{(void)connection;(void)remote;(void)error;g_main_loop_quit(loop);}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    gboolean about = FALSE;
    gboolean version = FALSE;
    gint peer_fd=-1;
    gchar *id_base=NULL;
    GOptionEntry entries[] = {
        { "about", 0, 0, G_OPTION_ARG_NONE, &about, "Show program information", NULL },
        { "version", 'v', 0, G_OPTION_ARG_NONE, &version, "Show version", NULL },
        { "peer-fd", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_INT, &peer_fd, "Private renderer transport", NULL },
        { "id-base", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &id_base, "Last allocated notification ID", NULL },
        { NULL }
    };
    GOptionContext *context = g_option_context_new("- Luxe notification service");
    g_option_context_add_main_entries(context, entries, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("luxenotify: %s\n", error->message);
        g_clear_error(&error);
        g_option_context_free(context);
        return EXIT_FAILURE;
    }
    g_option_context_free(context);

    if (version) {
        g_print("luxenotify %s\n", VERSION);
        return EXIT_SUCCESS;
    }

    if (about) {
        luxe_gtk_init();
        return show_about();
    }

    LuxeNotifyServer server = { 0 };
    if(id_base) {server.next_id=(guint)g_ascii_strtoull(id_base,NULL,10);g_free(id_base);}
    server.loop = g_main_loop_new(NULL, FALSE);
    server.introspection = g_dbus_node_info_new_for_xml(LUXE_NOTIFY_INTROSPECTION_XML, &error);
    if (server.introspection == NULL) {
        g_printerr("luxenotify: invalid D-Bus interface: %s\n", error->message);
        g_clear_error(&error);
        g_main_loop_unref(server.loop);
        return EXIT_FAILURE;
    }

    if(peer_fd>=0) {
        g_autoptr(GSocket) socket=g_socket_new_from_fd(peer_fd,&error);
        g_autoptr(GSocketConnection) stream=socket?g_socket_connection_factory_create_connection(socket):NULL;
        g_autoptr(GDBusConnection) peer=stream?g_dbus_connection_new_sync(G_IO_STREAM(stream),NULL,G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,NULL,NULL,&error):NULL;
        if(!peer){g_printerr("Notification renderer: %s\n",error?error->message:"invalid transport");return 1;}
        bus_acquired(peer,NULL,&server);
        g_signal_connect(peer,"closed",G_CALLBACK(peer_closed),server.loop);
    } else server.owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
                                      NOTIFICATION_BUS_NAME,
                                      G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                      bus_acquired,
                                      NULL,
                                      name_lost,
                                      &server,
                                      NULL);
    g_main_loop_run(server.loop);

    while (server.notifications != NULL)
        luxe_notify_server_close(&server, server.notifications->data, 3, FALSE);
    luxe_notification_stop_monitoring(&server);
    if (server.reposition_id != 0)
        g_source_remove(server.reposition_id);
    if (server.registration_id != 0 && server.connection != NULL)
        g_dbus_connection_unregister_object(server.connection, server.registration_id);
    if (server.control_registration_id != 0 && server.connection != NULL)
        g_dbus_connection_unregister_object(server.connection, server.control_registration_id);
    if (server.owner_id != 0)
        g_bus_unown_name(server.owner_id);
    g_clear_object(&server.connection);
    g_dbus_node_info_unref(server.introspection);
    g_main_loop_unref(server.loop);
    return EXIT_SUCCESS;
}
