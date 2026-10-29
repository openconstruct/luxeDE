/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main luxepower_apply_program_main
#include "../src/apply.c"
#undef main
#include <glib/gstdio.h>
#include <sys/wait.h>

static char *root_dir, *system_dir, *runtime_dir, *lock_path, *counter_path;
static char *stderr_path;

static void reset_fixture (void)
{
  g_autoptr(GDir) ignored = g_dir_open (system_dir, 0, NULL);
  const char *name;
  while ((name = g_dir_read_name (ignored))) {
    g_autofree char *path = g_build_filename (system_dir, name, NULL);
    g_unlink (path);
  }
  ignored = g_dir_open (runtime_dir, 0, NULL);
  while ((name = g_dir_read_name (ignored))) {
    g_autofree char *path = g_build_filename (runtime_dir, name, NULL);
    g_unlink (path);
  }
  g_unlink (counter_path);
  g_unlink (lock_path);
  g_unsetenv ("LUXEPOWER_TEST_FAIL_CALLS");
}

static void seed_masks (void)
{
  g_autofree char *path = g_build_filename (system_dir, SLEEP_TARGET_SUSPEND, NULL);
  g_assert_cmpint (symlink ("/dev/null", path), ==, 0);
  g_free (path);
  path = g_build_filename (runtime_dir, SLEEP_TARGET_HIBERNATE, NULL);
  g_assert_cmpint (symlink ("/dev/null", path), ==, 0);
  g_free (path);
  path = g_build_filename (system_dir, SLEEP_TARGET_HYBRID, NULL);
  g_assert_cmpint (symlink ("/dev/null", path), ==, 0);
  path = g_build_filename (runtime_dir, SLEEP_TARGET_HYBRID, NULL);
  g_assert_cmpint (symlink ("/dev/null", path), ==, 0);
  path = g_build_filename (system_dir, SLEEP_TARGET_SLEEP, NULL);
  g_assert_cmpint (symlink ("/dev/null", path), ==, 0);
}

static gboolean is_masked (const char *directory, const char *target)
{
  g_autofree char *path = g_build_filename (directory, target, NULL);
  struct stat info;
  return lstat (path, &info) == 0 && S_ISLNK (info.st_mode);
}

static int run_sleep (void)
{
  pid_t child = fork ();
  g_assert_cmpint (child, >=, 0);
  if (child == 0)
    {
      int error_fd = open (stderr_path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
      if (error_fd >= 0) { dup2 (error_fd, STDERR_FILENO); close (error_fd); }
      char *args[] = { "1", "1", "1", NULL };
      apply_sleep (args);
      _exit (0);
    }
  int status;
  g_assert_cmpint (waitpid (child, &status, 0), ==, child);
  return WIFEXITED (status) ? WEXITSTATUS (status) : 128;
}

static void test_success_removes_both_mask_scopes (void)
{
  reset_fixture ();
  seed_masks ();
  g_assert_cmpint (run_sleep (), ==, 0);
  const char *targets[] = { SLEEP_TARGET_SUSPEND, SLEEP_TARGET_HIBERNATE,
                            SLEEP_TARGET_HYBRID, SLEEP_TARGET_SLEEP };
  for (guint i = 0; i < G_N_ELEMENTS (targets); i++) {
    g_assert_false (is_masked (system_dir, targets[i]));
    g_assert_false (is_masked (runtime_dir, targets[i]));
  }
}

static void test_each_command_failure_restores_prior_masks (void)
{
  const char *targets[] = { SLEEP_TARGET_SUSPEND, SLEEP_TARGET_HIBERNATE,
                            SLEEP_TARGET_HYBRID, SLEEP_TARGET_SLEEP };
  for (int fail_at = 1; fail_at <= 4; fail_at++)
    {
      reset_fixture ();
      seed_masks ();
      g_autofree char *fail_call = g_strdup_printf ("%d", fail_at);
      g_setenv ("LUXEPOWER_TEST_FAIL_CALLS", fail_call, TRUE);
      g_assert_cmpint (run_sleep (), ==, 1);
      for (guint i = 0; i < G_N_ELEMENTS (targets); i++) {
        g_assert_cmpint (is_masked (system_dir, targets[i]), ==,
                         i == 0 || i == 2 || i == 3);
        g_assert_cmpint (is_masked (runtime_dir, targets[i]), ==,
                         i == 1 || i == 2);
      }
    }
}

static void test_rollback_failure_is_reported (void)
{
  reset_fixture ();
  seed_masks ();
  g_setenv ("LUXEPOWER_TEST_FAIL_CALLS", "1,2", TRUE);
  g_assert_cmpint (run_sleep (), ==, 1);
  g_autofree char *error = NULL;
  g_assert_true (g_file_get_contents (stderr_path, &error, NULL, NULL));
  g_assert_nonnull (strstr (error, "rollback was incomplete"));
}

int main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  root_dir = g_dir_make_tmp ("luxepower-sleep-XXXXXX", NULL);
  system_dir = g_build_filename (root_dir, "etc", NULL);
  runtime_dir = g_build_filename (root_dir, "run", NULL);
  lock_path = g_build_filename (root_dir, "sleep.lock", NULL);
  counter_path = g_build_filename (root_dir, "calls", NULL);
  stderr_path = g_build_filename (root_dir, "stderr", NULL);
  g_mkdir (system_dir, 0700);
  g_mkdir (runtime_dir, 0700);
  g_setenv ("LUXEPOWER_TEST_SYSTEM_DIR", system_dir, TRUE);
  g_setenv ("LUXEPOWER_TEST_RUNTIME_DIR", runtime_dir, TRUE);
  g_setenv ("LUXEPOWER_TEST_COUNTER", counter_path, TRUE);
  g_setenv ("LUXEPOWER_TEST_LOCK_PATH", lock_path, TRUE);
  g_test_add_func ("/sleep/both-mask-scopes", test_success_removes_both_mask_scopes);
  g_test_add_func ("/sleep/failure-rollback", test_each_command_failure_restores_prior_masks);
  g_test_add_func ("/sleep/rollback-failure-reported", test_rollback_failure_is_reported);
  int result = g_test_run ();
  reset_fixture ();
  g_rmdir (system_dir); g_rmdir (runtime_dir); g_rmdir (root_dir);
  g_free (root_dir); g_free (system_dir); g_free (runtime_dir);
  g_free (lock_path); g_free (counter_path); g_free (stderr_path);
  return result;
}
