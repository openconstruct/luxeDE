/* Disposable private-peer renderer for broker timeout tests. */
#include <gio/gio.h>
#include <fcntl.h>
#include <unistd.h>

static const char xml[] = "<node><interface name='org.freedesktop.Notifications'>"
    "<method name='Notify'><arg direction='in' type='s'/><arg direction='in' type='u'/>"
    "<arg direction='in' type='s'/><arg direction='in' type='s'/><arg direction='in' type='s'/>"
    "<arg direction='in' type='as'/><arg direction='in' type='a{sv}'/><arg direction='in' type='i'/>"
    "<arg direction='out' type='u'/></method></interface></node>";
static gboolean hangs;
static void call(GDBusConnection *connection, const char *sender, const char *path,
                 const char *interface, const char *method, GVariant *parameters,
                 GDBusMethodInvocation *invocation, gpointer data)
{
    (void)connection; (void)sender; (void)path; (void)interface;
    (void)method; (void)data;
    if (hangs) { g_object_ref(invocation); return; }
    g_autoptr(GVariant) summary = g_variant_get_child_value(parameters, 3);
    if (g_str_equal(g_variant_get_string(summary, NULL), "reject")) {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.luxe.Test.Rejected", "Fixture rejection");
        return;
    }
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 42));
}
static const GDBusInterfaceVTable table = { .method_call = call };
int main(void)
{
    const char *marker = g_getenv("LUXE_NOTIFY_TEST_MARKER");
    if (!marker) return 1;
    int marker_fd = open(marker, O_WRONLY | O_CREAT | O_EXCL, 0600);
    hangs = marker_fd >= 0;
    if (marker_fd >= 0) close(marker_fd);
    const char *starts = g_getenv("LUXE_NOTIFY_TEST_STARTS");
    int starts_fd = starts ? open(starts, O_WRONLY | O_CREAT | O_APPEND, 0600) : -1;
    if (starts_fd < 0 || write(starts_fd, "x\n", 2) != 2) return 5;
    close(starts_fd);
    g_autoptr(GError) error = NULL;
    g_autoptr(GSocket) socket = g_socket_new_from_fd(3, &error);
    if (!socket) return 2;
    g_autoptr(GSocketConnection) stream = g_socket_connection_factory_create_connection(socket);
    g_autoptr(GDBusConnection) connection = g_dbus_connection_new_sync(
        G_IO_STREAM(stream), NULL, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
        NULL, NULL, &error);
    if (!connection) return 3;
    g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, &error);
    if (!info || !g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
                                                       info->interfaces[0], &table, NULL, NULL, &error)) return 4;
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    return 0;
}
