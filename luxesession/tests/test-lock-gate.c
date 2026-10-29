/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
// Exercise the production completion gate; no sleep backend reaches D-Bus.
#define main logout_application_main
#define dbus_systemd_Suspend fake_suspend
#define dbus_systemd_Hibernate fake_hibernate
#include "../luxesession-logout/luxesession-logout.c"
#undef main

static guint suspends, hibernates;
void fake_suspend(GError **error) { (void)error; suspends++; }
void fake_hibernate(GError **error) { (void)error; hibernates++; }
static void completed(GObject *source, GAsyncResult *result, gpointer data)
{
    lock_action_ready(source, result, data);
    if (main_loop) g_main_loop_quit(main_loop);
}
static void complete(HandlerContext *context, LockAction action, gboolean ready, gboolean closed)
{
    LockActionRequest *request = g_new0(LockActionRequest, 1);
    request->context = context; request->action = action;
    GTask *task = g_task_new(NULL, NULL, completed, request);
    if (!closed) main_loop = g_main_loop_new(NULL, FALSE);
    if (ready) g_task_return_boolean(task, TRUE);
    else g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Fake locker startup failed");
    g_object_unref(task);
    if (!closed) {
        g_main_loop_run(main_loop); g_main_loop_unref(main_loop); main_loop = NULL;
    } else while (g_main_context_iteration(NULL, FALSE));
}
int main(void)
{
    luxe_gtk_init();
    HandlerContext context = {0};
    context.error_label = g_object_ref_sink(gtk_label_new(""));
    context.suspend_systemd = context.hibernate_systemd = TRUE;
    complete(&context, LOCK_THEN_SUSPEND, FALSE, FALSE);
    complete(&context, LOCK_THEN_HIBERNATE, FALSE, FALSE);
    g_assert_cmpuint(suspends + hibernates, ==, 0);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(context.error_label)), "failed"));
    complete(&context, LOCK_THEN_SUSPEND, TRUE, FALSE);
    g_assert_cmpuint(suspends, ==, 1);
    complete(&context, LOCK_THEN_HIBERNATE, TRUE, FALSE);
    g_assert_cmpuint(hibernates, ==, 1);
    complete(&context, LOCK_ONLY, TRUE, FALSE);
    complete(&context, LOCK_THEN_SUSPEND, TRUE, TRUE);
    g_assert_cmpuint(suspends + hibernates, ==, 2);
    g_object_unref(context.error_label);
    return 0;
}
