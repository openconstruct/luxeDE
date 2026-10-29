/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef LUXE_MAIN_DLG_H
#define LUXE_MAIN_DLG_H

#include <gtk/gtk.h>

void main_dlg_init( GtkWidget* dlg );

/* Revert the live preview when closing without Apply. */
void main_dlg_handle_close( GtkWidget* dlg );

/* Release deferred work and global state for a window that is being
 * destroyed. Safe to call more than once. */
void main_dlg_shutdown( GtkWidget* dlg );

/* Force completion of the deferred (idle) theme enumeration, e.g. for
 * tests and for theme install/remove reloads. */
void main_dlg_ensure_themes_loaded(void);

/* Late arrival of the async settings-daemon probe: switch from standalone
 * gtkrc mode to luxesession mode. Returns FALSE when the dialog is gone and
 * the caller must not change session state. */
gboolean main_dlg_note_session_daemon( void );

/* Current toolbar style (0 = icons only, 1 = text only,
 * 2 = text below icons, 3 = text beside icons). Used by the
 * in-process demo preview. */
gint main_dlg_get_tb_style( void );

void
on_apply_clicked                       (GtkButton       *button,
                                        gpointer         user_data);

void
on_font_changed                        (GtkFontButton   *fontbutton,
                                        gpointer         user_data);

void
on_install_theme_clicked               (GtkButton       *button,
                                        gpointer         user_data);

void
on_remove_theme_clicked                (GtkButton       *button,
                                        gpointer         user_data);

void
on_cursor_size_changed                 (GtkRange          *range,
                                        gpointer         user_data);

void
on_tb_style_changed                    (GtkComboBox     *combobox,
                                        gpointer         user_data);

#endif /* LUXE_MAIN_DLG_H */
