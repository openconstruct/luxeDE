#include "../luxe-about.h"
/* $Id: interface.c 3940 2008-02-10 22:48:45Z nebulon $
 *
 * Copyright (c) 2006 Johannes Zellner, <webmaster@nebulon.de>
 * Copyright (C) 2026 Jerry Howell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* 2008-04-26 modified by Hong Jen Yee to be used in LXDE. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include "luxe-gtk-ui.h"
#include "luxe-spacing.h"
#include "interface.h"

void show_preferences(GtkWindow *parent);
extern gint refresh_interval;
extern guint rID;
extern GtkApplication *luxetask_app;

/* Single definitions for the globals declared extern in interface.h */
GtkTreeStore *list_store;
GtkTreeSelection *selection;
GtkWidget *treeview;
GtkWidget *cpu_usage_progress_bar;
GtkWidget *mem_usage_progress_bar;
GtkTreeViewColumn *column;

/* pid -> GtkTreeRowReference* for O(1) row lookup (avoids full scans). */
static GHashTable *pid_to_row = NULL;

void cleanup_task_view(void)
{
    g_clear_pointer(&pid_to_row, g_hash_table_unref);
    g_clear_object(&list_store);
}


static void on_quit_action (GSimpleAction *action, GVariant *param, gpointer win);
static void on_preferences_action (GSimpleAction *action, GVariant *param, gpointer win);
static void on_about_action (GSimpleAction *action, GVariant *param, gpointer win);
static void on_task_action (GSimpleAction *action, GVariant *param, gpointer win);
static void on_prio_action (GSimpleAction *action, GVariant *param, gpointer win);
static void on_show_tasks_change (GSimpleAction *action, GVariant *value, gpointer win);
static void on_show_cached_change (GSimpleAction *action, GVariant *value, gpointer win);

static const GActionEntry win_actions[] = {
    { .name = "quit", .activate = on_quit_action },
    { .name = "preferences", .activate = on_preferences_action },
    { .name = "about", .activate = on_about_action },
    { .name = "task", .activate = on_task_action, .parameter_type = "s" },
    { .name = "prio", .activate = on_prio_action, .parameter_type = "s" },
    { .name = "show-user-tasks", .state = "false", .change_state = on_show_tasks_change },
    { .name = "show-root-tasks", .state = "false", .change_state = on_show_tasks_change },
    { .name = "show-other-tasks", .state = "false", .change_state = on_show_tasks_change },
    { .name = "show-full-path", .state = "false", .change_state = on_show_tasks_change },
    { .name = "show-cached-as-free", .state = "false", .change_state = on_show_cached_change },
};

GMenuModel *
create_task_menu_model (void)
{
    GMenu *menu = g_menu_new ();
    GMenuItem *it;
    /* NOTE: set_action_and_target_value() is used instead of embedding
     * targets in detailed-name strings ("win.task::STOP"), because a
     * bare word is not valid GVariant text and the item would not fire. */
    it = g_menu_item_new (_("Stop"), NULL);
    g_menu_item_set_action_and_target_value (it, "win.task",
                                             g_variant_new_string ("STOP"));
    g_menu_append_item (menu, it); g_object_unref (it);
    it = g_menu_item_new (_("Continue"), NULL);
    g_menu_item_set_action_and_target_value (it, "win.task",
                                             g_variant_new_string ("CONT"));
    g_menu_append_item (menu, it); g_object_unref (it);
    it = g_menu_item_new (_("Term"), NULL);
    g_menu_item_set_action_and_target_value (it, "win.task",
                                             g_variant_new_string ("TERM"));
    g_menu_append_item (menu, it); g_object_unref (it);
    it = g_menu_item_new (_("Kill"), NULL);
    g_menu_item_set_action_and_target_value (it, "win.task",
                                             g_variant_new_string ("KILL"));
    g_menu_append_item (menu, it); g_object_unref (it);

    GMenu *prio = g_menu_new ();
    const char *vals[] = { "-10", "-5", "0", "5", "10", NULL };
    for (int i = 0; vals[i]; i++)
    {
        it = g_menu_item_new (vals[i], NULL);
        g_menu_item_set_action_and_target_value (it, "win.prio",
                                                 g_variant_new_string (vals[i]));
        g_menu_append_item (prio, it); g_object_unref (it);
    }
    GMenuItem *prio_item = g_menu_item_new (_("Priority"), NULL);
    g_menu_item_set_submenu (prio_item, G_MENU_MODEL (prio));
    g_menu_append_item (menu, prio_item);
    g_object_unref (prio_item);
    g_object_unref (prio);
    return G_MENU_MODEL (menu);
}

static GMenuModel *
create_hamburger_model (void)
{
    GMenu *bar = g_menu_new ();

    GMenu *file = g_menu_new ();
    g_menu_append (file, _("_Quit"), "win.quit");

    GMenu *view = g_menu_new ();
    GMenuItem *it;
    it = g_menu_item_new (_("Show user tasks"), "win.show-user-tasks");
    g_menu_append_item (view, it); g_object_unref (it);
    it = g_menu_item_new (_("Show root tasks"), "win.show-root-tasks");
    g_menu_append_item (view, it); g_object_unref (it);
    it = g_menu_item_new (_("Show other tasks"), "win.show-other-tasks");
    g_menu_append_item (view, it); g_object_unref (it);
    it = g_menu_item_new (_("Show full cmdline"), "win.show-full-path");
    g_menu_append_item (view, it); g_object_unref (it);
    it = g_menu_item_new (_("Show memory used by cache as free"), "win.show-cached-as-free");
    g_menu_append_item (view, it); g_object_unref (it);
    g_menu_append (view, _("Preferences"), "win.preferences");

    GMenu *help = g_menu_new ();
    g_menu_append (help, _("About"), "win.about");

    g_menu_append_section (bar, NULL, G_MENU_MODEL (view));
    g_menu_append_section (bar, NULL, G_MENU_MODEL (help));
    g_menu_append_section (bar, NULL, G_MENU_MODEL (file));
    g_object_unref (file);
    g_object_unref (view);
    g_object_unref (help);
    return G_MENU_MODEL (bar);
}

GtkWidget* create_main_window (GtkApplication *app)
{
    GtkWidget *window;
    GtkWidget *header;
    GtkWidget *menu_button;
    GtkWidget *vbox_outer;
    GtkWidget *vbox1;
    GtkWidget *scrolledwindow1;
    GtkWidget *button3;

    GtkWidget *system_info_box;

    window = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (window), _("LuxeTask"));
    gtk_window_set_default_size (GTK_WINDOW (window), win_width, win_height);
    gtk_window_set_icon_name (GTK_WINDOW (window), "org.luxe.Task");

    g_action_map_add_action_entries (G_ACTION_MAP (window),
                                     win_actions, G_N_ELEMENTS (win_actions),
                                     window);
    /* sync initial toggle states (set after actions exist) */
    {
        GAction *a;
        a = g_action_map_lookup_action (G_ACTION_MAP (window), "show-user-tasks");
        g_simple_action_set_state (G_SIMPLE_ACTION (a),
            g_variant_new_boolean (show_user_tasks));
        a = g_action_map_lookup_action (G_ACTION_MAP (window), "show-root-tasks");
        g_simple_action_set_state (G_SIMPLE_ACTION (a),
            g_variant_new_boolean (show_root_tasks));
        a = g_action_map_lookup_action (G_ACTION_MAP (window), "show-other-tasks");
        g_simple_action_set_state (G_SIMPLE_ACTION (a),
            g_variant_new_boolean (show_other_tasks));
        a = g_action_map_lookup_action (G_ACTION_MAP (window), "show-full-path");
        g_simple_action_set_state (G_SIMPLE_ACTION (a),
            g_variant_new_boolean (show_full_path));
        a = g_action_map_lookup_action (G_ACTION_MAP (window), "show-cached-as-free");
        g_simple_action_set_state (G_SIMPLE_ACTION (a),
            g_variant_new_boolean (show_cached_as_free));
    }

    vbox_outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    /* R2 headerbar chrome: hamburger menu plus the details toggle. */
    header = gtk_header_bar_new ();
    menu_button = gtk_menu_button_new ();
    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (menu_button), "open-menu-symbolic");
    GMenuModel *hamburger = create_hamburger_model ();
    gtk_menu_button_set_menu_model (GTK_MENU_BUTTON (menu_button), hamburger);
    g_object_unref(hamburger);
    gtk_widget_set_tooltip_text (menu_button, _("Main menu"));
    gtk_header_bar_pack_end (GTK_HEADER_BAR (header), menu_button);

    button3 = gtk_toggle_button_new_with_label (_("more details"));
    gtk_widget_add_css_class (button3, "flat");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button3), full_view);
    gtk_header_bar_pack_end (GTK_HEADER_BAR (header), button3);

    /* GtkApplicationWindow hosts the header bar in an GtkBox,
     * never via gtk_window_set_titlebar. */
    {
        GtkWidget *toolbar_view = luxe_toolbar_new ();
        luxe_toolbar_add_top_bar (GTK_BOX (toolbar_view), header);
        luxe_toolbar_set_content (GTK_BOX (toolbar_view), vbox_outer);
        luxe_window_set_content (GTK_WINDOW (window),
                                            toolbar_view);
    }

    /* keyboard shortcuts (replaces GtkAccelGroup Escape / Ctrl+W) */
    {
        /* NOTE: gtk_shortcut_controller_new() returns GtkShortcutController*
         * on GTK 4.6 but GtkEventController* on newer 4.x; keep the base
         * type + casts so both compile. */
        GtkEventController *ctl = gtk_shortcut_controller_new ();
        gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (ctl),
                                           GTK_SHORTCUT_SCOPE_MANAGED);
        GtkShortcutTrigger *esc = gtk_keyval_trigger_new (GDK_KEY_Escape, 0);
        GtkShortcutTrigger *ctrlw = gtk_keyval_trigger_new (GDK_KEY_w, GDK_CONTROL_MASK);
        GtkShortcutAction *act = gtk_named_action_new ("win.quit");
        gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (ctl),
                                              gtk_shortcut_new (esc, act));
        gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (ctl),
                                              gtk_shortcut_new (ctrlw, g_object_ref(act)));
        gtk_widget_add_controller (window, ctl);
    }

    /* window content (bible spacing: 10px outer margins, 6px related) */
    vbox1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, LUXE_SPACING_INNER);
    gtk_widget_set_margin_start (vbox1, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_end (vbox1, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_top (vbox1, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_bottom (vbox1, LUXE_SPACING_OUTER);
    gtk_box_append (GTK_BOX (vbox_outer), vbox1);
    gtk_widget_set_vexpand (vbox1, TRUE);

    system_info_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER);
    gtk_box_set_homogeneous (GTK_BOX (system_info_box), TRUE);
    gtk_box_append (GTK_BOX (vbox1), system_info_box);

    cpu_usage_progress_bar = gtk_progress_bar_new ();
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (cpu_usage_progress_bar), TRUE);
    gtk_progress_bar_set_text (GTK_PROGRESS_BAR (cpu_usage_progress_bar), _("cpu usage"));
    gtk_box_append (GTK_BOX (system_info_box), cpu_usage_progress_bar);

    mem_usage_progress_bar = gtk_progress_bar_new ();
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (mem_usage_progress_bar), TRUE);
    gtk_progress_bar_set_text (GTK_PROGRESS_BAR (mem_usage_progress_bar), _("memory usage"));
    gtk_box_append (GTK_BOX (system_info_box), mem_usage_progress_bar);

    scrolledwindow1 = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow1), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_append (GTK_BOX (vbox1), scrolledwindow1);
    gtk_widget_set_vexpand (scrolledwindow1, TRUE);

    treeview = gtk_tree_view_new ();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolledwindow1), treeview);

    create_list_store();

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(list_store));

    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list_store), COLUMN_TIME, GTK_SORT_DESCENDING);

    g_signal_connect (window, "close-request", G_CALLBACK (on_window_close_request), NULL);
    {
        GtkGesture *right = gtk_gesture_click_new ();
        gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (right), 0);
        g_signal_connect (right, "pressed", G_CALLBACK (on_treeview_right_pressed), NULL);
        gtk_widget_add_controller (treeview, GTK_EVENT_CONTROLLER (right));
    }
    g_signal_connect (button3, "toggled", G_CALLBACK (on_button3_toggled_event), NULL);

    return window;
}

static void numeric_cell(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
                         GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    (void)col;
    gint column_id = GPOINTER_TO_INT(data);
    gchar buffer[64];
    if (column_id == COLUMN_MEM || column_id == COLUMN_RSS) {
        guint64 bytes;
        gtk_tree_model_get(model, iter, column_id, &bytes, -1);
        size_to_string(buffer, bytes);
    } else if (column_id == COLUMN_TIME) {
        gdouble percent;
        gtk_tree_model_get(model, iter, column_id, &percent, -1);
        g_snprintf(buffer, sizeof(buffer), "%.0f%%", percent);
    } else {
        gint number;
        gtk_tree_model_get(model, iter, column_id, &number, -1);
        g_snprintf(buffer, sizeof(buffer), "%d", number);
    }
    g_object_set(renderer, "text", buffer, NULL);
}

void create_list_store(void)
{
    static const gchar *titles[] = { N_("Command"), N_("User"), N_("CPU%"),
        N_("RSS"), N_("VM-Size"), N_("PID"), N_("State"), N_("Prio"), N_("PPID") };
    list_store = gtk_tree_store_new(10, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE,
        G_TYPE_UINT64, G_TYPE_UINT64, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT,
        G_TYPE_UINT64);
    pid_to_row = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                      (GDestroyNotify)gtk_tree_row_reference_free);
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++) {
        GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_title(column, _(titles[i]));
        gtk_tree_view_column_pack_start(column, renderer, TRUE);
        if (i == COLUMN_NAME || i == COLUMN_UNAME || i == COLUMN_STATE)
            gtk_tree_view_column_add_attribute(column, renderer, "text", i);
        else {
            g_object_set(renderer, "xalign", 1.0, NULL);
            gtk_tree_view_column_set_cell_data_func(column, renderer, numeric_cell,
                                                    GINT_TO_POINTER(i), NULL);
        }
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_sort_column_id(column, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    }
    change_list_store_view();
}

void show_about_dialog(GtkWindow *parent)
{
    luxe_show_about_full(parent, _("LuxeTask"), VERSION,
                         _("Lightweight task manager for Luxe project"), _("translator-credits"));
}

void change_list_store_view(void)
{
    gtk_tree_view_column_set_visible (gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), COLUMN_PPID), full_view);
    gtk_tree_view_column_set_visible (gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), COLUMN_MEM), full_view);
    //gtk_tree_view_column_set_visible (gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), COLUMN_RSS), full_view);
    gtk_tree_view_column_set_visible (gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), COLUMN_STATE), full_view);
    gtk_tree_view_column_set_visible (gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), COLUMN_PRIO), full_view);

    if(!show_root_tasks && !show_other_tasks)
        gtk_tree_view_column_set_visible (gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), COLUMN_UNAME), full_view);
    else
        gtk_tree_view_column_set_visible (gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), COLUMN_UNAME), TRUE);
}

void fill_list_item(guint i, GtkTreeIter *iter)
{
    struct task *task = &g_array_index(task_array, struct task, i);
    gtk_tree_store_set(list_store, iter,
        COLUMN_NAME, task->name, COLUMN_UNAME, task->uname,
        COLUMN_TIME, (gdouble)task->time_percentage,
        COLUMN_MEM, task->size * 1024, COLUMN_RSS, task->rss * 1024,
        COLUMN_PID, task->pid, COLUMN_PPID, task->ppid,
        COLUMN_STATE, task->state, COLUMN_PRIO, task->prio,
        COLUMN_START_TIME, task->start_time, -1);
}

void add_new_list_item(guint i)
{
        GtkTreeIter iter;
        gtk_tree_store_append(GTK_TREE_STORE(list_store), &iter, NULL);

        fill_list_item(i, &iter);

        if (pid_to_row != NULL)
        {
            struct task *task = &g_array_index(task_array, struct task, i);
            GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(list_store), &iter);
            if (path != NULL)
            {
                GtkTreeRowReference *ref = gtk_tree_row_reference_new(GTK_TREE_MODEL(list_store), path);
                gtk_tree_path_free(path);
                if (ref != NULL)
                    g_hash_table_replace(pid_to_row, GINT_TO_POINTER((gint)task->pid), ref);
            }
        }
}

static gboolean find_task_row(pid_t pid, GtkTreeIter *iter)
{
    GtkTreeRowReference *ref = g_hash_table_lookup(pid_to_row, GINT_TO_POINTER(pid));
    GtkTreePath *path = ref ? gtk_tree_row_reference_get_path(ref) : NULL;
    if (!path) return FALSE;
    gboolean found = gtk_tree_model_get_iter(GTK_TREE_MODEL(list_store), iter, path);
    gtk_tree_path_free(path);
    return found;
}

void refresh_list_item(guint i)
{
    GtkTreeIter iter;
    if (find_task_row(g_array_index(task_array, struct task, i).pid, &iter))
        fill_list_item(i, &iter);
}

void remove_list_item(pid_t pid)
{
    GtkTreeIter iter;
    gboolean found = find_task_row(pid, &iter);
    g_hash_table_remove(pid_to_row, GINT_TO_POINTER(pid));
    if (found) gtk_tree_store_remove(list_store, &iter);
}


/* change the task view (user, root, other) */
void change_task_view(void)
{
    gint i = 0;

    gtk_tree_store_clear(GTK_TREE_STORE(list_store));
    if (pid_to_row != NULL)
        g_hash_table_remove_all(pid_to_row);
    change_list_store_view();

    for(i = 0; i < tasks; i++)
    {
        struct task *task = &g_array_index(task_array, struct task, i);

        if(task_is_visible(task))
            add_new_list_item(i);
    }

    refresh_task_list();
}

void change_full_path(void)
{
    refresh_task_list();
}

static void on_prefs_apply(GtkButton *button, GtkWindow *dialog)
{
    (void)button;
    GtkSpinButton *spin = g_object_get_data(G_OBJECT(dialog), "refresh-spin");
    refresh_interval = gtk_spin_button_get_value_as_int(spin);
    if (rID) g_source_remove(rID);
    rID = g_timeout_add_seconds(refresh_interval, refresh_task_list_cb, NULL);
    luxe_dialog_close(dialog);
}

void show_preferences(GtkWindow *parent)
{
    GtkWindow *dlg = luxe_dialog_new();
    gtk_window_set_title(dlg, _("Preferences"));
    luxe_dialog_set_width(dlg, 400);
    GtkWidget *toolbar = luxe_toolbar_new();
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), FALSE);
    GtkWidget *cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
    GtkWidget *apply = gtk_button_new_with_mnemonic(_("_OK"));
    gtk_widget_add_css_class(apply, "suggested-action");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), apply);
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(luxe_dialog_close), dlg);
    g_signal_connect(apply, "clicked", G_CALLBACK(on_prefs_apply), dlg);
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar), header);
    GtkWidget *refresh_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER);
    gtk_widget_set_margin_start(refresh_box, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_end(refresh_box, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_top(refresh_box, LUXE_SPACING_OUTER);
    gtk_widget_set_margin_bottom(refresh_box, LUXE_SPACING_OUTER);
    GtkWidget *lbl = gtk_label_new(_("Refresh rate (seconds):"));
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    GtkWidget *refresh_spin = gtk_spin_button_new_with_range(1, 60, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(refresh_spin), refresh_interval);
    gtk_box_append (GTK_BOX (refresh_box), lbl);
    gtk_box_append (GTK_BOX (refresh_box), refresh_spin);
    g_object_set_data(G_OBJECT(dlg), "refresh-spin", refresh_spin);
    luxe_toolbar_set_content(GTK_BOX(toolbar), refresh_box);
    luxe_window_set_content(dlg, toolbar);
    luxe_dialog_present(dlg, GTK_WIDGET(parent));
}

/* --- win.* action handlers (back the GMenu models above) --- */

static void
on_quit_action (GSimpleAction *action, GVariant *param, gpointer win)
{
    (void) action; (void) param; (void) win;
    on_quit ();
}

static void
on_preferences_action (GSimpleAction *action, GVariant *param, gpointer win)
{
    (void) action; (void) param;
    show_preferences (GTK_WINDOW (win));
}

static void
on_about_action (GSimpleAction *action, GVariant *param, gpointer win)
{
    (void) action; (void) param;
    show_about_dialog (GTK_WINDOW (win));
}

static void
on_task_action (GSimpleAction *action, GVariant *param, gpointer win)
{
    (void) action; (void) win;
    handle_task_menu (NULL, (gchar *) g_variant_get_string (param, NULL));
}

static void
on_prio_action (GSimpleAction *action, GVariant *param, gpointer win)
{
    (void) action; (void) win;
    handle_prio_menu (NULL, (gchar *) g_variant_get_string (param, NULL));
}

static void
on_show_tasks_change (GSimpleAction *action, GVariant *value, gpointer win)
{
    gboolean v = g_variant_get_boolean (value);
    const char *name = g_action_get_name (G_ACTION (action));
    (void) win;
    g_simple_action_set_state (action, value);
    if (!g_strcmp0 (name, "show-user-tasks"))
        show_user_tasks = v;
    else if (!g_strcmp0 (name, "show-root-tasks"))
        show_root_tasks = v;
    else if (!g_strcmp0 (name, "show-other-tasks"))
        show_other_tasks = v;
    else if (!g_strcmp0 (name, "show-full-path"))
    {
        show_full_path = v;
        change_full_path ();
        return;
    }
    change_task_view ();
}

static void
on_show_cached_change (GSimpleAction *action, GVariant *value, gpointer win)
{
    (void) win;
    g_simple_action_set_state (action, value);
    show_cached_as_free = g_variant_get_boolean (value);
    change_task_view ();
}
