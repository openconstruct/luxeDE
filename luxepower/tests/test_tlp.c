#include "tlp.h"
#include "system.h"

#include <glib.h>
#include <glib/gstdio.h>

static const char *sample_stat =
  "--- TLP 1.10.2 --------------------------------------------\n"
  "\n"
  "+++ Configured Settings:\n"
  "defaults.conf L0214: START_CHARGE_THRESH_BAT0=\"0\"\n"
  "/etc/tlp.conf L0021: WIFI_PWR_ON_AC=\"off\"\n"
  "/etc/tlp.conf L0022: WIFI_PWR_ON_BAT=\"\"\n"
  "/etc/tlp.d/my.conf L0003: USB_AUTOSUSPEND=\"0\"\n"
  "defaults.conf L0300: PCIE_ASPM_ON_AC=\"performance\"\n";

static GHashTable *defaults;

static void
defaults_setup (void)
{
  defaults = tlp_extract_defaults ("/nonexistent/defaults.conf");
  g_hash_table_insert (defaults, g_strdup ("WIFI_PWR_ON_AC"),
                       tlp_setting_new ("WIFI_PWR_ON_AC", "on", TRUE, TRUE));
  g_hash_table_insert (defaults, g_strdup ("WIFI_PWR_ON_BAT"),
                       tlp_setting_new ("WIFI_PWR_ON_BAT", "on", TRUE, TRUE));
  g_hash_table_insert (defaults, g_strdup ("USB_AUTOSUSPEND"),
                       tlp_setting_new ("USB_AUTOSUSPEND", "1", TRUE, FALSE));
}

static void
test_extract_settings (void)
{
  GHashTable *settings = tlp_extract_settings (sample_stat);
  TlpSetting *setting;

  g_assert_nonnull (tlp_effective_value (settings, defaults, "WIFI_PWR_ON_AC"));
  g_assert_cmpstr (tlp_effective_value (settings, defaults, "WIFI_PWR_ON_AC"), ==, "off");
  setting = g_hash_table_lookup (settings, "WIFI_PWR_ON_BAT");
  g_assert_nonnull (setting);
  g_assert_false (setting->enabled);
  setting = g_hash_table_lookup (settings, "USB_AUTOSUSPEND");
  g_assert_nonnull (setting);
  g_assert_cmpstr (setting->value, ==, "0");
  g_assert_null (g_hash_table_lookup (settings, "START_CHARGE_THRESH_BAT0"));
  g_assert_null (g_hash_table_lookup (settings, "PCIE_ASPM_ON_AC"));

}

static void
test_build_changes (void)
{
  GHashTable *settings = tlp_extract_settings (sample_stat);
  GHashTable *desired;
  GHashTable *changes;
  TlpDesired wanted;

  desired = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) tlp_desired_free);

  wanted.enabled = FALSE;
  wanted.value = g_strdup ("on");
  g_hash_table_insert (desired, g_strdup ("WIFI_PWR_ON_BAT"), tlp_desired_copy (&wanted));
  changes = tlp_build_changes (settings, defaults, desired);
  g_assert_cmpuint (g_hash_table_size (changes), ==, 0);


  wanted.enabled = TRUE;
  wanted.value = g_strdup ("on");
  g_hash_table_replace (desired, g_strdup ("WIFI_PWR_ON_BAT"), tlp_desired_copy (&wanted));
  changes = tlp_build_changes (settings, defaults, desired);
  g_assert_cmpuint (g_hash_table_size (changes), ==, 1);


  g_hash_table_unref (desired);

}

static void
test_render (void)
{
  GHashTable *settings = tlp_extract_settings (sample_stat);
  GHashTable *changes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                               (GDestroyNotify) tlp_desired_free);
  TlpDesired wanted = { TRUE, NULL };
  g_autofree char *original = NULL;
  g_autofree char *rendered = NULL;

  original = g_strdup ("#WIFI_PWR_ON_AC=\"on\"\n#USB_AUTOSUSPEND=1\n#KEEP_ME=1\n\n");

  wanted.value = g_strdup ("off");
  g_hash_table_insert (changes, g_strdup ("WIFI_PWR_ON_AC"), tlp_desired_copy (&wanted));
  rendered = tlp_render_config (original, changes, defaults);
  g_assert_nonnull (strstr (rendered, "WIFI_PWR_ON_AC=\"off\""));
  g_assert_null (strstr (rendered, "#WIFI_PWR_ON_AC"));
  g_assert_nonnull (strstr (rendered, "#KEEP_ME=1"));

  g_hash_table_unref (changes);
  changes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) tlp_desired_free);
  wanted.enabled = FALSE;
  wanted.value = g_strdup ("on");
  g_hash_table_insert (changes, g_strdup ("WIFI_PWR_ON_AC"), tlp_desired_copy (&wanted));

  rendered = tlp_render_config (original, changes, defaults);
  g_assert_nonnull (strstr (rendered, "#WIFI_PWR_ON_AC=\"on\""));

  g_hash_table_unref (changes);
  changes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) tlp_desired_free);
  wanted.enabled = TRUE;
  wanted.value = g_strdup ("x");
  g_hash_table_insert (changes, g_strdup ("BRAND_NEW_KEY"), tlp_desired_copy (&wanted));

  rendered = tlp_render_config (original, changes, defaults);
  g_assert_cmpstr (strrchr (rendered, '\n') + 1, ==, "");
  g_assert_nonnull (strstr (rendered, "BRAND_NEW_KEY=x"));

  g_hash_table_unref (changes);
  tlp_free_settings (settings);
}

static void
test_logind_keys (void)
{
  GHashTable *keys = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  const char *base = "[Login]\nHandleLidSwitch=suspend\n#HandleLidSwitchDocked=suspend\n";
  const char *dropin = "[Login]\nHandleLidSwitch=ignore\nHandleLidSwitchDocked=lock\n";
  const char *other = "[Login]\nA=1\n[Other]\nHandleLidSwitch=suspend\n";

  system_parse_logind_text (base, keys);
  g_assert_cmpstr (g_hash_table_lookup (keys, "HandleLidSwitch"), ==, "suspend");
  g_assert_null (g_hash_table_lookup (keys, "HandleLidSwitchDocked"));

  system_parse_logind_text (dropin, keys);
  g_assert_cmpstr (g_hash_table_lookup (keys, "HandleLidSwitch"), ==, "ignore");
  g_assert_cmpstr (g_hash_table_lookup (keys, "HandleLidSwitchDocked"), ==, "lock");

  system_parse_logind_text (other, keys);
  g_assert_cmpstr (g_hash_table_lookup (keys, "HandleLidSwitch"), ==, "ignore");
  g_assert_null (g_hash_table_lookup (keys, "HandleLidSwitchExternalPower"));

  g_hash_table_unref (keys);
}

static void
test_config_line_validation (void)
{
  g_assert_true (tlp_config_line_is_valid ("CPU_BOOST_ON_AC=1"));
  g_assert_true (tlp_config_line_is_valid ("#X=1"));
  g_assert_false (tlp_config_line_is_valid ("rm -rf /"));
  g_assert_false (tlp_config_line_is_valid ("OtherKey=ignore"));
  g_assert_false (tlp_config_line_is_valid ("lowercase=1"));
}

static void
test_sleep_targets (void)
{
  g_assert_cmpstr (system_lid_action_value (LID_SUSPEND), ==, "suspend");
  g_assert_cmpstr (system_lid_action_value (LID_IGNORE), ==, "ignore");
  g_assert_cmpint (system_lid_action_index ("lock"), ==, 3);
  g_assert_cmpint (system_lid_action_index ("nonsense"), ==, 5);
}

static void test_render_stability(void)
{
  const char *original = "# user note\n  WIFI_PWR_ON_AC=on\nWIFI_PWR_ON_AC=off\nKEEP=1\n";
  g_autoptr(GHashTable) changes = g_hash_table_new_full(g_str_hash, g_str_equal,
    g_free, (GDestroyNotify)tlp_desired_free);
  g_autofree char *unchanged = tlp_render_config(original, changes, defaults);
  g_assert_cmpstr(unchanged, ==, original);
  TlpDesired desired = {TRUE, "on"};
  g_hash_table_insert(changes, g_strdup("WIFI_PWR_ON_AC"), tlp_desired_copy(&desired));
  g_autofree char *rendered = tlp_render_config(original, changes, defaults);
  g_assert_nonnull(strstr(rendered, "# user note\n"));
  g_assert_nonnull(strstr(rendered, "KEEP=1\n"));
  const char *assignment = strstr(rendered, "WIFI_PWR_ON_AC=");
  g_assert_nonnull(assignment);
  g_assert_null(strstr(assignment + 1, "WIFI_PWR_ON_AC="));
  g_autofree char *again = tlp_render_config(rendered, changes, defaults);
  g_assert_cmpstr(again, ==, rendered);
}

static gpointer cancel_later (gpointer data)
{
  g_usleep (100 * 1000);
  g_cancellable_cancel (data);
  return NULL;
}

static void test_capture(void)
{
  HelperResult *result = system_spawn_capture((const char *[]) {"/bin/printf", "abc", NULL}, NULL);
  g_assert_cmpint(result->code, ==, 0);
  g_assert_cmpstr(result->out, ==, "abc");
  helper_result_free(result);
  result = system_spawn_capture((const char *[]) {"/bin/sh", "-c", "kill -TERM $$", NULL}, NULL);
  g_assert_cmpint(result->code, ==, -1);
  helper_result_free(result);
  result = system_spawn_capture((const char *[]) {"/usr/bin/head", "-c", "1048577", "/dev/zero", NULL}, NULL);
  g_assert_cmpint(result->code, ==, -1);
  g_assert_nonnull(strstr(result->err, "1 MiB per-stream limit"));
  helper_result_free(result);
  GCancellable *cancellable = g_cancellable_new();
  GThread *canceller = g_thread_new ("cancel-probe", cancel_later, cancellable);
  result = system_spawn_capture((const char *const[]) {"/bin/sh", "-c", "sleep 30 & wait", NULL}, cancellable);
  g_thread_join (canceller);
  g_assert_cmpint(result->code, ==, -1);
  g_assert_nonnull(strstr(result->err, "cancelled"));
  helper_result_free(result);
  g_object_unref(cancellable);
  GError *error = NULL;
  system_set_brightness("../invalid", 1, &error);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error(&error);
  system_set_brightness("valid", -1, &error);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error(&error);
}

static void
test_config_read_limit (void)
{
  g_autofree char *dir = g_dir_make_tmp ("luxepower-config-XXXXXX", NULL);
  g_autofree char *path = g_build_filename (dir, "tlp.conf", NULL);
  g_autofree char *contents = g_malloc (TLP_MAX_CONFIG_BYTES + 1);
  g_autofree char *result = NULL;
  g_autoptr(GError) error = NULL;

  memset (contents, 'x', TLP_MAX_CONFIG_BYTES + 1);
  g_assert_true (g_file_set_contents (path, contents, TLP_MAX_CONFIG_BYTES, &error));
  result = tlp_read_config_file (path, &error);
  g_assert_no_error (error);
  g_assert_cmpuint (strlen (result), ==, TLP_MAX_CONFIG_BYTES);
  g_clear_pointer (&result, g_free);
  g_assert_true (g_file_set_contents (path, contents, TLP_MAX_CONFIG_BYTES + 1, &error));
  result = tlp_read_config_file (path, &error);
  g_assert_null (result);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error (&error);
  g_assert_true (g_file_set_contents (path, "A=1\0B=2\n", 8, &error));
  result = tlp_read_config_file (path, &error);
  g_assert_null (result);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error (&error);
  g_assert_cmpint (g_remove (path), ==, 0);
  result = tlp_read_config_file (path, &error);
  g_assert_null (result);
  g_assert_nonnull (error);
  g_assert_cmpint (g_rmdir (dir), ==, 0);
}

static void
write_battery_attribute (const char *directory, const char *name, const char *value)
{
  g_autofree char *path = g_build_filename (directory, name, NULL);
  g_assert_true (g_file_set_contents (path, value, -1, NULL));
}

static void
test_battery_summary (void)
{
  g_autofree char *root = g_dir_make_tmp ("luxepower-batteries-XXXXXX", NULL);
  g_autofree char *a = g_build_filename (root, "BAT0", NULL);
  g_autofree char *b = g_build_filename (root, "BAT1", NULL);
  g_assert_cmpint (g_mkdir (a, 0700), ==, 0);
  g_assert_cmpint (g_mkdir (b, 0700), ==, 0);
  write_battery_attribute (a, "type", "Battery\n");
  write_battery_attribute (a, "capacity", "10\n");
  write_battery_attribute (a, "energy_now", "10\n");
  write_battery_attribute (a, "energy_full", "100\n");
  write_battery_attribute (b, "type", "Battery\n");
  write_battery_attribute (b, "capacity", "90\n");
  write_battery_attribute (b, "energy_now", "900\n");
  write_battery_attribute (b, "energy_full", "1000\n");
  g_assert_cmpint (system_battery_percent_at (root), ==, 83);
  g_autofree char *missing = g_build_filename (b, "energy_full", NULL);
  g_assert_cmpint (g_remove (missing), ==, 0);
  g_assert_cmpint (system_battery_percent_at (root), ==, 50);
  write_battery_attribute (a, "capacity", "invalid\n");
  g_assert_cmpint (system_battery_percent_at (root), ==, 90);
  const char *names[] = { "type", "capacity", "energy_now", "energy_full" };
  for (guint i = 0; i < G_N_ELEMENTS (names); i++) {
    g_autofree char *p = g_build_filename (a, names[i], NULL);
    g_assert_cmpint (g_remove (p), ==, 0);
    g_autofree char *q = g_build_filename (b, names[i], NULL);
    if (g_file_test (q, G_FILE_TEST_EXISTS)) g_assert_cmpint (g_remove (q), ==, 0);
  }
  g_assert_cmpint (g_rmdir (a), ==, 0);
  g_assert_cmpint (g_rmdir (b), ==, 0);
  g_assert_cmpint (system_battery_percent_at (root), ==, -1);
  g_assert_cmpint (g_rmdir (root), ==, 0);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  defaults_setup ();

  g_test_add_func ("/tlp/extract-settings", test_extract_settings);
  g_test_add_func ("/tlp/build-changes", test_build_changes);
  g_test_add_func ("/tlp/render", test_render);
  g_test_add_func ("/system/logind-keys", test_logind_keys);
  g_test_add_func ("/tlp/config-line-validation", test_config_line_validation);
  g_test_add_func ("/system/sleep-targets", test_sleep_targets);
  g_test_add_func("/tlp/render-stability", test_render_stability);
  g_test_add_func("/system/capture", test_capture);
  g_test_add_func ("/tlp/config-read-limit", test_config_read_limit);
  g_test_add_func ("/system/battery-summary", test_battery_summary);

  return g_test_run ();
}
