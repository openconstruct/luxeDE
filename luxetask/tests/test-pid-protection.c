/*
 * Copyright (C) 2026 Jerry Howell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* Headless regression test for the pid-protection policy
 * (bible verification step 3): task_pid_is_protected() must protect
 * only pid <= 1, ourselves, and our own ancestor chain. In particular
 * an unrelated live process (here: our own pipe-blocked child, which
 * is a descendant, not an ancestor) must NOT be protected.
 *
 * Hermetic: only our own child receives SIGCONT; no process is killed. The child
 * blocks reading a pipe and exits on its own when the parent closes
 * the write end (EOF); the parent reaps it with waitpid(). */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include "xfce-taskmanager-linux.h"

/* Link stubs for symbols pulled in via xfce-taskmanager-linux.c that
 * are defined by main.c/utils.c in the real binary. */
gboolean show_full_path = FALSE;
int page_size = 4;
static guint errors_shown;
void show_error(const char *format, ...)
{
    (void)format;
    errors_shown++;
}

static int failures = 0;

static void check(gboolean cond, const char *msg)
{
    if(cond)
        printf("ok: %s\n", msg);
    else
    {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    }
    fflush(stdout);
}

int main(void)
{
    /* PID 1 (init) is always protected. */
    check(task_pid_is_protected(1) == TRUE, "pid 1 is protected");

    /* We ourselves are always protected. */
    check(task_pid_is_protected(getpid()) == TRUE, "getpid() is protected");
    check(task_pid_is_protected(getppid()) == TRUE, "direct parent is protected");
    struct task own;
    get_task_details(getpid(), &own);
    errno = 0;
    int own_priority = getpriority(PRIO_PROCESS, getpid());
    gboolean own_priority_ok = errno == 0;
    check(own_priority_ok, "read own priority");
    if (own_priority_ok && own_priority < 19) {
        guint before = errors_shown;
        set_priority_to_task(getpid(), own.start_time, own_priority + 1);
        check(errors_shown == before + 1, "protected self priority is rejected");
        check(getpriority(PRIO_PROCESS, getpid()) == own_priority, "own priority is unchanged");
    }

    /* A live child is a descendant, not an ancestor: unprotected. */
    {
        int fds[2] = { -1, -1 };
        pid_t child;

        if(pipe(fds) != 0)
        {
            fprintf(stderr, "FAIL: pipe: %s\n", g_strerror(errno));
            return 1;
        }
        child = fork();
        if(child < 0)
        {
            fprintf(stderr, "FAIL: fork: %s\n", g_strerror(errno));
            close(fds[0]);
            close(fds[1]);
            return 1;
        }
        if(child == 0)
        {
            /* Child: block until the parent closes the pipe, then
             * exit voluntarily (no signal involved). */
            char buf;
            ssize_t n;
            close(fds[1]);
            do {
                n = read(fds[0], &buf, 1);
            } while(n > 0);
            close(fds[0]);
            _exit(n == 0 ? 0 : 1);
        }
        /* Parent: the child is alive (blocked in read) here. */
        check(task_pid_is_protected(child) == FALSE, "live child is not protected");
        struct task details;
        get_task_details(child, &details);
        check(details.pid == child, "read child identity");
        check(task_open_handle(child, details.start_time + 1) < 0, "reject stale identity");
        check(task_open_handle(getpid(), 0) < 0, "reject protected identity");
        errno = 0;
        int priority = getpriority(PRIO_PROCESS, child);
        gboolean priority_ok = errno == 0;
        check(priority_ok, "read child priority");
        if (priority_ok && priority < 19) {
            guint before = errors_shown;
            set_priority_to_task(child, details.start_time + 1, priority + 1);
            check(errors_shown == before + 1, "stale child priority is rejected");
            check(getpriority(PRIO_PROCESS, child) == priority, "stale child priority unchanged");
            before = errors_shown;
            set_priority_to_task(child, details.start_time, priority + 1);
            check(errors_shown == before, "permitted child priority succeeds");
            check(getpriority(PRIO_PROCESS, child) == priority + 1, "child priority updated");
        }
        int handle = task_open_handle(child, details.start_time);
        check(handle >= 0, "open identity-bound child handle");
        if (handle >= 0) {
            send_signal_to_task(handle, SIGNAL_CONT);
            close(handle);
        }
        check(SIGNAL_TERM == SIGTERM, "Term means SIGTERM");

        /* Let the child observe EOF and exit on its own, then reap it. */
        close(fds[1]);
        close(fds[0]);
        {
            int status = 0;
            pid_t w;
            do {
                w = waitpid(child, &status, 0);
            } while(w < 0 && errno == EINTR);
            if(w != child)
            {
                fprintf(stderr, "FAIL: waitpid: %s\n", g_strerror(errno));
                failures++;
            }
            else if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                fprintf(stderr, "FAIL: child exited uncleanly (status %d)\n", status);
                failures++;
            }
            else
                printf("ok: child reaped cleanly\n");
        }
    }

    if(failures == 0)
        printf("pid-protection: all checks passed\n");
    else
        fprintf(stderr, "pid-protection: %d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
