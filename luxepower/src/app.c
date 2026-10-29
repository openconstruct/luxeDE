/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "app.h"

#include <glib/gstdio.h>

static void
window_on_map (GtkWidget *widget, gpointer data)
{
  (void) widget;

  LuxePowerWindow *win = data;

  if (!win->power.loaded) power_page_load (&win->power);
  display_page_refresh (&win->display);
  status_page_refresh (&win->status, FALSE);
}

static void
window_on_refresh (GSimpleAction *action, GVariant *value, gpointer data)
{
  (void) action;
  (void) value;

  LuxePowerWindow *win = data;

  power_page_load (&win->power);
  display_page_refresh (&win->display);
  status_page_refresh (&win->status, FALSE);
}

static void
window_on_lock_now (GSimpleAction *action, GVariant *value, gpointer data)
{
  (void) action;
  (void) value;

  LuxePowerWindow *win = data;
  request_screen_lock(win->window);
}

void
window_on_about (GSimpleAction *action, GVariant *value, gpointer data)
{
  (void) action;
  (void) value;

  LuxePowerWindow *win = data;
  GtkWindow *about = GTK_WINDOW (luxe_about_new ());

  g_object_set (about,
                "program-name", "LuxePower",
                "logo-icon-name", "org.luxe.LuxePower",
                "version", LUXEPOWER_VERSION,
                "website", "https://github.com/openconstruct/luxeDE",
                "copyright", "Copyright © 2026 Jerry Howell",
                "license-type", GTK_LICENSE_GPL_2_0,
                "authors", (const char *const[]) { "Jerry Howell 2026", NULL },
                "comments",
                "Power manager for the Luxe desktop: TLP control, sleep and lid "
                "actions, screen brightness, and idle screen locking.\n\n"
                "TLP interface guidance follows TLPUI by Daniel Pawlicki (d4nj1). "
                "TLP is maintained by the linrunner team. TLPUI and TLP keep "
                "their upstream licenses.",
                NULL);
  luxe_dialog_present (about, win->window);
}

static void
window_free (gpointer data)
{
  LuxePowerWindow *win = data;

  if (win == NULL)
    return;
  power_page_free (&win->power);
  if (win->display.devices != NULL)
    g_ptr_array_unref (win->display.devices);
  if (win->display.source_id != 0)
    g_source_remove (win->display.source_id);
  if (win->display.settings != NULL)
    g_object_unref (win->display.settings);
  g_clear_object(&win->status.buffer);
  g_free (win);
}

static void window_closed(GtkWidget *widget, gpointer data)
{
  LuxePowerWindow *win = data;
  win->closed = TRUE;
  g_object_set_data(G_OBJECT(widget), "luxe-power-closed", GINT_TO_POINTER(1));
  if (win->display.source_id) {
    g_source_remove(win->display.source_id);
    win->display.source_id = 0;
  }
}

static void
on_activate (GtkApplication *app, gpointer data)
{
  (void) data;

  GList *windows = gtk_application_get_windows(app);
  if (windows) {
    gtk_window_present(GTK_WINDOW(windows->data));
    return;
  }
  LuxePowerWindow *win = g_new0 (LuxePowerWindow, 1);
  GtkBox *toolbar;
  GtkHeaderBar *header;
  GtkStack *stack;
  GtkStackSwitcher *switcher;
  GSimpleAction *action;
  GMenu *menu;

  win->window = GTK_WIDGET (gtk_application_window_new (GTK_APPLICATION (app)));
  gtk_window_set_title (GTK_WINDOW (win->window), "LuxePower");
  gtk_window_set_default_size (GTK_WINDOW (win->window), 760, 640);
  gtk_window_set_icon_name (GTK_WINDOW (win->window), "org.luxe.LuxePower");
  g_object_set_data_full (G_OBJECT (win->window), "luxe-power-window", win, window_free);

  win->toast_overlay = gtk_overlay_new ();

  toolbar = GTK_BOX (luxe_toolbar_new ());
  header = GTK_HEADER_BAR (gtk_header_bar_new ());

  menu = g_menu_new ();
  g_menu_append (menu, "Refresh", "win.refresh");
  g_menu_append (menu, "Lock screen", "win.lock-now");
  g_menu_append (menu, "About LuxePower", "win.about");
  {
    GtkWidget *menu_button = gtk_menu_button_new ();

    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (menu_button), G_MENU_MODEL (menu));
    gtk_header_bar_pack_end (header, menu_button);
    g_object_unref (menu);
  }

  luxe_toolbar_add_top_bar (toolbar, GTK_WIDGET (header));

  power_page_init (&win->power, win);
  sleep_page_init (&win->sleep, win);
  display_page_init (&win->display, win);
  status_page_init (&win->status, win);

  stack = GTK_STACK (gtk_stack_new ());
  luxe_stack_add_titled_with_icon (stack, win->power.page, "power", "Power", "battery-symbolic");
  luxe_stack_add_titled_with_icon (stack, win->sleep.page, "sleep", "Sleep & lid",
                                       "weather-clear-night-symbolic");
  luxe_stack_add_titled_with_icon (stack, win->display.page, "display", "Display",
                                       "display-brightness-symbolic");
  luxe_stack_add_titled_with_icon (stack, win->status.page, "status", "Status",
                                       "dialog-information-symbolic");

  switcher = GTK_STACK_SWITCHER (gtk_stack_switcher_new ());
  g_object_set (switcher, "stack", stack, "policy", 0, NULL);
  luxe_toolbar_add_bottom_bar (toolbar, GTK_WIDGET (switcher));

  luxe_toolbar_set_content (toolbar, GTK_WIDGET (stack));
  gtk_overlay_set_child (GTK_OVERLAY (win->toast_overlay), GTK_WIDGET (toolbar));
  luxe_window_set_content (GTK_WINDOW (win->window), win->toast_overlay);

  action = g_simple_action_new ("refresh", NULL);
  g_signal_connect (action, "activate", G_CALLBACK (window_on_refresh), win);
  g_action_map_add_action (G_ACTION_MAP (win->window), G_ACTION (action));
  g_object_unref (action);

  action = g_simple_action_new ("lock-now", NULL);
  g_signal_connect (action, "activate", G_CALLBACK (window_on_lock_now), win);
  g_action_map_add_action (G_ACTION_MAP (win->window), G_ACTION (action));
  g_object_unref (action);

  action = g_simple_action_new ("about", NULL);
  g_signal_connect (action, "activate", G_CALLBACK (window_on_about), win);
  g_action_map_add_action (G_ACTION_MAP (win->window), G_ACTION (action));
  g_object_unref (action);

  g_signal_connect (win->window, "map", G_CALLBACK (window_on_map), win);
  g_signal_connect(win->window, "unrealize", G_CALLBACK(window_closed), win);

  gtk_window_present (GTK_WINDOW (win->window));
}

int
main (int argc, char **argv)
{
  GtkApplication *app;
  int status;

  luxe_ui_init ();
  app = GTK_APPLICATION (luxe_application_new (LUXEPOWER_APP_ID, G_APPLICATION_DEFAULT_FLAGS));
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  while (gtk_application_get_windows(GTK_APPLICATION(app)))
    gtk_window_destroy(gtk_application_get_windows(GTK_APPLICATION(app))->data);
  g_object_unref (app);
  return status;
}
