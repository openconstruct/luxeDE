/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "activity.h"
#include <vte/vte.h>

typedef struct {
  guint refs;
  gboolean answered;
  ActivityDone done;
  gpointer data;
  GDestroyNotify destroy;
  GWeakRef owner;
} ActivityJob;

static void job_unref(ActivityJob *job)
{
  if (--job->refs) return;
  if (job->destroy) job->destroy(job->data);
  g_weak_ref_clear(&job->owner);
  g_free(job);
}

static void job_closure_free(gpointer data, GClosure *closure)
{
  (void)closure;
  job_unref(data);
}

static void activity_finish(ActivityJob *job, int status)
{
  if (job->answered) return;
  job->answered = TRUE;
  GObject *owner = g_weak_ref_get(&job->owner);
  if (owner && !g_object_get_data(owner, "luxe-power-closed") && job->done)
    job->done(status, job->data);
  g_clear_object(&owner);
}

static void activity_child_exited(VteTerminal *terminal, gint status, gpointer data)
{
  ActivityJob *job = data;
  activity_finish(job, status);
  g_signal_handlers_disconnect_by_data(terminal, job);
}

static void activity_spawn_ready(VteTerminal *terminal, GPid pid, GError *error, gpointer data)
{
  (void)pid;
  ActivityJob *job = data;
  if (error || !terminal) {
    if (terminal && error) vte_terminal_feed(terminal, error->message, -1);
    activity_finish(job, -1);
    if (terminal) g_signal_handlers_disconnect_by_data(terminal, job);
  }
  job_unref(job);
}

GtkWidget *activity_terminal_new(void)
{
  GtkWidget *terminal = vte_terminal_new();
  gtk_widget_set_vexpand(terminal, TRUE);
  gtk_widget_set_hexpand(terminal, TRUE);
  return terminal;
}

GtkWidget *activity_present(GtkWidget *group, GtkWidget *box)
{
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(box))) gtk_box_remove(GTK_BOX(box), child);
  gtk_widget_set_visible(group, TRUE);
  GtkWidget *terminal = activity_terminal_new();
  gtk_widget_set_size_request(terminal, -1, 160);
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), terminal);
  gtk_box_append(GTK_BOX(box), scroll);
  return terminal;
}

void activity_run(GtkWidget *terminal, const char *const *argv,
                  ActivityDone done, gpointer data, GDestroyNotify destroy)
{
  ActivityJob *job = g_new0(ActivityJob, 1);
  job->refs = 2; /* spawn callback and signal closure */
  job->done = done;
  job->data = data;
  job->destroy = destroy;
  g_weak_ref_init(&job->owner, gtk_widget_get_root(terminal));
  g_signal_connect_data(terminal, "child-exited", G_CALLBACK(activity_child_exited),
                         job, job_closure_free, 0);
  g_auto(GStrv) copy = g_strdupv((char **)argv);
  vte_terminal_spawn_async(VTE_TERMINAL(terminal), VTE_PTY_DEFAULT, NULL, copy, NULL,
                            G_SPAWN_DEFAULT, NULL, NULL, NULL, -1, NULL, activity_spawn_ready, job);
}
