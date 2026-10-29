/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gio/gio.h>
#define SLAB_MAX_PINS 32
gboolean slab_id_valid(const char *id);
GPtrArray *slab_pins_load(const char *path, GError **error);
gboolean slab_pins_save(const char *path, GPtrArray *pins, GError **error);
gboolean slab_pins_save_if_unchanged(const char *path, GPtrArray *expected,
                                     GPtrArray *pins, GError **error);
int slab_pin_index(GPtrArray *pins, const char *id);
GPtrArray *slab_pins_move(GPtrArray *pins, const char *id, guint index);
