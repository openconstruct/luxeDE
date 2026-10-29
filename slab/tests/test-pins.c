/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/pins.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>

int main(void)
{
    g_autofree char *dir = g_dir_make_tmp("slab-pins-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(dir, "pins.txt", NULL);
    GError *error = NULL;
    g_autoptr(GPtrArray) defaults = slab_pins_load(path, &error);
    g_assert_no_error(error); g_assert_cmpuint(defaults->len, ==, 3);
    g_assert_false(slab_id_valid("../bad.desktop"));
    g_assert_false(slab_id_valid("foo\nbar.desktop"));
    g_assert_false(slab_id_valid("https://bad.desktop"));
    g_assert_true(slab_id_valid("org.luxe.Test.desktop"));
    g_autoptr(GPtrArray) moved = slab_pins_move(defaults, "pcmanager.desktop", 0);
    g_assert_cmpuint(moved->len, ==, 3);
    g_assert_cmpstr(g_ptr_array_index(moved, 0), ==, "pcmanager.desktop");
    g_assert_true(slab_pins_save(path, moved, &error)); g_assert_no_error(error);
    g_autoptr(GPtrArray) restored = slab_pins_load(path, &error);
    g_assert_no_error(error); g_assert_cmpuint(restored->len, ==, 3);
    g_assert_cmpstr(g_ptr_array_index(restored, 0), ==, "pcmanager.desktop");
    g_assert_true(g_file_set_contents(path, "", 0, &error));
    g_autoptr(GPtrArray) empty = slab_pins_load(path, &error);
    g_assert_no_error(error); g_assert_cmpuint(empty->len, ==, 0);
    g_assert_true(g_file_set_contents(path, "bad/path.desktop\n", -1, &error));
    g_assert_null(slab_pins_load(path, &error)); g_assert_nonnull(error); g_clear_error(&error);
    g_autofree char *large = g_strnfill(65537, 'a');
    g_assert_true(g_file_set_contents(path, large, 65537, &error));
    g_assert_null(slab_pins_load(path, &error)); g_assert_nonnull(error); g_clear_error(&error);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(symlink("missing-target", path), ==, 0);
    g_assert_null(slab_pins_load(path, &error)); g_clear_error(&error);
    g_assert_false(slab_pins_save(path, defaults, &error)); g_clear_error(&error);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_true(slab_pins_save(path, defaults, &error)); g_assert_no_error(error);
    g_autoptr(GPtrArray) stale = slab_pins_load(path, &error); g_assert_no_error(error);
    g_autoptr(GPtrArray) first_edit = slab_pins_move(stale, "pcmanager.desktop", 0);
    g_assert_true(slab_pins_save_if_unchanged(path, stale, first_edit, &error)); g_assert_no_error(error);
    g_autoptr(GPtrArray) stale_edit = slab_pins_move(stale, "luxeterm.desktop", 2);
    g_assert_false(slab_pins_save_if_unchanged(path, stale, stale_edit, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY); g_clear_error(&error);
    g_autoptr(GPtrArray) after_conflict = slab_pins_load(path, &error); g_assert_no_error(error);
    g_assert_cmpstr(g_ptr_array_index(after_conflict, 0), ==, "pcmanager.desktop");
    g_autofree char *lock_path = g_strconcat(path, ".lock", NULL);
    int lock_fd = g_open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    g_assert_cmpint(lock_fd, >=, 0); g_assert_cmpint(flock(lock_fd, LOCK_EX), ==, 0);
    g_autoptr(GPtrArray) child_edit = slab_pins_move(after_conflict, "org.luxe.Settings.desktop", 0);
    pid_t child = fork(); g_assert_cmpint(child, >=, 0);
    if (child == 0) {
        GError *child_error = NULL;
        gboolean ok = slab_pins_save_if_unchanged(path, after_conflict, child_edit, &child_error);
        g_clear_error(&child_error); _exit(ok ? 0 : 1);
    }
    g_usleep(100000); int status = 0;
    g_assert_cmpint(waitpid(child, &status, WNOHANG), ==, 0); /* The sibling writer waits for the shared lock. */
    g_assert_cmpint(flock(lock_fd, LOCK_UN), ==, 0); close(lock_fd);
    g_assert_cmpint(waitpid(child, &status, 0), ==, child);
    g_assert_true(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    g_autoptr(GPtrArray) after_child = slab_pins_load(path, &error); g_assert_no_error(error);
    g_assert_cmpstr(g_ptr_array_index(after_child, 0), ==, "org.luxe.Settings.desktop");
    g_unlink(path); g_unlink(lock_path); g_rmdir(dir);
    return 0;
}
