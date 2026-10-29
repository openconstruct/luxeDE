#include "../../luxe-about.h"
#include "luxe-dialogs-gtk.h"
/*
 * Copyright (C) 2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 * Copyright (C) 2026 Jerry Howell
 *
 * This file is a part of LXHotkey project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "luxehotkey.h"
#include "edit.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"

/* The main window is an GtkApplicationWindow owned by an GtkApplication:
 * module_gtk_run() creates the application and runs it; on_gtk_activate()
 * builds and presents the window. Closing the last window quits the run. */

static void set_actions_list(PluginData *data);
static void set_apps_list(PluginData *data);
static void set_action_enabled(PluginData *data, GSimpleAction *action,
                               GtkWidget *button, gboolean enabled);
static gboolean do_reload(PluginData *data);
static gboolean do_save(PluginData *data);

typedef enum { PENDING_RELOAD, PENDING_CLOSE } PendingDecision;

static gboolean has_unsaved_changes(PluginData *data)
{
    return data->save_action && g_action_get_enabled(G_ACTION(data->save_action));
}

static void finish_pending_decision(gint choice, gpointer user_data)
{
    PluginData *data = user_data;
    PendingDecision decision;

    if (!data->win || !GTK_IS_WINDOW(data->win))
        return;
    decision = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(data->win), "pending-decision"));
    data->decision_pending = FALSE;
    if (choice < 0 || choice == 0)
        return;
    if (choice == 1 && !do_save(data))
        return;
    if (decision == PENDING_RELOAD) {
        gboolean discarded = choice == 2;
        if (discarded)
            set_action_enabled(data, data->save_action, data->save_button, FALSE);
        if (!do_reload(data) && discarded)
            set_action_enabled(data, data->save_action, data->save_button, TRUE);
    }
    else {
        if (choice == 2)
            set_action_enabled(data, data->save_action, data->save_button, FALSE);
        data->allow_close = TRUE;
        gtk_window_close(GTK_WINDOW(data->win));
    }
}

static void ask_pending_decision(PluginData *data, PendingDecision decision)
{
    if (data->decision_pending || !data->win)
        return;
    data->decision_pending = TRUE;
    g_object_set_data(G_OBJECT(data->win), "pending-decision", GINT_TO_POINTER(decision));
    luxe_ask(GTK_WINDOW(data->win),
             decision == PENDING_RELOAD ? _("Save changes before reloading?")
                                        : _("Save changes before closing?"),
             NULL, (const char *const[]){_("Cancel"), _("Save"), _("Discard"), NULL},
             0, 1, finish_pending_decision, data);
}

void _main_refresh(PluginData *data)
{
    set_actions_list(data);
    set_apps_list(data);
}

/* Action sensitivity helpers: each logical action exists both as a
 * GSimpleAction (menus) and, where applicable, as a toolbar button.
 * The button guard is load-bearing: during window teardown GTK disposes
 * children in order and selection/switch-page handlers can still fire
 * after sibling buttons are already finalized (dangling, non-NULL). */
static void set_action_enabled(PluginData *data, GSimpleAction *action,
                               GtkWidget *button, gboolean enabled)
{
    if(action)
        g_simple_action_set_enabled(action, enabled);
    if(button && GTK_IS_WIDGET(button))
        gtk_widget_set_sensitive(button, enabled);
}

static gboolean do_reload(PluginData *data)
{
    GError *error = NULL;
    gpointer replacement;

    if (has_unsaved_changes(data)) {
        ask_pending_decision(data, PENDING_RELOAD);
        return FALSE;
    }

    replacement = data->cb->load(*data->config, &error);
    if (!replacement)
    {
        if (error) {
            _show_error(_("Problems loading configuration: "), error);
            g_error_free(error);
        }
        return FALSE;
    }
    _edit_cleanup(data);
    *data->config = replacement;
    _main_refresh(data);
    set_action_enabled(data, data->save_action, data->save_button, FALSE);
    return TRUE;
}

static gboolean do_save(PluginData *data)
{
    GError *error = NULL;

    if (data->cb->save(*data->config, &error))
    {
        set_action_enabled(data, data->save_action, data->save_button, FALSE);
        return TRUE;
    }
    else
    {
        _show_error(_("Problems saving configuration: "), error);
        g_error_free(error);
        return FALSE;
    }
}

static void do_quit(PluginData *data)
{
    if (data->win && GTK_IS_WINDOW(data->win))
        gtk_window_close(GTK_WINDOW(data->win));
    else
    {
        GApplication *app = g_application_get_default();
        if (app)
            g_application_quit(app);
    }
}

static void do_new(PluginData *data)
{
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(data->current_page));
    _edit_action(data, NULL);
}

static void do_panel_menu(PluginData *data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (data->edit_window)
    {
        gtk_window_present(GTK_WINDOW(data->edit_window));
        return;
    }
    if (!data->apps || !data->cb->set_app_key || !data->cb->get_app_options)
        return;
    gtk_notebook_set_current_page(data->notebook,
        gtk_notebook_page_num(data->notebook, GTK_WIDGET(data->apps)));
    data->current_page = data->apps;
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(data->apps));
    /* Reuse an existing binding so this shortcut can also be changed. */
    model = gtk_tree_view_get_model(data->apps);
    if (gtk_tree_model_get_iter_first(model, &iter))
        do
        {
            LuxeHotkeyApp *app = NULL;
            gtk_tree_model_get(model, &iter, 3, &app, -1);
            if (app && g_strcmp0(app->exec, "luxepanelctl menu") == 0)
            {
                gtk_tree_selection_select_iter(gtk_tree_view_get_selection(data->apps), &iter);
                break;
            }
        } while (gtk_tree_model_iter_next(model, &iter));
    _edit_action(data, NULL);
    if (data->edit_window && data->edit_exec)
    {
        gtk_window_set_title(GTK_WINDOW(data->edit_window), _("Panel Menu Shortcut"));
        gtk_editable_set_text(GTK_EDITABLE(data->edit_exec), "luxepanelctl menu");
        gtk_widget_grab_focus(data->edit_key1);
    }
}

static void do_del(PluginData *data)
{
    LuxeHotkeyGlobal *gl = NULL;
    LuxeHotkeyApp *app = NULL;
    GError *error = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;

    _edit_cleanup(data);
    if (data->current_page == data->acts)
    {
        /* global */
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->acts),
                                            &model, &iter))
            gtk_tree_model_get(model, &iter, 4, &gl, -1);
        if (gl)
        {
            LuxeHotkeyGlobal rem_act = *gl;

            rem_act.accel1 = rem_act.accel2 = NULL;
            if (data->cb->set_wm_key(*data->config, &rem_act, &error))
            {
                set_action_enabled(data, data->save_action, data->save_button, TRUE);
                _main_refresh(data);
            }
            else
            {
                _show_error(_("Cannot delete keybinding: "), error);
                g_error_free(error);
            }
        }
    }
    else
    {
        /* application */
        if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->apps),
                                            &model, &iter))
            gtk_tree_model_get(model, &iter, 3, &app, -1);
        if (app)
        {
            LuxeHotkeyApp rem_app = *app;

            rem_app.accel1 = rem_app.accel2 = NULL;
            if (data->cb->set_app_key(*data->config, &rem_app, &error))
            {
                set_action_enabled(data, data->save_action, data->save_button, TRUE);
                _main_refresh(data);
            }
            else
            {
                _show_error(_("Cannot delete keybinding: "), error);
                g_error_free(error);
            }
        }
    }
}

static void do_edit(PluginData *data)
{
    _edit_action(data, NULL);
}

static void do_about(PluginData *data)
{
    luxe_show_about_full(data->win ? GTK_WINDOW(data->win) : NULL,
                         "LuxeHotkey", VERSION, _("Keyboard shortcuts configurator"),
                         _("translator-credits"));
}

/* GAction callbacks (menus) and GtkButton callbacks (toolbar) share
 * the do_* implementations above. */
#define DEFINE_ACTION(name) \
    static void on_##name(GSimpleAction *act, GVariant *param, gpointer user_data) \
    { (void)act; (void)param; do_##name((PluginData *)user_data); }
#define DEFINE_ACTION_BTN(name) \
    DEFINE_ACTION(name) \
    static void on_##name##_btn(GtkButton *btn, gpointer user_data) \
    { (void)btn; do_##name((PluginData *)user_data); }

DEFINE_ACTION_BTN(reload)
DEFINE_ACTION_BTN(save)
DEFINE_ACTION(quit)
DEFINE_ACTION_BTN(new)
DEFINE_ACTION_BTN(panel_menu)
DEFINE_ACTION_BTN(del)
DEFINE_ACTION_BTN(edit)
DEFINE_ACTION(about)

static const GActionEntry win_actions[] = {
    { "reload", on_reload, NULL, NULL, NULL, {0} },
    { "save", on_save, NULL, NULL, NULL, {0} },
    { "quit", on_quit, NULL, NULL, NULL, {0} },
    { "new", on_new, NULL, NULL, NULL, {0} },
    { "panel-menu", on_panel_menu, NULL, NULL, NULL, {0} },
    { "del", on_del, NULL, NULL, NULL, {0} },
    { "edit", on_edit, NULL, NULL, NULL, {0} },
    { "about", on_about, NULL, NULL, NULL, {0} }
};

static const char menu_xml[] =
"<interface>"
"  <menu id='hamburger'>"
"    <section>"
"      <item>"
"        <attribute name='label' translatable='yes'>_Reload</attribute>"
"        <attribute name='action'>win.reload</attribute>"
"      </item>"
"      <item>"
"        <attribute name='label' translatable='yes'>_Save</attribute>"
"        <attribute name='action'>win.save</attribute>"
"      </item>"
"      <item>"
"        <attribute name='label' translatable='yes'>_Quit</attribute>"
"        <attribute name='action'>win.quit</attribute>"
"      </item>"
"    </section>"
"    <section>"
"      <item>"
"        <attribute name='label' translatable='yes'>_New</attribute>"
"        <attribute name='action'>win.new</attribute>"
"      </item>"
"      <item>"
"        <attribute name='label' translatable='yes'>Panel Menu Shortcut</attribute>"
"        <attribute name='action'>win.panel-menu</attribute>"
"      </item>"
"      <item>"
"        <attribute name='label' translatable='yes'>_Delete</attribute>"
"        <attribute name='action'>win.del</attribute>"
"      </item>"
"      <item>"
"        <attribute name='label' translatable='yes'>_Edit</attribute>"
"        <attribute name='action'>win.edit</attribute>"
"      </item>"
"    </section>"
"    <section>"
"      <item>"
"        <attribute name='label' translatable='yes'>_About</attribute>"
"        <attribute name='action'>win.about</attribute>"
"      </item>"
"    </section>"
"  </menu>"
"</interface>";

static GtkWidget *tool_button_new(const char *icon_name, const char *tooltip,
                                  GCallback callback, PluginData *data)
{
    GtkWidget *btn = gtk_button_new_from_icon_name(icon_name);
    gtk_widget_set_tooltip_text(btn, tooltip);
    g_signal_connect(btn, "clicked", callback, data);
    return btn;
}

static void on_notebook_switch_page(GtkNotebook *nb, GtkWidget *page, guint num,
                                    PluginData *data)
{
    gboolean has_selection;
    gboolean can_edit;
    (void)nb; (void)num;

    _edit_cleanup(data); /* abort edit */
    data->current_page = GTK_TREE_VIEW(page);
    has_selection = gtk_tree_selection_get_selected(gtk_tree_view_get_selection(data->current_page), NULL, NULL);
    if (data->current_page == data->acts)
        can_edit = (data->cb->set_wm_key != NULL);
    else /* apps page */
        can_edit = (data->cb->set_app_key != NULL);
    set_action_enabled(data, data->del_action, data->del_button, has_selection && can_edit);
    set_action_enabled(data, data->edit_action, data->edit_button, has_selection && can_edit);
    set_action_enabled(data, data->add_action, data->add_button, can_edit);
}

static void on_selection_changed(GtkTreeSelection *selection, PluginData *data)
{
    gboolean has_selection;

    if (gtk_tree_view_get_selection(data->current_page) != selection)
        return;

    _edit_cleanup(data); /* abort edit */
    has_selection = gtk_tree_selection_get_selected(selection, NULL, NULL);
    set_action_enabled(data, data->del_action, data->del_button, has_selection);
    set_action_enabled(data, data->edit_action, data->edit_button, has_selection);
}

static void on_row_activated(GtkTreeView *view, GtkTreePath *path,
                             GtkTreeViewColumn *column, PluginData *data)
{
    (void)view; (void)path; (void)column;
    _edit_action(data, NULL);
}

static void set_actions_list(PluginData *data)
{
    GtkListStore *model = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                                G_TYPE_STRING, G_TYPE_STRING,
                                                G_TYPE_POINTER);
    GList *acts = data->cb->get_wm_keys(*data->config, "*", NULL);
    GList *l;
    LuxeHotkeyGlobal *act;
    LuxeHotkeyAttr *attr, *opt;
    char *val, *_val;
    GtkTreeIter iter;

    for (l = acts; l; l = l->next)
    {
        act = l->data;
        if (act->actions == NULL)
            continue;
        attr = act->actions->data;
        _val = val = NULL;
        opt = NULL;
        if (attr->subopts)
        {
            opt = attr->subopts->data;
            if (opt->values)
            {
                if (attr->subopts->next)
                    _val = val = g_strdup_printf("%s:%s, ...", _(opt->name), _((char *)opt->values->data));
                else
                    _val = val = g_strdup_printf("%s:%s", _(opt->name), _((char *)opt->values->data));
            }
            else if (attr->subopts->next)
                _val = val = g_strdup_printf("%s, ...", _(opt->name));
            else
                val = _(opt->name);
        }
        gtk_list_store_insert_with_values(model, &iter, -1, 0, _(attr->name),
                                                            1, val,
                                                            2, act->accel1,
                                                            3, act->accel2,
                                                            4, act, -1);
        g_free(_val);
        //FIXME: this is a stub, it should show something better than just first action
    }
    g_list_free(acts);
    gtk_tree_view_set_model(data->acts, GTK_TREE_MODEL(model));
    g_object_unref(model);
}

static void set_apps_list(PluginData *data)
{
    GtkListStore *model = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING,
                                                G_TYPE_STRING, G_TYPE_POINTER);
    GList *apps = data->cb->get_app_keys(*data->config, "*", NULL);
    GList *l;
    LuxeHotkeyApp *app;
    GtkTreeIter iter;

    for (l = apps; l; l = l->next)
    {
        app = l->data;
        gtk_list_store_insert_with_values(model, &iter, -1, 0, app->exec,
                                                            1, app->accel1,
                                                            2, app->accel2,
                                                            3, app, -1);
    }
    g_list_free(apps);
    gtk_tree_view_set_model(data->apps, GTK_TREE_MODEL(model));
    g_object_unref(model);
}

static gboolean on_win_close(GtkWindow *win, gpointer user_data)
{
    PluginData *data = user_data;
    if (data->allow_close) {
        data->allow_close = FALSE;
    } else if (has_unsaved_changes(data)) {
        ask_pending_decision(data, PENDING_CLOSE);
        return TRUE;
    } else if (data->decision_pending) {
        return TRUE;
    }
    /* Disconnect the selection/page handlers first: the default handler
     * below destroys the window, and during child disposal the treeview
     * selection can emit "changed" after sibling toolbar buttons are
     * already finalized (use-after-free criticals without this). */
    if(data->acts)
        g_signal_handlers_disconnect_by_func(
            gtk_tree_view_get_selection(data->acts),
            (gpointer)on_selection_changed, data);
    if(data->apps)
        g_signal_handlers_disconnect_by_func(
            gtk_tree_view_get_selection(data->apps),
            (gpointer)on_selection_changed, data);
    if(data->notebook)
        g_signal_handlers_disconnect_by_func(data->notebook,
            (gpointer)on_notebook_switch_page, data);
    /* Detach tree models first: destroying a TreeView with an attached
     * model+selection trips a gtk_css_node assertion inside GTK. */
    if(data->acts)
        gtk_tree_view_set_model(data->acts, NULL);
    if(data->apps)
        gtk_tree_view_set_model(data->apps, NULL);
    _edit_cleanup(data);
    /* The default handler destroys the window; the application quits
     * automatically when its last window closes. */
    data->win = NULL;
    return FALSE;
}

static void build_main_window(GtkApplication *app, PluginData *data)
{
    GtkBuilder *builder;
    GMenuModel *menubar_model;
    GtkWidget *win, *header, *menu_button;
    GtkWidget *toolbar, *toolbar_view;
    GtkBox *vbox;
    GSimpleActionGroup *act_grp;
    const LuxeHotkeyPluginInit *cb = data->cb;

    win = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
    gtk_window_set_icon_name(GTK_WINDOW(win), LUXEHOTKEY_ICON);
    gtk_window_set_title(GTK_WINDOW(win), _("Keyboard shortcuts"));
    data->win = win;

    /* actions for menus */
    act_grp = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(act_grp), win_actions,
                                    G_N_ELEMENTS(win_actions), data);
    gtk_widget_insert_action_group(win, "win", G_ACTION_GROUP(act_grp));
    data->save_action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(act_grp), "save"));
    data->add_action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(act_grp), "new"));
    data->del_action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(act_grp), "del"));
    data->edit_action = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(act_grp), "edit"));

    vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));

    /* R2 headerbar chrome: hamburger menu carries the former menubar
     * items as sections; About is reachable from it. The header bar is
     * hosted as the top bar of an GtkBox, never as a window
     * titlebar. */
    builder = gtk_builder_new_from_string(menu_xml, -1);
    menubar_model = G_MENU_MODEL(gtk_builder_get_object(builder, "hamburger"));
    header = gtk_header_bar_new();
    menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), menubar_model);
    gtk_widget_set_tooltip_text(menu_button, _("Main menu"));
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);
    gtk_window_set_icon_name(GTK_WINDOW(win), "org.luxe.Hotkey");
    g_object_unref(builder);

    /* toolbar replaces GtkToolbar (removed in GTK4) */
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    data->save_button = tool_button_new("document-save", _("Save all changes and apply them"),
                                       G_CALLBACK(on_save_btn), data);
    gtk_widget_add_css_class(data->save_button, "suggested-action");
    set_action_enabled(data, data->save_action, data->save_button, FALSE);
    gtk_box_append(GTK_BOX(toolbar), data->save_button);
    gtk_box_append(GTK_BOX(toolbar),
                   tool_button_new("view-refresh", _("Reload"),
                                   G_CALLBACK(on_reload_btn), data));
    {
        GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
        gtk_box_append(GTK_BOX(toolbar), sep);
    }
    data->add_button = tool_button_new("list-add", _("Create new action"),
                                      G_CALLBACK(on_new_btn), data);
    gtk_box_append(GTK_BOX(toolbar), data->add_button);
    data->del_button = tool_button_new("list-remove", _("Remove selected action"),
                                      G_CALLBACK(on_del_btn), data);
    gtk_widget_add_css_class(data->del_button, "destructive-action");
    gtk_box_append(GTK_BOX(toolbar), data->del_button);
    data->edit_button = tool_button_new("document-edit", _("Change selected action"),
                                       G_CALLBACK(on_edit_btn), data);
    gtk_box_append(GTK_BOX(toolbar), data->edit_button);
    {
        GtkWidget *button = gtk_button_new_with_label(_("Panel Menu Shortcut"));
        gboolean available = cb->get_app_keys && cb->set_app_key && cb->get_app_options;
        gtk_widget_set_sensitive(button, available);
        g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(
            G_ACTION_MAP(act_grp), "panel-menu")), available);
        g_signal_connect(button, "clicked", G_CALLBACK(on_panel_menu_btn), data);
        gtk_box_append(GTK_BOX(toolbar), button);
    }
    set_action_enabled(data, data->del_action, data->del_button, FALSE);
    set_action_enabled(data, data->edit_action, data->edit_button, FALSE);
    gtk_box_append(vbox, toolbar);

    /* notebook - it contains two tabs: Actions and Programs */
    data->notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_set_scrollable(data->notebook, TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(data->notebook), TRUE);

    g_signal_connect_after(data->notebook, "switch-page",
                           G_CALLBACK(on_notebook_switch_page), data);

    gtk_box_append(vbox, GTK_WIDGET(data->notebook));

    /* setup notebook */
    if (cb->get_wm_keys)
    {
        data->acts = GTK_TREE_VIEW(gtk_tree_view_new());
        /* Interactive search is disabled: its popover trips the
         * gtk_css_node_insert_after assertion (upstream GTK issue
         * GNOME/gtk#4884) on keypress while the view is focused. */
        gtk_tree_view_set_enable_search(data->acts, FALSE);
        gtk_tree_view_insert_column_with_attributes(data->acts, 0, _("Action"),
                                                    gtk_cell_renderer_text_new(),
                                                    "text", 0, NULL);
        gtk_tree_view_insert_column_with_attributes(data->acts, 1, _("Option"),
                                                    gtk_cell_renderer_text_new(),
                                                    "text", 1, NULL);
        gtk_tree_view_insert_column_with_attributes(data->acts, 2, _("Hotkey 1"),
                                                    gtk_cell_renderer_text_new(),
                                                    "text", 2, NULL);
        gtk_tree_view_insert_column_with_attributes(data->acts, 3, _("Hotkey 2"),
                                                    gtk_cell_renderer_text_new(),
                                                    "text", 3, NULL);
        set_actions_list(data);
        g_signal_connect(data->acts, "row-activated", G_CALLBACK(on_row_activated), data);
        g_signal_connect(gtk_tree_view_get_selection(data->acts), "changed",
                         G_CALLBACK(on_selection_changed), data);
        gtk_notebook_append_page(data->notebook, GTK_WIDGET(data->acts),
                                 gtk_label_new(_("Actions")));
        gtk_widget_set_vexpand(GTK_WIDGET(data->acts), TRUE);
    }

    if (cb->get_app_keys)
    {
        data->apps = GTK_TREE_VIEW(gtk_tree_view_new());
        /* See above: keep the search popover from ever existing. */
        gtk_tree_view_set_enable_search(data->apps, FALSE);
        gtk_tree_view_insert_column_with_attributes(data->apps, 0, _("Command"),
                                                    gtk_cell_renderer_text_new(),
                                                    "text", 0, NULL);
        gtk_tree_view_insert_column_with_attributes(data->apps, 1, _("Hotkey 1"),
                                                    gtk_cell_renderer_text_new(),
                                                    "text", 1, NULL);
        gtk_tree_view_insert_column_with_attributes(data->apps, 2, _("Hotkey 2"),
                                                    gtk_cell_renderer_text_new(),
                                                    "text", 2, NULL);
        set_apps_list(data);
        g_signal_connect(data->apps, "row-activated", G_CALLBACK(on_row_activated), data);
        g_signal_connect(gtk_tree_view_get_selection(data->apps), "changed",
                         G_CALLBACK(on_selection_changed), data);
        gtk_notebook_append_page(data->notebook, GTK_WIDGET(data->apps),
                                 gtk_label_new(_("Programs")));
        gtk_widget_set_vexpand(GTK_WIDGET(data->apps), TRUE);
    }

    /* and finally present it all */
    data->current_page = GTK_TREE_VIEW(gtk_notebook_get_nth_page(data->notebook, 0));
    toolbar_view = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar_view), header);
    luxe_toolbar_set_content(GTK_BOX(toolbar_view), GTK_WIDGET(vbox));
    luxe_window_set_content(GTK_WINDOW(win), toolbar_view);
    g_signal_connect(win, "close-request", G_CALLBACK(on_win_close), data);
    gtk_window_present(GTK_WINDOW(win));
    g_object_unref(act_grp);
}

static void on_gtk_activate(GtkApplication *app, gpointer user_data)
{
    PluginData *data = user_data;

    /* Single-instance: re-present the existing window instead of
     * opening a second one. */
    if (data->win && GTK_IS_WINDOW(data->win))
    {
        gtk_window_present(GTK_WINDOW(data->win));
        return;
    }
    build_main_window(app, data);
}

static void module_gtk_run(const gchar *wm, const LuxeHotkeyPluginInit *cb,
                           gpointer *config, GError **error)
{
    PluginData data;
    GtkApplication *app;
    (void)error;

    memset(&data, 0, sizeof(data));
    data.wm = wm;
    data.cb = cb;
    data.config = config;

    app = GTK_APPLICATION(luxe_application_new("org.luxe.Hotkey",
                                              G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect(app, "activate", G_CALLBACK(on_gtk_activate), &data);
    g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    data.win = NULL;
    _edit_cleanup(&data);
}

void _show_error(const char *prefix, GError *error)
{
    if (error && error->message)
    {
        GApplication *app = g_application_get_default();
        GtkWindow *parent = GTK_IS_APPLICATION(app)
            ? gtk_application_get_active_window(GTK_APPLICATION(app)) : NULL;
        gchar *msg = g_strdup_printf("%s%s", prefix, error->message);
        luxe_show_error(parent, msg, NULL);
        g_free(msg);
    }
}

static void alert_dismissed(gint button, gpointer loop)
{
    (void)button;
    g_main_loop_quit(loop);
}

/* Called after the GUI has stopped (or never started), so there is no main
 * loop left to show an asynchronous alert: wait here until it is dismissed. */
static void module_gtk_alert(GError *error)
{
    GMainLoop *loop;

    if (!error || !error->message)
        return;
    loop = g_main_loop_new(NULL, FALSE);
    luxe_ask(NULL, error->message, NULL, (const char *const[]){_("OK"), NULL},
             0, 0, alert_dismissed, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}

static void module_gtk_init(int argc, char **argv)
{
    /* NOTE: explicit init is required — relying on GTK4 auto-init
     * segfaults in widget constructors here. luxe_ui_init() also
     * initializes GTK. main() calls luxe_ui_init() as well; it is
     * idempotent. */
    (void)argc; (void)argv;
    luxe_ui_init();
}

FM_DEFINE_MODULE(luxehotkey_gui, gtk)

LuxeHotkeyGUIPluginInit fm_module_init_luxehotkey_gui = {
    .run = module_gtk_run,
    .alert = module_gtk_alert,
    .init = module_gtk_init
};
