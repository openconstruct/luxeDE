/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_ABOUT_H
#define LUXE_ABOUT_H
#include "luxe-gtk-ui.h"
static void luxe_show_about(GtkWindow *parent, const char *name, const char *version, const char *description)
{
    static const char *developers[] = { "Jerry Howell (2026)", NULL };
    GtkWindow *dialog = luxe_about_new();
    g_object_set(dialog, "program-name", name, "logo-icon-name", "org.luxe.LuxUp",
        "version", version, "comments", description,
        "website", "https://github.com/openconstruct/luxeDE",
        "copyright", "Copyright (C) 2026 Jerry Howell", "license-type", GTK_LICENSE_GPL_2_0, NULL);
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), developers);
    luxe_dialog_present(dialog, GTK_WIDGET(parent));
}
#endif
