/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <glib/gstdio.h>
#define LUXERANDR_QUERY_TIMEOUT "1s"
#define main randr_application_main
#include "../src/luxerandr.c"
#undef main

static void write_xrandr_fixture(const char *path, const char *script)
{
    g_assert_true(g_file_set_contents(path, script, -1, NULL));
    /* GLib versions can replace the file on write and drop its old mode. */
    g_assert_cmpint(g_chmod(path, 0700), ==, 0);
    g_assert_cmpint(g_access(path, X_OK), ==, 0);
}

int main(void)
{
    gtk_init();
    g_autofree char *dir = g_dir_make_tmp("luxerandr-query-XXXXXX", NULL);
    g_assert_nonnull(dir);
    g_autofree char *path = g_build_filename(dir, "xrandr", NULL);
    write_xrandr_fixture(path, "#!/bin/sh\nexec /bin/sleep 5\n");
    g_autofree char *old_path = g_strdup(g_getenv("PATH"));
    g_setenv("PATH", dir, TRUE);
    gint64 start = g_get_monotonic_time();
    g_autofree char *output = query_xrandr_output(NULL);
    g_assert_null(output);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 3 * G_TIME_SPAN_SECOND);
    write_xrandr_fixture(path,
        "#!/bin/sh\nprintf 'DP-1 connected 800x600+0+0\\n   800x600 60.00*+\\n'\n");
    output = query_xrandr_output(NULL);
    g_assert_nonnull(output);
    g_assert_true(parse_xrandr_output(output));
    g_assert_cmpuint(g_slist_length(monitors), ==, 1);
    write_xrandr_fixture(path,
        "#!/bin/sh\nexec /usr/bin/head -c 1100000 /dev/zero\n");
    start = g_get_monotonic_time();
    g_clear_pointer(&output, g_free);
    output = query_xrandr_output(NULL);
    g_assert_null(output);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 3 * G_TIME_SPAN_SECOND);
    g_assert_cmpuint(g_slist_length(monitors), ==, 1);
    free_monitors();
    if (old_path) g_setenv("PATH", old_path, TRUE); else g_unsetenv("PATH");
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    return 0;
}
