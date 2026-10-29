/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_DIALOGS_H
#define LUXE_DIALOGS_H
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <stdlib.h>

typedef void (*LuxeDialogResponse)(gint button_index, gpointer user_data);
typedef struct {
    LuxeDialogResponse cb;
    gpointer user_data;
} LuxeDialogContext;

static void _luxe_dialog_done(GtkDialog *dialog, const char *response, gpointer data)
{
    (void)dialog;
    LuxeDialogContext *ctx = data;
    if (ctx->cb) ctx->cb(g_str_equal(response, "close") ? -1 : atoi(response), ctx->user_data);
}

static void luxe_ask(GtkWindow *parent, const char *message, const char *detail,
                     const char *const *buttons, gint cancel_button, gint default_button,
                     LuxeDialogResponse cb, gpointer user_data)
{
    GtkDialog *dialog = GTK_DIALOG(luxe_choice_dialog_new(message, detail));
    for (guint i = 0; buttons[i]; i++) {
        gchar *id = g_strdup_printf("%u", i);
        luxe_choice_add_response(dialog, id, buttons[i]);
        if ((gint)i == cancel_button) luxe_choice_set_close_response(dialog, id);
        if ((gint)i == default_button) luxe_choice_set_default_response(dialog, id);
        g_free(id);
    }
    LuxeDialogContext *ctx = g_new(LuxeDialogContext, 1);
    ctx->cb = cb;
    ctx->user_data = user_data;
    g_object_set_data_full(G_OBJECT(dialog), "luxe-response-context", ctx, g_free);
    g_signal_connect(dialog, "luxe-response", G_CALLBACK(_luxe_dialog_done), ctx);
    luxe_dialog_present(GTK_WINDOW(dialog), GTK_WIDGET(parent));
}

static void luxe_show_error(GtkWindow *parent, const char *message, const char *detail)
{
    luxe_ask(parent, message, detail, (const char *const[]){_("OK"), NULL}, 0, 0, NULL, NULL);
}

#define luxe_show_message luxe_show_error
#endif
