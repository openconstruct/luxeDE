/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "session-process.h"

#include <sys/prctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

struct GroupChildren { pid_t leader; pid_t member; };

static GroupChildren launch_group(bool ignore_termination)
{
    int pipefd[2];
    require(pipe(pipefd) == 0, "pipe failed");
    pid_t leader = fork();
    require(leader >= 0, "fork failed");
    if (leader == 0) {
        close(pipefd[0]);
        if (setpgid(0, 0) != 0) _exit(20);
        pid_t member = fork();
        if (member < 0) _exit(21);
        if (member == 0) {
            close(pipefd[1]);
            if (ignore_termination) { signal(SIGHUP, SIG_IGN); signal(SIGTERM, SIG_IGN); }
            for (;;) pause();
        }
        if (write(pipefd[1], &member, sizeof member) != sizeof member) _exit(22);
        close(pipefd[1]);
        _exit(0); // Stop code receives the exited, unreaped group leader.
    }
    close(pipefd[1]);
    pid_t member = -1;
    require(read(pipefd[0], &member, sizeof member) == sizeof member, "member handshake failed");
    close(pipefd[0]);
    siginfo_t info{};
    while (waitid(P_PID, static_cast<id_t>(leader), &info, WEXITED | WNOWAIT) != 0 && errno == EINTR) {}
    require(info.si_pid == leader, "session leader did not exit");
    // Keep the group leader unreaped until process-group cleanup completes.
    require(info.si_code == CLD_EXITED && info.si_status == 0, "group leader setup failed");
    return {leader, member};
}

static void reap(pid_t pid)
{
    int status = 0;
    pid_t result;
    do { result = waitpid(pid, &status, 0); } while (result < 0 && errno == EINTR);
    require(result == pid, "failed to reap isolated child");
}

int main() try
{
    require(prctl(PR_SET_CHILD_SUBREAPER, 1) == 0, "cannot enable isolated child reaping");
    require(stopSessionProcessGroup(1, std::chrono::milliseconds(0)) == SessionGroupStop::Invalid,
            "unsafe process group was accepted");

    GroupChildren normal = launch_group(false);
    require(stopSessionProcessGroup(normal.leader, std::chrono::seconds(1)) == SessionGroupStop::Graceful,
            "normal descendants were not stopped gracefully");
    reap(normal.member);
    reap(normal.leader);

    pid_t unrelated = fork();
    require(unrelated >= 0, "unrelated child fork failed");
    if (unrelated == 0) { setpgid(0, 0); signal(SIGTERM, SIG_IGN); for (;;) pause(); }
    GroupChildren stubborn = launch_group(true);
    require(stopSessionProcessGroup(stubborn.leader, std::chrono::milliseconds(150)) == SessionGroupStop::Forced,
            "TERM-ignoring session descendant survived the deadline");
    reap(stubborn.member);
    reap(stubborn.leader);
    require(kill(unrelated, 0) == 0, "cleanup escaped its session process group");
    kill(unrelated, SIGKILL);
    reap(unrelated);
    std::cout << "PASS: graceful session cleanup avoids killing zombie leaders; TERM-resistant descendants are killed, unrelated groups survive.\n";
    return EXIT_SUCCESS;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
}
