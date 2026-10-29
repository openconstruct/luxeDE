/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPANEL_DCLOCK_SCHEDULE_H
#define LUXEPANEL_DCLOCK_SCHEDULE_H
#include <glib.h>

/* Ceil to milliseconds so the timer cannot fire just before the boundary. */
static guint dclock_boundary_delay(gint64 seconds,gint microseconds,gboolean minute)
{
    gint64 remainder=seconds%60;
    if(remainder<0)remainder+=60;
    gint64 whole_seconds=minute ? 60-remainder : 1;
    gint64 microseconds_left=whole_seconds*G_USEC_PER_SEC-CLAMP(microseconds,0,999999);
    return (guint)((microseconds_left+999)/1000);
}
#endif
