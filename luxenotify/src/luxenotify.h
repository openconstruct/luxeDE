/*
 * Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <gio/gio.h>
#include <gtk/gtk.h>

typedef struct _LuxeNotifyServer LuxeNotifyServer;
typedef struct _LuxeNotification LuxeNotification;

struct _LuxeNotifyServer {
    GMainLoop *loop;
    GDBusConnection *connection;
    GDBusNodeInfo *introspection;
    GList *notifications;
    guint owner_id;
    guint registration_id;
    guint control_registration_id;
    guint next_id;
    guint reposition_id;
    guint published_count;
    GdkDisplay *workarea_display;
    GListModel *workarea_monitors;
    GHashTable *watched_monitors;
    gpointer workarea_x11;
    guint workarea_x11_source;
    gulong workarea_atom, workspace_atom;
    gboolean workarea_valid;
    gint workarea_x, workarea_y, workarea_width, workarea_height;
};

struct _LuxeNotification {
    LuxeNotifyServer *server;
    guint id;
    gchar *app_name;
    GtkWindow *window;
    GCancellable *icon_cancel;
    guint timeout_id;
    gboolean resident;
    gboolean has_default_action;
};

LuxeNotification *luxe_notification_window_new(LuxeNotifyServer *server,
                                                guint id,
                                                const gchar *app_name,
                                                const gchar *app_icon,
                                                const gchar *summary,
                                                const gchar *body,
                                                GVariant *actions,
                                                GVariant *hints,
                                                gint expire_timeout);
void luxe_notification_reposition_all(LuxeNotifyServer *server);
void luxe_notification_stop_monitoring(LuxeNotifyServer *server);
void luxe_notify_server_close(LuxeNotifyServer *server,
                              LuxeNotification *notification,
                              guint reason,
                              gboolean emit_signal);
void luxe_notify_server_action(LuxeNotifyServer *server,
                               LuxeNotification *notification,
                               const gchar *action_key);
void luxe_notify_server_active_count_changed(LuxeNotifyServer *server);
