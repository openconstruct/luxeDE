/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
 * Independent display transaction. stdin is the parent-liveness/decision
 * channel; stdout reports state. Commands are tokenized, never run in a shell.
 */
#define _GNU_SOURCE
#include <glib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef DISPLAY_GUARD_APPLY_MS
#define DISPLAY_GUARD_APPLY_MS 5000
#endif
#ifndef DISPLAY_GUARD_CONFIRM_MS
#define DISPLAY_GUARD_CONFIRM_MS 15000
#endif
#ifndef DISPLAY_GUARD_RESTORE_MS
#define DISPLAY_GUARD_RESTORE_MS 5000
#endif

static int set_command_process_group(pid_t pid, pid_t group)
{
#ifdef DISPLAY_GUARD_TEST_FAIL_SETPGID
    (void)pid;
    (void)group;
    errno = EPERM;
    return -1;
#else
    return setpgid(pid, group);
#endif
}

static void report(const char *message)
{
    gsize length = strlen(message), offset = 0;
    while (offset < length) {
        ssize_t n = write(STDOUT_FILENO, message + offset, length - offset);
        if (n > 0) offset += (gsize)n;
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
}

/* The child is kept unreaped until its process group is killed, preventing
 * PID/group reuse before the signal. As a subreaper, the guard can also wait
 * for ordinary descendants in that group before starting restoration. */
typedef enum { COMMAND_SPAWN_FAILED, COMMAND_GROUP_FAILED, COMMAND_TIMEOUT,
               COMMAND_INTERRUPTED, COMMAND_EXIT_FAILED, COMMAND_OK,
               COMMAND_UNREAPED } CommandResult;
static CommandResult run_command(char **argv, int timeout_ms, gboolean watch_parent)
{
    pid_t child = fork();
    if (child < 0) return COMMAND_SPAWN_FAILED;
    if (child == 0) {
        if (set_command_process_group(0, 0) < 0) _exit(126);
        int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); dup2(devnull, STDOUT_FILENO); dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp(argv[0], argv);
        _exit(127);
    }
    if (set_command_process_group(child, child) < 0 && getpgid(child) != child) {
        kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR);
        return COMMAND_GROUP_FAILED;
    }
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
    gboolean completed = FALSE, interrupted = FALSE;
    siginfo_t info = {0};
    while (g_get_monotonic_time() < deadline) {
        if (waitid(P_PID, child, &info, WEXITED | WNOHANG | WNOWAIT) == 0 && info.si_pid == child) {
            completed = TRUE;
            break;
        }
        if (watch_parent) {
            struct pollfd input = {.fd=STDIN_FILENO, .events=POLLIN | POLLHUP};
            int ready = poll(&input, 1, 25);
            if (ready > 0 && (input.revents & (POLLIN | POLLHUP | POLLERR))) {
                char decision;
                ssize_t n = read(STDIN_FILENO, &decision, 1);
                if (n <= 0 || decision != 0) { interrupted = TRUE; break; }
            }
        } else g_usleep(25000);
    }
    kill(-child, SIGKILL);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR);
    /* No descendant in the command's group may run while restore starts. */
    gint64 reap_deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    for (;;) {
        int extra;
        pid_t result = waitpid(-child, &extra, WNOHANG);
        if (result > 0) continue;
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 && errno == ECHILD) break;
        if (g_get_monotonic_time() >= reap_deadline) return COMMAND_UNREAPED;
        g_usleep(10000);
    }
    if (!completed && !interrupted) return COMMAND_TIMEOUT;
    if (interrupted) return COMMAND_INTERRUPTED;
    return completed && !interrupted && WIFEXITED(status) && WEXITSTATUS(status) == 0
        ? COMMAND_OK : COMMAND_EXIT_FAILED;
}

static const char *failure_code(CommandResult result)
{
    switch (result) {
    case COMMAND_SPAWN_FAILED: return "spawn";
    case COMMAND_GROUP_FAILED: return "process-group";
    case COMMAND_TIMEOUT: return "timeout";
    case COMMAND_INTERRUPTED: return "parent-disconnected";
    case COMMAND_EXIT_FAILED: return "xrandr-rejected";
    case COMMAND_UNREAPED: return "unsafe-child";
    case COMMAND_OK: return "success";
    }
    return "unknown";
}

static gboolean parse_command(const char *command, char ***argv)
{
    int argc = 0;
    if (!g_shell_parse_argv(command, &argc, argv, NULL) || argc < 1 || g_strcmp0((*argv)[0], "xrandr")) {
        g_strfreev(*argv); *argv = NULL; return FALSE;
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    g_auto(GStrv) apply = NULL;
    g_auto(GStrv) restore = NULL;
    signal(SIGPIPE, SIG_IGN);
    if (argc != 3 || !parse_command(argv[1], &apply) || !parse_command(argv[2], &restore) ||
        prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) {
        report("INVALID\n"); return 2;
    }
    CommandResult apply_result = run_command(apply, DISPLAY_GUARD_APPLY_MS, TRUE);
    if (apply_result == COMMAND_UNREAPED) { report("UNSAFE\n"); return 1; }
    gboolean applied = apply_result == COMMAND_OK;
    if (applied) {
        report("READY\n");
        struct pollfd input = {.fd=STDIN_FILENO, .events=POLLIN | POLLHUP};
        gint64 deadline = g_get_monotonic_time() + (gint64)DISPLAY_GUARD_CONFIRM_MS * 1000;
        while (g_get_monotonic_time() < deadline) {
            int wait_ms = (int)MIN((deadline - g_get_monotonic_time()) / 1000, 100);
            int ready = poll(&input, 1, MAX(wait_ms, 0));
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) break;
            if (ready > 0 && (input.revents & (POLLIN | POLLHUP | POLLERR))) {
                char decision;
                if (read(STDIN_FILENO, &decision, 1) == 1 && decision == 'K') {
                    report("KEPT\n"); return 0;
                }
                break;
            }
        }
    }
    gboolean restored = run_command(restore, DISPLAY_GUARD_RESTORE_MS, FALSE) == COMMAND_OK;
    if (restored && applied) report("REVERTED\n");
    else if (restored) {
        char message[96];
        g_snprintf(message,sizeof message,"APPLY_FAILED_RESTORED:%s\n",failure_code(apply_result));
        report(message);
    } else report("RESTORE_FAILED\n");
    return restored ? 0 : 1;
}
