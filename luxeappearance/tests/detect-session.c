/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#define main luxeappearance_entry
#include "../src/main.c"
#undef main
int main(void) {
    gtk_init(); check_luxesession();
    g_print("LuxeSession detected: %s\n", under_luxesession ? "yes" : "no");
    return under_luxesession ? 0 : 1;
}
