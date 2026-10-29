/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "session-process.h"

#include <dirent.h>
#include <signal.h>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <time.h>

namespace {

bool has_live_group_member(pid_t pgid)
{
    DIR *proc = opendir("/proc");
    if (!proc) return killpg(pgid, 0) == 0 || errno == EPERM;
    bool live = false;
    while (dirent *entry = readdir(proc)) {
        char *end = nullptr;
        long pid = strtol(entry->d_name, &end, 10);
        if (!end || *end != '\0' || pid <= 0) continue;
        std::ifstream stat(std::string("/proc/") + entry->d_name + "/stat");
        std::string line;
        if (!std::getline(stat, line)) continue;
        const std::size_t close = line.rfind(')');
        if (close == std::string::npos || close + 2 >= line.size()) continue;
        std::istringstream fields(line.substr(close + 2));
        char state = 0;
        long parent = 0, group = 0;
        if (fields >> state >> parent >> group && group == pgid &&
            state != 'Z' && state != 'X') {
            live = true;
            break;
        }
    }
    closedir(proc);
    return live;
}

void pause_for(std::chrono::milliseconds duration)
{
    timespec remaining{duration.count() / 1000,
                       (duration.count() % 1000) * 1000000};
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {}
}

} // namespace

SessionGroupStop stopSessionProcessGroup(pid_t pgid,
                                         std::chrono::milliseconds grace)
{
    if (pgid <= 1 || grace.count() < 0) return SessionGroupStop::Invalid;
    if (killpg(pgid, SIGHUP) != 0 && errno == ESRCH)
        return SessionGroupStop::Gone;
    if (killpg(pgid, SIGTERM) != 0) {
        return errno == ESRCH ? SessionGroupStop::Gone : SessionGroupStop::Failed;
    }

    const auto deadline = std::chrono::steady_clock::now() + grace;
    while (has_live_group_member(pgid)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            if (killpg(pgid, SIGKILL) == 0 || errno == ESRCH)
                return SessionGroupStop::Forced;
            return SessionGroupStop::Failed;
        }
        pause_for(std::chrono::milliseconds(50));
    }
    return SessionGroupStop::Graceful;
}
