/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MATINEE_APP_H
#define MATINEE_APP_H

#include "luxe-gtk-ui.h"

G_BEGIN_DECLS

#define MATINEE_TYPE_APP (matinee_app_get_type ())
G_DECLARE_FINAL_TYPE (MatineeApp, matinee_app, MATINEE, APP, GtkApplication)

MatineeApp *matinee_app_new (void);

G_END_DECLS

#endif /* MATINEE_APP_H */
