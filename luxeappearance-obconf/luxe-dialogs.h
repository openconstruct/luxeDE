/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_DIALOGS_H
#define LUXE_DIALOGS_H
#include <gtk/gtk.h>
#include <glib/gi18n.h>

typedef void (*LuxeDialogResponse)(gint button_index, gpointer user_data);

typedef struct {
    LuxeDialogResponse cb;
    gpointer user_data;
} LuxeDialogContext;

static void _luxe_dialog_done(GObject *source, GAsyncResult *result, gpointer data) {
    LuxeDialogContext *ctx = data;
    gint index = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL);
    if (ctx->cb) ctx->cb(index, ctx->user_data);
    g_free(ctx);
}

static void _luxe_show_alert(GtkWindow *parent, const char *message, const char *detail,
                             const char *const *buttons, gint cancel_button, gint default_button,
                             LuxeDialogResponse cb, gpointer user_data) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    if (detail) gtk_alert_dialog_set_detail(dialog, detail);
    if (buttons) gtk_alert_dialog_set_buttons(dialog, buttons);
    if (cancel_button >= 0) gtk_alert_dialog_set_cancel_button(dialog, cancel_button);
    if (default_button >= 0) gtk_alert_dialog_set_default_button(dialog, default_button);
    LuxeDialogContext *ctx = g_new(LuxeDialogContext, 1);
    ctx->cb = cb;
    ctx->user_data = user_data;
    gtk_alert_dialog_choose(dialog, parent, NULL, _luxe_dialog_done, ctx);
    g_object_unref(dialog);
}

static void luxe_show_error(GtkWindow *parent, const char *message, const char *detail) {
    _luxe_show_alert(parent, message, detail, (const char *const[]){_("OK"), NULL}, -1, 0, NULL, NULL);
}

static void luxe_show_message(GtkWindow *parent, const char *message, const char *detail) {
    _luxe_show_alert(parent, message, detail, (const char *const[]){_("OK"), NULL}, -1, 0, NULL, NULL);
}

static void luxe_ask(GtkWindow *parent, const char *message, const char *detail,
                     const char *const *buttons, gint cancel_button, gint default_button,
                     LuxeDialogResponse cb, gpointer user_data) {
    _luxe_show_alert(parent, message, detail, buttons, cancel_button, default_button, cb, user_data);
}

static void luxe_confirm(GtkWindow *parent, const char *message, const char *detail,
                         gboolean destructive, LuxeDialogResponse cb, gpointer user_data) {
    const char *buttons[] = { _("Cancel"), destructive ? _("Remove") : _("OK"), NULL };
    _luxe_show_alert(parent, message, detail, buttons, 0, 0, cb, user_data);
}
#endif
