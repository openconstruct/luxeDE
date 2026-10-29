/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/app.h"
#include <glib/gstdio.h>
#include <string.h>
#include "luxe-lock-request.h"

static GTask *pending_lock;
static guint lock_calls;
void __wrap_luxe_lock_request_async(const char *program, guint timeout_ms, GCancellable *cancel,
                                    GAsyncReadyCallback callback, gpointer data)
{
  g_assert_cmpstr(program, ==, LUXELOCK_PATH);
  g_assert_cmpuint(timeout_ms, ==, 10000);
  g_assert_null(pending_lock);
  lock_calls++;
  pending_lock = g_task_new(NULL, cancel, callback, data);
}

/* Production page code, with every hardware/privileged operation substituted. */
static guint brightness_calls;
static long last_brightness;
static gint fail_brightness;
static gint actual_brightness = 50;
static gint actual_maximum = 100;
static gint no_devices;
static GMutex brightness_lock;
static GCond brightness_cond;
static gboolean block_brightness, brightness_entered, release_brightness;
static void fake_brightness(const char *name, long value, GError **error)
{
  g_assert_cmpstr(name, ==, "fixture");
  gboolean fail = g_atomic_int_get(&fail_brightness);
  g_mutex_lock(&brightness_lock);
  if (block_brightness) {
    brightness_entered = TRUE;
    g_cond_broadcast(&brightness_cond);
    while (!release_brightness) g_cond_wait(&brightness_cond, &brightness_lock);
  }
  g_mutex_unlock(&brightness_lock);
  g_usleep(10000);
  last_brightness = value;
  brightness_calls++;
  if (fail)
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Fixture denial");
}
static GPtrArray *fake_devices(void)
{
  GPtrArray *devices = g_ptr_array_new_with_free_func(g_free);
  if (g_atomic_int_get(&no_devices)) return devices;
  BacklightDevice *device = g_new0(BacklightDevice, 1);
  g_strlcpy(device->name, "fixture", sizeof device->name);
  device->max = g_atomic_int_get(&actual_maximum); device->current = g_atomic_int_get(&actual_brightness);
  g_ptr_array_add(devices, device);
  return devices;
}
#define system_set_brightness fake_brightness
#define system_backlight_devices fake_devices
#include "../src/display-page.c"
#undef system_set_brightness
#undef system_backlight_devices
#include "../src/power-page.c"

static void pump(guint milliseconds)
{
  gint64 end = g_get_monotonic_time() + milliseconds * 1000;
  do {
    while (g_main_context_iteration(NULL, FALSE));
    g_usleep(1000);
  } while (g_get_monotonic_time() < end);
}
static gboolean contains_label(GtkWidget *widget, const char *needle)
{
  if (GTK_IS_LABEL(widget) && strstr(gtk_label_get_text(GTK_LABEL(widget)), needle)) return TRUE;
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
    if (contains_label(child, needle)) return TRUE;
  return FALSE;
}
static int completions, destructions, exit_status;
static void completed(int status, gpointer data)
{
  (void)data; completions++; exit_status = status;
}
static void destroyed(gpointer data) { (void)data; destructions++; }
static void wait_activity(int expected)
{
  for (int i = 0; i < 500 && destructions < expected; i++) pump(10);
  g_assert_cmpint(destructions, ==, expected);
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  luxe_ui_init();
  LuxePowerWindow win = {0};
  win.window = gtk_window_new();
  g_object_ref_sink(win.window);
  win.toast_overlay = gtk_overlay_new();
  luxe_window_set_content(GTK_WINDOW(win.window), win.toast_overlay);
  display_page_init(&win.display, &win);
  gtk_overlay_set_child(GTK_OVERLAY(win.toast_overlay), win.display.page);
  g_assert_false(gtk_widget_get_visible(win.display.lock_error_label));
  g_settings_set_boolean(win.display.settings, "idle-lock-failed", TRUE);
  pump(20);
  g_assert_true(gtk_widget_get_visible(win.display.lock_error_label));
  g_settings_set_boolean(win.display.settings, "idle-lock-failed", FALSE);
  pump(20);
  g_assert_false(gtk_widget_get_visible(win.display.lock_error_label));
  display_page_refresh(&win.display);
  gtk_window_present(GTK_WINDOW(win.window));
  pump(50);
  GtkWidget *box = gtk_widget_get_parent(win.display.scale);
  g_signal_emit_by_name(gtk_widget_get_first_child(box), "clicked");
  g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(win.display.scale)), ==, 45);
  g_signal_emit_by_name(gtk_widget_get_last_child(box), "clicked");
  g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(win.display.scale)), ==, 50);
  pump(300);
  g_assert_cmpuint(brightness_calls, ==, 1);
  display_page_send_brightness(&win.display);
  gtk_range_set_value(GTK_RANGE(win.display.scale), 75);
  display_page_send_brightness(&win.display);
  while (win.display.brightness_busy) pump(10);
  g_assert_cmpint(last_brightness, ==, 75);
  if (win.display.source_id) { g_source_remove(win.display.source_id); win.display.source_id = 0; }
  g_atomic_int_set(&actual_brightness, 37);
  g_atomic_int_set(&fail_brightness, 1);
  guint before_failure = brightness_calls;
  gtk_range_set_value(GTK_RANGE(win.display.scale), 70);
  pump(300);
  while (win.display.brightness_busy) pump(10);
  g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(win.display.scale)), ==, 37);
  g_assert_cmpint(display_page_current_device(&win.display)->current, ==, 37);
  pump(250);
  g_assert_cmpuint(brightness_calls, ==, before_failure + 1);
  GtkWindow *error_dialog = luxe_window_get_visible_dialog(GTK_WINDOW(win.window));
  g_assert_nonnull(error_dialog);
  luxe_dialog_close(error_dialog);
  g_atomic_int_set(&fail_brightness, 0);
  g_atomic_int_set(&fail_brightness, 1);
  g_mutex_lock(&brightness_lock);
  block_brightness = TRUE; brightness_entered = FALSE; release_brightness = FALSE;
  g_mutex_unlock(&brightness_lock);
  gtk_range_set_value(GTK_RANGE(win.display.scale), 55);
  if (win.display.source_id) { g_source_remove(win.display.source_id); win.display.source_id = 0; }
  display_page_send_brightness(&win.display);
  gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
  gboolean entered = FALSE;
  while (!entered && g_get_monotonic_time() < deadline) {
    pump(10);
    g_mutex_lock(&brightness_lock); entered = brightness_entered; g_mutex_unlock(&brightness_lock);
  }
  g_assert_true(entered);
  gtk_range_set_value(GTK_RANGE(win.display.scale), 80);
  display_page_send_brightness(&win.display);
  g_assert_true(win.display.brightness_pending);
  g_atomic_int_set(&fail_brightness, 0);
  g_mutex_lock(&brightness_lock); block_brightness = FALSE; release_brightness = TRUE;
  g_cond_broadcast(&brightness_cond); g_mutex_unlock(&brightness_lock);
  while (win.display.brightness_busy) pump(10);
  g_assert_cmpint(last_brightness, ==, 80);
  g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(win.display.scale)), ==, 80);
  g_assert_cmpint(display_page_current_device(&win.display)->current, ==, 80);
  if (win.display.source_id) { g_source_remove(win.display.source_id); win.display.source_id = 0; }
  error_dialog = luxe_window_get_visible_dialog(GTK_WINDOW(win.window));
  g_assert_nonnull(error_dialog);
  luxe_dialog_close(error_dialog);
  g_atomic_int_set(&actual_maximum, 200);
  g_atomic_int_set(&actual_brightness, 60);
  g_atomic_int_set(&fail_brightness, 1);
  gtk_range_set_value(GTK_RANGE(win.display.scale), 65);
  pump(300);
  while (win.display.brightness_busy) pump(10);
  g_assert_cmpint(display_page_current_device(&win.display)->max, ==, 200);
  g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(win.display.scale)), ==, 30);
  error_dialog = luxe_window_get_visible_dialog(GTK_WINDOW(win.window));
  g_assert_nonnull(error_dialog); luxe_dialog_close(error_dialog);
  g_atomic_int_set(&no_devices, 1);
  gtk_range_set_value(GTK_RANGE(win.display.scale), 45);
  pump(300);
  while (win.display.brightness_busy) pump(10);
  g_assert_cmpint(display_page_current_device(&win.display)->current, ==, 60);
  g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(win.display.scale)), ==, 45);
  error_dialog = luxe_window_get_visible_dialog(GTK_WINDOW(win.window));
  g_assert_nonnull(error_dialog);
  g_assert_true(contains_label(GTK_WIDGET(error_dialog), "current brightness could not be read"));
  luxe_dialog_close(error_dialog);
  g_atomic_int_set(&no_devices, 0);
  g_atomic_int_set(&fail_brightness, 0);

  PowerPage page = {0};
  page.settings = tlp_extract_settings("/etc/tlp.conf L1: TEST_VALUE=custom\n");
  page.defaults = tlp_extract_defaults("/nonexistent");
  const char *choices[] = { "auto", "off" };
  TlpRowSpec spec = { .title = "Test", .kind = TLP_ROW_COMBO,
    .keys = {"TEST_VALUE"}, .n_keys = 1, .choices = choices, .n_choices = 2 };
  GtkWidget *row = power_page_build_row(&page, &spec, "TEST_VALUE");
  g_object_ref_sink(row);
  g_assert_cmpuint(g_list_model_get_n_items(gtk_drop_down_get_model(GTK_DROP_DOWN(row))), ==, 4);
  g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(row)), ==, 3);
  g_object_unref(row);
  tlp_free_settings(page.settings); tlp_free_settings(page.defaults);
  char *hash = NULL;
  g_autofree char *path = stage_config_text("A=1\n", &hash, NULL);
  g_assert_nonnull(path);
  g_autofree char *expected = g_compute_checksum_for_string(G_CHECKSUM_SHA256, "A=1\n", -1);
  g_assert_cmpstr(hash, ==, expected);
  g_free(hash); g_unlink(path);

  GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *terminal_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(group), terminal_box);
  gtk_overlay_set_child(GTK_OVERLAY(win.toast_overlay), group);
  GtkWidget *terminal = activity_present(group, terminal_box);
  activity_run(terminal, (const char *[]) {"/bin/true", NULL}, completed, NULL, destroyed);
  wait_activity(1);
  g_assert_cmpint(completions, ==, 1);
  g_assert_true(g_spawn_check_wait_status(exit_status, NULL));
  terminal = activity_present(group, terminal_box);
  activity_run(terminal, (const char *[]) {"/nonexistent-luxe-command", NULL}, completed, NULL, destroyed);
  wait_activity(2);
  g_assert_cmpint(completions, ==, 2);
  g_assert_cmpint(exit_status, ==, -1);
  terminal = activity_present(group, terminal_box);
  activity_run(terminal, (const char *[]) {"/bin/sleep", "0.1", NULL}, completed, NULL, destroyed);
  request_screen_lock(win.window);
  request_screen_lock(win.window);
  g_assert_cmpuint(lock_calls, ==, 1);
  g_assert_nonnull(pending_lock);
  g_object_set_data(G_OBJECT(win.window), "luxe-power-closed", GINT_TO_POINTER(1));
  gtk_window_destroy(GTK_WINDOW(win.window));
  g_assert_true(g_cancellable_is_cancelled(g_task_get_cancellable(pending_lock)));
  g_task_return_boolean(pending_lock, TRUE);
  g_clear_object(&pending_lock);
  wait_activity(3);
  g_assert_null(g_object_get_data(G_OBJECT(win.window), "luxe-lock-pending"));
  g_assert_cmpint(completions, ==, 2);
  g_object_unref(win.window);
  g_clear_object(&win.display.settings);
  g_ptr_array_unref(win.display.devices);
  return 0;
}
