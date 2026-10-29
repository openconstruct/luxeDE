/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gio/gio.h>
gboolean lucid_state_write(GKeyFile *state, const char *path, GError **error);
GKeyFile *lucid_state_read(const char *path, GError **error);
char *lucid_session_path(const char *directory, const char *name);
/* Preserve a validated prior snapshot under its content hash. Never replace
 * an existing different file; failure leaves the caller's original untouched. */
char *lucid_state_preserve(GKeyFile *state, const char *directory, GError **error);
/* Keep a validated previous state before replacing it with an incomplete one. */
gboolean lucid_state_preserve_existing(const char *path, const char *directory,
                                      GError **error);
