/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"

#include <glib/gstdio.h>

typedef struct {
  SleepPage *page;
  char *failure_title;
} SleepTaskDone;

static void
sleep_task_done_free (gpointer data)
{
  SleepTaskDone *done = data;

  if (done == NULL)
    return;
  g_free (done->failure_title);
  g_free (done);
}

static void
sleep_page_set_controls_enabled (SleepPage *page, gboolean enabled)
{
  gtk_widget_set_sensitive (page->suspend_row, enabled);
  gtk_widget_set_sensitive (page->hibernate_row, enabled);
  gtk_widget_set_sensitive (page->hybrid_row, enabled);
  for (guint i = 0; i < G_N_ELEMENTS (page->lid_rows); i++)
    gtk_widget_set_sensitive (page->lid_rows[i], enabled);
}

static void
sleep_activity_done (int wait_status, gpointer data)
{
  SleepTaskDone *done = data;
  SleepPage *page = done->page;
  gboolean ok = g_spawn_check_wait_status (wait_status, NULL);

  page->updating = FALSE;
  sleep_page_set_controls_enabled (page, TRUE);
  sleep_page_refresh (page);
  if (ok)
    {
      gtk_widget_set_visible(page->terminal_group, FALSE);
      toast (page->win, "System power settings applied");
    }
  else show_error(page->page, done->failure_title, "Review the helper output above.");
}

void
sleep_page_run (  SleepPage *page, const char *const *args, const char *failure_title)
{
  const char *argv[12];
  SleepTaskDone *done;
  int i;

  if (page->updating)
    return;
  page->updating = TRUE;
  sleep_page_set_controls_enabled (page, FALSE);

  page->terminal = activity_present(page->terminal_group, page->terminal_box);

  argv[0] = "/usr/bin/pkexec";
  argv[1] = "--disable-internal-agent";
  argv[2] = LUXEPOWER_HELPER_PATH;
  for (i = 0; args[i] != NULL && i < 8; i++)
    argv[3 + i] = args[i];
  argv[3 + i] = NULL;

  done = g_new0 (SleepTaskDone, 1);
  done->page = page;
  done->failure_title = g_strdup (failure_title);

  activity_run (page->terminal, argv, sleep_activity_done, done, sleep_task_done_free);
}

void
sleep_page_refresh (SleepPage *page)
{
  SleepState state;
  LidState lid;
  int i;

  page->updating = TRUE;
  system_sleep_state (&state);
  gtk_switch_set_active (GTK_SWITCH (page->suspend_row), state.allow_suspend);
  gtk_switch_set_active (GTK_SWITCH (page->hibernate_row), state.allow_hibernate);
  gtk_switch_set_active (GTK_SWITCH (page->hybrid_row), state.allow_hybrid);

  system_lid_state (&lid);
  {
    LidAction values[3] = { lid.handle, lid.handle_external_power, lid.handle_docked };

    for (i = 0; i < 3; i++)
      {
        int index = 4;
        int j;

        for (j = 0; j < LID_ACTION_COUNT; j++)
          {
            if (lid_actions[j].action == values[i])
              {
                index = j;
                break;
              }
          }
        gtk_drop_down_set_selected (GTK_DROP_DOWN (page->lid_rows[i]), (guint) index);
      }
  }
  page->updating = FALSE;
}

static void
sleep_on_sleep_changed (GObject *row, GParamSpec *spec, gpointer data)
{
  SleepPage *page = data;
  const char *args[5];

  (void) row;
  (void) spec;

  if (page->updating)
    return;

  args[0] = "sleep";
  args[1] = gtk_switch_get_active (GTK_SWITCH (page->suspend_row)) ? "1" : "0";
  args[2] = gtk_switch_get_active (GTK_SWITCH (page->hibernate_row)) ? "1" : "0";
  args[3] = gtk_switch_get_active (GTK_SWITCH (page->hybrid_row)) ? "1" : "0";
  args[4] = NULL;

  sleep_page_run (page, args, "Sleep settings were not changed");
}

static void
sleep_on_lid_changed (GObject *row, GParamSpec *spec, gpointer data)
{
  SleepPage *page = data;
  const char *args[3] = { "lid", NULL, NULL };
  g_autofree char *entry = NULL;
  int i;

  (void) spec;

  if (page->updating)
    return;

  for (i = 0; i < 3; i++)
    {
      if (row != G_OBJECT(page->lid_rows[i])) continue;
      guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(row));
      if (selected >= LID_ACTION_COUNT || !lid_actions[selected].value) return;
      entry = g_strdup_printf("%s=%s", lid_key_names[i], lid_actions[selected].value);
      args[1] = entry;
      break;
    }
  if (entry) sleep_page_run(page, args, "Lid settings were not changed");
}

void
sleep_page_init (SleepPage *page, LuxePowerWindow *win)
{
  static const char *titles[3] = {
    "When the lid closes",
    "When the lid closes while plugged in",
    "When the lid closes while docked"
  };
  GtkWidget *group;
  GtkStringList *labels;
  int i;

  memset (page, 0, sizeof (*page));
  page->win = win;

  page->page = luxe_preferences_page_new ();

  group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (group), "System sleep");
  luxe_preferences_group_set_description (GTK_BOX (group),
                                         "Masked sleep paths cannot be triggered by keys, idle timeouts, or the lid. Allowing a path removes its systemd mask, including administrator-created masks.");

  page->suspend_row = luxe_switch_row_new ();
  luxe_row_set_title (GTK_WIDGET (page->suspend_row), "Allow suspend");
  page->hibernate_row = luxe_switch_row_new ();
  luxe_row_set_title (GTK_WIDGET (page->hibernate_row), "Allow hibernate");
  page->hybrid_row = luxe_switch_row_new ();
  luxe_row_set_title (GTK_WIDGET (page->hybrid_row), "Allow hybrid sleep");

  luxe_preferences_group_add (GTK_BOX (group), page->suspend_row);
  luxe_preferences_group_add (GTK_BOX (group), page->hibernate_row);
  luxe_preferences_group_add (GTK_BOX (group), page->hybrid_row);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page), GTK_BOX (group));

  labels = gtk_string_list_new (NULL);
  for (i = 0; i < LID_ACTION_COUNT; i++)
    gtk_string_list_append (labels, lid_actions[i].label);

  group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (group), "Lid actions");
  luxe_preferences_group_set_description (GTK_BOX (group),
                                         "Written to /etc/systemd/logind.conf.d/99-luxepower.conf and applied to logind immediately.");

  for (i = 0; i < 3; i++)
    {
      page->lid_rows[i] = luxe_combo_row_new ();
      luxe_row_set_title (GTK_WIDGET (page->lid_rows[i]), titles[i]);
      gtk_drop_down_set_model (GTK_DROP_DOWN (page->lid_rows[i]), G_LIST_MODEL (labels));
      g_signal_connect (page->lid_rows[i], "notify::selected",
                        G_CALLBACK (sleep_on_lid_changed), page);
      luxe_preferences_group_add (GTK_BOX (group), page->lid_rows[i]);
    }
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page), GTK_BOX (group));

  g_object_unref (labels);

  page->terminal_group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (page->terminal_group),
                                   "Privileged operation");
  page->terminal_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  luxe_preferences_group_add (GTK_BOX (page->terminal_group),
                             page->terminal_box);
  gtk_widget_set_visible (page->terminal_group, FALSE);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                            GTK_BOX (page->terminal_group));

  g_signal_connect (page->suspend_row, "notify::active", G_CALLBACK (sleep_on_sleep_changed), page);
  g_signal_connect (page->hibernate_row, "notify::active", G_CALLBACK (sleep_on_sleep_changed), page);
  g_signal_connect (page->hybrid_row, "notify::active", G_CALLBACK (sleep_on_sleep_changed), page);

  sleep_page_refresh (page);
}
