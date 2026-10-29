/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../plugins/dclock-schedule.h"
int main(void)
{
    g_assert_cmpuint(dclock_boundary_delay(0,0,TRUE),==,60000);
    g_assert_cmpuint(dclock_boundary_delay(59,0,TRUE),==,1000);
    g_assert_cmpuint(dclock_boundary_delay(59,999999,TRUE),==,1);
    g_assert_cmpuint(dclock_boundary_delay(20,500000,TRUE),==,39500);
    g_assert_cmpuint(dclock_boundary_delay(-1,500000,TRUE),==,500);
    g_assert_cmpuint(dclock_boundary_delay(59,0,FALSE),==,1000);
    g_assert_cmpuint(dclock_boundary_delay(59,999999,FALSE),==,1);
    return 0;
}
