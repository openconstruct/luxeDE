/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPOWER_TLP_H
#define LUXEPOWER_TLP_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define TLP_STAT_PATH "/usr/bin/tlp-stat"
#define TLP_PATH "/usr/sbin/tlp"
#define TLP_DEFAULTS_PATH "/usr/share/tlp/defaults.conf"
#define TLP_CONFIG_PATH "/etc/tlp.conf"
#define TLP_MAX_CONFIG_BYTES (4 * 1024 * 1024)

typedef struct {
  char *name;
  char *value;
  gboolean enabled;
  gboolean quoted;
} TlpSetting;

typedef struct {
  gboolean enabled;
  char *value;
} TlpDesired;

typedef enum {
  TLP_ROW_COMBO,
  TLP_ROW_SWITCH,
  TLP_ROW_SPIN
} TlpRowKind;

typedef struct {
  const char *title;
  int kind;
  const char *keys[2];
  int n_keys;
  const char *on;
  const char *off;
  const char *const *choices;
  int n_choices;
  gboolean dynamic_choices;
  char **owned_choices;
  char *owned_title;
  char *owned_key;
} TlpRowSpec;

typedef struct {
  const char *title;
  const TlpRowSpec *rows;
  int n_rows;
} TlpGroupSpec;

typedef struct {
  const char *title;
  const TlpRowSpec **rows;
  int n_rows;
} TlpOwnedGroup;

extern const TlpGroupSpec tlp_curated_groups[];
extern const int tlp_n_curated_groups;

extern const TlpRowSpec tlp_battery_rows[];
extern const int tlp_n_battery_rows;

extern const char *tlp_row_subtitle(const char *key);

gboolean tlp_installed (void);
char *tlp_stat_output (const char *const *args, GError **error);
GHashTable *tlp_extract_settings (const char *output);
GHashTable *tlp_extract_defaults (const char *path);
void tlp_free_settings (GHashTable *settings);
TlpDesired *tlp_desired_copy (const TlpDesired *desired);
void tlp_desired_free (TlpDesired *desired);
TlpSetting *tlp_setting_new (const char *name, const char *value, gboolean enabled, gboolean quoted);
char *tlp_effective_value (GHashTable *settings, GHashTable *defaults, const char *name);
void tlp_original_state (GHashTable *settings, GHashTable *defaults, const char *name,
                         gboolean *enabled, char **value);
GHashTable *tlp_build_changes (GHashTable *settings, GHashTable *defaults,
                               GHashTable *desired);
char *tlp_render_config (const char *original, GHashTable *changes, GHashTable *defaults);
char *tlp_read_user_config (GError **error);
char *tlp_read_config_file (const char *path, GError **error);
char *tlp_platform_profile_choices (void);
gboolean tlp_battery_thresholds_supported (void);
gboolean tlp_config_line_is_valid (const char *line);
TlpOwnedGroup *tlp_effective_groups (GHashTable *settings, int *n_groups);
void tlp_effective_groups_free (TlpOwnedGroup *groups, int n_groups);
void tlp_row_spec_free (TlpRowSpec *row);

G_END_DECLS

#endif
