/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "safe-file.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    gchar *directory = g_dir_make_tmp("luxesession-safe-file-XXXXXX", NULL);
    gchar *path;
    gchar *outside;
    gchar *link_path;
    GError *error = NULL;
    gchar *contents = NULL;
    gsize length = 0;
    struct stat st;

    g_assert_nonnull(directory);
    path = g_build_filename(directory, "settings.conf", NULL);
    outside = g_build_filename(directory, "outside.conf", NULL);
    link_path = g_build_filename(directory, "linked.conf", NULL);

    g_assert_true(lxsession_atomic_write_file(path, "first\n", 6, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_cmpstr(contents, ==, "first\n");
    g_assert_cmpuint(length, ==, 6);
    g_clear_pointer(&contents, g_free);

    g_assert_cmpint(g_chmod(path, 0640), ==, 0);
    g_assert_true(lxsession_atomic_write_file(path, "second\n", 7, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_stat(path, &st), ==, 0);
    g_assert_cmpint(st.st_mode & 0777, ==, 0640);

    g_assert_true(g_file_set_contents(outside, "outside\n", -1, &error));
    g_assert_no_error(error);
    g_assert_cmpint(symlink(outside, link_path), ==, 0);
    g_assert_false(lxsession_atomic_write_file(link_path, "overwrite\n", 10, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_true(g_file_get_contents(outside, &contents, &length, &error));
    g_assert_cmpstr(contents, ==, "outside\n");
    g_clear_pointer(&contents, g_free);

    g_assert_cmpint(g_unlink(link_path), ==, 0);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(mkfifo(path, 0600), ==, 0);
    g_assert_false(lxsession_atomic_write_file(path, "fifo\n", 5, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);

    g_unlink(path);
    g_unlink(outside);
    g_rmdir(directory);
    g_free(link_path);
    g_free(outside);
    g_free(path);
    g_free(directory);
    return 0;
}
