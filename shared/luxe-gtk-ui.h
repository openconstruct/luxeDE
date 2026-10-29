/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_GTK_UI_H
#define LUXE_GTK_UI_H
#include <gtk/gtk.h>
#include "luxe-renderer.h"
G_BEGIN_DECLS
typedef enum { LUXE_RESPONSE_DEFAULT, LUXE_RESPONSE_SUGGESTED, LUXE_RESPONSE_DESTRUCTIVE } LuxeResponseAppearance;
typedef enum { LUXE_COLOR_SCHEME_DEFAULT, LUXE_COLOR_SCHEME_FORCE_LIGHT, LUXE_COLOR_SCHEME_PREFER_LIGHT, LUXE_COLOR_SCHEME_PREFER_DARK, LUXE_COLOR_SCHEME_FORCE_DARK } LuxeColorScheme;
void luxe_ui_init(void);
GtkApplication *luxe_application_new(const char *, GApplicationFlags);
GtkWidget *luxe_toolbar_new(void);
void luxe_toolbar_set_content(GtkBox *, GtkWidget *);
void luxe_toolbar_add_top_bar(GtkBox *, GtkWidget *);
void luxe_toolbar_add_bottom_bar(GtkBox *, GtkWidget *);
void luxe_window_set_content(GtkWindow *, GtkWidget *);
GtkWindow *luxe_dialog_new(void);
void luxe_dialog_present(GtkWindow *, GtkWidget *);
void luxe_dialog_close(GtkWindow *);
void luxe_dialog_set_width(GtkWindow *, int);
void luxe_dialog_set_height(GtkWindow *, int);
GtkWindow *luxe_window_get_visible_dialog(GtkWindow *);
GtkWidget *luxe_preferences_page_new(void);
void luxe_preferences_page_add(GtkScrolledWindow *, GtkBox *);
void luxe_preferences_page_remove(GtkScrolledWindow *, GtkBox *);
void luxe_preferences_page_set_title(GtkScrolledWindow *, const char *);
void luxe_preferences_page_set_description(GtkScrolledWindow *, const char *);
GtkWidget *luxe_preferences_group_new(void);
void luxe_preferences_group_add(GtkBox *, GtkWidget *);
void luxe_preferences_group_set_title(GtkBox *, const char *);
void luxe_preferences_group_set_description(GtkBox *, const char *);
GtkWidget *luxe_action_row_new(void);
void luxe_row_set_title(GtkWidget *, const char *);
void luxe_row_set_use_markup(GtkWidget *, gboolean);
void luxe_row_set_subtitle(GtkWidget *, const char *);
void luxe_action_row_add_suffix(GtkListBoxRow *, GtkWidget *);
void luxe_action_row_set_activatable_widget(GtkListBoxRow *, GtkWidget *);
GtkWidget *luxe_combo_row_new(void);
GtkWidget *luxe_switch_row_new(void);
GtkWidget *luxe_spin_row_new(GtkAdjustment *, double, guint);
GtkWidget *luxe_spin_row_new_with_range(double, double, double);
GtkWidget *luxe_entry_row_new(void);
GtkWidget *luxe_switcher_bar_new(void);
void luxe_switcher_bar_set_stack(GtkRevealer *, GtkStack *);
GtkStackPage *luxe_stack_add_titled_with_icon(GtkStack *, GtkWidget *, const char *, const char *, const char *);
void luxe_switcher_set_policy(GtkStackSwitcher *, int);
void luxe_status_set_title(GtkBox *, const char *);
void luxe_status_set_description(GtkBox *, const char *);
GtkSettings *luxe_style_settings(void);
gboolean luxe_style_get_dark(GtkSettings *);
gboolean luxe_style_get_high_contrast(GtkSettings *);
void luxe_style_set_color_scheme(GtkSettings *, LuxeColorScheme);
GtkWindow *luxe_choice_dialog_new(const char *, const char *);
void luxe_choice_add_response(GtkDialog *, const char *, const char *);
void luxe_choice_add_responses(GtkDialog *, ...) G_GNUC_NULL_TERMINATED;
void luxe_choice_set_close_response(GtkDialog *, const char *);
void luxe_choice_set_default_response(GtkDialog *, const char *);
void luxe_choice_set_response_appearance(GtkDialog *, const char *, LuxeResponseAppearance);
LuxeResponseAppearance luxe_choice_get_response_appearance(GtkDialog *, const char *);
void luxe_choice_set_body(GtkDialog *, const char *);
void luxe_choice_set_extra_child(GtkDialog *, GtkWidget *);
GtkWidget *luxe_choice_get_extra_child(GtkDialog *);
void luxe_choice_choose(GtkDialog *, GtkWindow *, GCancellable *, GAsyncReadyCallback, gpointer);
const char *luxe_choice_choose_finish(GtkDialog *, GAsyncResult *);
void luxe_choice_respond(GtkDialog *, const char *);
GtkWindow *luxe_about_new(void);
void luxe_about_add_credit_section(GtkAboutDialog *, const char *, const char **);
void luxe_about_add_legal_section(GtkAboutDialog *, const char *, const char *, GtkLicense, const char *);
const char *luxe_about_get_license(GtkAboutDialog *);
typedef struct { char *message; guint timeout; } LuxeToast;
LuxeToast *luxe_toast_new(const char *);
void luxe_toast_set_timeout(LuxeToast *, guint);
void luxe_toast_overlay_add_toast(GtkOverlay *, LuxeToast *);
G_END_DECLS
#endif
