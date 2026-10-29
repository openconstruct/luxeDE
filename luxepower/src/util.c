/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"
#include "luxe-lock-request.h"

typedef struct { GtkWidget *window; GCancellable *cancel; gulong closed; } ScreenLockRequest;
static void screen_lock_ready(GObject *source, GAsyncResult *result, gpointer data)
{
  (void)source;
  ScreenLockRequest *request = data;
  g_autoptr(GError) error = NULL;
  gboolean ready = luxe_lock_request_finish(result, &error);
  g_signal_handler_disconnect(request->window, request->closed);
  g_object_set_data(G_OBJECT(request->window), "luxe-lock-pending", NULL);
  if (!ready && !g_object_get_data(G_OBJECT(request->window), "luxe-power-closed"))
    show_error(request->window, "Could not lock the screen", error->message);
  g_object_unref(request->window); g_object_unref(request->cancel); g_free(request);
}

void request_screen_lock(GtkWidget *parent)
{
  GtkRoot *root = gtk_widget_get_root(parent);
  if (!root || !GTK_IS_WINDOW(root) || g_object_get_data(G_OBJECT(root), "luxe-lock-pending")) return;
  ScreenLockRequest *request = g_new0(ScreenLockRequest, 1);
  request->window = g_object_ref(GTK_WIDGET(root));
  request->cancel = g_cancellable_new();
  request->closed = g_signal_connect_swapped(root, "unrealize", G_CALLBACK(g_cancellable_cancel), request->cancel);
  g_object_set_data(G_OBJECT(root), "luxe-lock-pending", GINT_TO_POINTER(1));
  luxe_lock_request_async(LUXELOCK_PATH, 10000, request->cancel, screen_lock_ready, request);
}

const int idle_minutes[8] = { 1, 2, 3, 5, 10, 15, 30, 60 };
const char *idle_labels[8] = {
  "1 minute", "2 minutes", "3 minutes", "5 minutes",
  "10 minutes", "15 minutes", "30 minutes", "1 hour"
};

const char *lid_key_names[3] = {
  "HandleLidSwitch", "HandleLidSwitchExternalPower", "HandleLidSwitchDocked"
};

void
show_error (GtkWidget *parent, const char *title, const char *detail)
{
  GtkDialog *dialog = GTK_DIALOG(luxe_choice_dialog_new(title ? title : "Error", detail));
  GtkRoot *root;

  luxe_choice_add_response(dialog, "close", "OK");
  root = gtk_widget_get_root (parent);
  if (root != NULL)
    luxe_dialog_present(GTK_WINDOW(dialog), GTK_WIDGET(root));
  else { g_object_ref_sink(dialog); g_object_unref(dialog); }
}

void
toast (LuxePowerWindow *win, const char *message)
{
  LuxeToast *t = luxe_toast_new (message);

  luxe_toast_set_timeout (t, 3);
  luxe_toast_overlay_add_toast (GTK_OVERLAY (win->toast_overlay), t);
}

GtkWidget *
dim_label (const char *text)
{
  GtkWidget *label = gtk_label_new (text != NULL ? text : "");

  gtk_label_set_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_add_css_class (label, "dim-label");
  return label;
}

GSettings *
lock_settings_open (void)
{
  GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
  GSettingsSchema *schema;

  if (source == NULL)
    return NULL;
  schema = g_settings_schema_source_lookup (source, LUXEPOWER_SCHEMA_ID, FALSE);
  if (schema == NULL)
    return NULL;
  g_settings_schema_unref (schema);
  return g_settings_new (LUXEPOWER_SCHEMA_ID);
}
