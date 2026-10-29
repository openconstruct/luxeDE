/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>
#include <unistd.h>
static guint responses;
static GtkWidget *entry(GtkWidget *widget)
{
    if (GTK_IS_ENTRY(widget)) return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = entry(child);
        if (found) return found;
    }
    return NULL;
}
static gboolean respond(gpointer unused)
{
    (void) unused;
    GListModel *windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(windows); ++i) {
        GtkWindow *window = g_list_model_get_item(windows, i);
        GtkWidget *field = entry(GTK_WIDGET(window));
        if (GTK_IS_DIALOG(window) && field) {
            if (!g_strcmp0(g_getenv("LUXE_POLKIT_FIXTURE_MODE"), "cancel"))
                gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_CANCEL);
            else {
                gtk_editable_set_text(GTK_EDITABLE(field), "fixture-password");
                gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_OK);
            }
            g_object_unref(window);
            return G_SOURCE_REMOVE;
        }
        g_object_unref(window);
    }
    return G_SOURCE_CONTINUE;
}
static void fixture_initiate(PolkitAgentSession *session)
{
    if (!g_strcmp0(g_getenv("LUXE_POLKIT_FIXTURE_MODE"), "crash")) _exit(7);
    if (!g_strcmp0(g_getenv("LUXE_POLKIT_FIXTURE_MODE"), "no-prompt")) return;
    g_signal_emit_by_name(session, "request", "Password: ", FALSE);
    if (g_strcmp0(g_getenv("LUXE_POLKIT_FIXTURE_MODE"), "wait"))
        g_timeout_add(100, respond, NULL);
}
static void fixture_response(PolkitAgentSession *session, const char *password)
{
    g_assert_cmpstr(password, ==, "fixture-password");
    ++responses;
    gboolean retry = !g_strcmp0(g_getenv("LUXE_POLKIT_FIXTURE_MODE"), "retry");
    g_signal_emit_by_name(session, "completed", !retry || responses > 1);
}
static void fixture_cancel(PolkitAgentSession *session)
{
    g_signal_emit_by_name(session, "completed", FALSE);
}
#define polkit_agent_session_initiate fixture_initiate
#define polkit_agent_session_response fixture_response
#define polkit_agent_session_cancel fixture_cancel
#include "../src/luxepolkit-listener.c"
#define main fixture_dialog_main
#include "../src/dialog-process.c"
#undef main
int main(int argc, char **argv)
{
    if (!g_strcmp0(g_getenv("LUXE_POLKIT_FIXTURE_MODE"), "never-ready")) {
        for (;;) pause();
    }
    if (!g_strcmp0(g_getenv("LUXE_POLKIT_FIXTURE_MODE"), "grab-contended")) {
        Display *other = XOpenDisplay(NULL);
        g_assert_nonnull(other);
        g_assert_cmpint(XGrabKeyboard(other, DefaultRootWindow(other), False, GrabModeAsync,
                                     GrabModeAsync, CurrentTime), ==, GrabSuccess);
        XSync(other, False);
    }
    return fixture_dialog_main(argc, argv);
}
