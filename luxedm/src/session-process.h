/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <chrono>
#include <sys/types.h>

enum class SessionGroupStop { Invalid, Gone, Graceful, Forced, Failed };

// Stop only the session's process group. The leader is kept unreaped by the
// caller while this runs, preventing its PID/PGID from being recycled.
SessionGroupStop stopSessionProcessGroup(pid_t pgid,
                                         std::chrono::milliseconds grace);
