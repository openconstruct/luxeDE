/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "luxe-gtk-ui.h"
static char *answer;
static guint callbacks;
static void done(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void)unused;
    g_free(answer);
    answer = g_strdup(luxe_choice_choose_finish(GTK_DIALOG(source), result));
    callbacks++;
}
static void settle(void) { while (g_main_context_iteration(NULL, FALSE)); }
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_setenv("GSK_RENDERER", "ngl", TRUE);
    luxe_ui_init();
    g_assert_cmpstr(g_getenv("GSK_RENDERER"), ==, "ngl");
    GtkWindow *parent = GTK_WINDOW(gtk_window_new());
    GtkWidget *toolbar = luxe_toolbar_new(), *header = gtk_header_bar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar), header);
    GtkWidget *page = luxe_preferences_page_new(), *group = luxe_preferences_group_new();
    luxe_preferences_group_set_title(GTK_BOX(group), "Native controls");
    GtkWidget *controls[] = { luxe_switch_row_new(), luxe_combo_row_new(), luxe_entry_row_new(), luxe_spin_row_new_with_range(1, 10, 1) };
    const char *labels[] = { "Follow theme", "Color", "Name", "Count" };
    const char *items[] = { "Light", "Dark", NULL };
    g_autoptr(GtkStringList) list = gtk_string_list_new(items);
    gtk_drop_down_set_model(GTK_DROP_DOWN(controls[1]), G_LIST_MODEL(list));
    for (guint i = 0; i < G_N_ELEMENTS(controls); i++) {
        luxe_row_set_title(controls[i], labels[i]);
        luxe_row_set_subtitle(controls[i], "Native keyboard-accessible setting");
        luxe_preferences_group_add(GTK_BOX(group), controls[i]);
    }
    luxe_preferences_page_add(GTK_SCROLLED_WINDOW(page), GTK_BOX(group));
    luxe_toolbar_set_content(GTK_BOX(toolbar), page);
    luxe_window_set_content(parent, toolbar);
    gtk_window_present(parent); settle();
    g_assert_true(GSK_IS_CAIRO_RENDERER(gtk_native_get_renderer(GTK_NATIVE(parent))));
    g_assert_true(gtk_window_get_titlebar(parent) == header);
    gtk_switch_set_active(GTK_SWITCH(controls[0]), TRUE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(controls[1]), 1);
    gtk_editable_set_text(GTK_EDITABLE(controls[2]), "Keyboard");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(controls[3]), 7);
    g_assert_true(gtk_switch_get_active(GTK_SWITCH(controls[0])));
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(controls[1])), ==, 1);
    g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(controls[2])), ==, "Keyboard");
    g_assert_cmpfloat(gtk_spin_button_get_value(GTK_SPIN_BUTTON(controls[3])), ==, 7);
    GtkWindow *choice = luxe_choice_dialog_new("Save changes?", "Keep or discard your work.");
    luxe_choice_add_responses(GTK_DIALOG(choice), "cancel", "Cancel", "save", "Save", NULL);
    luxe_choice_set_close_response(GTK_DIALOG(choice), "cancel");
    luxe_choice_set_response_appearance(GTK_DIALOG(choice), "save", LUXE_RESPONSE_SUGGESTED);
    g_assert_cmpint(luxe_choice_get_response_appearance(GTK_DIALOG(choice), "save"), ==, LUXE_RESPONSE_SUGGESTED);
    luxe_choice_choose(GTK_DIALOG(choice), parent, NULL, done, NULL);
    luxe_choice_respond(GTK_DIALOG(choice), "save"); settle();
    g_assert_cmpstr(answer, ==, "save");
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    choice = luxe_choice_dialog_new("Cancel", "Cancellation retains the document.");
    luxe_choice_add_response(GTK_DIALOG(choice), "cancel", "Cancel");
    luxe_choice_set_close_response(GTK_DIALOG(choice), "cancel");
    luxe_choice_choose(GTK_DIALOG(choice), parent, cancel, done, NULL);
    g_cancellable_cancel(cancel); settle();
    g_assert_cmpstr(answer, ==, "cancel");
    g_assert_cmpuint(callbacks, ==, 2);
    GtkWindow *about = luxe_about_new();
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(about), GTK_LICENSE_GPL_2_0);
    luxe_about_add_legal_section(GTK_ABOUT_DIALOG(about), "Upstream", "Original copyright", GTK_LICENSE_GPL_2_0, "Original license text");
    g_assert_nonnull(g_strstr_len(luxe_about_get_license(GTK_ABOUT_DIALOG(about)), -1, "Original license text"));
    g_assert_cmpint(gtk_about_dialog_get_license_type(GTK_ABOUT_DIALOG(about)), ==, GTK_LICENSE_GPL_2_0);
    gtk_window_destroy(about);
    gtk_window_destroy(parent); settle();
    g_free(answer);
    return 0;
}
