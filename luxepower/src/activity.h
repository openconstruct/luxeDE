/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPOWER_ACTIVITY_H
#define LUXEPOWER_ACTIVITY_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*ActivityDone) (int wait_status, gpointer data);

GtkWidget *activity_terminal_new (void);
GtkWidget *activity_present(GtkWidget *group, GtkWidget *box);
void activity_run (GtkWidget *terminal, const char *const *argv,
                   ActivityDone done, gpointer data, GDestroyNotify destroy);

G_END_DECLS

#endif
