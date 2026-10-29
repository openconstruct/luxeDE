/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxe-renderer.h"
#include "renderer-init.h"

void luxepanel_init_gtk(void)
{
    /* Share the tested Cairo/graphics policy with the other native frontends. */
    luxe_gtk_init();
}
