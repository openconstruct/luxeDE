/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/permissions-gtk4.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>
static guint32 mode(const char *path)
{
    struct stat info; g_assert_cmpint(stat(path, &info), ==, 0); return info.st_mode & 07777;
}
int main(void)
{
    GError *error = NULL;
    g_autofree char *dir = g_dir_make_tmp("pcmanager-permissions-XXXXXX", &error); g_assert_no_error(error);
    g_autofree char *path = g_build_filename(dir, "script", NULL);
    g_autofree char *renamed = g_build_filename(dir, "renamed", NULL);
    g_autofree char *other = g_build_filename(dir, "other", NULL);
    g_assert_true(g_file_set_contents(path, "#!/bin/sh\nexit 0\n", -1, &error)); g_assert_no_error(error);
    g_assert_true(g_file_set_contents(other, "keep", -1, &error)); g_assert_no_error(error);
    g_assert_cmpint(g_chmod(path, 0000), ==, 0); g_assert_cmpint(g_chmod(other, 0644), ==, 0);
    g_autoptr(GFile) file = g_file_new_for_path(path);
    PcmgrPermissionTarget *target = pcmgr_permission_target_new(file, &error); g_assert_no_error(error); g_assert_nonnull(target);
    g_assert_true(pcmgr_permission_target_apply(target, 0700, 0700, &error)); g_assert_no_error(error);
    g_assert_cmpuint(mode(path), ==, 0700);
    /* Preserve a concurrent change to unrelated bits, including special bits. */
    g_assert_cmpint(g_chmod(path, 04720), ==, 0);
    g_assert_true(pcmgr_permission_target_apply(target, 0100, 0, &error)); g_assert_no_error(error);
    g_assert_cmpuint(mode(path), ==, 04620);
    /* A replaced pathname must never change the symlink target. */
    g_assert_cmpint(g_rename(path, renamed), ==, 0); g_assert_cmpint(symlink(other, path), ==, 0);
    g_assert_true(pcmgr_permission_target_apply(target, 0100, 0100, &error)); g_assert_no_error(error);
    g_assert_cmpuint(mode(renamed), ==, 04720); g_assert_cmpuint(mode(other), ==, 0644);
    g_assert_null(pcmgr_permission_target_new(file, &error)); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED); g_clear_error(&error);
    g_assert_false(pcmgr_permission_target_apply(target, 04000, 0, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&error);
    g_assert_cmpint(g_unlink(renamed), ==, 0);
    g_assert_false(pcmgr_permission_target_apply(target, 0100, 0, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND); g_clear_error(&error);
    pcmgr_permission_target_free(target);
    /* Directory changes are not recursive. */
    g_autoptr(GFile) folder = g_file_new_for_path(dir);
    target = pcmgr_permission_target_new(folder, &error); g_assert_no_error(error);
    g_assert_true(pcmgr_permission_target_apply(target, 0020, 0020, &error)); g_assert_no_error(error);
    g_assert_cmpuint(mode(other), ==, 0644); pcmgr_permission_target_free(target);
    g_autoptr(GFile) remote = g_file_new_for_uri("sftp://example.invalid/test");
    g_assert_null(pcmgr_permission_target_new(remote, &error)); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED); g_clear_error(&error);
    g_assert_cmpint(g_unlink(path), ==, 0); g_assert_cmpint(g_unlink(other), ==, 0); g_assert_cmpint(g_rmdir(dir), ==, 0);
    g_print("PASS: executable permissions, mode 000, preserved bits, safe rename/symlink replacement, removed files, nonrecursive directories and remote refusal\n");
    return 0;
}
