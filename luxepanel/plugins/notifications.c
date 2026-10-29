/*
 * LuxePanel notification activity indicator.
 * Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "plugin.h"

#include <glib/gi18n.h>

#define NOTIFICATION_BUS_NAME "org.freedesktop.Notifications"
#define NOTIFICATION_OBJECT_PATH "/org/freedesktop/Notifications"
#define LUXE_NOTIFICATION_INTERFACE "org.luxe.Notifications"

typedef struct {
    GtkWidget *plugin;
    GtkWidget *indicator;
    GDBusConnection *connection;
    guint watch_id;
    guint signal_id;
    guint64 revision;
    char *owner;
    GCancellable *query;
} NotificationsPlugin;

typedef struct { GWeakRef widget; guint64 revision; } CountRequest;
static void cancel_count(NotificationsPlugin *plugin)
{
    if(plugin->query)g_cancellable_cancel(plugin->query);
    g_clear_object(&plugin->query);
}

static const char indicator_css[] =
    ".luxe-notification-indicator {"
    " background: @theme_selected_bg_color; color: @theme_selected_fg_color; border-radius: 999px;"
    " font-weight: 800; font-size: 1.05em; min-width: 18px; min-height: 18px;"
    " padding: 0 1px;"
    "}";

static void install_indicator_css(void)
{
    static gsize installed = 0;
    if (g_once_init_enter(&installed)) {
        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_string(provider, indicator_css);
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        g_once_init_leave(&installed, 1);
    }
}

static void update_indicator(NotificationsPlugin *plugin, guint count)
{
    gboolean visible = count != 0;
    gtk_widget_set_visible(plugin->indicator, visible);
    if (visible) {
        char *tooltip = g_strdup_printf(ngettext("%u notification waiting",
                                                 "%u notifications waiting", count), count);
        gtk_widget_set_tooltip_text(plugin->plugin, tooltip);
        g_free(tooltip);
    } else {
        gtk_widget_set_tooltip_text(plugin->plugin, NULL);
    }
}

static void count_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)source;
    CountRequest *request=user_data;
    GtkWidget *widget=g_weak_ref_get(&request->widget);
    NotificationsPlugin *plugin = widget ? luxepanel_plugin_get_data(widget) : NULL;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (plugin != NULL && plugin->revision==request->revision && reply != NULL &&
        g_variant_is_of_type(reply, G_VARIANT_TYPE("(u)"))) {
        guint count = 0;
        g_variant_get(reply, "(u)", &count);
        update_indicator(plugin, count);
    }
    if(plugin && plugin->revision==request->revision)g_clear_object(&plugin->query);
    g_clear_pointer(&reply, g_variant_unref);
    g_clear_error(&error);
    if(widget)g_object_unref(widget);
    g_weak_ref_clear(&request->widget);g_free(request);
}

static void request_count(NotificationsPlugin *plugin)
{
    if (plugin->connection == NULL || plugin->owner == NULL) return;
    cancel_count(plugin);
    plugin->revision++;
    plugin->query=g_cancellable_new();
    CountRequest *request=g_new0(CountRequest,1);
    g_weak_ref_init(&request->widget,plugin->plugin);request->revision=plugin->revision;
    g_dbus_connection_call(plugin->connection,
                           plugin->owner,
                           NOTIFICATION_OBJECT_PATH,
                           LUXE_NOTIFICATION_INTERFACE,
                           "GetActiveCount",
                           NULL,
                           G_VARIANT_TYPE("(u)"),
                           G_DBUS_CALL_FLAGS_NONE,
                           1500,
                           plugin->query,
                           count_ready,
                           request);
}

static void count_changed(GDBusConnection *connection,
                          const gchar *sender_name,
                          const gchar *object_path,
                          const gchar *interface_name,
                          const gchar *signal_name,
                          GVariant *parameters,
                          gpointer user_data)
{
    (void)connection;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    NotificationsPlugin *plugin = user_data;
    guint count = 0;
    if (plugin == NULL || g_strcmp0(sender_name,plugin->owner)!=0 || parameters == NULL ||
        !g_variant_is_of_type(parameters, G_VARIANT_TYPE("(u)")))
        return;
    g_variant_get(parameters, "(u)", &count);
    plugin->revision++;cancel_count(plugin);
    update_indicator(plugin, count);
}

static void notification_service_appeared(GDBusConnection *connection,
                                          const gchar *name,
                                          const gchar *owner,
                                          gpointer user_data)
{
    (void)name;
    NotificationsPlugin *plugin = user_data;
    if (plugin == NULL || owner == NULL)
        return;
    if (plugin->signal_id != 0)
        g_dbus_connection_signal_unsubscribe(plugin->connection, plugin->signal_id);
    plugin->revision++;cancel_count(plugin);
    g_set_object(&plugin->connection,connection);
    g_free(plugin->owner);plugin->owner=g_strdup(owner);
    plugin->signal_id = g_dbus_connection_signal_subscribe(plugin->connection,
        owner, LUXE_NOTIFICATION_INTERFACE, "ActiveCountChanged",
        NOTIFICATION_OBJECT_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        count_changed, plugin, NULL);
    request_count(plugin);
}

static void notification_service_vanished(GDBusConnection *connection,
                                          const gchar *name,
                                          gpointer user_data)
{
    (void)connection;
    (void)name;
    NotificationsPlugin *plugin = user_data;
    if (plugin == NULL)
        return;
    if (plugin->connection != NULL && plugin->signal_id != 0)
        g_dbus_connection_signal_unsubscribe(plugin->connection, plugin->signal_id);
    plugin->signal_id = 0;
    plugin->revision++;cancel_count(plugin);g_clear_pointer(&plugin->owner,g_free);
    update_indicator(plugin, 0);
}

static void notifications_destructor(gpointer user_data)
{
    NotificationsPlugin *plugin = user_data;
    if (plugin == NULL)
        return;
    plugin->revision++;cancel_count(plugin);g_clear_pointer(&plugin->owner,g_free);
    if (plugin->watch_id != 0)
        g_bus_unwatch_name(plugin->watch_id);
    if (plugin->connection != NULL && plugin->signal_id != 0)
        g_dbus_connection_signal_unsubscribe(plugin->connection, plugin->signal_id);
    g_clear_object(&plugin->connection);
    g_free(plugin);
}

static GtkWidget *notifications_constructor(LuxePanel *panel, config_setting_t *settings)
{
    (void)panel;
    (void)settings;
    NotificationsPlugin *plugin = g_new0(NotificationsPlugin, 1);
    plugin->plugin = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    plugin->indicator = gtk_label_new("!");
    gtk_widget_add_css_class(plugin->indicator, "luxe-notification-indicator");
    gtk_widget_set_halign(plugin->indicator, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(plugin->indicator, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(plugin->plugin), plugin->indicator);
    update_indicator(plugin, 0);
    install_indicator_css();
    luxepanel_plugin_set_data(plugin->plugin, plugin, notifications_destructor);
    plugin->watch_id = g_bus_watch_name(G_BUS_TYPE_SESSION,
        NOTIFICATION_BUS_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
        notification_service_appeared, notification_service_vanished, plugin, NULL);
    return plugin->plugin;
}

LuxePanelPluginInit luxepanel_static_plugin_notifications = {
    .name = N_("Notifications"),
    .description = N_("Show when notifications are waiting"),
    .new_instance = notifications_constructor,
    .one_per_system = 1
};
