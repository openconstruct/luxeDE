/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gio/gio.h>
#include "../luxesession/dbus-gnome-session.c"

LxsessionControlObject *lxsession_control_object_new(void) { return NULL; }
void lxsession_control_object_set_status_busy(LxsessionControlObject *self, guint xid)
{ (void)self; (void)xid; }
void lxsession_control_object_exit_status_busy(LxsessionControlObject *self)
{ (void)self; }
LxsessionSessionObject *lxsession_session_object_new(void) { return NULL; }
void lxsession_session_object_luxesession_shutdown(LxsessionSessionObject *self)
{ (void)self; }
void lxsession_session_object_luxesession_can_shutdown(
    LxsessionSessionObject *self, GAsyncReadyCallback callback, gpointer data)
{ (void)self; (void)callback; (void)data; }
gboolean lxsession_session_object_luxesession_can_shutdown_finish(
    LxsessionSessionObject *self, GAsyncResult *result)
{ (void)self; (void)result; return FALSE; }

static GMainLoop *loop;
static const char *expected_member;
static const char *expected_path;
static gboolean received;
static guint timeout_source;

static void on_signal(GDBusConnection *connection, const gchar *sender,
                      const gchar *object_path, const gchar *interface_name,
                      const gchar *signal_name, GVariant *parameters,
                      gpointer user_data)
{
    (void)connection; (void)sender; (void)user_data;
    if (g_strcmp0(object_path, "/org/gnome/SessionManager") != 0 ||
        g_strcmp0(interface_name, "org.gnome.SessionManager") != 0 ||
        g_strcmp0(signal_name, expected_member) != 0)
        return;
    const char *path = NULL;
    g_variant_get(parameters, "(&s)", &path);
    g_assert_cmpstr(path, ==, expected_path);
    received = TRUE;
    if (timeout_source) {
        g_source_remove(timeout_source);
        timeout_source = 0;
    }
    g_main_loop_quit(loop);
}

static gboolean signal_timeout(gpointer unused)
{
    (void)unused;
    timeout_source = 0;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void expect_signal(GObject *server, const char *member, const char *signal_name,
                          const char *path)
{
    expected_member = signal_name;
    expected_path = path;
    received = FALSE;
    g_signal_emit_by_name(server, member, path);
    timeout_source = g_timeout_add_seconds(2, signal_timeout, NULL);
    g_main_loop_run(loop);
    g_assert_true(received);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_autoptr(GTestDBus) bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    g_autoptr(GError) error = NULL;
    const gchar *address = g_test_dbus_get_bus_address(bus);
    GDBusConnectionFlags flags = G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                 G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION;
    g_autoptr(GDBusConnection) sender = g_dbus_connection_new_for_address_sync(
        address, flags, NULL, NULL, &error);
    g_assert_no_error(error);
    g_autoptr(GDBusConnection) receiver = g_dbus_connection_new_for_address_sync(
        address, flags, NULL, NULL, &error);
    g_assert_no_error(error);
    g_assert_nonnull(sender);
    g_assert_nonnull(receiver);

    g_autoptr(GObject) server = G_OBJECT(lxsession_gnome_session_server_new());
    guint registration = lxsession_gnome_session_server_register_object(
        server, sender, "/org/gnome/SessionManager", &error);
    g_assert_no_error(error);
    g_assert_cmpuint(registration, >, 0);
    loop = g_main_loop_new(NULL, FALSE);
    guint subscription = g_dbus_connection_signal_subscribe(receiver,
        g_dbus_connection_get_unique_name(sender), "org.gnome.SessionManager",
        NULL, "/org/gnome/SessionManager", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_signal, NULL, NULL);

    expect_signal(server, "client-added", "ClientAdded",
                  "/org/gnome/SessionManager/Client42");
    expect_signal(server, "client-removed", "ClientRemoved",
                  "/org/gnome/SessionManager/Client42");
    expect_signal(server, "inhibitor-added", "InhibitorAdded",
                  "/org/gnome/SessionManager/Inhibitor42");
    expect_signal(server, "inhibitor-removed", "InhibitorRemoved",
                  "/org/gnome/SessionManager/Inhibitor42");

    g_dbus_connection_signal_unsubscribe(receiver, subscription);
    g_dbus_connection_unregister_object(sender, registration);
    g_main_loop_unref(loop);
    loop = NULL;
    g_dbus_connection_close_sync(receiver, NULL, NULL);
    g_dbus_connection_close_sync(sender, NULL, NULL);
    g_test_dbus_down(bus);
    return 0;
}
