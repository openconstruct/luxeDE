/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#define main idle_program_main
#include "../src/idle.c"
#undef main
#include <glib/gstdio.h>

static int query_fixture(void *display, unsigned long root, XScreenSaverInfo *info)
{
  (void)display; (void)root;
  info->state = 1;
  info->idle = 120000;
  return 1;
}
int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  IdleMonitor monitor = {0};
  idle_monitor_schedule(&monitor);
  g_assert_cmpuint(monitor.poll_id, ==, 0);
  monitor.lock_enabled = TRUE;
  idle_monitor_schedule(&monitor);
  guint poll_id = monitor.poll_id;
  g_assert_cmpuint(poll_id, !=, 0);
  idle_monitor_schedule(&monitor);
  g_assert_cmpuint(monitor.poll_id, ==, poll_id);
  monitor.lock_enabled = FALSE;
  idle_monitor_schedule(&monitor);
  g_assert_cmpuint(monitor.poll_id, ==, 0);
  g_assert_null(g_main_context_find_source_by_id(NULL, poll_id));
  monitor.lock_enabled = TRUE;
  idle_monitor_schedule(&monitor);
  g_assert_cmpuint(monitor.poll_id, !=, 0);
  g_source_remove(monitor.poll_id);
  IdleSource source;
  idle_source_open(&source);
  g_assert_true(source.usable);
  g_assert_cmpint(idle_source_seconds(&source), >=, 0);
  idle_source_close(&source);
  g_assert_null(source.info);
  g_assert_null(source.display);
  g_assert_null(source.xlib_handle);
  idle_source_close(&source);
  XScreenSaverInfo info = {0};
  source = (IdleSource) {.usable = TRUE, .info = &info, .query_info = query_fixture};
  g_assert_cmpint(idle_source_seconds(&source), ==, 120);
  int fd = idle_acquire_single_instance();
  g_assert_cmpint(fd, >=, 0);
  g_assert_true(fcntl(fd, F_GETFD) & FD_CLOEXEC);
  g_assert_cmpint(idle_acquire_single_instance(), ==, -1);
  close(fd);
  fd = idle_acquire_single_instance();
  g_assert_cmpint(fd, >=, 0);
  close(fd);
  g_autofree char *path = g_build_filename(g_get_user_runtime_dir(), "luxepower-idle.lock", NULL);
  g_assert_cmpint(g_unlink(path), ==, 0);
  g_assert_cmpint(symlink("/dev/null", path), ==, 0);
  g_assert_cmpint(idle_acquire_single_instance(), ==, -1);
  g_unlink(path);
  return 0;
}
