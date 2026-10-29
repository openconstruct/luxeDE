/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"

#include <glib/gstdio.h>
#include <unistd.h>
#include <errno.h>

static void power_load_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancellable);
static void power_page_set_busy (PowerPage *page);

static void
power_page_set_rows_sensitive (PowerPage *page, gboolean sensitive)
{
  for (gsize i = 0; i < page->rows->len; i++)
    {
      PowerRow *row = g_ptr_array_index (page->rows, i);
      gtk_widget_set_sensitive (row->row, sensitive);
    }
}

typedef struct {
  GHashTable *settings;
  GHashTable *defaults;
  char *error;
  TlpOwnedGroup *groups;
  int n_groups;
} PowerLoadResult;

static PowerLoadResult *
power_load_result_new (GHashTable *settings, GHashTable *defaults, const char *error)
{
  PowerLoadResult *result = g_new0 (PowerLoadResult, 1);

  result->settings = settings;
  result->defaults = defaults;
  result->error = g_strdup (error);
  return result;
}

static void
power_load_result_free (gpointer data)
{
  PowerLoadResult *result = data;

  if (result == NULL)
    return;
  tlp_free_settings (result->settings);
  tlp_free_settings (result->defaults);
  g_free (result->error);
  tlp_effective_groups_free(result->groups, result->n_groups);
  g_free (result);
}

static void
power_status_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void) source;

  (void) data;

  (void) cancellable;
  gboolean on_ac = system_power_source ();
  int percent = system_battery_percent ();

  g_task_return_pointer (task, g_strdup_printf ("%d %d", on_ac ? 1 : 0, percent), g_free);
}

static void
power_status_done (GObject *source, GAsyncResult *result, gpointer data)
{
  (void) source;
  PowerPage *page = data;
  g_autofree char *packed = g_task_propagate_pointer (G_TASK (result), NULL);
  int on_ac = 0;
  int percent = -1;
  g_autofree char *text = NULL;

  if (packed == NULL || page->win->closed)
    return;
  sscanf (packed, "%d %d", &on_ac, &percent);

  text = on_ac ? g_strdup ("Plugged in") : g_strdup ("On battery");
  gtk_label_set_text (GTK_LABEL (page->power_label), text);

  g_free (g_steal_pointer (&text));
  text = percent >= 0 ? g_strdup_printf ("%d %%", percent) : g_strdup ("Not detected");
  gtk_label_set_text (GTK_LABEL (page->battery_label), text);
}

static void
power_page_refresh_status (PowerPage *page)
{
  GTask *task = g_task_new (page->win->window, NULL, power_status_done, page);

  g_task_set_task_data (task, page, NULL);
  g_task_run_in_thread (task, power_status_worker);
  g_object_unref (task);
}

static void
power_row_free (gpointer data)
{
  PowerRow *row = data;

  if (row == NULL)
    return;
  g_strfreev (row->owned_choices);
  g_free (row->key);
  g_free (row);
}

static void
power_page_clear_content (PowerPage *page)
{
  gsize i;

  for (i = 0; i < page->group_widgets->len; i++)
    {
      GtkWidget *group = g_ptr_array_index (page->group_widgets, i);

      luxe_preferences_page_remove (GTK_SCROLLED_WINDOW (page->page),
                                   GTK_BOX (group));
    }
  g_ptr_array_set_size (page->group_widgets, 0);
  g_ptr_array_set_size (page->rows, 0);

}

void
power_page_collect_desired (PowerPage *page, GHashTable *desired)
{
  gsize i;

  for (i = 0; i < page->rows->len; i++)
    {
      PowerRow *row = g_ptr_array_index (page->rows, i);
      TlpDesired wanted = { FALSE, NULL };
      TlpSetting *default_setting = g_hash_table_lookup (page->defaults, row->key);

      if (row->kind == TLP_ROW_COMBO)
        {
          guint selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (row->row));
          const char *choice = NULL;

          if (selected != GTK_INVALID_LIST_POSITION && selected > 0 && selected <= (guint) row->n_choices)
            choice = row->choices[selected - 1];

          if (choice != NULL)
            {
              wanted.enabled = TRUE;
              wanted.value = g_strdup (choice);
            }
          else
            {
              wanted.enabled = FALSE;
              wanted.value = tlp_effective_value (page->settings, page->defaults, row->key);
            }
        }
      else if (row->kind == TLP_ROW_SWITCH)
        {
          gboolean active = gtk_switch_get_active (GTK_SWITCH (row->row));
          const char *value = active ? row->on : row->off;

          wanted.value = g_strdup (value);
          if (default_setting != NULL && active == (g_strcmp0 (default_setting->value, row->on) == 0))
            wanted.enabled = FALSE;
          else
            wanted.enabled = TRUE;
        }
      else
        {
          int value = (int) gtk_spin_button_get_value (GTK_SPIN_BUTTON (row->row));

          wanted.value = g_strdup_printf ("%d", value);
          if (default_setting != NULL &&
              g_strcmp0 (wanted.value, default_setting->value) == 0)
            wanted.enabled = FALSE;
          else
            wanted.enabled = TRUE;
        }

      g_hash_table_replace (desired, g_strdup (row->key), tlp_desired_copy (&wanted));
      g_free(wanted.value);
    }
}

void
power_page_update_dirty (PowerPage *page)
{
  g_autoptr(GHashTable) desired = NULL;
  gboolean dirty;
  GHashTableIter iter;
  gpointer key, value;

  if (!page->loaded || page->applying || page->loading)
    {
      gtk_widget_set_sensitive (page->apply_button, FALSE);
      gtk_widget_set_sensitive (page->discard_button, FALSE);
      return;
    }

  desired = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) tlp_desired_free);
  power_page_collect_desired (page, desired);

  dirty = g_hash_table_size (desired) != g_hash_table_size (page->original);
  if (!dirty)
    {
      g_hash_table_iter_init (&iter, desired);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          TlpDesired *wanted = value;
          TlpDesired *orig = g_hash_table_lookup (page->original, key);

          if (orig == NULL || orig->enabled != wanted->enabled ||
              g_strcmp0 (orig->value, wanted->value) != 0)
            {
              dirty = TRUE;
              break;
            }
        }
    }

  gtk_widget_set_sensitive (page->apply_button, dirty);
  gtk_widget_set_sensitive (page->discard_button, dirty);
}

static void
power_on_row_changed (GObject *row, GParamSpec *spec, gpointer data)
{
  (void) row;

  (void) spec;
  power_page_update_dirty (data);
}

static GtkWidget *
power_page_build_row (PowerPage *page, const TlpRowSpec *spec, const char *key)
{
  PowerRow *row = g_new0 (PowerRow, 1);
  const char *subtitle = tlp_row_subtitle (key);
  g_autofree char *title = NULL;
  GtkWidget *widget;
  int i;

  row->kind = spec->kind;
  row->key = g_strdup (key);
  row->on = spec->on;
  row->off = spec->off;
  row->choices = spec->choices;
  row->n_choices = spec->n_choices;

  if (spec->n_keys > 1)
    {
      if (g_str_has_suffix (key, "_AC"))
        title = g_strdup_printf ("%s — plugged in", spec->title);
      else
        title = g_strdup_printf ("%s — on battery", spec->title);
    }
  else
    {
      title = g_strdup (spec->title);
    }

  if (spec->kind == TLP_ROW_COMBO)
    {
      GtkStringList *list;
      g_autofree char *current = tlp_effective_value (page->settings, page->defaults, key);
      gboolean enabled = FALSE;
      TlpSetting *setting = g_hash_table_lookup (page->settings, key);
      int selected = -1;

      if (setting != NULL)
        enabled = setting->enabled;

      if (spec->kind == TLP_ROW_COMBO)
        {
          row->owned_choices = g_new0 (char *, spec->n_choices + 1);
          for (i = 0; i < spec->n_choices; i++)
            row->owned_choices[i] = g_strdup (spec->choices[i]);
          row->choices = (const char *const *) row->owned_choices;
        }

      if (enabled && current[0] != '\0')
        {
          for (i = 0; i < row->n_choices; i++)
            {
              if (g_strcmp0 (row->choices[i], current) == 0)
                {
                  selected = i;
                  break;
                }
            }
        }

      widget = luxe_combo_row_new ();
      luxe_row_set_title (GTK_WIDGET (widget), title);
      luxe_row_set_subtitle (GTK_WIDGET (widget), subtitle);

      list = gtk_string_list_new (NULL);
      gtk_string_list_append (list, "Default (recommended)");
      if (selected < 0 && enabled && current[0] != '\0')
        {
          row->owned_choices = g_realloc (row->owned_choices,
                                          sizeof (char *) * (row->n_choices + 2));
          row->owned_choices[row->n_choices] = g_strdup (current);
          row->owned_choices[row->n_choices + 1] = NULL;
          row->choices = (const char *const *) row->owned_choices;
          row->n_choices++;
          selected = row->n_choices - 1;
        }
      for (i = 0; i < row->n_choices; i++)
        gtk_string_list_append (list, row->choices[i]);

      gtk_drop_down_set_model (GTK_DROP_DOWN (widget), G_LIST_MODEL (list));
      g_object_unref(list);
      gtk_drop_down_set_selected (GTK_DROP_DOWN (widget),
                                  selected >= 0 ? (guint) (selected + 1) : 0);
      g_signal_connect (widget, "notify::selected", G_CALLBACK (power_on_row_changed), page);
    }
  else if (spec->kind == TLP_ROW_SWITCH)
    {
      g_autofree char *current = tlp_effective_value (page->settings, page->defaults, key);

      widget = luxe_switch_row_new ();
      luxe_row_set_title (GTK_WIDGET (widget), title);
      luxe_row_set_subtitle (GTK_WIDGET (widget), subtitle);
      gtk_switch_set_active (GTK_SWITCH (widget),
                                 g_strcmp0 (current, spec->on) == 0);
      g_signal_connect (widget, "notify::active", G_CALLBACK (power_on_row_changed), page);
    }
  else
    {
      g_autofree char *current = tlp_effective_value (page->settings, page->defaults, key);
      GtkAdjustment *adjustment;
      long value;

      adjustment = gtk_adjustment_new (0, 0, 100, 1, 10, 0);
      widget = luxe_spin_row_new (adjustment, 1.0, 0);
      luxe_row_set_title (GTK_WIDGET (widget), title);
      luxe_row_set_subtitle (GTK_WIDGET (widget), subtitle);
      value = strtol (current, NULL, 10);
      value = CLAMP (value, 0, 100);
      gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), value);
      g_signal_connect (widget, "notify::value", G_CALLBACK (power_on_row_changed), page);
    }

  row->row = widget;
  g_object_set_data_full (G_OBJECT (widget), "power-row", row, power_row_free);
  return widget;
}

static void
power_page_capture_original (PowerPage *page)
{
  g_autoptr(GHashTable) desired = NULL;

  desired = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) tlp_desired_free);
  power_page_collect_desired (page, desired);
  if (page->original != NULL)
    g_hash_table_unref (page->original);
  page->original = g_steal_pointer (&desired);
}

static void
power_page_load_done (GObject *source, GAsyncResult *result, gpointer data)
{
  (void) source;
  PowerPage *page = data;
  PowerLoadResult *loaded = g_task_propagate_pointer (G_TASK (result), NULL);
  TlpOwnedGroup *groups;
  int n_groups;
  int g_i, r_i, k_i;
  gsize i;

  page->loading = FALSE;
  if (page->win->closed) {
    power_load_result_free(loaded);
    return;
  }
  page->loaded = FALSE;
  power_page_clear_content (page);

  if (loaded->error != NULL)
    {
      gtk_label_set_text (GTK_LABEL (page->error_label), loaded->error);
      gtk_widget_set_visible (page->error_group, TRUE);
      gtk_widget_set_visible (page->apply_group, FALSE);
      page->loaded = FALSE;
      power_load_result_free (loaded);
      return;
    }

  if (page->settings != NULL)
    g_hash_table_unref (page->settings);
  if (page->defaults != NULL)
    g_hash_table_unref (page->defaults);
  page->settings = loaded->settings;
  page->defaults = loaded->defaults;
  loaded->settings = NULL;
  loaded->defaults = NULL;

  gtk_widget_set_visible (page->error_group, FALSE);
  gtk_widget_set_visible (page->apply_group, TRUE);

  groups = loaded->groups;
  n_groups = loaded->n_groups;
  for (g_i = 0; g_i < n_groups; g_i++)
    {
      GtkWidget *group = luxe_preferences_group_new ();
      GPtrArray *built = g_ptr_array_new ();

      luxe_preferences_group_set_title (GTK_BOX (group), groups[g_i].title);

      for (r_i = 0; r_i < groups[g_i].n_rows; r_i++)
        {
          const TlpRowSpec *spec = groups[g_i].rows[r_i];

          for (k_i = 0; k_i < spec->n_keys; k_i++)
            {
              GtkWidget *widget = power_page_build_row (page, spec, spec->keys[k_i]);

              luxe_preferences_group_add (GTK_BOX (group), widget);
              g_ptr_array_add (built, widget);
            }
        }

      if (built->len > 0)
        {
          luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                                    GTK_BOX (group));
          g_ptr_array_add (page->group_widgets, group);
          for (i = 0; i < built->len; i++)
            {
              PowerRow *row = g_object_get_data (G_OBJECT (g_ptr_array_index (built, i)),
                                                 "power-row");
              g_ptr_array_add (page->rows, row);
            }
        }
      else
        {
          g_object_ref_sink(group);
          g_object_unref(group);
        }
      g_ptr_array_unref (built);
    }

  power_page_capture_original (page);
  page->loaded = TRUE;
  power_page_update_dirty (page);
  power_load_result_free (loaded);
}

void
power_page_load (PowerPage *page)
{
  GTask *task;

  if (page->win->closed || page->loading || page->applying) return;
  page->loading = TRUE;
  power_page_set_busy (page);
  power_page_refresh_status (page);

  task = g_task_new (page->win->window, NULL, power_page_load_done, page);
  g_task_set_task_data (task, page, NULL);
  g_task_run_in_thread (task, power_load_worker);
  g_object_unref (task);
}

static void
power_load_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void) source;

  (void) data;

  (void) cancellable;
  GHashTable *settings = NULL;
  GHashTable *defaults = NULL;
  g_autofree char *stat_text = NULL;
  GError *error = NULL;

  if (!tlp_installed ())
    {
      g_task_return_pointer (task,
                             power_load_result_new (NULL, NULL,
                                                    "tlp-stat and tlp must be installed"),
                             power_load_result_free);
      return;
    }

  stat_text = tlp_stat_output ((const char *const[]) { TLP_STAT_PATH, "-c", NULL }, &error);
  if (stat_text == NULL)
    {
      g_task_return_pointer (task,
                             power_load_result_new (NULL, NULL, error->message),
                             power_load_result_free);
      g_error_free (error);
      return;
    }

  settings = tlp_extract_settings (stat_text);
  defaults = tlp_extract_defaults (TLP_DEFAULTS_PATH);
  PowerLoadResult *loaded = power_load_result_new(settings, defaults, NULL);
  loaded->groups = tlp_effective_groups(settings, &loaded->n_groups);
  g_task_return_pointer(task, loaded, power_load_result_free);
}

static void
power_on_discard (GtkButton *button, gpointer data)
{
  (void) button;
  power_page_load (data);
}

static void
power_page_set_busy (PowerPage *page)
{
  gtk_widget_set_sensitive (page->apply_button, FALSE);
  gtk_widget_set_sensitive (page->discard_button, FALSE);
}

static char *
stage_config_text (const char *text, char **hash_out, GError **error)
{
  g_autofree char *path = NULL;

  int fd = g_file_open_tmp("luxepower-config-XXXXXX.conf", &path, error);
  if (fd < 0)
    return NULL;
  gsize length = strlen(text), offset = 0;
  while (offset < length) {
    ssize_t written = write(fd, text + offset, length - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) {
      g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                  "Cannot stage configuration: %s", g_strerror(errno));
      close(fd); g_unlink(path);
      return NULL;
    }
    offset += written;
  }
  if (close(fd) != 0) {
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "Cannot close staged configuration: %s", g_strerror(errno));
    g_unlink(path);
    return NULL;
  }
  *hash_out = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, length);
  return g_steal_pointer (&path);
}

static void
power_apply_done (int wait_status, gpointer data)
{
  PowerPage *page = data;
  gboolean ok = g_spawn_check_wait_status (wait_status, NULL);

  g_unlink (page->staged_path);
  g_clear_pointer (&page->staged_path, g_free);
  g_clear_pointer (&page->staged_digest, g_free);

  page->applying = FALSE;
  if (ok)
    {
      toast (page->win, "Power settings applied");
      power_page_load (page);
    }
  else
    {
      power_page_set_rows_sensitive (page, FALSE);
      gtk_widget_set_sensitive (page->apply_button, FALSE);
      gtk_widget_set_sensitive (page->discard_button, TRUE);
      show_error (page->page, "Power settings outcome is uncertain",
                  "Your edits are preserved for review. The configuration may have been written even though activation failed. Choose Discard to reload the current system configuration before making another change.");
    }
}

void
power_on_apply (GtkButton *button, gpointer data)
{
  PowerPage *page = data;
  g_autoptr(GHashTable) desired = NULL;
  GHashTable *changes;
  g_autofree char *original_text = NULL;
  g_autofree char *new_text = NULL;
  g_autofree char *original_digest = NULL;
  GError *error = NULL;
  GtkWidget *terminal;
  const char *argv[8];

  (void) button;

  if (page->applying || page->loading || !page->loaded || page->win->closed)
    return;
  page->applying = TRUE;
  power_page_set_busy (page);

  desired = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) tlp_desired_free);
  power_page_collect_desired (page, desired);
  changes = tlp_build_changes (page->settings, page->defaults, desired);

  if (g_hash_table_size (changes) == 0)
    {
      g_hash_table_unref (changes);
      page->applying = FALSE;
      power_page_update_dirty (page);
      return;
    }

  original_text = tlp_read_user_config (&error);
  if (original_text == NULL)
    {
      page->applying = FALSE;
      show_error (page->page, "Power settings were not written", error->message);
      g_error_free (error);
      g_hash_table_unref (changes);
      power_page_update_dirty (page);
      return;
    }

  new_text = tlp_render_config (original_text, changes, page->defaults);
  g_hash_table_unref (changes);
  original_digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256,
                                                    original_text, -1);

  page->staged_path = stage_config_text (new_text, &page->staged_digest, &error);
  if (page->staged_path == NULL)
    {
      page->applying = FALSE;
      show_error (page->page, "Power settings were not written", error->message);
      g_error_free (error);
      power_page_update_dirty (page);
      return;
    }

  terminal = activity_present(page->terminal_group, page->terminal_box);

  argv[0] = "/usr/bin/pkexec";
  argv[1] = "--disable-internal-agent";
  argv[2] = LUXEPOWER_HELPER_PATH;
  argv[3] = "config";
  argv[4] = page->staged_path;
  argv[5] = page->staged_digest;
  argv[6] = original_digest;
  argv[7] = NULL;
  activity_run (terminal, argv, power_apply_done, page, NULL);
}

void
power_page_init (PowerPage *page, LuxePowerWindow *win)
{
  GtkWidget *group;
  GtkWidget *row;

  memset (page, 0, sizeof (*page));
  page->win = win;
  page->rows = g_ptr_array_new ();
  page->group_widgets = g_ptr_array_new ();

  page->page = luxe_preferences_page_new ();

  group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (group), "Now");

  page->power_label = gtk_label_new ("…");
  gtk_widget_add_css_class (page->power_label, "dim-label");
  row = luxe_action_row_new ();
  luxe_row_set_title (GTK_WIDGET (row), "Power source");
  luxe_action_row_add_suffix (GTK_LIST_BOX_ROW (row), page->power_label);
  luxe_preferences_group_add (GTK_BOX (group), row);

  page->battery_label = gtk_label_new ("…");
  gtk_widget_add_css_class (page->battery_label, "dim-label");
  row = luxe_action_row_new ();
  luxe_row_set_title (GTK_WIDGET (row), "Battery");
  gtk_widget_set_tooltip_text (GTK_WIDGET (row),
      "For multiple batteries, charge is energy-weighted when available; otherwise it is an average of reported percentages.");
  luxe_action_row_add_suffix (GTK_LIST_BOX_ROW (row), page->battery_label);
  luxe_preferences_group_add (GTK_BOX (group), row);

  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page), GTK_BOX (group));

  page->error_group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (page->error_group),
                                   "Power settings unavailable");
  page->error_label = dim_label ("");
  luxe_preferences_group_add (GTK_BOX (page->error_group), page->error_label);
  gtk_widget_set_visible (page->error_group, FALSE);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                            GTK_BOX (page->error_group));

  page->apply_group = luxe_preferences_group_new ();
  {
    GtkWidget *buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

    gtk_widget_set_halign (buttons, GTK_ALIGN_END);
    page->discard_button = gtk_button_new_with_label ("Discard");
    g_signal_connect (page->discard_button, "clicked", G_CALLBACK (power_on_discard), page);
    page->apply_button = gtk_button_new_with_label ("Apply…");
    gtk_widget_add_css_class (page->apply_button, "suggested-action");
    g_signal_connect (page->apply_button, "clicked", G_CALLBACK (power_on_apply), page);
    gtk_box_append (GTK_BOX (buttons), page->discard_button);
    gtk_box_append (GTK_BOX (buttons), page->apply_button);
    luxe_preferences_group_add (GTK_BOX (page->apply_group), buttons);
  }
  gtk_widget_set_visible (page->apply_group, FALSE);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                            GTK_BOX (page->apply_group));

  page->terminal_group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (page->terminal_group),
                                   "Privileged operation");
  page->terminal_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  luxe_preferences_group_add (GTK_BOX (page->terminal_group),
                             page->terminal_box);
  gtk_widget_set_visible (page->terminal_group, FALSE);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                            GTK_BOX (page->terminal_group));
}


void
power_page_free (PowerPage *page)
{
  if (page->staged_path) g_unlink(page->staged_path);
  g_clear_pointer(&page->staged_path, g_free);
  g_clear_pointer(&page->staged_digest, g_free);
  if (page->settings != NULL)
    g_hash_table_unref (page->settings);
  if (page->defaults != NULL)
    g_hash_table_unref (page->defaults);
  if (page->original != NULL)
    g_hash_table_unref (page->original);
  g_ptr_array_unref (page->rows);
  g_ptr_array_unref (page->group_widgets);
}
