/* Copyright 2010 PCMan; Copyright (C) 2026 Jerry Howell.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxepolkit-listener.h"
#include <glib/gi18n.h>
#include <unistd.h>

static PolkitAgentListener *listener;
static PolkitSubject *session;
static gpointer listener_registration;
static gboolean registration_failed;

static void on_app_activate(GApplication *app, gpointer unused)
{
    (void) unused;
    if (listener_registration) return;
    GError *error = NULL;
    listener_registration = polkit_agent_listener_register(listener,
        POLKIT_AGENT_REGISTER_FLAGS_NONE, session, NULL, NULL, &error);
    if (!listener_registration) {
        g_printerr("Cannot register authentication agent: %s\n",
                   error ? error->message : "unknown error");
        g_clear_error(&error);
        registration_failed = TRUE;
        g_application_quit(app);
        return;
    }
    g_application_hold(app);
}

int main(int argc, char **argv)
{
#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif
    GError *error = NULL;
    GApplication *app = g_application_new("org.luxe.Polkit", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), NULL);
    listener = luxepolkit_listener_new();
    session = polkit_unix_session_new_for_process_sync(getpid(), NULL, &error);
    if (!session) {
        g_printerr("Cannot determine login session: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(listener);
        g_object_unref(app);
        return 1;
    }
    int status = g_application_run(app, argc, argv);
    if (listener_registration) polkit_agent_listener_unregister(listener_registration);
    g_object_unref(listener);
    g_object_unref(session);
    g_object_unref(app);
    return registration_failed ? 1 : status;
}
