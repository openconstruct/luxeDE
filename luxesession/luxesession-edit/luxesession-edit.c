#include "../luxe-about.h"
/*
 *      lxsession-edit.c
 *
 *      Copyright 2008 PCMan <pcman.tw@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <stdio.h>
#include <string.h>

#include "luxesession-edit-common.h"
#include "safe-file.h"
#include "../luxesession/profile.h"

#define CONFIG_FILE_NAME    "desktop.conf"

typedef struct
{
    GtkTreeView* view;
    GtkEntry* command;
    GtkButton* add;
    GtkButton* remove;
    GtkLabel* status;
    const char* session_name;
} AutostartControls;

static void set_autostart_status(AutostartControls* controls, const char* text,
                                 gboolean error)
{
    gtk_label_set_text(controls->status, text ? text : "");
}

static void on_autostart_command_changed(GtkEditable* editable, AutostartControls* controls)
{
    char* command = g_strdup(gtk_editable_get_text(editable));
    g_strstrip(command);
    gtk_widget_set_sensitive(GTK_WIDGET(controls->add), *command != '\0');
    g_free(command);
}

static void on_autostart_selection_changed(GtkTreeSelection* selection,
                                           AutostartControls* controls)
{
    GtkTreeIter iter;
    gtk_widget_set_sensitive(GTK_WIDGET(controls->remove),
                             gtk_tree_selection_get_selected(selection, NULL, &iter));
}

static void on_add_autostart_clicked(GtkButton* button, AutostartControls* controls)
{
    const char* command = gtk_editable_get_text(GTK_EDITABLE(controls->command));
    GError* error = NULL;
    if( add_autostart_command(command, controls->session_name, &error) )
    {
        reload_autostart(controls->view, controls->session_name);
        gtk_editable_set_text(GTK_EDITABLE(controls->command), "");
        gtk_widget_set_sensitive(GTK_WIDGET(controls->remove), FALSE);
        set_autostart_status(controls, _("Autostart application added."), FALSE);
    }
    else
    {
        set_autostart_status(controls, error ? error->message : _("Could not add the application."), TRUE);
        g_clear_error(&error);
    }
}

static void on_remove_autostart_clicked(GtkButton* button, AutostartControls* controls)
{
    GError* error = NULL;
    if( remove_selected_autostart(controls->view, controls->session_name, &error) )
    {
        reload_autostart(controls->view, controls->session_name);
        gtk_widget_set_sensitive(GTK_WIDGET(controls->remove), FALSE);
        set_autostart_status(controls, _("Autostart application removed."), FALSE);
    }
    else
    {
        set_autostart_status(controls, error ? error->message : _("Could not remove the application."), TRUE);
        g_clear_error(&error);
    }
}

typedef struct
{
    GtkWindow* window;
    AutostartControls controls;
    GtkEntry* wm;
    GKeyFile* kf;
    const char* wm_key;
} Editor;

static const char* session_name;
static const char* config_session;
static Editor* editor;

static void editor_free(gpointer data)
{
    Editor* e = data;
    g_key_file_free(e->kf);
    g_free(e);
    if (editor == e)
        editor = NULL;
}

/* Only the window-manager command waits for Save; autostart edits are
 * written as they happen. */
static gboolean save_window_manager(Editor* e)
{
    char *dir, *cfg, *data;
    const char* wm_cmd;
    gsize len;
    gboolean ok;

    if (!e->wm)
        return TRUE;
    wm_cmd = gtk_editable_get_text(GTK_EDITABLE(e->wm));
    if (!wm_cmd || !*wm_cmd)
        return FALSE;
    dir = g_build_filename(g_get_user_config_dir(), "luxesession", config_session, NULL);
    g_mkdir_with_parents(dir, 0700);
    cfg = g_build_filename(dir, CONFIG_FILE_NAME, NULL);
    g_free(dir);
    g_key_file_set_string(e->kf, "Session", e->wm_key, wm_cmd);
    data = g_key_file_to_data(e->kf, &len, NULL);
    ok = lxsession_atomic_write_file(cfg, data, len, NULL);
    g_free(data);
    g_free(cfg);
    return ok;
}

static void on_save_clicked(GtkButton* button, Editor* e)
{
    (void)button;
    GError *error = NULL;
    if (!save_autostart(session_name, &error)) {
        set_autostart_status(&e->controls, error->message, TRUE);
        g_clear_error(&error);
        return;
    }
    if (save_window_manager(e))
        gtk_window_close(e->window);
    else
        set_autostart_status(&e->controls, _("Could not save the window manager command."), TRUE);
}

static void on_cancel_clicked(GtkButton* button, Editor* e)
{
    (void)button;
    gtk_window_close(e->window);
}

static void on_save_action(GSimpleAction* action, GVariant* parameter, gpointer data)
{
    (void)action; (void)parameter; (void)data;
    if (editor)
        on_save_clicked(NULL, editor);
}

static void on_command_activate(GtkEntry* entry, AutostartControls* controls)
{
    (void)entry;
    if (gtk_widget_get_sensitive(GTK_WIDGET(controls->add)))
        on_add_autostart_clicked(controls->add, controls);
}

static void on_about(GSimpleAction* action, GVariant* parameter, gpointer data)
{
    (void)action; (void)parameter;
    GtkWindow* window = gtk_application_get_active_window(GTK_APPLICATION(data));
    luxe_show_about(window, "LuxeSession", VERSION, "Session settings for Luxe.");
}

/* Only offer the window-manager page inside a running LuxeSession, reading
 * the key LuxeSession itself obeys: the legacy single key wins. */
static gchar* load_window_manager(Editor* e)
{
    gchar *cfg, *wm_cmd = NULL;
    gboolean loaded;

    cfg = g_build_filename(g_get_user_config_dir(), "luxesession", config_session, CONFIG_FILE_NAME, NULL);
    loaded = lxsession_key_file_load_bounded(e->kf, cfg, NULL);
    g_free(cfg);
    if (!loaded)
    {
        const char* const* dir;
        for (dir = g_get_system_config_dirs(); *dir && !loaded; ++dir)
        {
            cfg = g_build_filename(*dir, "luxesession", config_session, CONFIG_FILE_NAME, NULL);
            loaded = lxsession_key_file_load_bounded(e->kf, cfg, NULL);
            g_free(cfg);
        }
    }
    if (loaded)
    {
        wm_cmd = g_key_file_get_string(e->kf, "Session", "window_manager", NULL);
        if (wm_cmd && *wm_cmd)
            e->wm_key = "window_manager";
        else
        {
            g_free(wm_cmd);
            wm_cmd = g_key_file_get_string(e->kf, "Session", "windows_manager/command", NULL);
        }
    }
    /* openbox-lxde belonged to the retired lxde-common package */
    if (!wm_cmd || !*wm_cmd || strcmp(wm_cmd, "openbox-lxde") == 0)
    {
        g_free(wm_cmd);
        wm_cmd = g_strdup("openbox");
    }
    return wm_cmd;
}

static void build_window(GtkApplication* app)
{
    GtkBuilder* builder = gtk_builder_new();
    GError* error = NULL;
    Editor* e;
    GtkWidget *header, *toolbar, *switcher, *save, *cancel, *menu_button;
    GMenu* menu;
    GtkStack* stack;

    if (!gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/luxesession-edit.ui", &error))
    {
        g_printerr("luxesession-edit: %s\n", error->message);
        g_error_free(error);
        g_object_unref(builder);
        g_application_quit(G_APPLICATION(app));
        return;
    }

    e = g_new0(Editor, 1);
    e->kf = g_key_file_new();
    e->wm_key = "windows_manager/command";
    e->window = GTK_WINDOW(gtk_application_window_new(app));
    g_object_set_data_full(G_OBJECT(e->window), "luxe-session-editor", e, editor_free);
    editor = e;

    stack = GTK_STACK(gtk_builder_get_object(builder, "stack"));
    e->controls.view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "autostarts"));
    e->controls.command = GTK_ENTRY(gtk_builder_get_object(builder, "autostart_command"));
    e->controls.add = GTK_BUTTON(gtk_builder_get_object(builder, "add_autostart"));
    e->controls.remove = GTK_BUTTON(gtk_builder_get_object(builder, "remove_autostart"));
    e->controls.status = GTK_LABEL(gtk_builder_get_object(builder, "autostart_status"));
    e->controls.session_name = session_name;
    e->wm = GTK_ENTRY(gtk_builder_get_object(builder, "wm"));

    gtk_window_set_title(e->window, _("Session Settings"));
    gtk_window_set_icon_name(e->window, "preferences-desktop");
    gtk_window_set_default_size(e->window, 640, 460);

    header = gtk_header_bar_new();
    switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), stack);
    luxe_switcher_set_policy(GTK_STACK_SWITCHER(switcher), 0);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), switcher);
    cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
    save = gtk_button_new_with_mnemonic(_("_Save"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(save), "app.save");
    gtk_widget_add_css_class(save, "suggested-action");
    menu = g_menu_new();
    g_menu_append(menu, _("_About Session Settings"), "app.about");
    menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
    gtk_widget_set_tooltip_text(menu_button, _("Main Menu"));
    g_object_unref(menu);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), cancel);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), save);

    toolbar = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar), header);
    luxe_toolbar_set_content(GTK_BOX(toolbar), GTK_WIDGET(stack));
    luxe_window_set_content(GTK_WINDOW(e->window), toolbar);

    init_list_view(e->controls.view);
    /* GtkTreeView's type-ahead entry is positioned with a GTK4 CSS-node
     * critical whenever a key reaches the focused list (Ctrl+S, Alt+F4);
     * a short autostart list does not need it. */
    gtk_tree_view_set_enable_search(e->controls.view, FALSE);
    load_autostart(session_name);
    gtk_tree_view_set_model(e->controls.view, GTK_TREE_MODEL(get_autostart_list()));
    g_signal_connect(e->controls.command, "changed", G_CALLBACK(on_autostart_command_changed), &e->controls);
    g_signal_connect(e->controls.command, "activate", G_CALLBACK(on_command_activate), &e->controls);
    g_signal_connect(e->controls.add, "clicked", G_CALLBACK(on_add_autostart_clicked), &e->controls);
    g_signal_connect(e->controls.remove, "clicked", G_CALLBACK(on_remove_autostart_clicked), &e->controls);
    g_signal_connect(gtk_tree_view_get_selection(e->controls.view), "changed",
                     G_CALLBACK(on_autostart_selection_changed), &e->controls);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel_clicked), e);

    if (g_getenv("_LUXESESSION_PID"))
    {
        gchar* wm_cmd = load_window_manager(e);
        gtk_editable_set_text(GTK_EDITABLE(e->wm), wm_cmd);
        g_free(wm_cmd);
    }
    else
    {
        GtkStackPage* page = GTK_STACK_PAGE(gtk_builder_get_object(builder, "adv_stack_page"));
        gtk_stack_remove(stack, gtk_stack_page_get_child(page));
        e->wm = NULL;
    }
    g_object_unref(builder);
    gtk_window_present(e->window);
}

static void on_activate(GApplication* app, gpointer data)
{
    (void)data;
    if (editor)
        gtk_window_present(editor->window);
    else
        build_window(GTK_APPLICATION(app));
}

static void on_startup(GApplication* app, gpointer data)
{
    static const GActionEntry actions[] = {
        { "about", on_about, NULL, NULL, NULL, {0} },
        { "save", on_save_action, NULL, NULL, NULL, {0} },
    };
    static const char* const save_accels[] = { "<Control>s", NULL };
    (void)data;
    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), app);
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save", save_accels);
}

int main(int argc, char** argv)
{
    GtkApplication* app;
    int status;
    char* app_argv[] = { argv[0], NULL };

#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    if( argc > 1 )
        session_name = argv[1];
    else
    {
        session_name = g_getenv("XDG_CURRENT_DESKTOP");
        if(!session_name)
        {
            session_name = g_getenv("DESKTOP_SESSION");
            if( G_UNLIKELY(!session_name) )
                session_name = "LXDE";
        }
    }

    /* The desktop name filters autostarts; DESKTOP_SESSION selects the
     * configuration profile (for example X-Luxe with the migrated LXDE profile). */
    config_session = argc > 1 ? argv[1] : g_getenv("DESKTOP_SESSION");
    if (!config_session || !*config_session) config_session = session_name;
    if (!luxe_profile_component_valid(session_name) ||
        !luxe_profile_component_valid(config_session))
    {
        g_printerr("luxesession-edit: invalid session name\n");
        return 2;
    }

    app = luxe_application_new("org.luxe.SessionSettings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    status = g_application_run(G_APPLICATION(app), 1, app_argv);
    g_object_unref(app);
    return status;
}
