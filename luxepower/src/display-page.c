/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

#include "app.h"

#include <glib/gstdio.h>

typedef struct {
  char *name;
  long value;
} BrightnessJob;

typedef struct {
  char *error;
  long actual;
  long maximum;
  gboolean have_actual;
  gboolean have_maximum;
} BrightnessResult;

static void brightness_result_free (gpointer data)
{
  BrightnessResult *result = data;
  g_free (result->error);
  g_free (result);
}

static void display_page_send_brightness(DisplayPage *page);
static BacklightDevice *display_page_current_device (DisplayPage *page);

static void
display_idle_lock_status_update (DisplayPage *page)
{
  gboolean failed = g_settings_get_boolean (page->settings, "idle-lock-failed");

  gtk_widget_set_visible (page->lock_error_label, failed);
}

static void
display_idle_lock_status_changed (GSettings *settings, const char *key, gpointer data)
{
  (void) settings;
  (void) key;
  display_idle_lock_status_update (data);
}

static void
brightness_job_free (gpointer data)
{
  BrightnessJob *job = data;

  if (job == NULL)
    return;
  g_free (job->name);
  g_free (job);
}

static void
brightness_task_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void) source;

  (void) data;

  (void) cancellable;
  BrightnessJob *job = g_task_get_task_data (task);
  GError *error = NULL;
  BrightnessResult *out = g_new0 (BrightnessResult, 1);

  system_set_brightness (job->name, job->value, &error);
  if (error != NULL)
    {
      out->error = g_steal_pointer (&error->message);
      g_error_free (error);
      g_autoptr(GPtrArray) devices = system_backlight_devices ();
      for (guint i = 0; i < devices->len; i++) {
        BacklightDevice *device = g_ptr_array_index (devices, i);
        if (g_str_equal (device->name, job->name)) {
          out->actual = device->current;
          out->maximum = device->max;
          out->have_actual = TRUE;
          out->have_maximum = TRUE;
          break;
        }
      }
      if (!out->have_actual) {
        char *message = g_strconcat (out->error, "\nThe current brightness could not be read.", NULL);
        g_free (out->error);
        out->error = message;
      }
    }
  else
    {
      out->actual = job->value;
      out->have_actual = TRUE;
    }
  g_task_return_pointer (task, out, brightness_result_free);
}

static void
brightness_task_done (GObject *source, GAsyncResult *result, gpointer data)
{
  (void) source;
  DisplayPage *page = data;
  BrightnessJob *job = g_task_get_task_data (G_TASK (result));
  BrightnessResult *out = g_task_propagate_pointer (G_TASK (result), NULL);
  page->brightness_busy = FALSE;
  if (page->win->closed) { brightness_result_free (out); return; }

  BacklightDevice *selected = display_page_current_device (page);
  for (guint i = 0; page->devices && i < page->devices->len; i++) {
    BacklightDevice *device = g_ptr_array_index (page->devices, i);
    if (g_str_equal (device->name, job->name) && out->have_actual) {
      if (out->have_maximum) device->max = out->maximum;
      device->current = out->actual;
      if (out->error && device == selected && !page->brightness_pending &&
          page->source_id == 0 && device->max > 0) {
        page->updating = TRUE;
        gtk_range_set_value (GTK_RANGE (page->scale),
                             CLAMP (100.0 * device->current / device->max, 0.0, 100.0));
        page->updating = FALSE;
      }
      break;
    }
  }

  if (out->error != NULL)
    {
      /* The logind SetBrightness D-Bus call failed: surface it both as a
       * modal dialog and as a transient status toast so the failure is
       * visible even when the dialog parent has no root yet. */
      if (page->win != NULL)
        toast (page->win, "Brightness could not be changed");
      show_error (page->page, "Brightness could not be changed", out->error);
    }
  brightness_result_free (out);
  if (page->brightness_pending) {
    page->brightness_pending = FALSE;
    display_page_send_brightness(page);
  }
}

static BacklightDevice *
display_page_current_device (DisplayPage *page)
{
  guint selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (page->device_row));

  if (page->devices == NULL || page->devices->len == 0)
    return NULL;
  if (selected == GTK_INVALID_LIST_POSITION || selected >= page->devices->len)
    return g_ptr_array_index (page->devices, 0);
  return g_ptr_array_index (page->devices, selected);
}

static void
display_page_send_brightness (DisplayPage *page)
{
  BacklightDevice *device;
  BrightnessJob *job;
  GTask *task;
  double fraction;

  if (page->brightness_busy) {
    page->brightness_pending = TRUE;
    return;
  }

  device = display_page_current_device (page);
  if (device == NULL)
    return;

  fraction = gtk_range_get_value (GTK_RANGE (page->scale)) / 100.0;
  job = g_new0 (BrightnessJob, 1);
  job->name = g_strdup (device->name);
  job->value = (long) (fraction * (double) device->max + 0.5);
  page->brightness_busy = TRUE;

  task = g_task_new (page->win->window, NULL, brightness_task_done, page);
  g_task_set_task_data (task, job, brightness_job_free);
  g_task_run_in_thread (task, brightness_task_worker);
  g_object_unref (task);
}

static gboolean
brightness_debounce (gpointer data)
{
  DisplayPage *page = data;

  page->source_id = 0;
  display_page_send_brightness (page);
  return G_SOURCE_REMOVE;
}

static void
display_on_brightness_changed (GtkRange *range, gpointer data)
{
  (void) range;
  DisplayPage *page = data;

  if (page->updating)
    return;
  if (page->source_id != 0)
    g_source_remove (page->source_id);
  page->source_id = g_timeout_add (200, brightness_debounce, page);
}

static void
display_on_fine (GtkButton *button, gpointer data)
{
  DisplayPage *page = data;
  double value;

  if (page->updating)
    return;
  value = gtk_range_get_value(GTK_RANGE(page->scale)) +
          GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "brightness-step"));
  gtk_range_set_value (GTK_RANGE (page->scale), CLAMP (value, 0.0, 100.0));
}

static void
display_on_device_selected (GObject *row, GParamSpec *spec, gpointer data)
{
  (void) row;

  (void) spec;
  DisplayPage *page = data;
  BacklightDevice *device;
  double percent;

  if (page->updating)
    return;

  if (page->source_id) {
    g_source_remove(page->source_id);
    page->source_id = 0;
  }
  page->brightness_pending = FALSE;
  page->updating = TRUE;
  device = display_page_current_device (page);
  if (device != NULL)
    {
      percent = 100.0 * (double) device->current / (double) device->max;
      gtk_range_set_value (GTK_RANGE (page->scale), CLAMP (percent, 0.0, 100.0));
    }
  page->updating = FALSE;
}

static void
display_on_minutes_changed (GObject *row, GParamSpec *spec, gpointer data)
{
  (void) row;

  (void) spec;
  DisplayPage *page = data;
  guint selected;

  if (page->updating || page->settings == NULL)
    return;

  selected = gtk_drop_down_get_selected (GTK_DROP_DOWN (page->minutes_row));
  if (selected != GTK_INVALID_LIST_POSITION && selected < G_N_ELEMENTS (idle_minutes))
    g_settings_set_uint (page->settings, "lock-minutes", idle_minutes[selected]);
}

static void
display_on_lock_now (GtkButton *button, gpointer data)
{
  (void) button;
  DisplayPage *page = data;
  request_screen_lock(page->page);
}

void
display_page_refresh (DisplayPage *page)
{
  BacklightDevice *device;
  g_autoptr(GString) names = NULL;
  gsize i;
  double percent;

  page->updating = TRUE;

  if (page->source_id) {
    g_source_remove(page->source_id);
    page->source_id = 0;
  }
  page->brightness_pending = FALSE;

  if (page->devices != NULL)
    g_ptr_array_unref (page->devices);
  page->devices = system_backlight_devices ();

  gtk_widget_set_visible (page->brightness_group, page->devices->len > 0);
  if (page->devices->len == 0)
    {
      page->updating = FALSE;
      return;
    }

  names = g_string_new (NULL);
  for (i = 0; i < page->devices->len; i++)
    {
      BacklightDevice *device = g_ptr_array_index (page->devices, i);

      g_string_append (names, device->name);
      if (i + 1 < page->devices->len)
        g_string_append_c (names, '\x1f');
    }
  {
    g_auto(GStrv) tokens = g_strsplit (names->str, "\x1f", -1);
    GtkStringList *list = gtk_string_list_new ((const char *const *) tokens);

    gtk_drop_down_set_model (GTK_DROP_DOWN (page->device_row), G_LIST_MODEL (list));
    g_object_unref (list);
  }
  gtk_drop_down_set_selected (GTK_DROP_DOWN (page->device_row), 0);

  device = display_page_current_device (page);
  if (device != NULL && device->max > 0)
    {
      percent = 100.0 * (double) device->current / (double) device->max;
      gtk_range_set_value (GTK_RANGE (page->scale), CLAMP (percent, 0.0, 100.0));
    }

  page->updating = FALSE;
}

void
display_page_init (DisplayPage *page, LuxePowerWindow *win)
{
  GtkWidget *box;
  GtkWidget *button;

  memset (page, 0, sizeof (*page));
  page->win = win;
  page->devices = g_ptr_array_new ();
  page->source_id = 0;

  page->page = luxe_preferences_page_new ();

  page->brightness_group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (page->brightness_group),
                                   "Screen brightness");

  page->device_row = luxe_combo_row_new ();
  luxe_row_set_title (GTK_WIDGET (page->device_row), "Backlight device");
  luxe_preferences_group_add (GTK_BOX (page->brightness_group), page->device_row);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  button = gtk_button_new_from_icon_name ("list-remove-symbolic");
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(button), "brightness-step", GINT_TO_POINTER(-5));
  gtk_widget_set_tooltip_text(button, "Decrease brightness");
  g_signal_connect (button, "clicked", G_CALLBACK (display_on_fine), page);
  gtk_box_append (GTK_BOX (box), button);

  page->scale = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  gtk_range_set_value (GTK_RANGE (page->scale), 50.0);
  gtk_scale_set_digits (GTK_SCALE (page->scale), 0);
  gtk_scale_set_draw_value (GTK_SCALE (page->scale), TRUE);
  gtk_widget_set_hexpand (page->scale, TRUE);
  g_signal_connect (page->scale, "value-changed", G_CALLBACK (display_on_brightness_changed), page);
  gtk_box_append (GTK_BOX (box), page->scale);

  button = gtk_button_new_from_icon_name ("list-add-symbolic");
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(button), "brightness-step", GINT_TO_POINTER(5));
  gtk_widget_set_tooltip_text(button, "Increase brightness");
  g_signal_connect (button, "clicked", G_CALLBACK (display_on_fine), page);
  gtk_box_append (GTK_BOX (box), button);

  luxe_preferences_group_add (GTK_BOX (page->brightness_group), box);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                            GTK_BOX (page->brightness_group));

  g_signal_connect (page->device_row, "notify::selected", G_CALLBACK (display_on_device_selected), page);

  page->lock_group = luxe_preferences_group_new ();
  luxe_preferences_group_set_title (GTK_BOX (page->lock_group), "Screen lock");
  luxe_preferences_group_set_description (GTK_BOX (page->lock_group),
                                         "An idle monitor runs in your session and starts the native lock screen.");

  page->lock_switch = luxe_switch_row_new ();
  luxe_row_set_title (GTK_WIDGET (page->lock_switch),
                                 "Lock the screen when idle");
  luxe_preferences_group_add (GTK_BOX (page->lock_group), page->lock_switch);

  page->minutes_row = luxe_combo_row_new ();
  luxe_row_set_title (GTK_WIDGET (page->minutes_row),
                                 "Idle time before locking");
  {
    GtkStringList *list = gtk_string_list_new (NULL);

    for (gsize i = 0; i < G_N_ELEMENTS (idle_labels); i++)
      gtk_string_list_append (list, idle_labels[i]);
    gtk_drop_down_set_model (GTK_DROP_DOWN (page->minutes_row), G_LIST_MODEL (list));
    g_object_unref (list);
  }
  luxe_preferences_group_add (GTK_BOX (page->lock_group), page->minutes_row);

  page->lock_error_label = gtk_label_new (
      "The idle lock failed. Your screen may not be locked.");
  gtk_label_set_xalign (GTK_LABEL (page->lock_error_label), 0.0f);
  gtk_label_set_wrap (GTK_LABEL (page->lock_error_label), TRUE);
  gtk_widget_add_css_class (page->lock_error_label, "error");
  gtk_widget_set_visible (page->lock_error_label, FALSE);
  luxe_preferences_group_add (GTK_BOX (page->lock_group), page->lock_error_label);

  button = gtk_button_new_with_label ("Lock now");
  gtk_widget_set_halign (button, GTK_ALIGN_START);
  gtk_widget_add_css_class (button, "suggested-action");
  g_signal_connect (button, "clicked", G_CALLBACK (display_on_lock_now), page);
  luxe_preferences_group_add (GTK_BOX (page->lock_group), button);

  gtk_widget_set_visible (page->lock_group, FALSE);
  luxe_preferences_page_add (GTK_SCROLLED_WINDOW (page->page),
                            GTK_BOX (page->lock_group));

  page->settings = lock_settings_open ();
  if (page->settings != NULL)
    {
      guint minutes = g_settings_get_uint (page->settings, "lock-minutes");
      int selected = 5;

      for (gsize i = 0; i < G_N_ELEMENTS (idle_minutes); i++)
        {
          if ((int) idle_minutes[i] == (int) minutes)
            {
              selected = i;
              break;
            }
        }

      page->updating = TRUE;
      g_settings_bind (page->settings, "lock-enabled", page->lock_switch,
                       "active", G_SETTINGS_BIND_DEFAULT);
      display_idle_lock_status_update (page);
      g_signal_connect (page->settings, "changed::idle-lock-failed",
                        G_CALLBACK (display_idle_lock_status_changed), page);
      gtk_drop_down_set_selected (GTK_DROP_DOWN (page->minutes_row), (guint) selected);
      gtk_widget_set_visible (page->lock_group, TRUE);
      page->updating = FALSE;

      g_signal_connect (page->minutes_row, "notify::selected",
                        G_CALLBACK (display_on_minutes_changed), page);
    }
}
