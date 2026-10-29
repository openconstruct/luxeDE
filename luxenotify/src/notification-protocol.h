/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#define LUXE_NOTIFY_BUS_NAME "org.freedesktop.Notifications"
#define LUXE_NOTIFY_OBJECT_PATH "/org/freedesktop/Notifications"
#define LUXE_NOTIFY_INTERFACE "org.freedesktop.Notifications"
#define LUXE_NOTIFY_CONTROL_INTERFACE "org.luxe.Notifications"

/* Shared by the GTK renderer and its GTK-free broker. Keeping the public
 * interfaces in one compiled-in definition prevents schema drift. */
#define LUXE_NOTIFY_INTROSPECTION_XML \
    "<node>" \
    " <interface name='org.freedesktop.Notifications'>" \
    "  <method name='GetCapabilities'><arg direction='out' name='capabilities' type='as'/></method>" \
    "  <method name='Notify'>" \
    "   <arg direction='in' name='app_name' type='s'/>" \
    "   <arg direction='in' name='replaces_id' type='u'/>" \
    "   <arg direction='in' name='app_icon' type='s'/>" \
    "   <arg direction='in' name='summary' type='s'/>" \
    "   <arg direction='in' name='body' type='s'/>" \
    "   <arg direction='in' name='actions' type='as'/>" \
    "   <arg direction='in' name='hints' type='a{sv}'/>" \
    "   <arg direction='in' name='expire_timeout' type='i'/>" \
    "   <arg direction='out' name='id' type='u'/>" \
    "  </method>" \
    "  <method name='CloseNotification'><arg direction='in' name='id' type='u'/></method>" \
    "  <method name='GetServerInformation'>" \
    "   <arg direction='out' name='name' type='s'/>" \
    "   <arg direction='out' name='vendor' type='s'/>" \
    "   <arg direction='out' name='version' type='s'/>" \
    "   <arg direction='out' name='spec_version' type='s'/>" \
    "  </method>" \
    "  <signal name='NotificationClosed'><arg name='id' type='u'/><arg name='reason' type='u'/></signal>" \
    "  <signal name='ActionInvoked'><arg name='id' type='u'/><arg name='action_key' type='s'/></signal>" \
    " </interface>" \
    " <interface name='org.luxe.Notifications'>" \
    "  <method name='GetActiveCount'><arg direction='out' type='u'/></method>" \
    "  <signal name='ActiveCountChanged'><arg name='count' type='u'/></signal>" \
    " </interface>" \
    "</node>"

#define LUXE_NOTIFY_SERVER_NAME "LuxeNotify"
#define LUXE_NOTIFY_SERVER_VENDOR "Luxe"
#define LUXE_NOTIFY_SPEC_VERSION "1.2"

static const char *const LUXE_NOTIFY_CAPABILITIES[] = {
    "actions", "body", "body-hyperlinks", "body-markup", "icon-static", NULL
};
