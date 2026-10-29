/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MATINEE_WINDOW_H
#define MATINEE_WINDOW_H

#include "luxe-gtk-ui.h"

#include "matinee-engine.h"
#include "matinee-model.h"
#include "matinee-visual.h"

G_BEGIN_DECLS

#define MATINEE_TYPE_WINDOW (matinee_window_get_type ())
G_DECLARE_FINAL_TYPE (MatineeWindow, matinee_window, MATINEE, WINDOW, GtkApplicationWindow)

MatineeWindow *matinee_window_new (GtkApplication *app);

typedef struct {
	const char *label;
	const char *action;
	const char *accelerator;
} MatineeShortcut;
extern const MatineeShortcut matinee_shortcuts[];
extern const gsize matinee_shortcuts_count;

/* Enqueue files and start playing the first one. */
void matinee_window_open_files (MatineeWindow *window, const char * const *files);

G_END_DECLS

#endif /* MATINEE_WINDOW_H */
