/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    g_autofree char *dir = g_dir_make_tmp("luxerandr-group-failure-XXXXXX", NULL);
    g_assert_nonnull(dir);
    g_autofree char *script = g_build_filename(dir, "xrandr", NULL);
    g_autofree char *log = g_build_filename(dir, "log", NULL);
    g_autofree char *contents = NULL;
    g_autofree char *stdout_text = NULL;
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) guard = NULL;

    g_assert_true(g_file_set_contents(script,
        "#!/bin/sh\nprintf 'started\\n' >> \"$LUXE_RANDR_GROUP_FAILURE_LOG\"\n", -1, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_chmod(script, 0700), ==, 0);
    g_subprocess_launcher_setenv(launcher, "PATH", dir, TRUE);
    g_subprocess_launcher_setenv(launcher, "LUXE_RANDR_GROUP_FAILURE_LOG", log, TRUE);
    guard = g_subprocess_launcher_spawn(launcher, &error, argv[1],
        "xrandr --apply", "xrandr --restore", NULL);
    g_assert_no_error(error);
    g_assert_nonnull(guard);
    g_assert_true(g_subprocess_communicate_utf8(guard, NULL, NULL, &stdout_text, NULL, &error));
    g_assert_no_error(error);
    g_assert_false(g_subprocess_wait_check(guard, NULL, &error));
    g_assert_error(error, G_SPAWN_EXIT_ERROR, 1);
    g_clear_error(&error);
    g_assert_cmpstr(stdout_text, ==, "RESTORE_FAILED\n");
    g_assert_false(g_subprocess_get_successful(guard));
    g_assert_false(g_file_test(log, G_FILE_TEST_EXISTS));
    g_assert_cmpint(g_unlink(script), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    return 0;
}
