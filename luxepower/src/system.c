/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

#include "system.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define SPAWN_CAPTURE_LIMIT (1024 * 1024)
#ifndef SPAWN_CAPTURE_TIMEOUT_US
#define SPAWN_CAPTURE_TIMEOUT_US (15 * G_USEC_PER_SEC)
#endif
#define SPAWN_CAPTURE_KILL_GRACE_US (G_USEC_PER_SEC)

#define LOGIND_CONF "/etc/systemd/logind.conf"
#define LOGIND_DROPIN_DIR "/etc/systemd/logind.conf.d"

gboolean
system_power_source (void)
{
  g_autoptr(GDir) dir = NULL;
  const char *name;

  dir = g_dir_open ("/sys/class/power_supply", 0, NULL);
  if (dir == NULL)
    return FALSE;

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *path = NULL;
      g_autofree char *content = NULL;

      path = g_build_filename ("/sys/class/power_supply", name, "online", NULL);
      if (!g_file_get_contents (path, &content, NULL, NULL))
        continue;
      if (g_str_equal(g_strstrip(content), "1"))
        return TRUE;
    }
  return FALSE;
}

int system_battery_percent_at(const char *root)
{
  g_autoptr(GDir) dir = g_dir_open(root, 0, NULL);
  if (!dir) return -1;
  const char *name;
  guint count = 0;
  guint64 capacity_sum = 0;
  long double energy_now_sum = 0, energy_full_sum = 0;
  gboolean all_energy = TRUE;
  while ((name = g_dir_read_name(dir))) {
    g_autofree char *type_path = g_build_filename(root, name, "type", NULL);
    g_autofree char *type = NULL;
    if (!g_file_get_contents(type_path, &type, NULL, NULL) || !g_str_equal(g_strstrip(type), "Battery")) continue;
    g_autofree char *path = g_build_filename(root, name, "capacity", NULL);
    g_autofree char *text = NULL;
    guint64 percent;
    if (!g_file_get_contents(path, &text, NULL, NULL) ||
        !g_ascii_string_to_unsigned(g_strstrip(text), 10, 0, 100, &percent, NULL)) continue;
    count++;
    capacity_sum += percent;
    g_autofree char *now_path = g_build_filename(root, name, "energy_now", NULL);
    g_autofree char *full_path = g_build_filename(root, name, "energy_full", NULL);
    g_autofree char *now_text = NULL;
    g_autofree char *full_text = NULL;
    guint64 now, full;
    if (!g_file_get_contents(now_path, &now_text, NULL, NULL) ||
        !g_file_get_contents(full_path, &full_text, NULL, NULL) ||
        !g_ascii_string_to_unsigned(g_strstrip(now_text), 10, 0, G_MAXUINT64, &now, NULL) ||
        !g_ascii_string_to_unsigned(g_strstrip(full_text), 10, 1, G_MAXUINT64, &full, NULL) || now > full)
      all_energy = FALSE;
    else { energy_now_sum += now; energy_full_sum += full; }
  }
  if (!count) return -1;
  if (all_energy) return (int)(100.0L * energy_now_sum / energy_full_sum + 0.5L);
  return (int)((capacity_sum + count / 2) / count);
}

int system_battery_percent(void)
{ return system_battery_percent_at("/sys/class/power_supply"); }

GPtrArray *
system_backlight_devices (void)
{
  g_autoptr(GDir) dir = NULL;
  GPtrArray *devices = g_ptr_array_new_with_free_func (g_free);
  const char *name;

  dir = g_dir_open ("/sys/class/backlight", 0, NULL);
  if (dir == NULL)
    return devices;

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autofree char *max_path = NULL;
      g_autofree char *cur_path = NULL;
      g_autofree char *max_text = NULL;
      g_autofree char *cur_text = NULL;
      BacklightDevice *device;
      long max;
      gsize k;
      gboolean name_ok = TRUE;

      /* Canonicalization guard: directory entries must be plain device
       * names. Reject traversal ("/", ".", "..") and anything outside a
       * tight sysfs-name grammar so the paths built below cannot escape
       * /sys/class/backlight. */
      if (name[0] == '\0' || strlen (name) > 64 ||
          g_strcmp0 (name, ".") == 0 || g_strcmp0 (name, "..") == 0 ||
          strchr (name, '/') != NULL)
        continue;
      for (k = 0; name[k] != '\0'; k++)
        {
          char c = name[k];

          if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                c == '.' || c == ':' || c == '+'))
            {
              name_ok = FALSE;
              break;
            }
        }
      if (!name_ok)
        continue;

      max_path = g_build_filename ("/sys/class/backlight", name, "max_brightness", NULL);
      if (!g_file_get_contents (max_path, &max_text, NULL, NULL))
        continue;
      guint64 parsed;
      if (!g_ascii_string_to_unsigned(g_strstrip(max_text), 10, 1, G_MAXUINT32, &parsed, NULL))
        continue;
      max = parsed;

      cur_path = g_build_filename ("/sys/class/backlight", name, "brightness", NULL);
      if (!g_file_get_contents (cur_path, &cur_text, NULL, NULL))
        continue;

      device = g_new0 (BacklightDevice, 1);
      g_strlcpy (device->name, name, sizeof (device->name));
      device->max = max;
      device->current = g_ascii_string_to_unsigned(g_strstrip(cur_text), 10, 0, max, &parsed, NULL) ? (long)parsed : 0;
      g_ptr_array_add (devices, device);
    }
  return devices;
}

void
system_backlight_devices_free (GPtrArray *devices)
{
  if (devices != NULL)
    g_ptr_array_unref (devices);
}

static gboolean
is_masked_target (const char *target)
{
  const char *directories[] = { "/etc/systemd/system", "/run/systemd/system", NULL };
  for (guint i = 0; directories[i]; i++) {
    g_autofree char *path = g_build_filename(directories[i], target, NULL);
    struct stat info;
    if (lstat(path, &info) != 0) continue;
    if (!S_ISLNK(info.st_mode)) continue;
    g_autofree char *destination = g_file_read_link(path, NULL);
    if (!destination) continue;
    g_autofree char *absolute = g_canonicalize_filename(destination, directories[i]);
    if (g_str_equal(absolute, "/dev/null")) return TRUE;
  }
  return FALSE;
}

void
system_sleep_state (SleepState *state)
{
  state->allow_suspend = !is_masked_target ("suspend.target");
  state->allow_hibernate = !is_masked_target ("hibernate.target");
  state->allow_hybrid = !is_masked_target ("hybrid-sleep.target");
  if (is_masked_target("sleep.target"))
    state->allow_suspend = state->allow_hibernate = state->allow_hybrid = FALSE;
}

static void
collect_logind_keys (const char *path, GHashTable *keys)
{
  g_autofree char *text = NULL;

  if (!g_file_get_contents (path, &text, NULL, NULL))
    return;
  system_parse_logind_text (text, keys);
}

void
system_parse_logind_text (const char *text, GHashTable *keys)
{
  g_auto(GStrv) lines = NULL;
  gboolean in_login = FALSE;
  gsize i;

  lines = g_strsplit (text, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      g_autofree char *stripped = NULL;
      char *equals;
      char *line = lines[i];

      stripped = g_strdup (line);
      g_strstrip (stripped);
      if (stripped[0] == '\0' || stripped[0] == '#' || stripped[0] == ';')
        continue;
      if (stripped[0] == '[')
        {
          in_login = g_str_equal(stripped, "[Login]");
          continue;
        }
      if (!in_login)
        continue;

      equals = strchr (stripped, '=');
      if (equals == NULL)
        continue;
      *equals = '\0';
      g_hash_table_replace (keys,
                            g_strdup (g_strstrip (stripped)),
                            g_strdup (g_strstrip (equals + 1)));
    }
}

static LidAction
lid_action_from_value (const char *value)
{
  int i;

  for (i = 0; i < LID_ACTION_COUNT; i++)
    {
      if (g_strcmp0 (lid_actions[i].value, value) == 0)
        return lid_actions[i].action;
    }
  return LID_CUSTOM;
}

static void collect_logind_dropins(GHashTable *keys, const char *const *directories)
{
  g_autoptr(GHashTable) files = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  for (guint i = 0; directories[i]; i++) {
    g_autoptr(GDir) dir = g_dir_open(directories[i], 0, NULL);
    if (!dir) continue;
    const char *name;
    while ((name = g_dir_read_name(dir)))
      if (g_str_has_suffix(name, ".conf"))
        g_hash_table_replace(files, g_strdup(name), g_build_filename(directories[i], name, NULL));
  }
  GList *names = g_list_sort(g_hash_table_get_keys(files), (GCompareFunc)g_strcmp0);
  for (GList *item = names; item; item = item->next)
    collect_logind_keys(g_hash_table_lookup(files, item->data), keys);
  g_list_free(names);
}

void system_lid_state(LidState *state)
{
  g_autoptr(GHashTable) keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  collect_logind_keys(LOGIND_CONF, keys);
  const char *directories[] = { "/usr/lib/systemd/logind.conf.d", "/usr/local/lib/systemd/logind.conf.d",
                                "/run/systemd/logind.conf.d", LOGIND_DROPIN_DIR, NULL };
  collect_logind_dropins(keys, directories);
  const char *value = g_hash_table_lookup(keys, "HandleLidSwitch");
  state->handle = lid_action_from_value(value && *value ? value : "suspend");
  value = g_hash_table_lookup(keys, "HandleLidSwitchExternalPower");
  state->handle_external_power = value && *value ? lid_action_from_value(value) : state->handle;
  value = g_hash_table_lookup(keys, "HandleLidSwitchDocked");
  state->handle_docked = lid_action_from_value(value && *value ? value : "ignore");
}

const char *
system_lid_action_value (LidAction action)
{
  int i;

  for (i = 0; i < LID_ACTION_COUNT; i++)
    {
      if (lid_actions[i].action == action)
        return lid_actions[i].value;
    }
  return "ignore";
}

int
system_lid_action_index (const char *value)
{
  LidAction action = lid_action_from_value (value);

  switch (action)
    {
    case LID_SUSPEND:
      return 0;
    case LID_HIBERNATE:
      return 1;
    case LID_HYBRID_SLEEP:
      return 2;
    case LID_LOCK:
      return 3;
    case LID_CUSTOM:
      return 5;
    default:
      return 4;
    }
}

void
system_set_brightness (const char *name, long value, GError **error)
{
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(GVariant) reply = NULL;
  gsize k;

  if (name == NULL || name[0] == '\0' || strlen (name) > 64 ||
      g_strcmp0 (name, ".") == 0 || g_strcmp0 (name, "..") == 0 ||
      strchr (name, '/') != NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "refusing unexpected backlight device name");
      return;
    }
  for (k = 0; name[k] != '\0'; k++)
    {
      char c = name[k];

      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' ||
            c == '.' || c == ':' || c == '+'))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "refusing unexpected backlight device name");
          return;
        }
    }
  if (value < 0 || (guint64)value > G_MAXUINT32) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "invalid brightness value");
    return;
  }

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
  if (bus == NULL)
    return;

  reply = g_dbus_connection_call_sync (bus,
                                       "org.freedesktop.login1",
                                       "/org/freedesktop/login1/session/self",
                                       "org.freedesktop.login1.Session",
                                       "SetBrightness",
                                       g_variant_new ("(ssu)", "backlight", name, (guint) value),
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       error);
}

static void
spawn_process_group (gpointer data)
{
  (void) data;
  setpgid (0, 0);
}

static HelperResult *
helper_result_new (int code, const char *out, const char *err)
{
  HelperResult *result = g_new0 (HelperResult, 1);

  result->code = code;
  result->out = g_strdup (out != NULL ? out : "");
  result->err = g_strdup (err != NULL ? err : "");
  return result;
}

void
helper_result_free (HelperResult *result)
{
  if (result == NULL)
    return;
  g_free (result->out);
  g_free (result->err);
  g_free (result);
}

static HelperResult *
spawn_capture (const char *const *argv, GCancellable *cancellable)
{
  GPid pid = 0;
  int stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;
  int status = 0;
  gboolean child_done = FALSE, stdout_open = TRUE, stderr_open = TRUE;
  gboolean timed_out = FALSE, output_limited = FALSE, cancelled = FALSE, terminating = FALSE;
  gboolean force_sent = FALSE;
  gint64 deadline = g_get_monotonic_time () + SPAWN_CAPTURE_TIMEOUT_US;
  gint64 kill_deadline = 0;
  g_autoptr(GError) error = NULL;
  g_autoptr(GString) out = g_string_sized_new (4096);
  g_autoptr(GString) err = g_string_sized_new (1024);

  /* Drain both pipes together so neither can block the child. The process
   * group lets timeout/output-limit cleanup reap helpers with descendants. */
  if (!g_spawn_async_with_pipes (NULL, (char **) argv, NULL,
                                 G_SPAWN_DO_NOT_REAP_CHILD,
                                 spawn_process_group, NULL, &pid,
                                 &stdin_fd, &stdout_fd, &stderr_fd, &error))
    return helper_result_new (-1, "", error->message);

  if (stdin_fd >= 0) close (stdin_fd);
  fcntl (stdout_fd, F_SETFL, fcntl (stdout_fd, F_GETFL) | O_NONBLOCK);
  fcntl (stderr_fd, F_SETFL, fcntl (stderr_fd, F_GETFL) | O_NONBLOCK);

  while (!child_done || stdout_open || stderr_open)
    {
      struct pollfd fds[2];
      nfds_t n_fds = 0;
      gint64 now = g_get_monotonic_time ();
      int wait_ms = 100;

      if (!terminating && cancellable != NULL && g_cancellable_is_cancelled (cancellable))
        {
          cancelled = TRUE;
          terminating = TRUE;
          kill_deadline = now + SPAWN_CAPTURE_KILL_GRACE_US;
          kill (-pid, SIGTERM);
          kill (pid, SIGTERM);
        }
      if (!terminating && now >= deadline)
        {
          timed_out = TRUE;
          terminating = TRUE;
          kill_deadline = now + SPAWN_CAPTURE_KILL_GRACE_US;
          kill (-pid, SIGTERM);
          kill (pid, SIGTERM);
        }
      if (terminating && !force_sent && now >= kill_deadline)
        {
          kill (-pid, SIGKILL);
          if (!child_done)
            kill (pid, SIGKILL);
          force_sent = TRUE;
        }

      if (stdout_open)
        fds[n_fds++] = (struct pollfd) { stdout_fd, POLLIN | POLLHUP, 0 };
      if (stderr_open)
        fds[n_fds++] = (struct pollfd) { stderr_fd, POLLIN | POLLHUP, 0 };
      if (n_fds > 0 && poll (fds, n_fds, wait_ms) < 0 && errno != EINTR)
        {
          terminating = TRUE;
          kill_deadline = now + SPAWN_CAPTURE_KILL_GRACE_US;
          kill (-pid, SIGTERM);
          kill (pid, SIGTERM);
        }

      for (nfds_t i = 0; i < n_fds; i++)
        {
          gboolean is_stdout = fds[i].fd == stdout_fd;
          GString *buffer = is_stdout ? out : err;
          gboolean *is_open = is_stdout ? &stdout_open : &stderr_open;
          char chunk[8192];

          if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR)))
            continue;
          for (;;)
            {
              ssize_t got = read (fds[i].fd, chunk, sizeof chunk);
              if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                break;
              if (got <= 0)
                {
                  close (fds[i].fd);
                  *is_open = FALSE;
                  break;
                }
              if (!output_limited && (gsize) got <= SPAWN_CAPTURE_LIMIT - buffer->len)
                g_string_append_len (buffer, chunk, got);
              else if (!output_limited)
                {
                  output_limited = TRUE;
                  terminating = TRUE;
                  kill_deadline = g_get_monotonic_time () + SPAWN_CAPTURE_KILL_GRACE_US;
                  kill (-pid, SIGTERM);
                  kill (pid, SIGTERM);
                }
            }
        }

      if (!child_done)
        {
          pid_t waited = waitpid (pid, &status, WNOHANG);
          if (waited == pid || (waited < 0 && errno == ECHILD))
            child_done = TRUE;
          else if (waited < 0 && errno != EINTR)
            {
              child_done = TRUE;
              status = 0;
            }
        }
    }

  g_spawn_close_pid (pid);
  if (timed_out)
    g_string_append (err, "\nhelper exceeded its deadline");
  if (output_limited)
    g_string_append (err, "\nhelper output exceeded the 1 MiB per-stream limit");

  g_autofree char *valid_out = g_utf8_make_valid (out->str, out->len);
  g_autofree char *valid_err = g_utf8_make_valid (err->str, err->len);
  int code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
  if (timed_out || output_limited || cancelled || terminating)
    code = -1;
  if (cancelled)
    {
      g_autofree char *cancelled_err = g_strdup_printf ("%s\nhelper cancelled during shutdown", valid_err);
      g_free (valid_err);
      valid_err = g_steal_pointer (&cancelled_err);
    }
  return helper_result_new (code, valid_out, valid_err);
}

HelperResult *
system_spawn_capture (const char *const *args, GCancellable *cancellable)
{
  return spawn_capture (args, cancellable);
}

