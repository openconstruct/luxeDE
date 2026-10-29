/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "plugin.h"
static GQueue pending=G_QUEUE_INIT;
static void fake_call(GDBusConnection *connection,const gchar *bus_name,const gchar *path,
                      const gchar *interface,const gchar *method,GVariant *parameters,
                      const GVariantType *reply_type,GDBusCallFlags flags,gint timeout,
                      GCancellable *cancel,GAsyncReadyCallback callback,gpointer data)
{
    (void)connection;(void)path;(void)interface;(void)method;(void)parameters;
    (void)reply_type;(void)flags;(void)timeout;(void)cancel;
    g_assert_true(g_str_has_prefix(bus_name,":"));
    /* Simulate a backend that delivers a reply even after cancellation. */
    g_queue_push_tail(&pending,g_task_new(NULL,NULL,callback,data));
}
static GVariant *fake_finish(GDBusConnection *connection,GAsyncResult *result,GError **error)
{(void)connection;return g_task_propagate_pointer(G_TASK(result),error);}
static guint fake_subscribe(GDBusConnection *connection,const gchar *sender,const gchar *interface,
                            const gchar *member,const gchar *path,const gchar *arg0,
                            GDBusSignalFlags flags,GDBusSignalCallback callback,gpointer data,GDestroyNotify destroy)
{(void)connection;(void)sender;(void)interface;(void)member;(void)path;(void)arg0;(void)flags;(void)callback;(void)data;(void)destroy;return 1;}
static void fake_unsubscribe(GDBusConnection *connection,guint id){(void)connection;(void)id;}
#define g_dbus_connection_call fake_call
#define g_dbus_connection_call_finish fake_finish
#define g_dbus_connection_signal_subscribe fake_subscribe
#define g_dbus_connection_signal_unsubscribe fake_unsubscribe
#include "../plugins/notifications.c"
#undef g_dbus_connection_call
#undef g_dbus_connection_call_finish
#undef g_dbus_connection_signal_subscribe
#undef g_dbus_connection_signal_unsubscribe
void x11_root_watch_window(Window window){(void)window;}
static void reply(guint value)
{
    GTask *task=g_queue_pop_head(&pending);g_assert_nonnull(task);
    g_task_return_pointer(task,g_variant_ref_sink(g_variant_new("(u)",value)),(GDestroyNotify)g_variant_unref);
    g_object_unref(task);while(g_main_context_iteration(NULL,FALSE));
}
int main(void)
{
    gtk_init();luxepanel_plugin_qdata=g_quark_from_static_string("LuxePanel::plugin-data");
    NotificationsPlugin *plugin=g_new0(NotificationsPlugin,1);
    plugin->plugin=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);g_object_ref_sink(plugin->plugin);
    plugin->indicator=gtk_label_new("!");gtk_box_append(GTK_BOX(plugin->plugin),plugin->indicator);
    luxepanel_plugin_set_data(plugin->plugin,plugin,notifications_destructor);
    GDBusConnection *fake=(GDBusConnection *)g_object_new(G_TYPE_OBJECT,NULL);
    notification_service_appeared(fake,"service",":1.10",plugin);
    g_autoptr(GVariant) seven=g_variant_ref_sink(g_variant_new("(u)",7));
    count_changed(fake,":1.10",NULL,NULL,NULL,seven,plugin);
    reply(1);
    g_assert_true(gtk_widget_get_visible(plugin->indicator));
    g_assert_nonnull(strstr(gtk_widget_get_tooltip_text(plugin->plugin),"7"));
    notification_service_appeared(fake,"service",":1.10",plugin);
    notification_service_vanished(fake,"service",plugin);reply(5);
    g_assert_false(gtk_widget_get_visible(plugin->indicator));
    notification_service_appeared(fake,"service",":1.11",plugin);
    count_changed(fake,":1.10",NULL,NULL,NULL,seven,plugin);
    reply(2);g_assert_nonnull(strstr(gtk_widget_get_tooltip_text(plugin->plugin),"2"));
    notification_service_appeared(fake,"service",":1.11",plugin);
    GtkWidget *widget=plugin->plugin;g_object_unref(widget);reply(9);
    g_object_unref(fake);g_assert_true(g_queue_is_empty(&pending));
    return 0;
}
