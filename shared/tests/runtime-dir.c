/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxe-runtime-dir.h"

int main(void)
{
    g_autofree char *base = g_dir_make_tmp("luxe-runtime-test-XXXXXX", NULL);
    g_assert_nonnull(base);
    g_autofree char *path = g_strdup_printf("%s/%lu", base, (unsigned long)getuid());
    g_unsetenv("XDG_RUNTIME_DIR");
    g_assert_false(luxe_restore_runtime_dir_from(base));
    g_assert_cmpint(g_mkdir(path, 0755), ==, 0);
    g_assert_false(luxe_restore_runtime_dir_from(base));
    g_assert_cmpint(g_chmod(path, 0700), ==, 0);
    g_assert_true(luxe_restore_runtime_dir_from(base));
    g_assert_cmpstr(g_getenv("XDG_RUNTIME_DIR"), ==, path);
    g_setenv("XDG_RUNTIME_DIR", "/custom/runtime", TRUE);
    g_assert_true(luxe_restore_runtime_dir_from(base));
    g_assert_cmpstr(g_getenv("XDG_RUNTIME_DIR"), ==, "/custom/runtime");
    g_assert_cmpint(g_rmdir(path), ==, 0);
    g_assert_cmpint(symlink("/tmp", path), ==, 0);
    g_unsetenv("XDG_RUNTIME_DIR");
    g_assert_false(luxe_restore_runtime_dir_from(base));
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(base), ==, 0);
    return 0;
}
