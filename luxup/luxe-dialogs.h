/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_DIALOGS_H
#define LUXE_DIALOGS_H
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
static void luxe_show_error(GtkWindow *parent, const char *message, const char *detail)
{
    GtkDialog *dialog = GTK_DIALOG(luxe_choice_dialog_new(message, detail));
    luxe_choice_add_response(dialog, "close", _("OK"));
    luxe_dialog_present(GTK_WINDOW(dialog), GTK_WIDGET(parent));
}
#endif
