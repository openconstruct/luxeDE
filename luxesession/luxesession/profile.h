/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LUXESESSION_PROFILE_H
#define LUXESESSION_PROFILE_H

#include <glib.h>

G_BEGIN_DECLS

gboolean luxe_profile_component_valid(const gchar *value);
gboolean luxe_process_start_time(gint pid, guint64 *start_time);

G_END_DECLS

#endif /* LUXESESSION_PROFILE_H */
