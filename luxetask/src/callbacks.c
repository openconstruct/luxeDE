/*
 *  xfce4-taskmanager - very simple taskmanger
 *
 *  Copyright (c) 2006 Johannes Zellner, <webmaster@nebulon.de>
 *  Copyright (C) 2026 Jerry Howell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include "luxe-gtk-ui.h"

#include "callbacks.h"

extern guint rID;
extern GtkWidget *main_window;
extern GtkApplication *luxetask_app;

void on_button3_toggled_event(GtkToggleButton *button, gpointer data)
{
    (void) data;
    full_view = gtk_toggle_button_get_active(button);
    change_list_store_view();
}

void on_treeview_right_pressed (GtkGestureClick *gesture, int n_press,
                                double x, double y, gpointer data)
{
    (void) n_press; (void) x; (void) y; (void) data;
    guint button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture));
    if (button == GDK_BUTTON_SECONDARY)
    {
        GtkTreePath *path = NULL;
        gint bx, by;
        gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(treeview), x, y, &bx, &by);
        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview), bx, by, &path, NULL, NULL, NULL))
            return;
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_path_free(path);
        GMenuModel *model = create_task_menu_model();
        GtkWidget *pop = gtk_popover_menu_new_from_model (model);
        g_object_unref(model);
        gtk_widget_set_parent (pop, treeview);
        gtk_popover_set_has_arrow (GTK_POPOVER (pop), FALSE);
        gtk_popover_set_pointing_to (GTK_POPOVER (pop),
            &(GdkRectangle){ (int) x, (int) y, 1, 1 });
        gtk_popover_popup (GTK_POPOVER (pop));
        g_signal_connect_swapped (pop, "closed", G_CALLBACK (gtk_widget_unparent), pop);
    }
}

gboolean on_window_close_request (GtkWindow *win, gpointer data)
{
    (void) win; (void) data;
    on_quit ();
    return FALSE; /* let the window close after saving config */
}

typedef struct {
    gint task_action;
    gint handle;
} TaskActionConfirm;

typedef struct {
    gint pid;
    guint64 start_time;
    gint priority;
} TaskPriorityConfirm;

static void on_task_confirmed(gboolean confirmed, gpointer data)
{
    TaskActionConfirm *request = data;
    if (confirmed) {
        send_signal_to_task(request->handle, request->task_action);
        if (main_window) refresh_task_list();
    }
    close(request->handle);
    g_free(request);
}

static gboolean selected_task(gint *pid, guint64 *start_time)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) return FALSE;
    gtk_tree_model_get(model, &iter, COLUMN_PID, pid, COLUMN_START_TIME, start_time, -1);
    return *pid > 0;
}

static void on_priority_confirmed(gboolean confirmed, gpointer data)
{
    TaskPriorityConfirm *request = data;
    if (confirmed) {
        set_priority_to_task(request->pid, request->start_time, request->priority);
        if (main_window) refresh_task_list();
    }
    g_free(request);
}

void handle_task_menu(GtkWidget *widget, gchar *signal)
{
    (void)widget;
    gint action = g_strcmp0(signal, "KILL") == 0 ? SIGNAL_KILL :
                  g_strcmp0(signal, "TERM") == 0 ? SIGNAL_TERM :
                  g_strcmp0(signal, "STOP") == 0 ? SIGNAL_STOP :
                  g_strcmp0(signal, "CONT") == 0 ? SIGNAL_CONT : SIGNAL_NO;
    gint pid;
    guint64 start_time;
    if (!action || !selected_task(&pid, &start_time)) return;
    gint handle = task_open_handle(pid, start_time);
    if (handle < 0) return;
    if (action == SIGNAL_STOP || action == SIGNAL_CONT) {
        send_signal_to_task(handle, action);
        close(handle);
        refresh_task_list();
    } else {
        TaskActionConfirm *request = g_new(TaskActionConfirm, 1);
        request->task_action = action;
        request->handle = handle;
        confirm(action == SIGNAL_KILL ? _("Really kill the task?") : _("Really terminate the task?"),
                on_task_confirmed, request);
    }
}

void handle_prio_menu(GtkWidget *widget, gchar *prio)
{
    (void)widget;
    gint pid;
    gint64 priority;
    guint64 start_time;
    if (prio && g_ascii_string_to_signed(prio, 10, -20, 19, &priority, NULL) &&
        selected_task(&pid, &start_time)) {
        GtkTreeModel *model;
        GtkTreeIter iter;
        gchar *name = NULL;
        if (!gtk_tree_selection_get_selected(selection, &model, &iter))
            return;
        gtk_tree_model_get(model, &iter, COLUMN_NAME, &name, -1);
        TaskPriorityConfirm *request = g_new(TaskPriorityConfirm, 1);
        request->pid = pid;
        request->start_time = start_time;
        request->priority = (gint) priority;
        gchar *question = g_strdup_printf(
            _("Change priority of %s (PID %d) to %d?"),
            name ? name : _("Unknown task"), pid, request->priority);
        g_free(name);
        confirm(question, on_priority_confirmed, request);
        g_free(question);
    }
}

void on_quit(void)
{
    /* "close-request" + "destroy" both fire on window close; guard
     * against double-run (double-free of config_file). */
    static gboolean done = FALSE;
    if (done)
        return;
    done = TRUE;
    if (rID)
    {
        g_source_remove (rID);
        rID = 0;
    }
    save_config();
    g_clear_pointer (&config_file, g_free);

    if (luxetask_app)
        g_application_quit (G_APPLICATION (luxetask_app));
}
