/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPOWER_SYSTEM_H
#define LUXEPOWER_SYSTEM_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#ifndef LUXEPOWER_HELPER_PATH
#define LUXEPOWER_HELPER_PATH "/usr/libexec/luxepower-apply"
#endif
#ifndef LUXELOCK_PATH
#define LUXELOCK_PATH "/usr/bin/luxelock"
#endif
#define LUXEPOWER_SCHEMA_ID "org.luxe.LuxePower"

typedef struct {
  int code;
  char *out;
  char *err;
} HelperResult;

typedef struct {
  char name[128];
  long max;
  long current;
} BacklightDevice;

typedef enum {
  LID_SUSPEND,
  LID_HIBERNATE,
  LID_HYBRID_SLEEP,
  LID_LOCK,
  LID_IGNORE,
  LID_CUSTOM
} LidAction;

static const struct { LidAction action; const char *value; const char *label; } lid_actions[] = {
  { LID_SUSPEND, "suspend", "Suspend" },
  { LID_HIBERNATE, "hibernate", "Hibernate" },
  { LID_HYBRID_SLEEP, "hybrid-sleep", "Hybrid sleep" },
  { LID_LOCK, "lock", "Lock screen" },
  { LID_IGNORE, "ignore", "Do nothing" },
  { LID_CUSTOM, NULL, "Other system action (unchanged)" },
};

#define LID_ACTION_COUNT 6

typedef struct {
  gboolean allow_suspend;
  gboolean allow_hibernate;
  gboolean allow_hybrid;
} SleepState;

typedef struct {
  LidAction handle;
  LidAction handle_external_power;
  LidAction handle_docked;
} LidState;

gboolean system_power_source (void);
int system_battery_percent (void);
int system_battery_percent_at (const char *root);
GPtrArray *system_backlight_devices (void);
void system_backlight_devices_free (GPtrArray *devices);
void system_sleep_state (SleepState *state);
void system_lid_state (LidState *state);
void system_parse_logind_text (const char *text, GHashTable *keys);
const char *system_lid_action_value (LidAction action);
int system_lid_action_index (const char *value);
void system_set_brightness (const char *name, long value, GError **error);
HelperResult *system_spawn_capture (const char *const *args, GCancellable *cancellable);
void helper_result_free (HelperResult *result);

G_END_DECLS

#endif
