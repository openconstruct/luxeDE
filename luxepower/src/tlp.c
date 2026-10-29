/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

#include "tlp.h"
#include "system.h"

#include <string.h>

#define POWER_SUPPLY_DIR "/sys/class/power_supply"
#include <sys/stat.h>

#define TLP_PROPERTY_PATTERN "^\\s*(\\S+\\.conf)\\s+L\\d+:\\s+([A-Z0-9_]+)=(.*)$"
#define TLP_KEY_LINE_PATTERN "^\\s*#?\\s*([A-Z][A-Z0-9_]*)\\s*="
#define TLP_PROFILE_TOKEN_PATTERN "^[a-z][a-z-]*$"

static const char *tlp_cpu_policy_choices[] = {
  "performance", "balance_performance", "default", "balance_power", "power",
  NULL
};

static const char *tlp_pcie_aspm_choices[] = {
  "default", "performance", "powersave", "powersupersave",
  NULL
};

static const char *tlp_runtime_pm_choices[] = {
  "on", "auto",
  NULL
};

static const TlpRowSpec processor_rows[] = {
  { .title = "Power profile", .kind = TLP_ROW_COMBO, .keys = { "PLATFORM_PROFILE_ON_AC", "PLATFORM_PROFILE_ON_BAT" }, .n_keys = 2, .dynamic_choices = TRUE },
  { .title = "CPU performance policy", .kind = TLP_ROW_COMBO, .keys = { "CPU_ENERGY_PERF_POLICY_ON_AC", "CPU_ENERGY_PERF_POLICY_ON_BAT" }, .n_keys = 2, .choices = tlp_cpu_policy_choices, .n_choices = 5 },
  { .title = "CPU boost", .kind = TLP_ROW_SWITCH, .keys = { "CPU_BOOST_ON_AC", "CPU_BOOST_ON_BAT" }, .n_keys = 2, .on = "1", .off = "0" },
};

static const TlpRowSpec drives_rows[] = {
  { .title = "Wi-Fi power saving", .kind = TLP_ROW_SWITCH, .keys = { "WIFI_PWR_ON_AC", "WIFI_PWR_ON_BAT" }, .n_keys = 2, .on = "on", .off = "off" },
  { .title = "USB autosuspend", .kind = TLP_ROW_SWITCH, .keys = { "USB_AUTOSUSPEND" }, .n_keys = 1, .on = "1", .off = "0" },
  { .title = "PCIe ASPM", .kind = TLP_ROW_COMBO, .keys = { "PCIE_ASPM_ON_AC", "PCIE_ASPM_ON_BAT" }, .n_keys = 2, .choices = tlp_pcie_aspm_choices, .n_choices = 4 },
  { .title = "Runtime power management", .kind = TLP_ROW_COMBO, .keys = { "RUNTIME_PM_ON_AC", "RUNTIME_PM_ON_BAT" }, .n_keys = 2, .choices = tlp_runtime_pm_choices, .n_choices = 2 },
};

const TlpGroupSpec tlp_curated_groups[] = {
  { "Processor", processor_rows, 3 },
  { "Drives and devices", drives_rows, 4 },
};

const int tlp_n_curated_groups = 2;

const TlpRowSpec tlp_battery_rows[] = {
  { .title = "Start charging below", .kind = TLP_ROW_SPIN, .keys = { "START_CHARGE_THRESH_BAT0" }, .n_keys = 1 },
  { .title = "Stop charging at", .kind = TLP_ROW_SPIN, .keys = { "STOP_CHARGE_THRESH_BAT0" }, .n_keys = 1 },
};

const int tlp_n_battery_rows = 2;

const char *
tlp_row_subtitle (const char *key)
{
  static const struct { const char *key; const char *text; } subtitles[] = {
    { "PLATFORM_PROFILE_ON_AC", "Platform power profile requested from the firmware (plugged in)" },
    { "PLATFORM_PROFILE_ON_BAT", "Platform power profile requested from the firmware (on battery)" },
    { "CPU_ENERGY_PERF_POLICY_ON_AC", "Governor energy-performance preference (plugged in)" },
    { "CPU_ENERGY_PERF_POLICY_ON_BAT", "Governor energy-performance preference (on battery)" },
    { "CPU_BOOST_ON_AC", "Allow turbo frequencies when plugged in" },
    { "CPU_BOOST_ON_BAT", "Allow turbo frequencies on battery" },
    { "WIFI_PWR_ON_AC", "Wi-Fi power saving when plugged in" },
    { "WIFI_PWR_ON_BAT", "Wi-Fi power saving on battery" },
    { "USB_AUTOSUSPEND", "Suspend USB devices when idle" },
    { "PCIE_ASPM_ON_AC", "PCIe link power management (plugged in)" },
    { "PCIE_ASPM_ON_BAT", "PCIe link power management (on battery)" },
    { "RUNTIME_PM_ON_AC", "Runtime power management for devices (plugged in)" },
    { "RUNTIME_PM_ON_BAT", "Runtime power management for devices (on battery)" },
    { "START_CHARGE_THRESH_BAT0", "Begin charging below this battery level; 0 disables" },
    { "STOP_CHARGE_THRESH_BAT0", "Pause charging above this battery level; 100 disables" },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (subtitles); i++)
    {
      if (g_strcmp0 (subtitles[i].key, key) == 0)
        return subtitles[i].text;
    }
  if (g_str_has_prefix (key, "START_CHARGE_THRESH_BAT"))
    return "Begin charging below this battery level; 0 disables";
  if (g_str_has_prefix (key, "STOP_CHARGE_THRESH_BAT"))
    return "Pause charging above this battery level; 100 disables";
  return "";
}

gboolean
tlp_installed (void)
{
  return g_file_test (TLP_STAT_PATH, G_FILE_TEST_EXISTS) &&
         g_file_test (TLP_PATH, G_FILE_TEST_EXISTS);
}

char *
tlp_stat_output (const char *const *args, GError **error)
{
  HelperResult *result = system_spawn_capture (args, NULL);

  if (result->code != 0)
    {
      g_autofree char *message = NULL;

      if (result->err[0] != '\0' && result->out[0] != '\0')
        message = g_strdup_printf ("%s\n%s", result->err, result->out);
      else if (result->err[0] != '\0')
        message = g_strdup (result->err);
      else if (result->out[0] != '\0')
        message = g_strdup (result->out);
      else
        message = g_strdup_printf ("command failed: %s", args[0]);
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, message);
      helper_result_free (result);
      return NULL;
    }

  char *output = g_strdup (result->out);
  helper_result_free (result);
  return output;
}

TlpSetting *
tlp_setting_new (const char *name, const char *value, gboolean enabled, gboolean quoted)
{
  TlpSetting *setting = g_new0 (TlpSetting, 1);

  setting->name = g_strdup (name);
  setting->value = g_strdup (value);
  setting->enabled = enabled;
  setting->quoted = quoted;
  return setting;
}

static TlpSetting *
setting_new (const char *name, const char *value, gboolean enabled, gboolean quoted)
{
  return tlp_setting_new (name, value, enabled, quoted);
}

static void
setting_free (gpointer data)
{
  TlpSetting *setting = data;

  if (setting == NULL)
    return;
  g_free (setting->name);
  g_free (setting->value);
  g_free (setting);
}

static gboolean
split_key_value (const char *text, char **name_out, char **value_out, gboolean *quoted_out)
{
  const char *equals;
  g_autofree char *name = NULL;
  g_autofree char *raw = NULL;
  gsize length;
  gboolean quoted;

  equals = strchr (text, '=');
  if (equals == NULL || equals == text)
    return FALSE;

  name = g_strndup (text, equals - text);
  name = g_strstrip (g_steal_pointer (&name));
  if (name[0] == '\0')
    return FALSE;

  raw = g_strdup (equals + 1);
  raw = g_strstrip (g_steal_pointer (&raw));
  length = strlen (raw);
  quoted = length >= 2 && raw[0] == '"' && raw[length - 1] == '"';

  *name_out = g_steal_pointer (&name);
  *quoted_out = quoted;
  *value_out = quoted ? g_strndup (raw + 1, length - 2) : g_steal_pointer (&raw);
  return TRUE;
}

GHashTable *
tlp_extract_settings (const char *output)
{
  GHashTable *map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, setting_free);
  g_autoptr(GRegex) pattern = NULL;
  g_auto(GStrv) lines = NULL;
  gsize i;

  if (output == NULL)
    return map;

  pattern = g_regex_new (TLP_PROPERTY_PATTERN, G_REGEX_OPTIMIZE, 0, NULL);
  lines = g_strsplit (output, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      g_autoptr(GMatchInfo) match = NULL;
      g_autofree char *config_file = NULL;
      g_autofree char *name = NULL;
      g_autofree char *value = NULL;
      gboolean quoted;

      if (!g_regex_match (pattern, lines[i], 0, &match))
        continue;

      config_file = g_match_info_fetch (match, 1);
      if (g_strcmp0(config_file, "defaults.conf") == 0 || g_strcmp0(config_file, TLP_DEFAULTS_PATH) == 0)
        continue;

      name = g_match_info_fetch (match, 2);
      value = g_match_info_fetch (match, 3);
      quoted = strlen (value) >= 2 && g_str_has_prefix (value, "\"") && g_str_has_suffix (value, "\"");
      if (quoted)
        {
          char *inner = g_strndup (value + 1, strlen (value) - 2);
          g_free (value);
          value = inner;
        }

      {
        TlpSetting *setting = setting_new (name, value, value[0] != '\0', quoted);
        g_hash_table_replace (map, g_steal_pointer (&name), setting);
      }
    }
  return map;
}

GHashTable *
tlp_extract_defaults (const char *path)
{
  GHashTable *map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, setting_free);
  g_autofree char *text = NULL;
  g_auto(GStrv) lines = NULL;
  gsize i;

  if (!g_file_get_contents (path, &text, NULL, NULL))
    return map;

  lines = g_strsplit (text, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      g_autofree char *name = NULL;
      g_autofree char *value = NULL;
      const char *cursor = lines[i];
      gboolean quoted;

      while (*cursor == ' ' || *cursor == '\t')
        cursor++;
      if (*cursor == '\0' || *cursor == '#')
        continue;

      if (!split_key_value (cursor, &name, &value, &quoted))
        continue;

      {
        TlpSetting *setting = setting_new (name, value, TRUE, quoted);
        g_hash_table_replace (map, g_steal_pointer (&name), setting);
      }
    }
  return map;
}

void
tlp_free_settings (GHashTable *settings)
{
  if (settings != NULL)
    g_hash_table_unref (settings);
}

TlpDesired *
tlp_desired_copy (const TlpDesired *desired)
{
  TlpDesired *copy = g_new0 (TlpDesired, 1);

  copy->enabled = desired->enabled;
  copy->value = g_strdup (desired->value);
  return copy;
}

void
tlp_desired_free (TlpDesired *desired)
{
  if (desired == NULL)
    return;
  g_free (desired->value);
  g_free (desired);
}

char *
tlp_effective_value (GHashTable *settings, GHashTable *defaults, const char *name)
{
  TlpSetting *setting = g_hash_table_lookup (settings, name);
  TlpSetting *default_setting = g_hash_table_lookup (defaults, name);

  if (setting != NULL && setting->enabled)
    return g_strdup (setting->value);
  if (default_setting != NULL)
    return g_strdup (default_setting->value);
  return g_strdup ("");
}

void
tlp_original_state (GHashTable *settings, GHashTable *defaults, const char *name,
                    gboolean *enabled, char **value)
{
  TlpSetting *setting = g_hash_table_lookup (settings, name);

  if (setting != NULL && setting->enabled)
    {
      *enabled = TRUE;
      *value = g_strdup (setting->value);
      return;
    }
  *enabled = FALSE;
  *value = tlp_effective_value (settings, defaults, name);
}

GHashTable *
tlp_build_changes (GHashTable *settings, GHashTable *defaults, GHashTable *desired)
{
  GHashTable *changes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                               (GDestroyNotify) tlp_desired_free);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, desired);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *name = key;
      const TlpDesired *wanted = value;
      gboolean original_enabled;
      g_autofree char *original_value = NULL;

      tlp_original_state (settings, defaults, name, &original_enabled, &original_value);
      if (wanted->enabled == original_enabled &&
          g_strcmp0 (wanted->value, original_value) == 0)
        continue;
      g_hash_table_insert (changes, g_strdup (name), tlp_desired_copy (wanted));
    }
  return changes;
}

static void
append_setting_line (GString *out, const char *name, const TlpDesired *wanted,
                     gboolean quoted)
{
  if (!wanted->enabled)
    {
      if (quoted)
        g_string_append_printf (out, "#%s=\"%s\"\n", name, wanted->value);
      else
        g_string_append_printf (out, "#%s=%s\n", name, wanted->value);
      return;
    }
  if (quoted)
    g_string_append_printf (out, "%s=\"%s\"\n", name, wanted->value);
  else
    g_string_append_printf (out, "%s=%s\n", name, wanted->value);
}

char *
tlp_render_config (const char *original, GHashTable *changes, GHashTable *defaults)
{
  if (g_hash_table_size(changes) == 0) return g_strdup(original);
  GString *out = g_string_new (NULL);
  g_autoptr(GRegex) line_pattern = NULL;
  g_auto(GStrv) lines = NULL;
  GHashTable *pending;
  GHashTableIter iter;
  gpointer key, value;
  gsize i;

  line_pattern = g_regex_new (TLP_KEY_LINE_PATTERN, G_REGEX_OPTIMIZE, 0, NULL);
  pending = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) tlp_desired_free);
  g_hash_table_iter_init (&iter, changes);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_hash_table_insert (pending, g_strdup (key), tlp_desired_copy (value));

  lines = g_strsplit (original, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      const char *cursor = lines[i];
      const char *equals;
      g_autofree char *name = NULL;
      gsize name_length;
      if (!lines[i][0] && !lines[i + 1]) break;

      if (!g_regex_match (line_pattern, lines[i], 0, NULL))
        {
          g_string_append (out, lines[i]);
          g_string_append_c (out, '\n');
          continue;
        }

      while (*cursor == ' ' || *cursor == '\t')
        cursor++;
      if (*cursor == '#')
        cursor++;
      while (*cursor == ' ' || *cursor == '\t') cursor++;

      equals = strchr (cursor, '=');
      if (equals == NULL || equals == cursor)
        {
          g_string_append (out, lines[i]);
          g_string_append_c (out, '\n');
          continue;
        }

      name_length = equals - cursor;
      name = g_strndup (cursor, name_length);
      g_strstrip(name);

      if (g_hash_table_contains (pending, name))
        {
          TlpDesired *wanted = g_hash_table_lookup (pending, name);
          TlpSetting *default_setting = g_hash_table_lookup (defaults, name);
          gboolean quoted = default_setting != NULL && default_setting->quoted;

          append_setting_line (out, name, wanted, quoted);
          g_hash_table_remove (pending, name);
          continue;
        }
      if (g_hash_table_contains(changes, name)) continue; /* stale duplicate */

      g_string_append (out, lines[i]);
      g_string_append_c (out, '\n');
    }

  GList *names = g_list_sort(g_hash_table_get_keys(pending), (GCompareFunc)g_strcmp0);
  for (GList *item = names; item; item = item->next)
    {
      TlpDesired *wanted = g_hash_table_lookup(pending, item->data);
      TlpSetting *default_setting = g_hash_table_lookup (defaults, item->data);
      gboolean quoted = default_setting != NULL && default_setting->quoted;

      append_setting_line (out, item->data, wanted, quoted);
    }
  g_list_free(names);

  g_hash_table_unref (pending);
  return g_string_free (out, FALSE);
}

char *
tlp_read_config_file (const char *path, GError **error)
{
  g_autoptr(GFile) file = g_file_new_for_path (path);
  g_autoptr(GFileInputStream) stream = g_file_read (file, NULL, error);
  g_autoptr(GString) text = NULL;
  char buffer[8192];

  if (stream == NULL)
    return NULL;
  text = g_string_new (NULL);
  for (;;)
    {
      gsize remaining = TLP_MAX_CONFIG_BYTES + 1 - text->len;
      gssize count = g_input_stream_read (G_INPUT_STREAM (stream), buffer,
                                          MIN (sizeof buffer, remaining), NULL, error);
      if (count < 0)
        return NULL;
      if (count == 0)
        return g_string_free (g_steal_pointer (&text), FALSE);
      if (memchr (buffer, '\0', count) != NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                       "%s contains a NUL byte", path);
          return NULL;
        }
      g_string_append_len (text, buffer, count);
      if (text->len > TLP_MAX_CONFIG_BYTES)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "%s is unexpectedly large", path);
          return NULL;
        }
    }
}

char *
tlp_read_user_config (GError **error)
{
  return tlp_read_config_file (TLP_CONFIG_PATH, error);
}

static char *
profile_choices_from_line (const char *line)
{
  g_auto(GStrv) tokens = NULL;
  GString *out = g_string_new (NULL);
  g_autoptr(GRegex) token_pattern = NULL;
  gsize i;
  int count = 0;

  token_pattern = g_regex_new (TLP_PROFILE_TOKEN_PATTERN, G_REGEX_OPTIMIZE, 0, NULL);
  tokens = g_strsplit (line, " ", -1);
  for (i = 0; tokens[i] != NULL; i++)
    {
      if (!g_regex_match (token_pattern, tokens[i], 0, NULL))
        continue;
      g_string_append_printf (out, "%s%s", out->len > 0 ? "," : "", tokens[i]);
      count++;
    }
  if (count == 0)
    {
      g_string_free (out, TRUE);
      return NULL;
    }
  return g_string_free (out, FALSE);
}

char *
tlp_platform_profile_choices (void)
{
  g_autofree char *text = NULL;
  char *newline;
  char *marker;

  if (g_file_get_contents ("/sys/firmware/acpi/platform_profile_choices", &text, NULL, NULL))
    {
      newline = strchr (text, '\n');
      if (newline != NULL)
        *newline = '\0';
      return profile_choices_from_line (text);
    }

  g_clear_pointer (&text, g_free);
  text = tlp_stat_output ((const char *const[]) { TLP_STAT_PATH, "-p", NULL }, NULL);
  if (text == NULL)
    return NULL;

  marker = strstr (text, "/sys/firmware/acpi/platform_profile_choices");
  if (marker == NULL)
    return NULL;

  marker += strlen ("/sys/firmware/acpi/platform_profile_choices");
  marker = strchr (marker, '=');
  if (marker == NULL)
    return NULL;
  marker++;

  newline = strchr (marker, '\n');
  if (newline != NULL)
    *newline = '\0';

  return profile_choices_from_line (marker);
}

gboolean
tlp_config_line_is_valid (const char *line)
{
  gsize i = 0;
  gsize key_len;
  const char *value;
  gsize value_len;

  if (line == NULL || line[0] == '\0' || strlen (line) > 1024)
    return FALSE;
  if (line[0] == '#')
    i++;
  if (!(line[i] >= 'A' && line[i] <= 'Z'))
    return FALSE;
  i++;
  while (line[i] != '\0' && line[i] != '=')
    {
      if (!((line[i] >= 'A' && line[i] <= 'Z') ||
            (line[i] >= '0' && line[i] <= '9') || line[i] == '_'))
        return FALSE;
      i++;
    }
  key_len = (line[0] == '#') ? i - 1 : i;
  if (key_len == 0 || key_len > 64)
    return FALSE;
  if (line[i] != '=')
    return FALSE;
  /* /etc/tlp.conf is shell-sourced: restrict the value grammar so a
   * staged config cannot smuggle shell metacharacters through the
   * privileged helper. */
  value = line + i + 1;
  value_len = strlen (value);
  if (value_len > 512)
    return FALSE;
  if (value_len == 0)
    return TRUE;
  if (value[0] == '"')
    {
      gsize k;

      if (value_len < 2 || value[value_len - 1] != '"')
        return FALSE;
      for (k = 1; k + 1 < value_len; k++)
        {
          char c = value[k];

          if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
                c == '/' || c == ':' || c == ',' || c == '+' || c == '@' ||
                c == '%' || c == '=' || c == ' '))
            return FALSE;
        }
      return TRUE;
    }
  for (i = 0; value[i] != '\0'; i++)
    {
      char c = value[i];

      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
            c == '/' || c == ':' || c == ',' || c == '+' || c == '@' ||
            c == '%' || c == '='))
        return FALSE;
    }
  return TRUE;
}

gboolean
tlp_battery_thresholds_supported (void)
{
  g_autoptr(GDir) directory = g_dir_open (POWER_SUPPLY_DIR, 0, NULL);
  const char *name;

  if (directory == NULL)
    return FALSE;
  while ((name = g_dir_read_name (directory)) != NULL)
    {
      g_autofree char *start = g_build_filename (POWER_SUPPLY_DIR, name,
                                                  "charge_start_threshold", NULL);
      g_autofree char *stop = g_build_filename (POWER_SUPPLY_DIR, name,
                                                 "charge_stop_threshold", NULL);
      if (g_file_test (start, G_FILE_TEST_EXISTS) &&
          g_file_test (stop, G_FILE_TEST_EXISTS))
        return TRUE;
    }
  return FALSE;
}

static gint
compare_names (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (*(char * const *) a, *(char * const *) b);
}

static GPtrArray *
supported_battery_names (void)
{
  GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GDir) directory = g_dir_open (POWER_SUPPLY_DIR, 0, NULL);
  const char *name;

  if (directory == NULL)
    return names;
  while ((name = g_dir_read_name (directory)) != NULL)
    {
      gboolean safe_name = *name != '\0';
      for (const char *p = name; *p; p++)
        if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-')
          safe_name = FALSE;
      if (!safe_name)
        continue;

      g_autofree char *device = g_build_filename (POWER_SUPPLY_DIR, name, NULL);
      g_autofree char *type_path = g_build_filename (device, "type", NULL);
      g_autofree char *start_path = g_build_filename (device, "charge_start_threshold", NULL);
      g_autofree char *stop_path = g_build_filename (device, "charge_stop_threshold", NULL);
      g_autofree char *type = NULL;
      if (!g_file_get_contents (type_path, &type, NULL, NULL) ||
          !g_file_test (start_path, G_FILE_TEST_EXISTS) ||
          !g_file_test (stop_path, G_FILE_TEST_EXISTS))
        continue;
      g_strstrip (type);
      if (g_strcmp0 (type, "Battery") == 0)
        g_ptr_array_add (names, g_strdup (name));
    }
  g_ptr_array_sort (names, compare_names);
  return names;
}

static TlpRowSpec *
row_spec_copy (const TlpRowSpec *spec, const char *const *dynamic_choices, int n_dynamic)
{
  TlpRowSpec *copy = g_new0 (TlpRowSpec, 1);
  int i;

  *copy = *spec;
  copy->owned_title = NULL;
  copy->owned_key = NULL;
  copy->owned_choices = NULL;
  if (dynamic_choices != NULL)
    {
      copy->owned_choices = g_new0 (char *, n_dynamic + 1);
      for (i = 0; i < n_dynamic; i++)
        copy->owned_choices[i] = g_strdup (dynamic_choices[i]);
      copy->choices = (const char *const *) copy->owned_choices;
      copy->n_choices = n_dynamic;
    }
  return copy;
}

static gboolean
row_key_supported (const TlpRowSpec *row, const char *key,
                   GHashTable *settings, GHashTable *defaults)
{
  (void) row;

  if (g_hash_table_contains (defaults, key))
    return TRUE;
  return g_hash_table_contains (settings, key);
}

TlpOwnedGroup *
tlp_effective_groups (GHashTable *settings, int *n_groups)
{
  g_autoptr(GHashTable) defaults = tlp_extract_defaults (TLP_DEFAULTS_PATH);
  g_autofree char *profile_choices = NULL;
  g_auto(GStrv) profile_list = NULL;
  int profile_count = 0;
  TlpOwnedGroup *out;
  int out_count = 0;
  int i;

  *n_groups = 0;

  profile_choices = tlp_platform_profile_choices ();
  if (profile_choices != NULL)
    profile_list = g_strsplit (profile_choices, ",", -1);
  if (profile_list != NULL)
    {
      for (i = 0; profile_list[i] != NULL; i++)
        profile_count++;
      if (profile_count == 0)
        g_clear_pointer (&profile_list, g_strfreev);
    }

  out = g_new0 (TlpOwnedGroup, tlp_n_curated_groups + 1);
  for (i = 0; i < tlp_n_curated_groups; i++)
    {
      const TlpGroupSpec *spec = &tlp_curated_groups[i];
      GPtrArray *rows = g_ptr_array_new_with_free_func ((GDestroyNotify) tlp_row_spec_free);
      int j;

      for (j = 0; j < spec->n_rows; j++)
        {
          const TlpRowSpec *row = &spec->rows[j];

          gboolean supported = FALSE;
          int k;

          for (k = 0; k < row->n_keys; k++)
            {
              if (row_key_supported (row, row->keys[k], settings, defaults))
                {
                  supported = TRUE;
                  break;
                }
            }
          if (!supported)
            continue;

          if (row->dynamic_choices)
            {
              if (profile_list == NULL || profile_count == 0)
                continue;
              g_ptr_array_add (rows, row_spec_copy (row, (const char *const *) profile_list, profile_count));
              continue;
            }
          g_ptr_array_add (rows, row_spec_copy (row, NULL, 0));
        }

      if (rows->len == 0)
        {
          g_ptr_array_unref (rows);
          continue;
        }

      out[out_count].title = spec->title;
      out[out_count].rows = (const TlpRowSpec **) rows->pdata;
      out[out_count].n_rows = rows->len;
      g_ptr_array_free (rows, FALSE);
      out_count++;
    }

  if (tlp_battery_thresholds_supported ())
    {
      GPtrArray *battery = g_ptr_array_new_with_free_func ((GDestroyNotify) tlp_row_spec_free);
      g_autoptr(GPtrArray) names = supported_battery_names ();
      for (guint i = 0; i < names->len; i++)
        {
          const char *name = g_ptr_array_index (names, i);
          for (int j = 0; j < tlp_n_battery_rows; j++)
            {
              const TlpRowSpec *source = &tlp_battery_rows[j];
              gsize key_length = strlen (source->keys[0]);
              TlpRowSpec *row;
              if (key_length < 4)
                continue;
              g_autofree char *key = g_strdup_printf ("%.*s%s",
                  (int) (key_length - 4), source->keys[0], name);
              if (!row_key_supported (source, key, settings, defaults))
                continue;
              row = row_spec_copy (source, NULL, 0);
              row->owned_title = g_strdup_printf ("%s (%s)", source->title, name);
              row->title = row->owned_title;
              row->owned_key = g_strdup (key);
              row->keys[0] = row->owned_key;
              g_ptr_array_add (battery, row);
            }
        }

      if (battery->len > 0)
        {
          out[out_count].title = "Battery care";
          out[out_count].rows = (const TlpRowSpec **) battery->pdata;
          out[out_count].n_rows = battery->len;
          g_ptr_array_free (battery, FALSE);
          out_count++;
        }
      else
        g_ptr_array_unref (battery);
    }

  *n_groups = out_count;
  return out;
}

void
tlp_effective_groups_free (TlpOwnedGroup *groups, int n_groups)
{
  int i, j;

  if (groups == NULL)
    return;
  for (i = 0; i < n_groups; i++)
    {
      for (j = 0; j < groups[i].n_rows; j++)
        {
          TlpRowSpec *row = (TlpRowSpec *) groups[i].rows[j];

          tlp_row_spec_free(row);
        }
      g_free ((gpointer) groups[i].rows);
    }
  g_free (groups);
}

void
tlp_row_spec_free (TlpRowSpec *row)
{
  if (row == NULL)
    return;
  g_strfreev (row->owned_choices);
  g_free (row->owned_title);
  g_free (row->owned_key);
  g_free (row);
}
