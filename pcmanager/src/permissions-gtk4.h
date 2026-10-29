/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gtk/gtk.h>
typedef struct _PcmgrPermissionTarget PcmgrPermissionTarget;
PcmgrPermissionTarget *pcmgr_permission_target_new(GFile *file, GError **error);
void pcmgr_permission_target_free(PcmgrPermissionTarget *target);
guint32 pcmgr_permission_target_mode(PcmgrPermissionTarget *target);
gboolean pcmgr_permission_target_apply(PcmgrPermissionTarget *target, guint32 mask, guint32 bits, GError **error);
/* The window owns the editor. Updates are asynchronous and selected-item-only. */
GtkWidget *pcmgr_permissions_editor(GtkWindow *window, GPtrArray *files, GtkWidget **apply);
