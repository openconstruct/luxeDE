/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gio/gio.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>
static guint registrations;
static PolkitSubject *fixture_session(gint pid, GCancellable *cancel, GError **error)
{
    (void) pid; (void) cancel; (void) error;
    return polkit_unix_session_new("fixture-session");
}
static gpointer fixture_register(PolkitAgentListener *agent, PolkitAgentRegisterFlags flags,
    PolkitSubject *subject, const gchar *path, GCancellable *cancel, GError **error)
{
    (void) agent; (void) flags; (void) subject; (void) path; (void) cancel; (void) error;
    registrations++;
    return GINT_TO_POINTER(1);
}
static void fixture_unregister(gpointer handle) { g_assert_true(handle == GINT_TO_POINTER(1)); }
#define polkit_unix_session_new_for_process_sync fixture_session
#define polkit_agent_listener_register fixture_register
#define polkit_agent_listener_unregister fixture_unregister
#define main agent_main
#include "../src/luxepolkit.c"
#undef main
static gboolean finish(gpointer unused)
{
    (void) unused;
    g_assert_cmpuint(registrations, ==, 1);
    /* Simulate a second invocation: it must not register another listener. */
    g_application_activate(g_application_get_default());
    g_assert_cmpuint(registrations, ==, 1);
    g_application_quit(g_application_get_default());
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv)
{
    g_timeout_add(50, finish, NULL);
    return agent_main(argc, argv);
}
