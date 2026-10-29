/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static void run_case(const char *guard, const char *dir, const char *mode,
                     const char *decision, const char *expected, gboolean expect_ready)
{
    g_autofree char *log = g_build_filename(dir, "log", NULL);
    g_unlink(log);
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE);
    g_subprocess_launcher_setenv(launcher, "PATH", dir, TRUE);
    g_subprocess_launcher_setenv(launcher, "GUARD_LOG", log, TRUE);
    g_subprocess_launcher_setenv(launcher, "GUARD_MODE", mode, TRUE);
    g_autoptr(GError) error = NULL;
    g_autoptr(GSubprocess) child = g_subprocess_launcher_spawn(launcher, &error,
        guard, "xrandr --apply", "xrandr --restore", NULL);
    g_assert_no_error(error); g_assert_nonnull(child);
    g_autoptr(GDataInputStream) output = g_data_input_stream_new(g_subprocess_get_stdout_pipe(child));
    GOutputStream *input = g_subprocess_get_stdin_pipe(child);
    if (expect_ready) {
        g_autofree char *ready = g_data_input_stream_read_line(output, NULL, NULL, &error);
        g_assert_no_error(error); g_assert_cmpstr(ready, ==, "READY");
    }
    if (decision && *decision)
        g_assert_true(g_output_stream_write_all(input, decision, 1, NULL, NULL, &error));
    else if (decision) {
        if (!expect_ready) g_usleep(100000);
        g_assert_true(g_output_stream_close(input, NULL, &error));
    }
    g_assert_no_error(error);
    g_autofree char *result = g_data_input_stream_read_line(output, NULL, NULL, &error);
    g_assert_no_error(error); g_assert_cmpstr(result, ==, expected);
    g_assert_true(g_subprocess_wait_check(child, NULL, &error)); g_assert_no_error(error);
    if (g_str_equal(mode, "hang")) g_usleep(1200000);
    g_autofree char *contents = NULL;
    g_assert_true(g_file_get_contents(log, &contents, NULL, NULL));
    g_assert_nonnull(strstr(contents, "apply\n"));
    g_assert_null(strstr(contents, "escaped\n"));
    if (g_str_equal(mode, "partial")) g_assert_nonnull(strstr(contents, "partial\n"));
    if (g_str_equal(expected, "KEPT")) g_assert_null(strstr(contents, "restore\n"));
    else g_assert_nonnull(strstr(contents, "restore\n"));
    g_unlink(log);
}

static void parent_crash(const char *guard, const char *dir, const char *mode)
{
    g_autofree char *log = g_build_filename(dir, "crash-log", NULL);
    int ready_pipe[2];
    g_assert_cmpint(pipe(ready_pipe), ==, 0);
    pid_t parent = fork();
    g_assert_cmpint(parent, >=, 0);
    if (parent == 0) {
        close(ready_pipe[0]);
        int input[2], output[2];
        if (pipe(input) || pipe(output)) _exit(10);
        pid_t worker = fork();
        if (worker < 0) _exit(11);
        if (worker == 0) {
            dup2(input[0], STDIN_FILENO); dup2(output[1], STDOUT_FILENO);
            close(input[0]); close(input[1]); close(output[0]); close(output[1]); close(ready_pipe[1]);
            setenv("PATH", dir, 1); setenv("GUARD_LOG", log, 1); setenv("GUARD_MODE", mode, 1);
            execl(guard, guard, "xrandr --apply", "xrandr --restore", (char *)NULL);
            _exit(12);
        }
        close(input[0]); close(output[1]);
        if (!strcmp(mode, "hang")) {
            /* Wait until fake Apply starts, then simulate GUI death before
             * the guard can report a successful Apply. */
            for (int attempt = 0; access(log, F_OK) && attempt < 100; attempt++) usleep(1000);
            if (access(log, F_OK)) _exit(13);
        } else {
            char ready[6];
            ssize_t n = read(output[0], ready, sizeof ready);
            if (n != 6 || memcmp(ready, "READY\n", 6)) _exit(13);
        }
        if (write(ready_pipe[1], "R", 1) != 1) _exit(14);
        /* Keep the guard's input open until the test kills this GUI stand-in. */
        for (;;) pause();
    }
    close(ready_pipe[1]);
    char ready;
    g_assert_cmpint(read(ready_pipe[0], &ready, 1), ==, 1);
    close(ready_pipe[0]);
    g_assert_cmpint(kill(parent, SIGKILL), ==, 0);
    int status;
    g_assert_cmpint(waitpid(parent, &status, 0), ==, parent);
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    g_autofree char *contents = NULL;
    do {
        g_clear_pointer(&contents, g_free);
        g_file_get_contents(log, &contents, NULL, NULL);
        if (contents && strstr(contents, "restore\n")) break;
        g_usleep(10000);
    } while (g_get_monotonic_time() < deadline);
    g_assert_nonnull(contents);
    g_assert_nonnull(strstr(contents, "apply\n"));
    g_assert_nonnull(strstr(contents, "restore\n"));
    if (!strcmp(mode, "hang")) {
        g_usleep(1200000);
        g_clear_pointer(&contents, g_free);
        g_assert_true(g_file_get_contents(log, &contents, NULL, NULL));
        g_assert_null(strstr(contents, "escaped\n"));
    }
    g_unlink(log);
}

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    g_autofree char *dir = g_dir_make_tmp("luxerandr-guard-XXXXXX", NULL);
    g_autofree char *script = g_build_filename(dir, "xrandr", NULL);
    const char *source = "#!/bin/sh\n"
        "case \"$1\" in\n"
        " --apply) echo apply >> \"$GUARD_LOG\"; case \"$GUARD_MODE\" in\n"
        " fail) exit 1;; partial) echo partial >> \"$GUARD_LOG\"; exit 1;; hang) (/bin/sleep 1; echo escaped >> \"$GUARD_LOG\") & wait;; esac;;\n"
        " --restore) echo restore >> \"$GUARD_LOG\";;\n"
        "esac\n";
    g_assert_true(g_file_set_contents(script, source, -1, NULL));
    g_assert_cmpint(g_chmod(script, 0700), ==, 0);
    run_case(argv[1], dir, "normal", "K", "KEPT", TRUE);
    run_case(argv[1], dir, "normal", "R", "REVERTED", TRUE);
    run_case(argv[1], dir, "normal", "", "REVERTED", TRUE);
    run_case(argv[1], dir, "normal", NULL, "REVERTED", TRUE);
    run_case(argv[1], dir, "fail", NULL, "APPLY_FAILED_RESTORED:xrandr-rejected", FALSE);
    run_case(argv[1], dir, "partial", NULL, "APPLY_FAILED_RESTORED:xrandr-rejected", FALSE);
    run_case(argv[1], dir, "hang", NULL, "APPLY_FAILED_RESTORED:timeout", FALSE);
    run_case(argv[1], dir, "hang", "", "APPLY_FAILED_RESTORED:parent-disconnected", FALSE);
    parent_crash(argv[1], dir, "normal");
    parent_crash(argv[1], dir, "hang");
    g_assert_cmpint(g_unlink(script), ==, 0); g_assert_cmpint(g_rmdir(dir), ==, 0);
    return 0;
}
