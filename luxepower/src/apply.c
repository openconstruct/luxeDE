/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

#include "system.h"
#include "tlp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>

#define APPLY_MAX_CONFIG_BYTES (4 * 1024 * 1024)
#ifndef SYSTEMCTL_PATH
#define SYSTEMCTL_PATH "/usr/bin/systemctl"
#endif
#define LOGIND_DROPIN_DIR "/etc/systemd/logind.conf.d"
#define LOGIND_DROPIN LOGIND_DROPIN_DIR "/99-luxepower.conf"
#define SLEEP_TARGET_SUSPEND "suspend.target"
#define SLEEP_TARGET_HIBERNATE "hibernate.target"
#define SLEEP_TARGET_HYBRID "hybrid-sleep.target"
#define SLEEP_TARGET_SLEEP "sleep.target"
#ifndef SYSTEMD_SYSTEM_DIR
#ifdef LUXEPOWER_TESTING
#define SYSTEMD_SYSTEM_DIR (g_getenv ("LUXEPOWER_TEST_SYSTEM_DIR"))
#else
#define SYSTEMD_SYSTEM_DIR "/etc/systemd/system"
#endif
#endif
#ifndef SYSTEMD_RUNTIME_DIR
#ifdef LUXEPOWER_TESTING
#define SYSTEMD_RUNTIME_DIR (g_getenv ("LUXEPOWER_TEST_RUNTIME_DIR"))
#else
#define SYSTEMD_RUNTIME_DIR "/run/systemd/system"
#endif
#endif
#ifndef SLEEP_LOCK_PATH
#ifdef LUXEPOWER_TESTING
#define SLEEP_LOCK_PATH (g_getenv ("LUXEPOWER_TEST_LOCK_PATH"))
#else
#define SLEEP_LOCK_PATH "/run/lock/luxepower-sleep.lock"
#endif
#endif

static char *read_current_tlp_config (const char *path, mode_t *mode);

static void
fail (const char *message)
{
  fprintf (stderr, "%s\n", message);
  exit (1);
}

static void scrub_environment(void)
{
  g_autofree char *caller = g_strdup(getenv("PKEXEC_UID"));
  clearenv();
  setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
  setenv("LANG", "C.UTF-8", 1);
  setenv("LC_ALL", "C.UTF-8", 1);
  if (caller) setenv("PKEXEC_UID", caller, 1);
}

static int require_pkexec_uid(void)
{
#ifdef LUXEPOWER_TESTING
  return getuid ();
#else
  const char *text = getenv("PKEXEC_UID");
  guint64 uid;
  if (geteuid() != 0 || !text ||
      !g_ascii_string_to_unsigned(text, 10, 0, G_MAXINT, &uid, NULL))
    fail("luxepower-apply requires root and a valid authenticated pkexec user");
  return uid;
#endif
}

static char *
verified_staged_text (const char *path, const char *expected_hash, int caller_uid)
{
  int fd;
  struct stat info;
  gsize size = 0;
  GChecksum *checksum;

  fd = open (path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    fail ("cannot open the staged configuration file");
  if (fstat (fd, &info) != 0)
    {
      close (fd);
      fail ("cannot inspect the staged configuration file");
    }
  if (!S_ISREG (info.st_mode))
    {
      close (fd);
      fail ("the staged configuration file is not a regular file");
    }
  if (info.st_uid != (uid_t) caller_uid)
    {
      close (fd);
      fail ("the staged configuration file is not owned by the authenticated user");
    }
  if ((info.st_mode & 077) != 0)
    {
      close (fd);
      fail ("the staged configuration file has group or other access bits");
    }
  if (info.st_size < 0 || info.st_size > APPLY_MAX_CONFIG_BYTES)
    {
      close (fd);
      fail ("the staged configuration file is unexpectedly large");
    }

  size = (gsize) info.st_size;
  {
    char *text = malloc (size + 1);
    gsize read_total = 0;

    if (text == NULL)
      {
        close (fd);
        fail ("out of memory");
      }
    while (read_total < size)
      {
        ssize_t got = read (fd, text + read_total, size - read_total);
        if (got < 0 && errno == EINTR) continue;

        if (got <= 0)
          {
            close (fd);
            free (text);
            fail ("cannot read the staged configuration file");
          }
        read_total += got;
      }
    text[read_total] = '\0';
    close (fd);
    if (memchr(text, '\0', read_total)) {
      free(text);
      fail("the staged configuration contains a NUL byte");
    }

    checksum = g_checksum_new (G_CHECKSUM_SHA256);
    g_checksum_update (checksum, (const guchar *) text, read_total);
    {
          g_autofree char *digest = g_strdup (g_checksum_get_string (checksum));
      g_checksum_free (checksum);
      if (g_strcmp0 (digest, expected_hash) != 0)
        {
          free (text);
          fail ("the staged configuration file does not match the content reviewed in the interface");
        }
    }
    return text;
  }
}

static void
validated_config (const char *text)
{
  const char *line = text;

  while (line != NULL && line[0] != '\0')
    {
      const char *end = strchr (line, '\n');
      gsize length;
      char *copy;

      length = end != NULL ? (gsize) (end - line) : strlen (line);
      copy = g_strndup (line, length);
      g_strstrip (copy);

      if (copy[0] != '\0' && copy[0] != '#' && !tlp_config_line_is_valid (copy))
        {
          fprintf (stderr,
                   "refusing a configuration line LuxePower cannot safely validate; the file was not changed: %.80s\n",
                   copy);
          g_free (copy);
          exit (1);
        }
      g_free (copy);

      if (end == NULL)
        break;
      line = end + 1;
    }
}

static gboolean
atomic_write (const char *path, const char *text, gsize length, mode_t mode,
              const char *expected_current_digest)
{
  g_autofree char *template = g_strdup_printf ("%s.luxepower-XXXXXX", path);
  int fd;
  ssize_t written;
  int saved_errno;

  fd = g_mkstemp (template);
  if (fd < 0)
    fail ("cannot create the temporary file");
  written = 0;
  while ((gsize) written < length)
    {
      ssize_t got = write (fd, text + written, length - written);
      if (got < 0 && errno == EINTR) continue;

      if (got <= 0)
        {
          saved_errno = errno;
          close (fd);
          unlink (template);
          fail (g_strerror (saved_errno));
        }
      written += got;
    }
  gboolean flushed = fchmod(fd, mode) == 0 && fsync(fd) == 0;
  int closed = close(fd);
  if (!flushed || closed != 0)
    {
      unlink (template);
      fail ("cannot flush the temporary file");
    }
  if (expected_current_digest != NULL)
    {
      mode_t current_mode;
      g_autofree char *current = read_current_tlp_config (path, &current_mode);
      g_autofree char *digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, current, -1);

      if (g_strcmp0 (digest, expected_current_digest) != 0)
        {
          unlink (template);
          fail ("TLP configuration changed during the transaction; the external edit was preserved, reload and apply again");
        }
    }
  /* Recheck immediately before replacement. The transaction lock coordinates
   * LuxePower instances; external editors may ignore it, so this narrows the
   * final race but cannot make arbitrary writers participate in the lock. */
  if (rename (template, path) != 0)
    {
      unlink (template);
      fail ("cannot install the file");
    }
  g_autofree char *parent = g_path_get_dirname(path);
  fd = open(parent, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
  if (fd < 0) return FALSE;
  flushed = fsync(fd) == 0;
  closed = close(fd);
  return flushed && closed == 0;
}

static char *
read_current_tlp_config (const char *path, mode_t *mode)
{
  int fd = open (path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  struct stat before, after;
  char *text;
  gsize size, offset = 0;

  if (fd < 0 || fstat (fd, &before) != 0 || !S_ISREG (before.st_mode) ||
#ifdef LUXEPOWER_TEST_FILE_OWNER
      before.st_uid != getuid () ||
#else
      before.st_uid != 0 ||
#endif
      (before.st_mode & 022) != 0 ||
      before.st_size < 0 || before.st_size > APPLY_MAX_CONFIG_BYTES)
    {
      if (fd >= 0) close (fd);
      fail ("cannot safely read the current TLP configuration; reload before applying");
    }
  size = (gsize) before.st_size;
  text = malloc (size + 1);
  if (text == NULL)
    {
      close (fd);
      fail ("out of memory");
    }
  while (offset < size)
    {
      ssize_t count = read (fd, text + offset, size - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0)
        {
          close (fd);
          free (text);
          fail ("the current TLP configuration changed during the read");
        }
      offset += count;
    }
  text[size] = '\0';
  if (memchr (text, '\0', size) != NULL || fstat (fd, &after) != 0 ||
      before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
      before.st_size != after.st_size || before.st_mtime != after.st_mtime ||
      before.st_ctime != after.st_ctime || before.st_uid != after.st_uid ||
      before.st_mode != after.st_mode)
    {
      close (fd);
      free (text);
      fail ("the current TLP configuration changed during the read");
    }
  close (fd);
  *mode = before.st_mode & 0777;
  return text;
}

static gboolean
start_tlp (GError **error)
{
  const char *argv[] = { TLP_PATH, "start", NULL };
  int status = 0;

  return g_spawn_sync (NULL, (char **) argv, NULL, 0, NULL, NULL,
                       NULL, NULL, &status, error) &&
         g_spawn_check_wait_status (status, error);
}

static gboolean
run_systemctl (const char *verb, const char *target, gboolean runtime)
{
  const char *argv[5];
  int status = 0;
  GError *error = NULL;
  gboolean allowed_target = FALSE;

  /* Defensive allowlist: callers only pass constants, but the privileged
   * helper must never mask an arbitrary unit even if a future caller
   * passes attacker-influenced data. */
  if (g_strcmp0 (verb, "mask") != 0 && g_strcmp0 (verb, "unmask") != 0)
    fail ("refusing unexpected systemctl verb");
  allowed_target = g_strcmp0 (target, SLEEP_TARGET_SUSPEND) == 0 ||
                   g_strcmp0 (target, SLEEP_TARGET_HIBERNATE) == 0 ||
                   g_strcmp0 (target, SLEEP_TARGET_HYBRID) == 0 ||
                   g_strcmp0 (target, SLEEP_TARGET_SLEEP) == 0;
  if (!allowed_target)
    fail ("refusing unexpected sleep target");

  argv[0] = SYSTEMCTL_PATH;
  argv[1] = verb;
  argv[2] = runtime ? "--runtime" : target;
  argv[3] = runtime ? target : NULL;
  argv[4] = NULL;
  if (!g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL,
                     NULL, NULL, &status, &error))
    {
      fprintf (stderr, "systemctl %s %s failed: %s\n", verb, target,
               error != NULL ? error->message : "unknown error");
      g_clear_error (&error);
      return FALSE;
    }
  if (!g_spawn_check_wait_status (status, NULL))
    {
      fprintf (stderr, "systemctl %s %s failed\n", verb, target);
      return FALSE;
    }
  return TRUE;
}

typedef struct {
  gboolean persistent;
  gboolean runtime;
} SleepMaskState;

static gboolean
path_is_masked (const char *directory, const char *target)
{
  g_autofree char *path = g_build_filename (directory, target, NULL);
  struct stat info;

  if (lstat (path, &info) != 0 || !S_ISLNK (info.st_mode))
    return FALSE;
  g_autofree char *destination = g_file_read_link (path, NULL);
  if (destination == NULL)
    return FALSE;
  g_autofree char *absolute = g_canonicalize_filename (destination, directory);
  return g_str_equal (absolute, "/dev/null");
}

static SleepMaskState
sleep_mask_state (const char *target)
{
  return (SleepMaskState) {
    path_is_masked (SYSTEMD_SYSTEM_DIR, target),
    path_is_masked (SYSTEMD_RUNTIME_DIR, target)
  };
}

static gboolean
restore_sleep_mask (const char *target, SleepMaskState previous)
{
  gboolean ok = run_systemctl ("unmask", target, FALSE);

  if (previous.persistent && !run_systemctl ("mask", target, FALSE))
    ok = FALSE;
  if (previous.runtime && !run_systemctl ("mask", target, TRUE))
    ok = FALSE;
  return ok;
}

static void
apply_config (char **arguments)
{
  int caller_uid;
  int lock_fd;
  char *text;
  char *original_text;
  g_autofree char *applied_digest = NULL;
  mode_t mode;
  GError *error = NULL;
  g_autofree char *current_digest = NULL;

  if (arguments[0] == NULL || arguments[1] == NULL || arguments[2] == NULL || arguments[3] != NULL)
    fail ("usage: luxepower-apply config FILE STAGED_SHA256 ORIGINAL_SHA256");
  if (strlen (arguments[1]) != 64 || strlen (arguments[2]) != 64)
    fail ("refusing malformed hash argument");
  for (gsize h = 0; h < 64; h++)
    {
      char c = arguments[1][h];
      char original = arguments[2][h];

      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) ||
          !((original >= '0' && original <= '9') || (original >= 'a' && original <= 'f') ||
            (original >= 'A' && original <= 'F')))
        fail ("refusing malformed hash argument");
    }

  caller_uid = require_pkexec_uid ();
  text = verified_staged_text (arguments[0], arguments[1], caller_uid);
  applied_digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, text, -1);
  validated_config (text);

  lock_fd = open ("/run/lock/luxepower-tlp.lock",
                  O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  struct stat lock_info;
  if (lock_fd < 0 || fstat (lock_fd, &lock_info) != 0 || !S_ISREG (lock_info.st_mode) ||
      lock_info.st_uid != 0 || (lock_info.st_mode & 077) != 0)
    {
      if (lock_fd >= 0) close (lock_fd);
      free (text);
      fail ("cannot safely open the TLP configuration transaction lock");
    }
  while (flock (lock_fd, LOCK_EX) != 0)
    {
      if (errno == EINTR) continue;
      close (lock_fd);
      free (text);
      fail ("cannot acquire the TLP configuration transaction lock");
    }

  original_text = read_current_tlp_config (TLP_CONFIG_PATH, &mode);
  current_digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, original_text, -1);
  if (g_strcmp0 (current_digest, arguments[2]) != 0)
    {
      close (lock_fd);
      free (original_text);
      free (text);
      fail ("TLP configuration changed since it was reviewed; reload before applying");
    }

  if (!atomic_write (TLP_CONFIG_PATH, text, strlen (text), mode, arguments[2]))
    {
      fprintf (stderr, "TLP configuration was installed, but its directory could not be flushed; restoring the reviewed original\n");
      if (!atomic_write (TLP_CONFIG_PATH, original_text, strlen (original_text), mode, applied_digest))
        fail ("TLP configuration was restored but its directory flush is uncertain; inspect /etc/tlp.conf before applying again");
      fail ("TLP configuration directory flush failed; the reviewed original was restored");
    }
  if (start_tlp (&error))
    {
      free (text);
      free (original_text);
      close (lock_fd);
      return;
    }

  fprintf (stderr, "TLP activation failed: %s\n",
           error != NULL ? error->message : "unknown error");
  g_clear_error (&error);
  gboolean restored_durably = atomic_write (TLP_CONFIG_PATH, original_text,
                                            strlen (original_text), mode, applied_digest);
  free (text);
  if (start_tlp (&error))
    {
      free (original_text);
      close (lock_fd);
      fail (restored_durably ?
            "TLP activation failed; previous configuration was restored and restarted" :
            "TLP activation failed; previous configuration was restored and restarted, but its directory flush is uncertain");
    }
  fprintf (stderr, "TLP rollback activation failed: %s\n",
           error != NULL ? error->message : "unknown error");
  g_clear_error (&error);
  free (original_text);
  close (lock_fd);
  fail ("TLP activation failed and rollback could not be confirmed; inspect /etc/tlp.conf and run tlp start");
}

static void
apply_sleep (char **arguments)
{
  const char *targets[] = {
    SLEEP_TARGET_SUSPEND, SLEEP_TARGET_HIBERNATE,
    SLEEP_TARGET_HYBRID, SLEEP_TARGET_SLEEP
  };
  SleepMaskState previous[G_N_ELEMENTS (targets)];
  const char *verbs[G_N_ELEMENTS (targets)];
  int lock_fd;
  int allow_suspend;
  int allow_hibernate;
  int allow_hybrid;
  int i;

  if (arguments[0] == NULL || arguments[1] == NULL ||
      arguments[2] == NULL || arguments[3] != NULL)
    fail ("usage: luxepower-apply sleep SUSPEND HIBERNATE HYBRID");

  for (i = 0; i < 3; i++)
    {
      if ((arguments[i][0] != '0' && arguments[i][0] != '1') || arguments[i][1] != '\0')
        fail ("refusing malformed sleep argument");
    }

  require_pkexec_uid ();
  allow_suspend = arguments[0][0] == '1';
  allow_hibernate = arguments[1][0] == '1';
  allow_hybrid = arguments[2][0] == '1';
  verbs[0] = allow_suspend ? "unmask" : "mask";
  verbs[1] = allow_hibernate ? "unmask" : "mask";
  verbs[2] = allow_hybrid ? "unmask" : "mask";
  verbs[3] = !allow_suspend && !allow_hibernate && !allow_hybrid ? "mask" : "unmask";

  lock_fd = open (SLEEP_LOCK_PATH,
                  O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock_fd < 0)
    fail ("cannot open the sleep-settings transaction lock");
  struct stat lock_info;
  if (fstat (lock_fd, &lock_info) != 0 || !S_ISREG (lock_info.st_mode) ||
      lock_info.st_uid !=
#ifdef LUXEPOWER_TESTING
      getuid () ||
#else
      0 ||
#endif
      (lock_info.st_mode & 077) != 0)
    {
      close (lock_fd);
      fail ("refusing an unsafe sleep-settings transaction lock");
    }
  while (flock (lock_fd, LOCK_EX) != 0)
    {
      if (errno == EINTR) continue;
      close (lock_fd);
      fail ("cannot acquire the sleep-settings transaction lock");
    }

  for (i = 0; i < (int) G_N_ELEMENTS (targets); i++)
    previous[i] = sleep_mask_state (targets[i]);
  for (i = 0; i < (int) G_N_ELEMENTS (targets); i++)
    {
      if (run_systemctl (verbs[i], targets[i], FALSE))
        continue;

      gboolean rollback_ok = TRUE;
      while (i >= 0)
        {
          if (!restore_sleep_mask (targets[i], previous[i]))
            rollback_ok = FALSE;
          i--;
        }
      close (lock_fd);
      if (rollback_ok)
        fail ("sleep settings failed; previous mask states were restored");
      fail ("sleep settings failed and rollback was incomplete; inspect systemd sleep-target masks");
    }
  close (lock_fd);
}

static void
apply_lid (char **arguments)
{
  int count = 0;
  g_autoptr(GKeyFile) config = g_key_file_new();
  static const char *values[] = {
    "ignore", "suspend", "hibernate", "hybrid-sleep", "suspend-then-hibernate",
    "lock", "poweroff", "reboot", "halt", "kexec", NULL
  };
  static const char *keys[] = {
    "HandleLidSwitch", "HandleLidSwitchExternalPower", "HandleLidSwitchDocked", NULL
  };
  int i, j;
  gboolean known_key, known_value;

  require_pkexec_uid ();

  for (count = 0; arguments[count] != NULL; count++)
    {
      const char *equals = strchr (arguments[count], '=');
      gsize key_length;
      const char *value;

      if (strlen (arguments[count]) > 128)
        fail ("refusing malformed lid argument");
      if (count >= 16)
        fail ("refusing malformed lid argument");
      if (equals == NULL)
        fail ("refusing malformed lid argument");
      key_length = (gsize) (equals - arguments[count]);
      value = equals + 1;
      if (key_length == 0 || key_length > 64 || strlen (value) == 0 || strlen (value) > 64)
        {
          fprintf (stderr, "refusing malformed lid argument: %.80s\n", arguments[count]);
          exit (1);
        }

      known_key = FALSE;
      for (i = 0; keys[i] != NULL; i++)
        {
          if (strlen (keys[i]) == key_length && strncmp (keys[i], arguments[count], key_length) == 0)
            {
              known_key = TRUE;
              break;
            }
        }
      known_value = FALSE;
      for (j = 0; values[j] != NULL; j++)
        {
          if (g_strcmp0 (values[j], value) == 0)
            {
              known_value = TRUE;
              break;
            }
        }
      if (!known_key || !known_value)
        {
          fprintf (stderr, "refusing malformed lid argument: %.80s\n", arguments[count]);
          exit (1);
        }
    }

  if (count == 0)
    fail ("usage: luxepower-apply lid KEY=VALUE...");

  GError *load_error = NULL;
  if (!g_key_file_load_from_file(config, LOGIND_DROPIN, G_KEY_FILE_KEEP_COMMENTS, &load_error) &&
      !g_error_matches(load_error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    fail("cannot read existing logind drop-in; refusing to overwrite it");
  g_clear_error(&load_error);
  for (i = 0; arguments[i] != NULL; i++)
    {
      const char *equals = strchr(arguments[i], '=');
      g_autofree char *key = g_strndup(arguments[i], equals - arguments[i]);
      g_key_file_set_string(config, "Login", key, equals + 1);
    }
  gsize length;
  g_autofree char *body = g_key_file_to_data(config, &length, NULL);

  if (g_mkdir_with_parents (LOGIND_DROPIN_DIR, 0755) != 0 && errno != EEXIST)
    fail ("cannot create the logind drop-in directory");
  if (!atomic_write(LOGIND_DROPIN, body, length, 0644, NULL))
    fail ("logind drop-in was installed, but its directory flush could not be confirmed; inspect the file before applying again");

  {
    const char *argv[5] = { SYSTEMCTL_PATH, "kill", "--signal=HUP",
                            "systemd-logind.service", NULL };
    int status = 0;
    GError *error = NULL;

    if (!g_spawn_sync(NULL, (char **)argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL,
                      NULL, NULL, &status, &error) || !g_spawn_check_wait_status(status, &error)) {
      g_printerr("Lid configuration was saved, but logind reload failed: %s\n",
                 error ? error->message : "unknown error");
      g_clear_error(&error);
      exit(1);
    }
    g_clear_error (&error);
  }
}


static void
apply_stats (char **arguments)
{
  GError *error = NULL;
  int status = 0;

  for (gsize i = 0; arguments[i] != NULL; i++)
    {
      const char *argument = arguments[i];
      if (i >= 14) fail("too many tlp-stat arguments");

      if (argument[0] != '-' || argument[1] == '\0' || strlen (argument) > 8)
        fail ("refusing malformed tlp-stat argument");
      for (gsize j = 1; argument[j] != '\0'; j++)
        {
          if (argument[j] != '-' && !(argument[j] >= 'a' && argument[j] <= 'z') &&
              !(argument[j] >= 'A' && argument[j] <= 'Z'))
            fail ("refusing malformed tlp-stat argument");
        }
    }

  require_pkexec_uid ();

  {
    const char *argv[16] = { TLP_STAT_PATH, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

    for (gsize i = 0; arguments[i] != NULL && i < 14; i++)
      argv[1 + i] = arguments[i];

    if (!g_spawn_sync (NULL, (char **) argv, NULL, 0, NULL, NULL,
                       NULL, NULL, &status, &error))
      {
        fprintf (stderr, "tlp-stat failed: %s\n",
                 error != NULL ? error->message : "unknown error");
        g_clear_error (&error);
        exit (1);
      }
  }

  if (!g_spawn_check_wait_status (status, &error))
    {
      fprintf (stderr, "%s\n", error != NULL ? error->message : "tlp-stat failed");
      g_clear_error (&error);
      exit (1);
    }
}

int
main (int argc, char **argv)
{
  scrub_environment ();

  if (argc < 2)
    fail ("usage: luxepower-apply config|sleep|lid|stats ...");

  if (g_strcmp0 (argv[1], "config") == 0)
    apply_config (argv + 2);
  else if (g_strcmp0 (argv[1], "sleep") == 0)
    apply_sleep (argv + 2);
  else if (g_strcmp0 (argv[1], "lid") == 0)
    apply_lid (argv + 2);
  else if (g_strcmp0 (argv[1], "stats") == 0)
    apply_stats (argv + 2);
  else
    {
      g_printerr ("unknown mode: %s\n", argv[1]);
      exit (1);
    }
  return 0;
}
