/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"

#include <glib/gstdio.h>

static void
status_stats_done (int wait_status, gpointer data)
{
  StatusPage *page = data;

  page->fetching = FALSE;
  if (g_spawn_check_wait_status (wait_status, NULL))
    gtk_text_buffer_set_text (page->buffer, "Complete statistics printed in the terminal above.", -1);
  else
    gtk_text_buffer_set_text (page->buffer, "The complete statistics request failed; review the output above.", -1);
}

static void
status_page_run (StatusPage *page, const char *const *argv)
{
  GtkWidget *terminal;

  if (page->fetching)
    return;
  page->fetching = TRUE;
  gtk_text_buffer_set_text (page->buffer, "Gathering…", -1);

  terminal = activity_present(page->terminal_group, page->terminal_box);

  activity_run (terminal, argv, status_stats_done, page, NULL);
}

static void
status_simple_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void) source;
  (void) data;
  HelperResult *result = system_spawn_capture ((const char *const[]) { TLP_STAT_PATH, "-r", "-t", "-c", "-s", "-u", NULL }, cancellable);

  g_task_return_pointer (task, result, (GDestroyNotify) helper_result_free);
}

static void
status_simple_done (GObject *source, GAsyncResult *result, gpointer data)
{
  (void) source;

  StatusPage *page = data;
  HelperResult *result_data = g_task_propagate_pointer (G_TASK (result), NULL);
  if (page->win->closed) {
    helper_result_free(result_data);
    return;
  }
  g_autofree char *output = result_data->code == 0 ? g_strdup(result_data->out) :
    g_strdup_printf("Statistics request failed (%d): %s\n%s", result_data->code,
                    result_data->err, result_data->out);

  helper_result_free (result_data);
  page->fetching = FALSE;
  gtk_text_buffer_set_text (page->buffer, output != NULL ? output : "", -1);
}

void
status_page_refresh (StatusPage *page, gboolean complete)
{
  if (complete)
    {
      const char *args[5] = { "/usr/bin/pkexec", "--disable-internal-agent",
                              LUXEPOWER_HELPER_PATH, "stats", NULL };

      status_page_run (page, args);
    }
  else
    {
      if (page->fetching)
        return;
      GTask *task = g_task_new(page->win->window, NULL, status_simple_done, page);
      page->fetching = TRUE;
      gtk_text_buffer_set_text (page->buffer, "Gathering…", -1);
      g_task_set_task_data (task, page, NULL);
      g_task_run_in_thread (task, status_simple_worker);
      g_object_unref (task);
    }
}
static void
status_on_refresh (GtkButton *button, gpointer data)
{
  (void) button;


  status_page_refresh (data, FALSE);
}

static void
status_on_full (GtkButton *button, gpointer data)
{
  (void) button;


  status_page_refresh (data, TRUE);
}

void
status_page_init (StatusPage *page, LuxePowerWindow *win)
{
  GtkWidget *group;
  GtkWidget *view;
  GtkWidget *scroll;

  memset (page, 0, sizeof (*page));
  page->win = win;

  page->page = luxe_preferences_page_new ();

  group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (group), "TLP status");
  luxe_preferences_group_set_description (GTK_BOX (group),
                                         "System summary without administrator privileges.");
  page->refresh_button = gtk_button_new_with_label ("Refresh");
  gtk_widget_set_halign (page->refresh_button, GTK_ALIGN_START);
  g_signal_connect (page->refresh_button, "clicked", G_CALLBACK (status_on_refresh), page);
  luxe_preferences_group_add (GTK_BOX (group), page->refresh_button);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page), GTK_BOX (group));

  group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (group), "Complete statistics");
  luxe_preferences_group_set_description (GTK_BOX (group),
                                         "Requests administrator authentication and shows every supported device.");
  page->full_button = gtk_button_new_with_label ("Fetch complete statistics…");
  gtk_widget_set_halign (page->full_button, GTK_ALIGN_START);
  gtk_widget_add_css_class (page->full_button, "suggested-action");
  g_signal_connect (page->full_button, "clicked", G_CALLBACK (status_on_full), page);
  luxe_preferences_group_add (GTK_BOX (group), page->full_button);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page), GTK_BOX (group));

  group = luxe_preferences_group_new ();
  page->buffer = gtk_text_buffer_new (NULL);
  gtk_text_buffer_set_text (page->buffer, "Choose Refresh to gather the current power status.", -1);
  view = gtk_text_view_new_with_buffer (page->buffer);
  gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace (GTK_TEXT_VIEW (view), TRUE);
  gtk_text_view_set_left_margin (GTK_TEXT_VIEW (view), 6);
  gtk_text_view_set_right_margin (GTK_TEXT_VIEW (view), 6);
  scroll = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), view);
  gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (scroll), 420);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  luxe_preferences_group_add (GTK_BOX (group), scroll);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page), GTK_BOX (group));

  page->terminal_group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (page->terminal_group),
                                   "Complete statistics");
  page->terminal_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  luxe_preferences_group_add (GTK_BOX (page->terminal_group),
                             page->terminal_box);
  gtk_widget_set_visible (page->terminal_group, FALSE);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                            GTK_BOX (page->terminal_group));
}
