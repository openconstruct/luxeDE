/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPOWER_APP_H
#define LUXEPOWER_APP_H

#include "system.h"
#include "tlp.h"

#include "luxe-gtk-ui.h"
void request_screen_lock(GtkWidget *parent);

#define LUXEPOWER_APP_ID "org.luxe.LuxePower"
#ifndef LUXEPOWER_VERSION
#define LUXEPOWER_VERSION "0.2.0-6"
#endif

typedef struct LuxePowerWindow LuxePowerWindow;

typedef struct {
  GtkWidget *row;
  int kind;
  const char *on;
  const char *off;
  const char *const *choices;
  int n_choices;
  char **owned_choices;
  char *key;
} PowerRow;

typedef struct {
  GtkWidget *page;
  GtkWidget *power_label;
  GtkWidget *battery_label;
  GtkWidget *error_group;
  GtkWidget *error_label;
  GtkWidget *apply_group;
  GtkWidget *apply_button;
  GtkWidget *discard_button;
  GHashTable *settings;
  GHashTable *defaults;
  GHashTable *original;
  GPtrArray *rows;
  GPtrArray *group_widgets;
  GtkWidget *terminal_group;
  GtkWidget *terminal_box;
  char *staged_path;
  char *staged_digest;
  LuxePowerWindow *win;
  gboolean loaded;
  gboolean applying;
  gboolean loading;
} PowerPage;

typedef struct {
  GtkWidget *page;
  GtkWidget *suspend_row;
  GtkWidget *hibernate_row;
  GtkWidget *hybrid_row;
  GtkWidget *lid_rows[3];
  GtkWidget *terminal_group;
  GtkWidget *terminal_box;
  GtkWidget *terminal;
  LuxePowerWindow *win;
  gboolean updating;
} SleepPage;

typedef struct {
  GtkWidget *page;
  GtkWidget *brightness_group;
  GtkWidget *device_row;
  GtkWidget *scale;
  GtkWidget *lock_group;
  GtkWidget *lock_switch;
  GtkWidget *minutes_row;
  GtkWidget *lock_error_label;
  GSettings *settings;
  GPtrArray *devices;
  guint source_id;
  LuxePowerWindow *win;
  gboolean updating;
  gboolean brightness_busy;
  gboolean brightness_pending;
} DisplayPage;

typedef struct {
  GtkWidget *page;
  GtkTextBuffer *buffer;
  GtkWidget *refresh_button;
  GtkWidget *full_button;
  GtkWidget *terminal_group;
  GtkWidget *terminal_box;
  gboolean fetching;
  LuxePowerWindow *win;
} StatusPage;

struct LuxePowerWindow {
  gboolean closed;
  GtkWidget *window;
  GtkWidget *toast_overlay;
  PowerPage power;
  SleepPage sleep;
  DisplayPage display;
  StatusPage status;
};

extern const char *lid_key_names[3];

extern const int idle_minutes[8];
extern const char *idle_labels[8];

#include "activity.h"

void show_error (GtkWidget *parent, const char *title, const char *detail);
void toast (LuxePowerWindow *win, const char *message);
GtkWidget *dim_label (const char *text);
GSettings *lock_settings_open (void);

void power_page_init (PowerPage *page, LuxePowerWindow *win);
void power_page_free (PowerPage *page);
void power_page_load (PowerPage *page);
void power_page_collect_desired (PowerPage *page, GHashTable *desired);

void sleep_page_init (SleepPage *page, LuxePowerWindow *win);
void sleep_page_refresh (SleepPage *page);

void display_page_init (DisplayPage *page, LuxePowerWindow *win);
void display_page_refresh (DisplayPage *page);

void status_page_init (StatusPage *page, LuxePowerWindow *win);
void status_page_refresh (StatusPage *page, gboolean complete);

#endif
