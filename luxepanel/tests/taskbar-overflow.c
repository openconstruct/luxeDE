/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include "icon-grid.h"
#include "panel.h"

/* The grid checks whether its root is a LuxePanel. This fixture uses a
 * regular GtkWindow, so these symbols are never used for panel layout. */
GType luxepanel_get_type(void) { return GTK_TYPE_BOX; }
gint panel_get_height(LuxePanel *panel) { (void) panel; return 32; }

int main(void)
{
    gtk_init();
    GtkWidget *window = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *tasks = panel_icon_grid_new(GTK_ORIENTATION_HORIZONTAL, 180, 32, 1, 0, 32);
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 36);
    gtk_window_set_child(GTK_WINDOW(window), box);
    gtk_box_append(GTK_BOX(box), tasks);
    gtk_widget_set_hexpand(tasks, TRUE);
    panel_icon_grid_set_constrain_width(PANEL_ICON_GRID(tasks), TRUE);
    for (int i = 0; i < 15; i++) {
        GtkWidget *button = gtk_button_new_with_label("Window");
        panel_icon_grid_add(PANEL_ICON_GRID(tasks), button);
    }
    GtkWidget *clock = gtk_label_new("Clock");
    GtkWidget *tray = gtk_label_new("Tray");
    GtkWidget *volume = gtk_label_new("Volume");
    gtk_widget_set_size_request(clock, 80, -1);
    gtk_widget_set_size_request(tray, 80, -1);
    gtk_widget_set_size_request(volume, 80, -1);
    gtk_box_append(GTK_BOX(box), clock);
    gtk_box_append(GTK_BOX(box), tray);
    gtk_box_append(GTK_BOX(box), volume);
    gtk_window_present(GTK_WINDOW(window));
    for (int i = 0; i < 100; i++) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(10000);
    }
    int minimum = -1, natural = -1;
    gtk_widget_measure(tasks, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, &natural, NULL, NULL);
    g_assert_cmpint(minimum, ==, 0);
    g_assert_cmpint(natural, >, 600);
    g_assert_cmpint(gtk_widget_get_overflow(tasks), ==, GTK_OVERFLOW_HIDDEN);
    graphene_rect_t bounds;
    g_assert_true(gtk_widget_compute_bounds(volume, box, &bounds));
    g_assert_cmpfloat(bounds.origin.x, >=, 0);
    g_assert_cmpfloat(bounds.origin.x + bounds.size.width, <=, gtk_widget_get_width(box));
    g_assert_cmpint(gtk_widget_get_width(tasks), <, natural);
    gtk_window_destroy(GTK_WINDOW(window));
    g_print("PASS: many task buttons leave clock, tray and volume on panel.\n");
    return 0;
}
