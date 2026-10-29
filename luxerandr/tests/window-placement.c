/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/window-placement.h"

int main(void)
{
    const LuxeRandRRect l_shape[]={{0,0,1000,700},{1000,500,800,600}};
    gint x=0,y=0;
    g_assert_true(luxerandr_recovery_position(100,100,500,400,l_shape,2,120,32,&x,&y));
    g_assert_cmpint(x,==,100);g_assert_cmpint(y,==,100);
    g_assert_true(luxerandr_recovery_position(1050,100,500,400,l_shape,2,120,32,&x,&y));
    g_assert_true(luxerandr_titlebar_fits(x,y,500,400,&l_shape[0],120,32) ||
                  luxerandr_titlebar_fits(x,y,500,400,&l_shape[1],120,32));
    g_assert_false(x==1050 && y==100);
    g_assert_false(luxerandr_recovery_position(0,0,100,100,NULL,0,120,32,&x,&y));
    g_assert_false(luxerandr_recovery_position(0,0,0,100,l_shape,2,120,32,&x,&y));
    return 0;
}
