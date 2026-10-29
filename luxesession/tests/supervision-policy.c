/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "supervision-policy.h"
#include <assert.h>

int main(void)
{
    static const guint expected[] = { 1, 1, 2, 4, 8, 16, 16, 16 };
    for (guint i = 0; i < G_N_ELEMENTS(expected); i++)
        assert(lxsession_restart_delay_seconds(i) == expected[i]);
    assert(LUXESESSION_HEALTHY_RUNTIME_USEC == 60 * G_TIME_SPAN_SECOND);
    return 0;
}
