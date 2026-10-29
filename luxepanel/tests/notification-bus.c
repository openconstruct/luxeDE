/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../plugins/notifications.c"
void x11_root_watch_window(Window window){(void)window;}
static GDBusMethodInvocation *pending;
static void handle_method(GDBusConnection *connection,const gchar *sender,const gchar *path,
                          const gchar *interface,const gchar *method,GVariant *parameters,
                          GDBusMethodInvocation *invocation,gpointer data)
{
    (void)connection;(void)sender;(void)path;(void)interface;(void)parameters;(void)data;
    g_assert_cmpstr(method,==,"GetActiveCount");g_assert_null(pending);pending=g_object_ref(invocation);
}
static const GDBusInterfaceVTable vtable={.method_call=handle_method};
static void pump_until(gboolean (*condition)(gpointer),gpointer data)
{
    gint64 deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while(!condition(data)&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_true(condition(data));
}
static gboolean has_pending(gpointer data){(void)data;return pending!=NULL;}
static gboolean tooltip_has_seven(gpointer data)
{const char *text=gtk_widget_get_tooltip_text(GTK_WIDGET(data));return text&&strstr(text,"7");}
static gboolean tooltip_has_three(gpointer data)
{const char *text=gtk_widget_get_tooltip_text(GTK_WIDGET(data));return text&&strstr(text,"3");}
static gboolean indicator_hidden(gpointer data){return !gtk_widget_get_visible(GTK_WIDGET(data));}
int main(void)
{
    gtk_init();luxepanel_plugin_qdata=g_quark_from_static_string("LuxePanel::plugin-data");
    g_autoptr(GError) error=NULL;
    g_autoptr(GDBusConnection) bus=g_bus_get_sync(G_BUS_TYPE_SESSION,NULL,&error);g_assert_no_error(error);
    const char *xml="<node><interface name='org.luxe.Notifications'><method name='GetActiveCount'><arg type='u' direction='out'/></method><signal name='ActiveCountChanged'><arg type='u'/></signal></interface></node>";
    g_autoptr(GDBusNodeInfo) info=g_dbus_node_info_new_for_xml(xml,&error);g_assert_no_error(error);
    guint registration=g_dbus_connection_register_object(bus,NOTIFICATION_OBJECT_PATH,info->interfaces[0],&vtable,NULL,NULL,&error);
    g_assert_no_error(error);g_assert_cmpuint(registration,>,0);
    guint owner=g_bus_own_name_on_connection(bus,NOTIFICATION_BUS_NAME,G_BUS_NAME_OWNER_FLAGS_NONE,NULL,NULL,NULL,NULL);
    GtkWidget *widget=notifications_constructor(NULL,NULL);g_object_ref_sink(widget);
    NotificationsPlugin *plugin=luxepanel_plugin_get_data(widget);
    pump_until(has_pending,NULL);
    g_assert_true(g_dbus_connection_emit_signal(bus,NULL,NOTIFICATION_OBJECT_PATH,LUXE_NOTIFICATION_INTERFACE,
                                                "ActiveCountChanged",g_variant_new("(u)",7),&error));
    g_assert_no_error(error);pump_until(tooltip_has_seven,widget);
    g_dbus_method_invocation_return_value(pending,g_variant_new("(u)",1));g_clear_object(&pending);
    gint64 end=g_get_monotonic_time()+100000;
    while(g_get_monotonic_time()<end){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_true(tooltip_has_seven(widget));
    g_bus_unown_name(owner);pump_until(indicator_hidden,plugin->indicator);
    g_autoptr(GDBusConnection) bus2=g_dbus_connection_new_for_address_sync(g_getenv("DBUS_SESSION_BUS_ADDRESS"),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,NULL,NULL,&error);
    g_assert_no_error(error);g_assert_nonnull(bus2);
    guint registration2=g_dbus_connection_register_object(bus2,NOTIFICATION_OBJECT_PATH,info->interfaces[0],&vtable,NULL,NULL,&error);
    g_assert_no_error(error);g_assert_cmpuint(registration2,>,0);
    guint owner2=g_bus_own_name_on_connection(bus2,NOTIFICATION_BUS_NAME,G_BUS_NAME_OWNER_FLAGS_NONE,NULL,NULL,NULL,NULL);
    pump_until(has_pending,NULL);
    g_assert_true(g_dbus_connection_emit_signal(bus,NULL,NOTIFICATION_OBJECT_PATH,LUXE_NOTIFICATION_INTERFACE,
                                                "ActiveCountChanged",g_variant_new("(u)",7),&error));g_assert_no_error(error);
    g_dbus_method_invocation_return_value(pending,g_variant_new("(u)",3));g_clear_object(&pending);
    pump_until(tooltip_has_three,widget);
    end=g_get_monotonic_time()+100000;
    while(g_get_monotonic_time()<end){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_true(tooltip_has_three(widget));
    g_bus_unown_name(owner2);pump_until(indicator_hidden,plugin->indicator);
    g_object_unref(widget);g_dbus_connection_unregister_object(bus2,registration2);
    g_dbus_connection_unregister_object(bus,registration);
    while(g_main_context_iteration(NULL,FALSE));
    return 0;
}
