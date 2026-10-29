/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "profile.h"

#include <unistd.h>

int main(void)
{
    guint64 start = 0;
    g_assert_true(luxe_profile_component_valid("LXDE"));
    g_assert_true(luxe_profile_component_valid("X-Luxe_2"));
    g_assert_false(luxe_profile_component_valid(NULL));
    g_assert_false(luxe_profile_component_valid(""));
    g_assert_false(luxe_profile_component_valid("../LXDE"));
    g_assert_false(luxe_profile_component_valid("LXDE/other"));
    g_assert_false(luxe_profile_component_valid("LXDE.name"));
    g_assert_true(luxe_process_start_time((gint)getpid(), &start));
    g_assert_cmpuint(start, >, 0);
    g_assert_false(luxe_process_start_time(-1, &start));
    return 0;
}
