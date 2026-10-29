/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPOLKIT_DIALOG_UI_H
#define LUXEPOLKIT_DIALOG_UI_H
#include "../luxe-about.h"
/* Shared by the real authentication request and the harmless visual preview. */
static void luxepolkit_prepare_dialog(GtkDialog *dialog)
{
    GtkWidget *cancel = gtk_dialog_add_button(dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
    GtkWidget *authenticate = gtk_dialog_add_button(dialog, _("_Authenticate"), GTK_RESPONSE_OK);
    gtk_widget_add_css_class(authenticate, "suggested-action");
    gtk_widget_set_size_request(cancel, 90, 32);
    gtk_widget_set_size_request(authenticate, 110, 32);
    GtkWidget *actions = gtk_widget_get_parent(authenticate);
    gtk_widget_set_margin_start(actions, 10);
    gtk_widget_set_margin_end(actions, 10);
    gtk_widget_set_margin_bottom(actions, 10);
    gtk_box_set_spacing(GTK_BOX(actions), 6);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_OK);
    luxe_add_about_button(GTK_WINDOW(dialog), "LuxePolkit", VERSION,
        _("Authentication agent for Luxe, derived from LXPolkit."));
    /* Keep About accessible without competing with the authentication action. */
    GtkWidget *header = gtk_window_get_titlebar(GTK_WINDOW(dialog));
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header), ":close");
}
#endif
