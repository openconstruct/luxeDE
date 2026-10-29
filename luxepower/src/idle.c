/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "system.h"
#include "tlp.h"
#include "luxe-lock-request.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define POLL_SECONDS 10
#define REARM_COOLDOWN_SECONDS 30
#define LOCK_MINUTES_MIN 1
#define LOCK_MINUTES_MAX 1440

typedef struct {
  unsigned long window;
  int state;
  int kind;
  unsigned long til_or_since;
  unsigned long idle;
  unsigned long event_mask;
} XScreenSaverInfo;

typedef struct {
  void *xlib_handle;
  void *xss_handle;
  void *display;
  unsigned long root;
  XScreenSaverInfo *info;
  int (*query_info) (void *display, unsigned long drawable, XScreenSaverInfo *info);
  gboolean usable;
} IdleSource;

typedef struct {
  IdleSource idle;
  GSettings *settings;
  gboolean lock_enabled;
  int lock_minutes;
  int cooldown;
  gboolean locking;
  gboolean fallback_query_pending;
  gboolean shutting_down;
  guint active_workers;
  GCancellable *cancellable;
  GMainLoop *loop;
  guint poll_id;
} IdleMonitor;

static gboolean idle_poll (gpointer data);

/* Disabled locking needs settings events, not periodic X requests. Keep the
 * existing enabled cadence and cooldown semantics unchanged. */
static void
idle_monitor_schedule (IdleMonitor *monitor)
{
  if (monitor->lock_enabled && monitor->poll_id == 0)
    monitor->poll_id = g_timeout_add_seconds (POLL_SECONDS, idle_poll, monitor);
  else if (!monitor->lock_enabled && monitor->poll_id != 0)
    {
      g_source_remove (monitor->poll_id);
      monitor->poll_id = 0;
    }
}

static void
idle_source_close (IdleSource *source)
{
  if (source->xlib_handle != NULL && source->info != NULL)
    {
      int (*free_info) (void *) = dlsym (source->xlib_handle, "XFree");

      if (free_info != NULL)
        free_info (source->info);
    }
  source->info = NULL;

  if (source->display != NULL && source->xlib_handle != NULL)
    {
      void (*close_display) (void *) = dlsym (source->xlib_handle, "XCloseDisplay");

      if (close_display != NULL)
        close_display (source->display);
    }
  source->display = NULL;
  source->usable = FALSE;
  if (source->xss_handle) dlclose(source->xss_handle);
  if (source->xlib_handle) dlclose(source->xlib_handle);
  source->xss_handle = source->xlib_handle = NULL;
}

static void
idle_source_open (IdleSource *source)
{
  void *(*open_display) (const char *);
  unsigned long (*default_root) (void *display);
  XScreenSaverInfo *(*alloc_info) (void);
  int (*query_extension) (void *display, int *event_base, int *error_base);

  memset (source, 0, sizeof (*source));

  if (g_getenv ("DISPLAY") == NULL)
    return;

  source->xlib_handle = dlopen ("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
  source->xss_handle = dlopen ("libXss.so.1", RTLD_NOW | RTLD_LOCAL);
  if (source->xlib_handle == NULL || source->xss_handle == NULL)
    return;

  open_display = dlsym (source->xlib_handle, "XOpenDisplay");
  default_root = dlsym (source->xlib_handle, "XDefaultRootWindow");
  query_extension = dlsym (source->xss_handle, "XScreenSaverQueryExtension");
  alloc_info = dlsym (source->xss_handle, "XScreenSaverAllocInfo");
  source->query_info = dlsym (source->xss_handle, "XScreenSaverQueryInfo");

  if (open_display == NULL || default_root == NULL ||
      query_extension == NULL || alloc_info == NULL || source->query_info == NULL)
    return;

  source->display = open_display (NULL);
  if (source->display == NULL)
    return;

  {
    int event_base = 0;
    int error_base = 0;

    if (!query_extension (source->display, &event_base, &error_base))
      {
        idle_source_close (source);
        return;
      }
  }

  source->root = default_root (source->display);
  source->info = alloc_info ();
  source->usable = source->info != NULL;
}

static int
idle_source_seconds (IdleSource *source)
{
  if (source->usable)
    {
      if (!source->query_info (source->display, source->root, source->info))
        return -1;
      return MIN(source->info->idle / 1000, G_MAXINT);
    }

  return -1;
}

static GSettings *
idle_settings_open (void)
{
  GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
  GSettingsSchema *schema;

  if (source == NULL)
    return NULL;
  schema = g_settings_schema_source_lookup (source, LUXEPOWER_SCHEMA_ID, FALSE);
  if (schema == NULL)
    return NULL;
  g_settings_schema_unref (schema);
  return g_settings_new (LUXEPOWER_SCHEMA_ID);
}

static void
idle_monitor_update_settings (IdleMonitor *monitor)
{
  monitor->lock_enabled = g_settings_get_boolean (monitor->settings, "lock-enabled");
  monitor->lock_minutes = (int) g_settings_get_uint (monitor->settings, "lock-minutes");
  if (monitor->lock_minutes < LOCK_MINUTES_MIN)
    monitor->lock_minutes = LOCK_MINUTES_MIN;
  if (monitor->lock_minutes > LOCK_MINUTES_MAX)
    monitor->lock_minutes = LOCK_MINUTES_MAX;
  idle_monitor_schedule (monitor);
}

static void
idle_settings_changed (GSettings *settings, const char *key, gpointer data)
{
  (void) settings;
  (void) key;

  idle_monitor_update_settings (data);
}

static void
idle_lock_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void) source;
  (void) data;
  GError *error = NULL;
  if (luxe_lock_request (LUXELOCK_PATH, 10000, cancellable, &error))
    g_task_return_boolean (task, TRUE);
  else
    g_task_return_error (task, error);
}

static void
idle_lock_done (GObject *source, GAsyncResult *result, gpointer data)
{
  (void) source;

  IdleMonitor *monitor = data;

  g_autoptr(GError) error = NULL;
  if (!g_task_propagate_boolean(G_TASK(result), &error))
    {
      g_warning("Idle screen locking failed: %s", error ? error->message : "unknown error");
      g_settings_set_boolean (monitor->settings, "idle-lock-failed", TRUE);
    }
  else
    g_settings_set_boolean (monitor->settings, "idle-lock-failed", FALSE);
  monitor->locking = FALSE;
  monitor->cooldown = REARM_COOLDOWN_SECONDS;
  monitor->active_workers--;
  if (monitor->shutting_down && monitor->active_workers == 0)
    g_main_loop_quit (monitor->loop);
}

static void
idle_lock_soon (IdleMonitor *monitor)
{
  GTask *task;

  if (monitor->shutting_down)
    return;
  monitor->locking = TRUE;
  monitor->active_workers++;
  task = g_task_new (NULL, monitor->cancellable, idle_lock_done, monitor);
  g_task_run_in_thread (task, idle_lock_worker);
  g_object_unref (task);
}

static void
idle_fallback_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void) source;
  (void) data;
  HelperResult *result = system_spawn_capture (
      (const char *const[]) { "/usr/bin/xprintidle", NULL }, cancellable);
  g_task_return_pointer (task, result, (GDestroyNotify) helper_result_free);
}

static void
idle_fallback_done (GObject *source, GAsyncResult *result, gpointer data)
{
  (void) source;
  IdleMonitor *monitor = data;
  g_autoptr(GError) error = NULL;
  HelperResult *query = g_task_propagate_pointer (G_TASK (result), &error);
  guint64 milliseconds;

  monitor->fallback_query_pending = FALSE;
  monitor->active_workers--;
  if (query == NULL)
    {
      g_debug ("idle-time query failed: %s", error != NULL ? error->message : "unknown error");
    }
  else if (query->code == 0 &&
      g_ascii_string_to_unsigned (g_strstrip (query->out), 10, 0, G_MAXUINT64,
                                  &milliseconds, NULL) &&
      monitor->lock_enabled && !monitor->locking && monitor->cooldown <= 0 &&
      milliseconds / 1000 >= (guint64) monitor->lock_minutes * 60)
    idle_lock_soon (monitor);
  else if (query->code != 0)
    g_debug ("idle-time helper failed: %s", query->err);

  helper_result_free (query);
  if (monitor->shutting_down && monitor->active_workers == 0)
    g_main_loop_quit (monitor->loop);
}

static void
idle_fallback_query (IdleMonitor *monitor)
{
  GTask *task;

  if (monitor->fallback_query_pending || monitor->shutting_down)
    return;
  monitor->fallback_query_pending = TRUE;
  monitor->active_workers++;
  task = g_task_new (NULL, monitor->cancellable, idle_fallback_done, monitor);
  g_task_run_in_thread (task, idle_fallback_worker);
  g_object_unref (task);
}

static gboolean
idle_poll (gpointer data)
{
  IdleMonitor *monitor = data;
  int idle_seconds;

  if (monitor->cooldown > 0)
    monitor->cooldown -= POLL_SECONDS;

  if (!monitor->lock_enabled || monitor->locking || monitor->cooldown > 0)
    return G_SOURCE_CONTINUE;

  if (!monitor->idle.usable)
    {
      idle_fallback_query (monitor);
      return G_SOURCE_CONTINUE;
    }

  idle_seconds = idle_source_seconds (&monitor->idle);
  if (idle_seconds < 0 || idle_seconds < monitor->lock_minutes * 60)
    return G_SOURCE_CONTINUE;

  idle_lock_soon (monitor);
  return G_SOURCE_CONTINUE;
}

static gboolean
idle_shutdown (gpointer data)
{
  IdleMonitor *monitor = data;

  monitor->shutting_down = TRUE;
  if (monitor->poll_id != 0)
    {
      g_source_remove (monitor->poll_id);
      monitor->poll_id = 0;
    }
  g_cancellable_cancel (monitor->cancellable);
  if (monitor->active_workers == 0)
    g_main_loop_quit (monitor->loop);
  return G_SOURCE_REMOVE;
}

static int
idle_acquire_single_instance (void)
{
  const char *runtime = g_get_user_runtime_dir ();
  g_autofree char *path = g_build_filename (runtime, "luxepower-idle.lock", NULL);
  int fd = open (path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);

  if (fd < 0)
    return -1;
  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 077)) {
    close(fd);
    return -1;
  }
  if (flock (fd, LOCK_EX | LOCK_NB) != 0)
    {
      close (fd);
      return -1;
    }
  return fd;
}

int
main (int argc, char **argv)
{
  (void) argc;
  (void) argv;

  int lock_fd;
  IdleMonitor monitor;
  GMainLoop *loop;

  lock_fd = idle_acquire_single_instance ();
  if (lock_fd < 0)
    return 0;

  memset (&monitor, 0, sizeof (monitor));
  monitor.cancellable = g_cancellable_new ();
  idle_source_open (&monitor.idle);

  if (!monitor.idle.usable &&
      !g_file_test ("/usr/bin/xprintidle", G_FILE_TEST_IS_EXECUTABLE))
    {
      g_printerr ("luxepower-idle: no idle-time source available\n");
      idle_source_close(&monitor.idle);
      close(lock_fd);
      return EXIT_FAILURE;
    }

  monitor.settings = idle_settings_open ();
  if (monitor.settings == NULL)
    {
      g_printerr ("luxepower-idle: schema %s not installed\n", LUXEPOWER_SCHEMA_ID);
      idle_source_close (&monitor.idle);
      close(lock_fd);
      return EXIT_FAILURE;
    }

  idle_monitor_update_settings (&monitor);
  g_signal_connect (monitor.settings, "changed", G_CALLBACK (idle_settings_changed), &monitor);

  loop = g_main_loop_new (NULL, FALSE);
  monitor.loop = loop;
  g_unix_signal_add (SIGTERM, idle_shutdown, &monitor);
  g_unix_signal_add (SIGINT, idle_shutdown, &monitor);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  if (monitor.poll_id != 0)
    g_source_remove (monitor.poll_id);

  idle_source_close (&monitor.idle);
  g_object_unref (monitor.settings);
  g_object_unref (monitor.cancellable);
  close (lock_fd);
  return 0;
}
