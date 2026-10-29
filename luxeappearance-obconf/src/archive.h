/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __install_h
#define __install_h

#include <glib.h>
#include <gio/gio.h>

void   archive_install_async (const gchar *path,
                              GAsyncReadyCallback callback,
                              gpointer user_data);
gchar* archive_install_finish (GAsyncResult *result, GError **error);
void   archive_create  (const gchar *path);
void   archive_cancel_pending (void);

#endif
