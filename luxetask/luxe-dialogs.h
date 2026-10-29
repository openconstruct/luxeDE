/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_DIALOGS_H
#define LUXE_DIALOGS_H
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>

typedef void (*LuxeDialogResponse)(gint button_index, gpointer user_data);
typedef struct {
    LuxeDialogResponse cb;
    gpointer data;
    gboolean answered;
} LuxeDialogContext;

static void luxe_context_free(gpointer data)
{
    LuxeDialogContext *ctx = data;
    if (!ctx->answered) ctx->cb(0, ctx->data);
    g_free(ctx);
}

static void luxe_response(GtkDialog *dialog, const char *response, gpointer data)
{
    (void)dialog;
    LuxeDialogContext *ctx = data;
    if (ctx->answered) return;
    ctx->answered = TRUE;
    ctx->cb(g_str_equal(response, "accept") ? 1 : 0, ctx->data);
}

static void luxe_show_error(GtkWindow *parent, const char *message, const char *detail)
{
    GtkDialog *dialog = GTK_DIALOG(luxe_choice_dialog_new(_("Task operation failed"), NULL));
    gchar *body = detail ? g_strconcat(message, "\n", detail, NULL) : g_strdup(message);
    luxe_choice_set_body(dialog, body);
    g_free(body);
    luxe_choice_add_response(dialog, "close", _("OK"));
    luxe_dialog_present(GTK_WINDOW(dialog), GTK_WIDGET(parent));
}

static void luxe_confirm(GtkWindow *parent, const char *message, const char *detail,
                         gboolean destructive, LuxeDialogResponse cb, gpointer data)
{
    GtkDialog *dialog = GTK_DIALOG(luxe_choice_dialog_new(message, detail));
    luxe_choice_add_responses(dialog, "cancel", _("Cancel"), "accept", _("OK"), NULL);
    luxe_choice_set_close_response(dialog, "cancel");
    luxe_choice_set_default_response(dialog, "cancel");
    if (destructive) luxe_choice_set_response_appearance(dialog, "accept", LUXE_RESPONSE_DESTRUCTIVE);
    LuxeDialogContext *ctx = g_new0(LuxeDialogContext, 1);
    ctx->cb = cb;
    ctx->data = data;
    g_object_set_data_full(G_OBJECT(dialog), "response-context", ctx, luxe_context_free);
    g_signal_connect(dialog, "luxe-response", G_CALLBACK(luxe_response), ctx);
    luxe_dialog_present(GTK_WINDOW(dialog), GTK_WIDGET(parent));
}
#endif
