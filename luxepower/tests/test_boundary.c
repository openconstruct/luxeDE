/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <unistd.h>
#include <sys/wait.h>
#include <glib/gstdio.h>
#define main power_helper_main
#include "../src/apply.c"
#undef main

static char *directory, *staged, *digest;

static void must_fail(void (*operation)(void))
{
  pid_t pid = fork();
  g_assert_cmpint(pid, >=, 0);
  if (!pid) { operation(); _exit(0); }
  int status;
  g_assert_cmpint(waitpid(pid, &status, 0), ==, pid);
  g_assert_true(WIFEXITED(status));
  g_assert_cmpint(WEXITSTATUS(status), ==, 1);
}
static void read_stage(void) { g_free(verified_staged_text(staged, digest, getuid())); }
static void bad_hash(void) { g_free(verified_staged_text(staged, "wrong", getuid())); }
static void bad_owner(void) { g_free(verified_staged_text(staged, digest, getuid() + 1)); }
static void invalid_config(void) { validated_config("CPU_BOOST_ON_AC=$(id)\n"); }
static void conflicting_config_write(void)
{
  atomic_write(staged, "CPU_BOOST_ON_AC=0\n", strlen("CPU_BOOST_ON_AC=0\n"), 0600, digest);
}
static void invalid_uid(void) { g_setenv("PKEXEC_UID", "0oops", TRUE); require_pkexec_uid(); }
static void too_many_stats(void)
{
  char *args[17];
  for (int i = 0; i < 16; i++) args[i] = "-s";
  args[16] = NULL;
  apply_stats(args);
}
static void bad_target(void) { run_systemctl("mask", "ssh.service", FALSE); }
static void test_mask_scopes(void)
{
  g_autofree char *root = g_dir_make_tmp("luxepower-mask-XXXXXX", NULL);
  g_autofree char *etc = g_build_filename(root, "etc", NULL);
  g_autofree char *run = g_build_filename(root, "run", NULL);
  g_autofree char *etc_target = g_build_filename(etc, SLEEP_TARGET_SUSPEND, NULL);
  g_autofree char *run_target = g_build_filename(run, SLEEP_TARGET_SUSPEND, NULL);
  g_assert_cmpint(g_mkdir_with_parents(etc, 0700), ==, 0);
  g_assert_cmpint(g_mkdir_with_parents(run, 0700), ==, 0);
  g_assert_cmpint(symlink("/dev/null", run_target), ==, 0);
  g_assert_false(path_is_masked(etc, SLEEP_TARGET_SUSPEND));
  g_assert_true(path_is_masked(run, SLEEP_TARGET_SUSPEND));
  g_assert_cmpint(symlink("/usr/lib/systemd/system/suspend.target", etc_target), ==, 0);
  g_assert_true(path_is_masked(run, SLEEP_TARGET_SUSPEND));
  g_assert_cmpint(unlink(etc_target), ==, 0);
  g_assert_cmpint(unlink(run_target), ==, 0);
  g_assert_cmpint(g_rmdir(etc), ==, 0);
  g_assert_cmpint(g_rmdir(run), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
}
static void environment(void)
{
  g_setenv("BASH_ENV", "/malicious", TRUE);
  g_setenv("PKEXEC_UID", "1000", TRUE);
  scrub_environment();
  g_assert_null(g_getenv("BASH_ENV"));
  g_assert_cmpstr(g_getenv("PKEXEC_UID"), ==, "1000");
  g_assert_cmpstr(g_getenv("PATH"), ==, "/usr/sbin:/usr/bin:/sbin:/bin");
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/luxepower/mask-scopes", test_mask_scopes);
  directory = g_dir_make_tmp("luxe-power-boundary-XXXXXX", NULL);
  g_assert_nonnull(directory);
  staged = g_build_filename(directory, "staged", NULL);
  const char *text = "CPU_BOOST_ON_AC=1\n";
  digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, text, -1);
  atomic_write(staged, text, strlen(text), 0600, NULL);
  read_stage();
  validated_config(text);
  must_fail(bad_hash); must_fail(bad_owner); must_fail(invalid_config);
  g_assert_true(g_file_set_contents(staged, "EXTERNAL=1\n", -1, NULL));
  must_fail(conflicting_config_write);
  g_autofree char *preserved = NULL;
  g_assert_true(g_file_get_contents(staged, &preserved, NULL, NULL));
  g_assert_cmpstr(preserved, ==, "EXTERNAL=1\n");
  g_assert_true(g_file_set_contents(staged, text, -1, NULL));
  must_fail(invalid_uid); must_fail(too_many_stats); must_fail(bad_target);
  g_assert_cmpint(chmod(staged, 0644), ==, 0);
  must_fail(read_stage);
  g_assert_cmpint(unlink(staged), ==, 0);
  g_assert_cmpint(symlink("/dev/null", staged), ==, 0);
  must_fail(read_stage);
  g_assert_cmpint(unlink(staged), ==, 0);
  g_assert_cmpint(mkfifo(staged, 0600), ==, 0);
  must_fail(read_stage); /* Must reject without blocking on a FIFO writer. */
  g_assert_cmpint(unlink(staged), ==, 0);
  atomic_write(staged, "A=1\0B=2", 7, 0600, NULL);
  must_fail(read_stage);
  g_assert_cmpint(unlink(staged), ==, 0);
  g_assert_cmpint(rmdir(directory), ==, 0);
  pid_t pid = fork();
  if (!pid) { environment(); _exit(0); }
  int status;
  g_assert_cmpint(waitpid(pid, &status, 0), ==, pid);
  g_assert_cmpint(status, ==, 0);
  g_free(directory); g_free(staged); g_free(digest);
  return 0;
}
