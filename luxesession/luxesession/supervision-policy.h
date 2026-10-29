/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXESESSION_SUPERVISION_POLICY_H
#define LUXESESSION_SUPERVISION_POLICY_H

#include <glib.h>

#define LUXESESSION_HEALTHY_RUNTIME_USEC (60 * G_TIME_SPAN_SECOND)

static inline guint
lxsession_restart_delay_seconds(guint crash_count)
{
    if (crash_count == 0)
        return 1;
    return 1u << MIN(crash_count - 1, 4u);
}

#endif
