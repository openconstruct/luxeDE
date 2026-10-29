/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXESESSION_MIME_DEFAULTS_H
#define LUXESESSION_MIME_DEFAULTS_H
#include <gio/gio.h>

gboolean lxsession_set_mime_defaults(const gchar * const *types, gint count,
                                    const gchar *desktop_id, GError **error);
gboolean lxsession_set_browser(const gchar *command, GError **error);
#endif
