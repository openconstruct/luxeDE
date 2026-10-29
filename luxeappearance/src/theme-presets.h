/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_THEME_PRESETS_H
#define LUXE_THEME_PRESETS_H
#include <gtk/gtk.h>
#include "luxe-theme-catalog.h"

const LuxeTheme *luxe_theme_find(const char *gtk_name);
char *luxe_theme_colors(const LuxeTheme *theme, GError **error);
char *luxe_theme_css_update(const char *existing, gboolean enabled, GError **error);
gboolean luxe_theme_write_bridge(const LuxeTheme *theme, GError **error);
gboolean luxe_theme_refresh_bridge(const LuxeTheme *theme, GError **error);
char *luxe_theme_wallpaper(const LuxeTheme *theme);
#endif
