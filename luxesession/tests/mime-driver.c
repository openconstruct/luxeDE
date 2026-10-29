/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "mime-defaults.h"
#include <string.h>
int main(int argc, char **argv)
{
    g_autoptr(GError) error = NULL;
    gboolean ok;
    if (argc == 2)
        ok = lxsession_set_browser(argv[1], &error);
    else if (argc >= 4 && g_str_equal(argv[1], "--mime"))
        ok = lxsession_set_mime_defaults((const gchar * const *)(argv + 3),
                                       argc - 3, argv[2], &error);
    else
        return 2;
    if (!ok) {
        g_assert_nonnull(error);
        g_printerr("%s\n", error->message);
    }
    return ok ? 0 : 1;
}
