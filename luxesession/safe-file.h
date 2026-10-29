/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LUXESESSION_SAFE_FILE_H
#define LUXESESSION_SAFE_FILE_H

#include <glib.h>

G_BEGIN_DECLS

gboolean lxsession_atomic_write_file(const gchar *path,
                                     const gchar *contents,
                                     gssize length,
                                     GError **error);

gboolean lxsession_read_bounded_file(const gchar *path,
                                     gsize max_bytes,
                                     gchar **contents,
                                     gsize *length,
                                     GError **error);

gchar **lxsession_parse_command(const gchar *command_line,
                                const gchar *delimiter,
                                int max_tokens);

gboolean lxsession_key_file_load_bounded(GKeyFile *key_file,
                                         const gchar *path,
                                         GError **error);

G_END_DECLS

#endif /* LUXESESSION_SAFE_FILE_H */
