#include "../luxe-about.h"
/**
 *      Copyright 2008 Fred Chien <cfsghost@gmail.com>
 *      Copyright (c) 2010 LxDE Developers, see the file AUTHORS for details.
 *      Copyright 2026 Jerry Howell <jerr@luxe.desktop>
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

#include <glib.h>
#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <vte/vte.h>
#include <langinfo.h>
#include <locale.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <pwd.h>

#include "luxeterm.h"
#include "setting.h"
#include "preferences.h"

/* Utilities. */
static void terminal_tab_set_position(GtkWidget * notebook, gint tab_position);
static gchar * terminal_get_current_dir(LuxeTerm * terminal);
static Term * current_term(LuxeTerm * terminal);

/* Open a matched URL without a shell.
 * Terminal output is attacker-influenced, so the URL must never pass
 * through shell parsing. Prefer the default URI handler; fall back to
 * argv-based xdg-open with no shell involved. */
static void terminal_open_url(const gchar * url)
{
    if (url == NULL || url[0] == '\0')
        return;
    GError * error = NULL;
    if (g_app_info_launch_default_for_uri(url, NULL, &error))
        return;
    if (error != NULL)
        g_clear_error(&error);
    gchar * argv[] = { (gchar *) "xdg-open", (gchar *) url, NULL };
    if (!g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, &error))
    {
        g_warning("Failed to open URL");
        g_clear_error(&error);
    }
}

/* Current tab's Term, or NULL when there is no current page
 * (page -1, e.g. empty notebook) or the index is out of range. */
static Term * current_term(LuxeTerm * terminal)
{
    if (terminal == NULL || terminal->terms == NULL || terminal->notebook == NULL)
        return NULL;
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(terminal->notebook));
    if (page < 0 || (guint) page >= terminal->terms->len)
        return NULL;
    return g_ptr_array_index(terminal->terms, page);
}

/* Menu and accelerator event handlers (GAction "activate"/"change-state"
 * callbacks; user_data is the LuxeTerm *). */
static void terminal_new_window_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_new_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_close_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_close_window_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_open_url_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_copy_url_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_copy_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_paste_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_clear_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_name_tab_response_event(GObject *source, GAsyncResult *result, gpointer data);
static void terminal_name_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_previous_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_next_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_move_tab_execute(LuxeTerm * terminal, gint direction);
static void terminal_move_tab_left_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_move_tab_right_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_zoom(LuxeTerm * terminal);
static void terminal_zoom_in_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_zoom_out_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_zoom_reset_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_switch_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);
static void terminal_about_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data);

/* Window creation, destruction, and control. */
static void terminal_switch_page_event(GtkNotebook * notebook, GtkWidget * page, guint num, LuxeTerm * terminal);
static void close_all_terminal_tabs(LuxeTerm * terminal);
static void on_close_confirmation_response(GObject *source, GAsyncResult *result, gpointer data);
static gboolean terminal_close_window_confirmation_request(GtkWindow * window, LuxeTerm * terminal);
static void terminal_window_exit(LuxeTerm * terminal, GObject * where_the_object_was);
static void terminal_child_exited_event(VteTerminal * vte, gint status, Term * term);
static void terminal_request_close_tab(Term *term);
static void terminal_track_confirmation(LuxeTerm *terminal, GtkWindow *dialog)
{
    terminal->confirm_dialog = dialog;
    g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer *)&terminal->confirm_dialog);
}
static void terminal_forget_confirmation(LuxeTerm *terminal, GObject *dialog)
{
    if (terminal->confirm_dialog == GTK_WINDOW(dialog)) {
        g_object_remove_weak_pointer(dialog, (gpointer *)&terminal->confirm_dialog);
        terminal->confirm_dialog = NULL;
    }
}
static void terminal_close_button_event(GtkButton * button, Term * term);
static void terminal_tab_middle_click_event(GtkGestureClick * gesture, int n_press, double x, double y, Term * term);
static void terminal_vte_cursor_moved_event(VteTerminal * vte, Term * term);
static void terminal_vte_pressed_event(GtkGestureClick * gesture, int n_press, double x, double y, Term * term);
typedef struct { GdkRGBA foreground, background, colors[16]; } TerminalPalette;
static void terminal_settings_apply_to_term(LuxeTerm *terminal, Term *term, const TerminalPalette *palette);
static Term * terminal_new(LuxeTerm * terminal, const gchar * label, const gchar * pwd,
                           gchar **env, gchar **exec, gboolean interactive_shell);
static void terminal_new_tab(LuxeTerm * terminal, const gchar * label);
static void terminal_free(Term * term);
static Term * terminal_ref(Term * term);
static void terminal_unref(Term * term);
static void terminal_menubar_initialize(LuxeTerm * terminal);
static void terminal_settings_apply(LuxeTerm *terminal, const TerminalPalette *palette);
static void terminal_shortcuts_rebuild(LuxeTerm * terminal);

/* Help when user enters an invalid command. */
static gchar usage_display[] = {
    "Usage:\n"
    "  luxeterm [Options...] - LuxeTerm is a terminal emulator\n\n"
    "Options:\n"
    "  -e, --command=STRING             Execute the argument to this option inside the terminal\n"
    "  --geometry=COLUMNSxROWS          Set the terminal's size\n"
    "  -l, --loginshell                 Execute login shell\n"
    "  -t, -T, --title=,\n"
    "    --tabs=NAME[,NAME[,NAME[...]]] Set the terminal's title\n"
    "  --working-directory=DIRECTORY    Set the terminal's working directory\n"
    "  --no-remote                      Do not accept or send remote commands\n"
    "  -v, --version                    Version information\n"
};

/* Window actions (registered per-window as win.*) and the GMenu models
 * for the hamburger menu and the terminal context popup. This replaces the
 * removed GtkUIManager/GtkActionGroup machinery and data/menu.ui. */
static const GActionEntry win_actions[] =
{
    { "new-window", terminal_new_window_activate_event, NULL, NULL, NULL },
    { "new-tab", terminal_new_tab_activate_event, NULL, NULL, NULL },
    { "close-tab", terminal_close_tab_activate_event, NULL, NULL, NULL },
    { "close-window", terminal_close_window_activate_event, NULL, NULL, NULL },
    { "open-url", terminal_open_url_activate_event, NULL, NULL, NULL },
    { "copy-url", terminal_copy_url_activate_event, NULL, NULL, NULL },
    { "copy", terminal_copy_activate_event, NULL, NULL, NULL },
    { "paste", terminal_paste_activate_event, NULL, NULL, NULL },
    { "clear", terminal_clear_activate_event, NULL, NULL, NULL },
    { "preferences", terminal_preferences_dialog, NULL, NULL, NULL },
    { "name-tab", terminal_name_tab_activate_event, NULL, NULL, NULL },
    { "previous-tab", terminal_previous_tab_activate_event, NULL, NULL, NULL },
    { "next-tab", terminal_next_tab_activate_event, NULL, NULL, NULL },
    { "move-tab-left", terminal_move_tab_left_activate_event, NULL, NULL, NULL },
    { "move-tab-right", terminal_move_tab_right_activate_event, NULL, NULL, NULL },
    { "zoom-in", terminal_zoom_in_activate_event, NULL, NULL, NULL },
    { "zoom-out", terminal_zoom_out_activate_event, NULL, NULL, NULL },
    { "zoom-reset", terminal_zoom_reset_activate_event, NULL, NULL, NULL },
    { "switch-tab", terminal_switch_tab_activate_event, "i", NULL, NULL },
    { "about", terminal_about_activate_event, NULL, NULL, NULL },
};

/* Hamburger menu model. The former GtkPopoverMenuBar's File/Edit/Tabs/Help
 * sections all live in one menu, opened from the header bar menu button.
 * The former top-level submenu items are now plain sections; the File/Edit
 * groups keep their internal separators. */
static GMenuModel * terminal_hamburger_model(void)
{
    GMenu * bar = g_menu_new();
    GMenu * file = g_menu_new();
    GMenu * edit = g_menu_new();
    GMenu * tabs = g_menu_new();
    GMenu * help = g_menu_new();
    GMenuItem * item;

    g_menu_append(file, _("New _Window"), "win.new-window");
    g_menu_append(file, _("New T_ab"), "win.new-tab");
    GMenu * file_sec = g_menu_new();
    g_menu_append(file_sec, _("_Close Tab"), "win.close-tab");
    g_menu_append(file_sec, _("Close _Window"), "win.close-window");

    g_menu_append(edit, _("Cop_y"), "win.copy");
    g_menu_append(edit, _("_Paste"), "win.paste");
    g_menu_append(edit, _("Clear scr_ollback"), "win.clear");
    GMenu * zoom_sec = g_menu_new();
    g_menu_append(zoom_sec, _("Zoom _In"), "win.zoom-in");
    g_menu_append(zoom_sec, _("Zoom O_ut"), "win.zoom-out");
    g_menu_append(zoom_sec, _("Zoom _Reset"), "win.zoom-reset");
    GMenu * pref_sec = g_menu_new();
    g_menu_append(pref_sec, _("Preference_s"), "win.preferences");

    g_menu_append(tabs, _("Na_me Tab"), "win.name-tab");
    g_menu_append(tabs, _("Pre_vious Tab"), "win.previous-tab");
    g_menu_append(tabs, _("Ne_xt Tab"), "win.next-tab");
    g_menu_append(tabs, _("Move Tab _Left"), "win.move-tab-left");
    g_menu_append(tabs, _("Move Tab _Right"), "win.move-tab-right");

    g_menu_append(help, _("_About"), "win.about");

    GMenu * file_top = g_menu_new();
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(file));
    g_menu_append_item(file_top, item);
    g_object_unref(item);
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(file_sec));
    g_menu_append_item(file_top, item);
    g_object_unref(item);

    GMenu * edit_top = g_menu_new();
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(edit));
    g_menu_append_item(edit_top, item);
    g_object_unref(item);
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(zoom_sec));
    g_menu_append_item(edit_top, item);
    g_object_unref(item);
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(pref_sec));
    g_menu_append_item(edit_top, item);
    g_object_unref(item);

    g_menu_append_section(bar, NULL, G_MENU_MODEL(file_top));
    g_menu_append_section(bar, NULL, G_MENU_MODEL(edit_top));
    g_menu_append_section(bar, NULL, G_MENU_MODEL(tabs));
    g_menu_append_section(bar, NULL, G_MENU_MODEL(help));

    g_object_unref(file);
    g_object_unref(file_sec);
    g_object_unref(file_top);
    g_object_unref(edit);
    g_object_unref(zoom_sec);
    g_object_unref(pref_sec);
    g_object_unref(edit_top);
    g_object_unref(tabs);
    g_object_unref(help);
    return G_MENU_MODEL(bar);
}

/* Context popup model. The URL items are only present when a URL was
 * matched under the cursor (replaces per-action gtk_action_set_visible). */
static GMenuModel * terminal_popup_model(gboolean with_url)
{
    GMenu * menu = g_menu_new();
    g_menu_append(menu, _("New _Window"), "win.new-window");
    g_menu_append(menu, _("New _Tab"), "win.new-tab");
    if (with_url)
    {
        GMenu * url_sec = g_menu_new();
        g_menu_append(url_sec, _("Open _URL"), "win.open-url");
        g_menu_append(url_sec, _("Copy _URL"), "win.copy-url");
        GMenuItem * item = g_menu_item_new_section(NULL, G_MENU_MODEL(url_sec));
        g_menu_append_item(menu, item);
        g_object_unref(item);
        g_object_unref(url_sec);
    }
    GMenu * clip_sec = g_menu_new();
    g_menu_append(clip_sec, _("Cop_y"), "win.copy");
    g_menu_append(clip_sec, _("_Paste"), "win.paste");
    g_menu_append(clip_sec, _("Cl_ear scrollback"), "win.clear");
    GMenuItem * item = g_menu_item_new_section(NULL, G_MENU_MODEL(clip_sec));
    g_menu_append_item(menu, item);
    g_object_unref(item);
    g_object_unref(clip_sec);
    GMenu * tab_sec = g_menu_new();
    g_menu_append(tab_sec, _("Preference_s"), "win.preferences");
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(tab_sec));
    g_menu_append_item(menu, item);
    g_object_unref(item);
    g_object_unref(tab_sec);
    GMenu * nav_sec = g_menu_new();
    g_menu_append(nav_sec, _("Na_me Tab"), "win.name-tab");
    g_menu_append(nav_sec, _("Pre_vious Tab"), "win.previous-tab");
    g_menu_append(nav_sec, _("Ne_xt Tab"), "win.next-tab");
    g_menu_append(nav_sec, _("Move Tab _Left"), "win.move-tab-left");
    g_menu_append(nav_sec, _("Move Tab _Right"), "win.move-tab-right");
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(nav_sec));
    g_menu_append_item(menu, item);
    g_object_unref(item);
    g_object_unref(nav_sec);
    GMenu * close_sec = g_menu_new();
    g_menu_append(close_sec, _("_Close Tab"), "win.close-tab");
    item = g_menu_item_new_section(NULL, G_MENU_MODEL(close_sec));
    g_menu_append_item(menu, item);
    g_object_unref(item);
    g_object_unref(close_sec);
    return G_MENU_MODEL(menu);
}

/* Set the position of the tabs on the main window. */
static void terminal_tab_set_position(GtkWidget * notebook, gint tab_position)
{
    switch (tab_position)
    {
        case 0:
            gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
            break;
        case 1:
            gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_BOTTOM);
            break;
        case 2:
            gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_LEFT);
            break;
        default:
            gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_RIGHT);
            break;
    }
}

static gchar * terminal_get_current_dir(LuxeTerm * terminal)
{
    gchar * proc_cwd = NULL;

#ifdef __linux
    /* Try to get the working directory from /proc. */
    gint current = gtk_notebook_get_current_page(GTK_NOTEBOOK(terminal->notebook));
    if (current != -1)
    {
        /* Search for the Term structure corresponding to the current tab. */
        guint i;
        for (i = 0; i < terminal->terms->len; i++)
        {
            Term * term = g_ptr_array_index(terminal->terms, i);
            if (term->index == current)
            {
                /* Get the working directory corresponding to the process ID. */
                gchar proc_cwd_link[PATH_MAX];
                g_snprintf(proc_cwd_link, PATH_MAX, "/proc/%d/cwd", term->pid);
                proc_cwd = g_file_read_link(proc_cwd_link, NULL);
                break;
            }
        }

    }
#endif

    if (proc_cwd == NULL)
    {
        proc_cwd = g_get_current_dir();
    }

    return proc_cwd;
}

/* Get preferred shell.
 * Modified from eggshell.c of gnome-terminal. */
static const gchar * terminal_get_preferred_shell_for_env(const gchar * const *environment) {
    const gchar *shell;
    struct passwd *pw;
    const gchar *fallback_shell = "/bin/sh";

    shell = environment ? g_environ_getenv((gchar **)environment, "SHELL") : g_getenv("SHELL");
    if (geteuid() == getuid() && getegid() == getgid()) {
        if (shell != NULL) {
            if (access(shell, X_OK) == 0) {
                return shell;
            }
        }
    }

    pw = getpwuid(getuid());
    if (pw && pw->pw_shell) {
        if (access (pw->pw_shell, X_OK) == 0) {
            return pw->pw_shell;
        }
    }

    if (access (fallback_shell, X_OK) == 0) {
        return fallback_shell;
    }

    return fallback_shell; /* Keep argv valid; VTE reports a failed spawn. */
}

/* Rebuild the window's keyboard shortcuts from the user settings.
 * GTK4 has no GtkAccelGroup/GtkAccelMap; accelerators are installed with
 * gtk_application_set_accels_for_action() on detailed win.* action names,
 * which route to the focused window. The <Alt>n tab shortcuts are only
 * installed when enabled and more than one tab exists. */
static void terminal_shortcuts_rebuild(LuxeTerm * terminal)
{
    GtkApplication * app = terminal->parent->app;
    Setting * setting = get_setting();
    const char * empty[] = { NULL };

    gtk_application_set_accels_for_action(app, "win.new-window", (const char * const[]){ setting->new_window_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.new-tab", (const char * const[]){ setting->new_tab_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.close-tab", (const char * const[]){ setting->close_tab_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.close-window", (const char * const[]){ setting->close_window_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.copy", (const char * const[]){ setting->copy_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.paste", (const char * const[]){ setting->paste_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.zoom-in", (const char * const[]){ setting->zoom_in_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.zoom-out", (const char * const[]){ setting->zoom_out_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.zoom-reset", (const char * const[]){ setting->zoom_reset_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.name-tab", (const char * const[]){ setting->name_tab_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.previous-tab", (const char * const[]){ setting->previous_tab_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.next-tab", (const char * const[]){ setting->next_tab_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.move-tab-left", (const char * const[]){ setting->move_tab_left_accel, NULL });
    gtk_application_set_accels_for_action(app, "win.move-tab-right", (const char * const[]){ setting->move_tab_right_accel, NULL });

    /* <Alt>n tab switching. */
    guint i;
    for (i = 0; i < 9; i++)
    {
        gchar * detailed = g_strdup_printf("win.switch-tab(%u)", i);
        if (!setting->disable_alt)
        {
            gchar * accel = g_strdup_printf("<Alt>%d", i + 1);
            gtk_application_set_accels_for_action(app, detailed, (const char * const[]){ accel, NULL });
            g_free(accel);
        }
        else
        {
            gtk_application_set_accels_for_action(app, detailed, empty);
        }
        g_free(detailed);
    }
}

/* Handler for the win.switch-tab action (replaces the <Alt>n closures).
 * The int32 parameter is the 0-based tab index. */
static void terminal_switch_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    gint32 index = g_variant_get_int32(param);
    if (index >= 0 && (guint) index < terminal->terms->len &&
        (guint) index < (guint) gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal->notebook)))
    {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(terminal->notebook), index);
    }
}

/* Handler for "activate" signal on File/New Window menu item.
 * Open a new window. */
static void terminal_new_window_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    CommandArguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.working_directory = terminal_get_current_dir(terminal);
    arguments.environment = (const gchar * const *)terminal->launch_env;
    luxeterm_initialize(terminal->parent, &arguments);
    g_free(arguments.working_directory);
}

/* Handler for "activate" signal on File/New Tab menu item.
 * Open a new tab. */
static void terminal_new_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    terminal_new_tab(terminal, NULL);
}

static void terminal_new_tab(LuxeTerm * terminal, const gchar * label)
{
    Term * term;
    gchar * proc_cwd = terminal_get_current_dir(terminal);

    /* Propagate the working directory of the current tab to the new tab.
     * If the working directory was determined above, use it; otherwise default to the working directory of the process.
     * Create the new terminal. */

    if (terminal->login_shell)
    {
        /* Create a login shell, this should be cleaner. */
        gchar * * exec = g_malloc(3 * sizeof(gchar *));
        exec[0] = g_strdup(terminal_get_preferred_shell_for_env((const gchar * const *)terminal->launch_env));
        char * shellname = g_path_get_basename(exec[0]);
        exec[1] = g_strdup_printf("-%s", shellname);
        g_free(shellname);
        exec[2] = NULL;
        term = terminal_new(terminal, label, proc_cwd, terminal->launch_env, exec, TRUE);
    }
    else
    {
        term = terminal_new(terminal, label, proc_cwd, terminal->launch_env, NULL, TRUE);
    }
    g_free(proc_cwd);

    /* Add a tab to the notebook and the "terms" array. */
    gtk_notebook_append_page(GTK_NOTEBOOK(terminal->notebook), term->box, term->tab);
    term->index = gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal->notebook)) - 1;
    g_ptr_array_add(terminal->terms, term);

    /* Make the new terminal's tab current and, if we went from one to more than one tab,
     * turn the tab display on. */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(term->parent->notebook), term->index);
    if (term->index > 0)
    {
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(term->parent->notebook), TRUE);
    }

    /* Refresh <Alt>n shortcuts. */
    terminal_shortcuts_rebuild(terminal);
}

/* Handler for "activate" signal on File/Close Tab menu item.
 * Close the current tab. */
static void terminal_close_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    Term * term = current_term(terminal);
    if (term == NULL)
        return;
    terminal_request_close_tab(term);
}

/* Handler for "activate" signal on File/Close Window menu item.
 * Close the current window. */
static void terminal_close_window_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    gtk_window_close(GTK_WINDOW(terminal->window));
}

/* Handler for the "Open URL" right-click menu item.
 * Opens the matched url with no shell involved. */
static void terminal_open_url_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
        Term * term = current_term(terminal);
        if (term == NULL)
            return;
        if (term->matched_url)
        {
                terminal_open_url(term->matched_url);
        }
}

/* Handler for the "Copy URL" right-click menu item.
 * Copy the URL to the clipboard. */
static void terminal_copy_url_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    Term * term = current_term(terminal);
    if (term == NULL)
        return;
    if (term->matched_url)
    {
        GdkClipboard * clipboard = gtk_widget_get_clipboard(term->vte);
        gdk_clipboard_set_text(clipboard, term->matched_url);
    }
}

/* Handler for "activate" signal on Edit/Copy menu item.
 * Copy to the clipboard. */
static void terminal_copy_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    Term * term = current_term(terminal);
    if (term == NULL)
        return;
    /* vte_terminal_copy_clipboard() is gone in new VTE; copy is an action
     * signal now. */
    g_signal_emit_by_name(VTE_TERMINAL(term->vte), "copy-clipboard");
}

/* Handler for "activate" signal on Edit/Paste menu item.
 * Paste from the clipboard. */
static void terminal_paste_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    Term * term = current_term(terminal);
    if (term == NULL)
        return;
    gtk_widget_grab_focus(term->vte);
    vte_terminal_paste_clipboard(VTE_TERMINAL(term->vte));
}

/* Handler for "clear scrollback" signal on Edit/Paste menu item.
 * Clear scrollback. */
static void terminal_clear_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    Term * term = current_term(terminal);
    if (term == NULL)
        return;
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term->vte), 0);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term->vte), get_setting()->scrollback);
}

/* Dialog owns the tab token; a child may exit while it is open. */
static void terminal_name_tab_response_event(GObject *source, GAsyncResult *result, gpointer data)
{
    Term *term = data;
    const char *response = luxe_choice_choose_finish(GTK_DIALOG(source), result);
    if (!term->closed && g_str_equal(response, "rename")) {
        GtkWidget *entry = luxe_choice_get_extra_child(GTK_DIALOG(source));
        const char *title = gtk_editable_get_text(GTK_EDITABLE(entry));
        term->user_specified_label = *title != '\0';
        if (!*title) title = vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
        if (!title) title = "LuxeTerm";
        gtk_label_set_text(GTK_LABEL(term->label), title);
        if (current_term(term->parent) == term)
            gtk_window_set_title(GTK_WINDOW(term->parent->window), title);
    }
    terminal_unref(term);
}
static void terminal_name_tab_activate_event(GSimpleAction *action, GVariant *param, gpointer data)
{
    (void)action; (void)param;
    LuxeTerm *terminal = data;
    Term *term = current_term(terminal);
    if (!term) return;
    GtkWindow *dialog = luxe_choice_dialog_new(_("Name Tab"), NULL);
    luxe_choice_add_responses(GTK_DIALOG(dialog), "cancel", _("Cancel"), "rename", _("Rename"), NULL);
    luxe_choice_set_default_response(GTK_DIALOG(dialog), "rename");
    luxe_choice_set_close_response(GTK_DIALOG(dialog), "cancel");
    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), gtk_label_get_text(GTK_LABEL(term->label)));
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    luxe_choice_set_extra_child(GTK_DIALOG(dialog), entry);
    luxe_choice_choose(GTK_DIALOG(dialog), GTK_WINDOW(terminal->window), NULL,
                            terminal_name_tab_response_event, terminal_ref(term));
}

/* Handler for "activate" signal on Tabs/Previous Tab menu item.
 * Cycle through tabs in the reverse direction. */
static void terminal_previous_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    /* Cycle through tabs. */
    if (gtk_notebook_get_current_page(GTK_NOTEBOOK(terminal->notebook)) == 0)
    {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(terminal->notebook), -1);
    }
    else
    {
        gtk_notebook_prev_page(GTK_NOTEBOOK(terminal->notebook));
    }
}

/* Handler for "activate" signal on Tabs/Next Tab menu item.
 * Cycle through tabs in the forward direction. */
static void terminal_next_tab_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    /* Cycle through tabs. */
    if (gtk_notebook_get_current_page(GTK_NOTEBOOK(terminal->notebook)) == gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal->notebook)) - 1)
    {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(terminal->notebook), 0);
    }
    else
    {
        gtk_notebook_next_page(GTK_NOTEBOOK(terminal->notebook));
    }
}

/* Helper for move tab left and right. */
static void terminal_move_tab_execute(LuxeTerm * terminal, gint direction)
{
    GtkNotebook * notebook = GTK_NOTEBOOK(terminal->notebook);
    gint current_page_number = gtk_notebook_get_current_page(notebook);
    gint target_page_number = current_page_number + direction;

    /* prevent out of index, cast should be safe as we catch negatives prior */
    if (current_page_number < 0 || target_page_number < 0 || (guint) target_page_number >= terminal->terms->len)
    {
        return;
    }

    /* swap index in terms array and its id */
    Term * term_current = g_ptr_array_index(terminal->terms, current_page_number);
    Term * term_target = g_ptr_array_index(terminal->terms, target_page_number);
    g_ptr_array_index(terminal->terms, target_page_number) = term_current;
    g_ptr_array_index(terminal->terms, current_page_number) = term_target;
    term_current->index = target_page_number;
    term_target->index = current_page_number;

    gtk_notebook_reorder_child(notebook, gtk_notebook_get_nth_page(notebook, current_page_number), target_page_number);

    /* update shortcuts (replaces terminal_update_alt) */
    terminal_shortcuts_rebuild(terminal);
}

/* Handler for "activate" signal on Tabs/Move Tab Left menu item.
 * Move the tab one position in the reverse direction. */
static void terminal_move_tab_left_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    terminal_move_tab_execute(terminal, -1);
}

/* Handler for "activate" signal on Tabs/Move Tab Right menu item.
 * Move the tab one position in the forward direction. */
static void terminal_move_tab_right_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    terminal_move_tab_execute(terminal, 1);
}

/* Helper for terminal zoom in/out. */
static void terminal_zoom(LuxeTerm * terminal)
{
    Term *term;
    guint i;

    if (terminal->terms->len <= 0) {
        return;
    }

    for (i = 0; i < terminal->terms->len; i++) {
        term = g_ptr_array_index(terminal->terms, i);
        vte_terminal_set_font_scale(VTE_TERMINAL(term->vte), terminal->scale);
    }
}

/* Handler for "activate" signal on Tabs/Zoom In */
static void terminal_zoom_in_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    terminal->scale = MIN(terminal->scale + 0.1, 4.0);
    terminal_zoom(terminal);
}

/* Handler for "activate" signal on Tabs/Zoom Out */
static void terminal_zoom_out_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    if (terminal->scale < 0.3)
        return;
    terminal->scale -= 0.1;
    terminal_zoom(terminal);
}

/* Handler for "activate" signal on Tabs/Zoom Reset */
static void terminal_zoom_reset_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    terminal->scale = 1.0;
    terminal_zoom(terminal);
}

/* Handler for "activate" signal on Help/About menu item. */
static void terminal_about_activate_event(GSimpleAction * action, GVariant * param, gpointer user_data)
{
    LuxeTerm * terminal = user_data;
    (void) action;
    (void) param;
    luxe_show_about_full(GTK_WINDOW(terminal->window), _("LuxeTerm"), VERSION,
                         _("Terminal emulator for Luxe project"), _("translator-credits"));
}

/* Handler for "switch-page" event on the tab notebook. */
static void terminal_switch_page_event(GtkNotebook * notebook, GtkWidget * page, guint num, LuxeTerm * terminal)
{
    (void)notebook;
    (void)page;
    if (terminal->terms->len > num)
    {
        Term * term = g_ptr_array_index(terminal->terms, num);

        /* Remove bold markup from tab title when activating it */
        const gchar *titaux;
        if (term->user_specified_label)
            titaux=gtk_label_get_text(GTK_LABEL(term->label));
        else
            titaux=vte_terminal_get_window_title(VTE_TERMINAL(term->vte));
        /* Also remove asterisk prefix if present */
        if (titaux && g_str_has_prefix(titaux,"* "))
            titaux+=2;
        /* Window titles come from terminal output and must not be
         * interpreted as Pango markup. */
        gtk_label_set_text(GTK_LABEL(term->label), titaux ? titaux : "" );

        /* Propagate the title to the toplevel window. */
        const gchar * title = gtk_label_get_text(GTK_LABEL(term->label));
        gtk_window_set_title(GTK_WINDOW(terminal->window), ((title != NULL) ? title : _("LuxeTerm Terminal Emulator")));
    }
}

/* Handler for "window-title-changed" signal on a Term. */
static void terminal_window_title_changed_event(VteTerminal * vte, Term * term)
{
    if (term->closed) return;
    /* Copy the VTE data out into the tab and the window title, unless the user edited the tab label. */
    if ( ! term->user_specified_label)
    {
        gtk_label_set_text(GTK_LABEL(term->label), vte_terminal_get_window_title(VTE_TERMINAL(vte)));
        gtk_widget_set_tooltip_text(term->label, vte_terminal_get_window_title(VTE_TERMINAL(vte)));
        GtkNotebook * notebook = GTK_NOTEBOOK(term->parent->notebook);
        /* Set title if term is currently active tab */
        if (GTK_WIDGET(term->box) == gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook)))
            gtk_window_set_title(GTK_WINDOW(term->parent->window), vte_terminal_get_window_title(VTE_TERMINAL(vte)));
    }
}

static gboolean terminal_tab_has_active_job(Term *term)
{
    if (!term || term->closed) return FALSE;
    if (term->pid <= 1)
        return term->spawn_cancel && !g_cancellable_is_cancelled(term->spawn_cancel);
    if (!term->interactive_shell)
        return TRUE;

    VtePty *pty = vte_terminal_get_pty(VTE_TERMINAL(term->vte));
    if (!pty) return TRUE;
    pid_t foreground = tcgetpgrp(vte_pty_get_fd(pty));
    pid_t shell_group = getpgid(term->pid);
    /* A failed query is treated conservatively: never close a possibly busy
     * PTY just because job-control state could not be read. */
    if (foreground <= 0 || shell_group <= 0) return TRUE;
    return foreground != shell_group;
}

static guint terminal_active_job_count(LuxeTerm *terminal)
{
    guint active = 0;
    if (!terminal || !terminal->terms) return 0;
    for (guint i = 0; i < terminal->terms->len; i++)
        active += terminal_tab_has_active_job(g_ptr_array_index(terminal->terms, i));
    return active;
}

/* Handler for "close-request" signal on a LuxeTerm window. */
static gboolean terminal_close_window_confirmation_request(GtkWindow * window, LuxeTerm * terminal)
{
    (void) window;
    guint active_jobs = terminal_active_job_count(terminal);
    if (get_setting()->disable_confirm || active_jobs == 0)
        return FALSE;
    if (terminal->confirming) return TRUE;
    terminal->confirming = TRUE;
    gchar *msg = active_jobs == 1
        ? g_strdup(_("Closing this terminal may stop a running command. Continue?"))
        : g_strdup_printf(_("Closing this terminal may stop %u running commands. Continue?"), active_jobs);
    GtkWindow *dialog = luxe_choice_dialog_new(_("Close terminal?"), msg);
    terminal_track_confirmation(terminal, dialog);
    luxe_choice_add_responses(GTK_DIALOG(dialog), "cancel", _("Cancel"), "close", _("Close"), NULL);
    luxe_choice_set_default_response(GTK_DIALOG(dialog), "cancel");
    luxe_choice_set_close_response(GTK_DIALOG(dialog), "cancel");
    luxe_choice_set_response_appearance(GTK_DIALOG(dialog), "close", LUXE_RESPONSE_DESTRUCTIVE);
    luxe_choice_choose(GTK_DIALOG(dialog), GTK_WINDOW(terminal->window), NULL,
                            on_close_confirmation_response, g_object_ref(terminal->window));
    g_free(msg);
    /* The close proceeds asynchronously if confirmed. */
    return TRUE;
}

static void close_all_terminal_tabs(LuxeTerm * terminal)
{
	/* The last close destroys its window and can release terminal, so do
	 * not dereference terminal after that final child-exited call. */
	while (terminal->terms != NULL && terminal->terms->len > 1) {
		Term *term = g_ptr_array_index(terminal->terms, 0);
		terminal_child_exited_event(VTE_TERMINAL(term->vte), 0, term);
	}
	if (terminal->terms != NULL && terminal->terms->len == 1) {
		Term *term = g_ptr_array_index(terminal->terms, 0);
		terminal_child_exited_event(VTE_TERMINAL(term->vte), 0, term);
	}
}

static void on_close_confirmation_response(GObject *source, GAsyncResult *result, gpointer data)
{
    GtkWidget *window = data;
    LuxeTerm *terminal = g_object_get_data(G_OBJECT(window), "luxe-terminal");
    const char *response = luxe_choice_choose_finish(GTK_DIALOG(source), result);
    terminal_forget_confirmation(terminal, source);
    terminal->confirming = FALSE;
    if (!terminal->closed && g_str_equal(response, "close"))
        close_all_terminal_tabs(terminal);
    g_object_unref(window);
}

typedef struct { Term *term; GtkWidget *window; } CloseTabRequest;

static void on_close_tab_response(GObject *source, GAsyncResult *result, gpointer pointer)
{
    CloseTabRequest *request = pointer;
    LuxeTerm *terminal = g_object_get_data(G_OBJECT(request->window), "luxe-terminal");
    const char *response = luxe_choice_choose_finish(GTK_DIALOG(source), result);
    if (terminal) {
        terminal_forget_confirmation(terminal, source);
        terminal->confirming = FALSE;
    }
    if (terminal && !terminal->closed && !request->term->closed && g_str_equal(response, "close"))
        terminal_child_exited_event(VTE_TERMINAL(request->term->vte), 0, request->term);
    terminal_unref(request->term);
    g_object_unref(request->window);
    g_free(request);
}

static void terminal_request_close_tab(Term *term)
{
    if (!term || term->closed || term->parent->closed || term->parent->confirming) return;
    if (get_setting()->disable_confirm || !terminal_tab_has_active_job(term)) {
        terminal_child_exited_event(VTE_TERMINAL(term->vte), 0, term);
        return;
    }
    LuxeTerm *terminal = term->parent;
    terminal->confirming = TRUE;
    GtkWindow *dialog = luxe_choice_dialog_new(_("Close terminal tab?"),
        _("Closing this tab may stop its running command. Continue?"));
    terminal_track_confirmation(terminal, dialog);
    luxe_choice_add_responses(GTK_DIALOG(dialog), "cancel", _("Cancel"), "close", _("Close"), NULL);
    luxe_choice_set_default_response(GTK_DIALOG(dialog), "cancel");
    luxe_choice_set_close_response(GTK_DIALOG(dialog), "cancel");
    luxe_choice_set_response_appearance(GTK_DIALOG(dialog), "close", LUXE_RESPONSE_DESTRUCTIVE);
    CloseTabRequest *request = g_new0(CloseTabRequest, 1);
    request->term = terminal_ref(term);
    request->window = g_object_ref(terminal->window);
    luxe_choice_choose(GTK_DIALOG(dialog), GTK_WINDOW(terminal->window), NULL,
                       on_close_tab_response, request);
}

static void terminal_unrealize(GtkWidget *window, LuxeTerm *terminal)
{
    (void)window;
    terminal->closed = TRUE;
    if (terminal->confirming && terminal->confirm_dialog)
        luxe_choice_respond(GTK_DIALOG(terminal->confirm_dialog), "cancel");
    for (guint i = 0; terminal->terms && i < terminal->terms->len; i++) {
        Term *term = g_ptr_array_index(terminal->terms, i);
        term->closed = TRUE;
        g_cancellable_cancel(term->spawn_cancel);
    }
}

/* Weak-notify callback for LuxeTerm object. */
static void terminal_window_exit(LuxeTerm * terminal, GObject * where_the_object_was)
{
    (void) where_the_object_was;
	LXTermWindow * lxtermwin = terminal->parent;
    g_strfreev(terminal->launch_env);
	if (terminal->terms != NULL) {
		for (guint i = 0; i < terminal->terms->len; i++)
			terminal_free(g_ptr_array_index(terminal->terms, i));
		g_ptr_array_free(terminal->terms, TRUE);
		terminal->terms = NULL;
	}

    /* GtkApplication quits on its own when the last window is destroyed;
     * here just remove the element and reindex the survivors. */
    if (lxtermwin->windows->len == 1) {
        g_ptr_array_remove_index(lxtermwin->windows, terminal->index);
        g_slice_free(LuxeTerm, terminal);
        return;
    }

    /* Remove the element and decrease the index number of each succeeding element. */
    g_ptr_array_remove_index(lxtermwin->windows, terminal->index);
    guint i;
    for (i = terminal->index; i < lxtermwin->windows->len; i++)
    {
        LuxeTerm * t = g_ptr_array_index(lxtermwin->windows, i);
        t->index --;
    }

    /* Release */
    g_slice_free(LuxeTerm, terminal);
}

/* Handler for "child-exited" signal on VTE. */
static void terminal_child_exited_event(VteTerminal * vte, gint status, Term * term)
{
    if (term->closed) return;
    (void) vte;
    (void) status;
    LuxeTerm * terminal = term->parent;

    if (terminal->confirming && terminal->confirm_dialog)
        luxe_choice_respond(GTK_DIALOG(terminal->confirm_dialog), "cancel");

    g_signal_handler_disconnect(G_OBJECT(term->vte), term->exit_handler_id);

    /* Last tab being deleted.  Deallocate memory and close the window. */
	if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal->notebook)) == 1)
	{
		g_ptr_array_remove_index(terminal->terms, term->index);
		g_ptr_array_free(terminal->terms, TRUE);
		terminal->terms = NULL;
		terminal_free(term);
		gtk_window_destroy(GTK_WINDOW(terminal->window));
    }

    /* Not last tab being deleted. */
    else
    {
        /* Remove the element and decrease the index number of each succeeding element. */
        g_ptr_array_remove_index(terminal->terms, term->index);
        guint i;
        for (i = term->index; i < terminal->terms->len; i++)
        {
            Term * t = g_ptr_array_index(terminal->terms, i);
            t->index --;
        }

        /* Delete the tab and free the Term structure. */
        gtk_notebook_remove_page(GTK_NOTEBOOK(terminal->notebook), term->index);
        terminal_free(term);

        /* If only one page is left, hide the tab. */
        if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal->notebook)) == 1)
            gtk_notebook_set_show_tabs(GTK_NOTEBOOK(terminal->notebook), FALSE);

        /* Refresh <Alt>n shortcuts. */
        terminal_shortcuts_rebuild(terminal);
    }
}

/* Adapter for "activate" signal on Close button of tab and File/Close Tab menu item and accelerator. */
static void terminal_close_button_event(GtkButton * button, Term * term)
{
    (void) button;
    terminal_request_close_tab(term);
}


/* Handler for middle click on a notebook tab: close the tab. */
static void terminal_tab_middle_click_event(GtkGestureClick * gesture, int n_press, double x, double y, Term * term)
{
    (void) gesture;
    (void) n_press;
    (void) x;
    (void) y;
    terminal_close_button_event(NULL, term);
}

/* URL under the given widget-relative coordinates, or NULL. */
static gchar * terminal_get_match_at(VteTerminal * vte, double x, double y)
{
    gint tag = -1;
    return vte_terminal_check_match_at(vte, x, y, &tag);
}

static void terminal_show_popup_menu(VteTerminal * vte, double x, double y, Term * term)
{
    /* Match a URL under the cursor; the URL section is only added when
     * something matched. */
    g_free(term->matched_url);
    term->matched_url = terminal_get_match_at(vte, x, y);

    if (term->popup) {
        gtk_popover_popdown(GTK_POPOVER(term->popup));
        return;
    }
    g_autoptr(GMenuModel) model = terminal_popup_model(term->matched_url != NULL);
    GtkWidget * pop = gtk_popover_menu_new_from_model(model);
    term->popup = pop;
    g_object_add_weak_pointer(G_OBJECT(pop), (gpointer *)&term->popup);
    gtk_widget_set_parent(pop, GTK_WIDGET(vte));
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    gtk_popover_set_pointing_to(GTK_POPOVER(pop),
        &(GdkRectangle){ (int) x, (int) y, 1, 1 });
    gtk_popover_popup(GTK_POPOVER(pop));
    g_signal_connect_swapped(pop, "closed", G_CALLBACK(gtk_widget_unparent), pop);
}

/* Handler for "cursor-moved" signal on VTE */
static void terminal_vte_cursor_moved_event(VteTerminal * vte, Term * term)
{
    if (term->closed) return;
    LuxeTerm * terminal = term->parent;
    Term * activeterm = current_term(terminal);
    if (activeterm == NULL)
        return;
    /* If the activity is in a background tab, set its label to bold */
    if ( activeterm != term ) {
        if (term->user_specified_label) {
            const gchar *currentcustomtitle=gtk_label_get_text(GTK_LABEL(term->label));
            gchar *plain = NULL;
            /* Only add an asterisk if there isn't one already */
            if (!g_str_has_prefix(currentcustomtitle,"* "))
                plain = g_strconcat("* ", currentcustomtitle, NULL);
            else
                plain = g_strdup(currentcustomtitle);
            /* Escape: custom titles are user text, not markup. */
            gchar *escaped = g_markup_escape_text(plain, -1);
            gchar *marked = g_strdup_printf("<b>%s</b>", escaped);
            gtk_label_set_markup(GTK_LABEL(term->label), marked);
            g_free(plain);
            g_free(escaped);
            g_free(marked);
        } else {
            const gchar *wtitle = vte_terminal_get_window_title(VTE_TERMINAL(vte));
            /* Escape: window titles come from terminal output. */
            gchar *escaped = g_markup_escape_text(wtitle ? wtitle : "", -1);
            gchar *marked = g_strdup_printf("<b>* %s</b>", escaped);
            gtk_label_set_markup(GTK_LABEL(term->label), marked);
            g_free(escaped);
            g_free(marked);
	}
    }
}

/* Handler for the "pressed" signal of the click gesture on VTE. */
static void terminal_vte_pressed_event(GtkGestureClick * gesture, int n_press, double x, double y, Term * term)
{
    /* Only single presses (old code skipped GDK_2BUTTON_PRESS etc.). */
    if (n_press != 1)
        return;

    VteTerminal * vte = VTE_TERMINAL(term->vte);
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

    /* Right-click. */
    if (button == GDK_BUTTON_SECONDARY)
    {
        /* Own secondary click before VTE handles its legacy PRIMARY-selection
         * paste.  In GTK4, leaving the event unclaimed lets VTE consume the
         * release, so the context menu's Clipboard action is unreliable. */
        gtk_widget_grab_focus(term->vte);
        terminal_show_popup_menu(vte, x, y, term);
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }

    /* Control left click: open URL under cursor. */
    else if ((button == GDK_BUTTON_PRIMARY) && (state & GDK_CONTROL_MASK))
    {
        gchar * match = terminal_get_match_at(vte, x, y);
        if (match != NULL)
        {
            terminal_open_url(match);
            g_free(match);
        }
    }
}

/* Apply new settings in an LuxeTerm to its tab Term. */
static TerminalPalette terminal_current_palette(void)
{
    Setting *setting = get_setting();
    TerminalPalette result;
    if (!setting->follow_desktop_theme ||
        !setting_desktop_palette(&result.foreground, &result.background, result.colors)) {
        result.foreground = setting->foreground_color;
        result.background = setting->background_color;
        memcpy(result.colors, setting->palette_color, sizeof result.colors);
    }
    return result;
}

static void terminal_settings_apply_to_term(LuxeTerm *terminal, Term *term, const TerminalPalette *palette)
{
    Setting * setting = get_setting();
    PangoFontDescription * font_desc;

    /* Terminal properties. */
    font_desc = pango_font_description_from_string(setting->font_name);
    vte_terminal_set_font(VTE_TERMINAL(term->vte), font_desc);
    pango_font_description_free(font_desc);
    vte_terminal_set_font_scale(VTE_TERMINAL(term->vte), terminal->scale);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term->vte), setting->scrollback);
    /* NOTE: vte_terminal_set_allow_bold() is gone in new VTE; the
     * disallow-bold preference is kept in the config file but has no effect. */
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(term->vte), ((setting->cursor_blink) ? VTE_CURSOR_BLINK_ON : VTE_CURSOR_BLINK_OFF));
    vte_terminal_set_cursor_shape(VTE_TERMINAL(term->vte), ((setting->cursor_underline) ? VTE_CURSOR_SHAPE_UNDERLINE : VTE_CURSOR_SHAPE_BLOCK));
    vte_terminal_set_audible_bell(VTE_TERMINAL(term->vte), setting->audible_bell);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(term->vte), setting->hide_pointer);
    /* VTE includes alphanumerics automatically; normalize legacy ranges
     * to its unique ASCII-punctuation grammar without changing the file. */
    GString *exceptions = g_string_new(NULL);
    const char *characters = setting->word_selection_characters;
    if (strchr(characters, '-')) g_string_append_c(exceptions, '-');
    for (const char *p = characters; *p; p++)
        if (g_ascii_ispunct(*p) && *p != '-' && !strchr(exceptions->str, *p))
            g_string_append_c(exceptions, *p);
    vte_terminal_set_word_char_exceptions(VTE_TERMINAL(term->vte), exceptions->str);
    g_string_free(exceptions, TRUE);

    /* Foreground, background and palette colors. */
    vte_terminal_set_colors(VTE_TERMINAL(term->vte), &palette->foreground,
                            &palette->background, palette->colors, 16);

    /* Hide or show scrollbar. */
    gtk_widget_set_visible(term->scrollbar, !setting->hide_scroll_bar);

    /* Hide or show Close button. */
    gtk_widget_set_visible(term->close_button, !setting->hide_close_button);
}

typedef struct
{
	Term *term;
	GCancellable *cancel;
} SpawnToken;

static void spawn_token_free(SpawnToken * token)
{
	g_clear_object(&token->cancel);
	terminal_unref(token->term);
	g_free(token);
}

/* spawn_async completion: remember the child pid and arm child-exited. */
static void terminal_spawned_event(VteTerminal * vte, GPid pid, GError * error, SpawnToken * token)
{
	Term *term = token->term;
	if (error != NULL)
	{
		if (vte && !term->closed && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            vte_terminal_feed(vte, error->message, -1);
            vte_terminal_feed(vte, "\r\n", -1);
        }
	}
	else if (vte && !term->closed)
	{
		term->pid = pid;
	}
	spawn_token_free(token);
}

/* Create a new terminal. */
static Term * terminal_new(LuxeTerm * terminal, const gchar * label, const gchar * pwd,
                           gchar **env, gchar **exec, gboolean interactive_shell)
{
    /* Create and initialize Term structure for new terminal. */
	Term * term = g_slice_new0(Term);
	term->parent = terminal;
	term->references = 1;
    term->interactive_shell = interactive_shell;

    /* Create a VTE and a vertical scrollbar, and place them inside a horizontal box. */
    term->vte = vte_terminal_new();
    g_object_ref_sink(term->vte);
    term->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    term->scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
    gtk_widget_set_hexpand(term->vte, TRUE);
    gtk_widget_set_vexpand(term->vte, TRUE);
    gtk_widget_set_vexpand(term->scrollbar, TRUE);
    gtk_box_append(GTK_BOX(term->box), term->vte);
    gtk_box_append(GTK_BOX(term->box), term->scrollbar);

    /* Set up the VTE. VTE is UTF-8 only now (no set_encoding), but the
     * backspace/delete bindings survived. */
    vte_terminal_set_backspace_binding(VTE_TERMINAL(term->vte), VTE_ERASE_ASCII_DELETE);
    vte_terminal_set_delete_binding(VTE_TERMINAL(term->vte), VTE_ERASE_DELETE_SEQUENCE);

    /* Match URL's, etc. (VteRegex replaces GRegex matching). */
    GError * regex_error = NULL;
    VteRegex * dingus1 = vte_regex_new_for_match(DINGUS1, -1, PCRE2_UTF | PCRE2_NO_UTF_CHECK | PCRE2_MULTILINE, &regex_error);
    if (dingus1 != NULL)
    {
        gint tag = vte_terminal_match_add_regex(VTE_TERMINAL(term->vte), dingus1, 0);
        vte_terminal_match_set_cursor_name(VTE_TERMINAL(term->vte), tag, "pointer");
        vte_regex_unref(dingus1);
    }
    else
    {
        g_warning("Bad URL regex: %s\n", regex_error ? regex_error->message : "?");
        g_clear_error(&regex_error);
    }
    VteRegex * dingus2 = vte_regex_new_for_match(DINGUS2, -1, PCRE2_UTF | PCRE2_NO_UTF_CHECK | PCRE2_MULTILINE, &regex_error);
    if (dingus2 != NULL)
    {
        gint tag = vte_terminal_match_add_regex(VTE_TERMINAL(term->vte), dingus2, 0);
        vte_terminal_match_set_cursor_name(VTE_TERMINAL(term->vte), tag, "pointer");
        vte_regex_unref(dingus2);
    }
    else
    {
        g_warning("Bad URL regex: %s\n", regex_error ? regex_error->message : "?");
        g_clear_error(&regex_error);
    }

    /* Create a horizontal box as the toplevel for the tab label
     * (GtkEventBox is removed; gestures provide the events). */
    term->tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    /* Create the Close button. */
    term->close_button = gtk_button_new_from_icon_name("window-close");
    gtk_button_set_has_frame(GTK_BUTTON(term->close_button), FALSE);
    gtk_widget_set_focusable(term->close_button, FALSE);
    gtk_widget_set_tooltip_text(term->close_button, _("Close Tab"));

    /* Come up with default label and window title */
    if (exec == NULL)
    {
        vte_terminal_feed(VTE_TERMINAL(term->vte), "\033]0;LuxeTerm\007", -1);
    }
    else
    {
        /* Set title to the command being called */
        gchar * cmd = g_path_get_basename(exec[0]);
        gchar * title = g_strdup_printf("\033]0;%s\007", cmd);
        vte_terminal_feed(VTE_TERMINAL(term->vte), title, -1);
        g_free(cmd);
        g_free(title);
    }

    /* Create the label exactly once. */
    if (label != NULL)
    {
        /* User specified label. */
        term->label = gtk_label_new(label);
        term->user_specified_label = TRUE;
    }
    else
    {
        term->label = gtk_label_new(vte_terminal_get_window_title(VTE_TERMINAL(term->vte)));
        term->user_specified_label = FALSE;
    }
    gtk_widget_set_size_request(GTK_WIDGET(term->label), 100, -1);
    gtk_label_set_ellipsize(GTK_LABEL(term->label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(term->label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(term->label), 0.5);

    /* Pack everything into the tab toplevel. */
    gtk_widget_set_hexpand(term->label, TRUE);
    gtk_box_append(GTK_BOX(term->tab), term->label);
    gtk_box_append(GTK_BOX(term->tab), term->close_button);

    /* Attach the scrollbar to the VTE's adjustment. */
    gtk_scrollbar_set_adjustment(GTK_SCROLLBAR(term->scrollbar),
        gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(term->vte)));

    /* Fork the process that will have the VTE as its controlling terminal.
     * spawn_sync is gone in new VTE; the pid arrives in the callback. */
    if (exec == NULL)
    {
        exec = g_malloc(3 * sizeof(gchar *));
        exec[0] = g_strdup(terminal_get_preferred_shell_for_env((const gchar * const *)env));
        exec[1] = g_path_get_basename(exec[0]);
        exec[2] = NULL;
    }

	term->spawn_cancel = g_cancellable_new();
	SpawnToken *spawn_token = g_new0(SpawnToken, 1);
	spawn_token->term = terminal_ref(term);
	spawn_token->cancel = g_object_ref(term->spawn_cancel);
	vte_terminal_spawn_async(
                    VTE_TERMINAL(term->vte),
                    VTE_PTY_NO_LASTLOG | VTE_PTY_NO_UTMP | VTE_PTY_NO_WTMP,
                    pwd,
                    exec,
                    env,
                    (env ? G_SPAWN_SEARCH_PATH_FROM_ENVP : G_SPAWN_SEARCH_PATH) | G_SPAWN_FILE_AND_ARGV_ZERO,
                    NULL,
                    NULL,
                    NULL,
                    -1,
					term->spawn_cancel,
					(VteTerminalSpawnAsyncCallback) terminal_spawned_event,
					spawn_token);
    g_strfreev(exec);

    /* Click handling via gestures (button-press-event is removed). */
    GtkGesture * tab_middle = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_middle), GDK_BUTTON_MIDDLE);
    g_signal_connect(tab_middle, "pressed", G_CALLBACK(terminal_tab_middle_click_event), term);
    gtk_widget_add_controller(term->tab, GTK_EVENT_CONTROLLER(tab_middle));
    g_signal_connect(G_OBJECT(term->close_button), "clicked", G_CALLBACK(terminal_close_button_event), term);
    GtkGesture * vte_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(vte_click), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(vte_click), GTK_PHASE_CAPTURE);
    g_signal_connect(vte_click, "pressed", G_CALLBACK(terminal_vte_pressed_event), term);
    gtk_widget_add_controller(term->vte, GTK_EVENT_CONTROLLER(vte_click));
    term->exit_handler_id = g_signal_connect(G_OBJECT(term->vte), "child-exited", G_CALLBACK(terminal_child_exited_event), term);
    g_signal_connect(G_OBJECT(term->vte), "cursor-moved", G_CALLBACK(terminal_vte_cursor_moved_event), term);
    g_signal_connect(G_OBJECT(term->vte), "window-title-changed", G_CALLBACK(terminal_window_title_changed_event), term);

    /* Apply user preferences. */
    TerminalPalette palette = terminal_current_palette();
    terminal_settings_apply_to_term(terminal, term, &palette);
    return term;
}

/* Deallocate a Term structure. */
static void terminal_free(Term * term)
{
    term->closed = TRUE;
	if (term->spawn_cancel != NULL)
		g_cancellable_cancel(term->spawn_cancel);
    if (term->popup) {
        GtkWidget *popup = term->popup;
        g_object_remove_weak_pointer(G_OBJECT(popup), (gpointer *)&term->popup);
        term->popup = NULL;
        if (gtk_widget_get_parent(popup)) gtk_widget_unparent(popup);
    }
    g_signal_handlers_disconnect_by_data(term->vte, term);
    g_clear_object(&term->vte);
	terminal_unref(term);
}

static Term * terminal_ref(Term * term)
{
	g_atomic_int_inc(&term->references);
	return term;
}

static void terminal_unref(Term * term)
{
	if (!g_atomic_int_dec_and_test(&term->references))
		return;
	g_clear_object(&term->spawn_cancel);
	g_free(term->matched_url);
	g_slice_free(Term, term);
}

/* Initialize the header bar's hamburger menu (plus win.* actions). The
 * former GtkPopoverMenuBar was replaced by a header bar with a menu
 * button; its menu model keeps every action from the old menu bar. */
static void terminal_menubar_initialize(LuxeTerm * terminal)
{
    g_action_map_add_action_entries(G_ACTION_MAP(terminal->window),
                                    win_actions, G_N_ELEMENTS(win_actions),
                                    terminal);
    terminal->menu = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(terminal->menu), "open-menu-symbolic");
    g_autoptr(GMenuModel) model = terminal_hamburger_model();
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(terminal->menu), model);
    gtk_widget_set_tooltip_text(terminal->menu, _("Main Menu"));
    gtk_widget_set_visible(terminal->menu, !get_setting()->hide_menu_bar);
    terminal_shortcuts_rebuild(terminal);
}

static gboolean terminal_program_exists(const char *program, const gchar * const *environment,
                                        const char *cwd)
{
    if (strchr(program, '/')) {
        g_autofree char *path = g_canonicalize_filename(program, cwd);
        return g_file_test(path, G_FILE_TEST_IS_REGULAR) && access(path, X_OK) == 0;
    }
    const char *path = environment ? g_environ_getenv((gchar **)environment, "PATH") : g_getenv("PATH");
    g_auto(GStrv) directories = g_strsplit(path ? path : "/bin:/usr/bin", ":", -1);
    for (guint i = 0; directories[i]; i++) {
        g_autofree char *directory = g_canonicalize_filename(*directories[i] ? directories[i] : ".", cwd);
        g_autofree char *candidate = g_build_filename(directory, program, NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR) && access(candidate, X_OK) == 0) return TRUE;
    }
    return FALSE;
}

static gboolean process_arguments_for_env(gint argc, gchar **argv, CommandArguments *arguments,
                                          const gchar * const *environment, const char *cwd)
{
    memset(arguments, 0, sizeof(*arguments));
    arguments->exit_status = 1;
    if (argc < 1 || !argv || !argv[0]) return FALSE;
    arguments->executable = argv[0];
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (g_str_has_prefix(arg, "--command=")) {
            g_clear_pointer(&arguments->command, g_strfreev);
            if (!g_shell_parse_argv(arg + 10, NULL, &arguments->command, NULL)) return FALSE;
        } else if (g_str_equal(arg, "-e") || g_str_equal(arg, "--command")) {
            g_clear_pointer(&arguments->command, g_strfreev);
            if (++i >= argc || !*argv[i]) return FALSE;
            arguments->command = g_strdupv(argv + i);
            break;
        } else if (g_str_has_prefix(arg, "--geometry=")) {
            g_auto(GStrv) parts = g_strsplit(arg + 11, "x", -1);
            guint64 columns, rows;
            if (g_strv_length(parts) != 2 ||
                !g_ascii_string_to_unsigned(parts[0], 10, 2, 1000, &columns, NULL) ||
                !g_ascii_string_to_unsigned(parts[1], 10, 2, 1000, &rows, NULL)) return FALSE;
            arguments->geometry_columns = columns;
            arguments->geometry_rows = rows;
        } else if (g_str_equal(arg, "-l") || g_str_equal(arg, "--loginshell")) {
            arguments->login_shell = TRUE;
        } else if (g_str_has_prefix(arg, "--title=")) {
            arguments->title = argv[i] + 8;
        } else if (g_str_has_prefix(arg, "--tabs=")) {
            arguments->tabs = argv[i] + 7;
        } else if (g_str_equal(arg, "-t") || g_str_equal(arg, "-T") || g_str_equal(arg, "--title")) {
            if (++i >= argc) return FALSE;
            arguments->title = argv[i];
        } else if (g_str_has_prefix(arg, "--working-directory=")) {
            arguments->working_directory = argv[i] + 20;
        } else if (g_str_equal(arg, "--no-remote")) {
            arguments->no_remote = TRUE;
        } else if (g_str_equal(arg, "-v") || g_str_equal(arg, "--version")) {
            g_print("%s\n", PACKAGE_STRING);
            arguments->exit_status = 0;
            return FALSE;
        } else if (g_str_equal(arg, "-h") || g_str_equal(arg, "--help")) {
            g_print("%s", usage_display);
            arguments->exit_status = 0;
            return FALSE;
        } else {
            g_printerr("Unknown terminal option: %s\n", arg);
            return FALSE;
        }
    }
    guint count = arguments->command ? g_strv_length(arguments->command) : 0;
    if (arguments->command && (!count || !*arguments->command[0])) return FALSE;
    arguments->interactive_shell = count == 0;
    /* Preserve the legacy one-string -e shell command convention. */
    gboolean shell_string = FALSE;
    if (count == 1) {
        g_autofree char *working = arguments->working_directory
            ? g_canonicalize_filename(arguments->working_directory, cwd) : NULL;
        shell_string = !terminal_program_exists(arguments->command[0], environment, working ? working : cwd);
    }
    if (arguments->login_shell || shell_string) {
        const char *shell = terminal_get_preferred_shell_for_env(environment);
        if (!shell) return FALSE;
        g_autofree char *base = g_path_get_basename(shell);
        char **exec = g_new0(char *, count ? 5 : 3);
        exec[0] = g_strdup(shell);
        exec[1] = g_strdup_printf("-%s", base);
        if (count) {
            exec[2] = g_strdup("-c");
            GString *command = g_string_new(NULL);
            for (guint i = 0; i < count; i++) {
                g_autofree char *quoted = shell_string ? g_strdup(arguments->command[i]) :
                    g_shell_quote(arguments->command[i]);
                if (i) g_string_append_c(command, ' ');
                g_string_append(command, quoted);
            }
            exec[3] = g_string_free(command, FALSE);
        }
        g_strfreev(arguments->command);
        arguments->command = exec;
    } else if (count) {
        char **exec = g_new0(char *, count + 2);
        exec[0] = g_strdup(arguments->command[0]);
        memcpy(exec + 1, arguments->command, count * sizeof(char *));
        g_free(arguments->command);
        arguments->command = exec;
    }
    arguments->exit_status = 0;
    return TRUE;
}

gboolean luxeterm_process_arguments(gint argc, gchar **argv, CommandArguments *arguments)
{
    g_autofree char *cwd = g_get_current_dir();
    return process_arguments_for_env(argc, argv, arguments, NULL, cwd);
}

/* Initialize a new LuxeTerm.
 * This is a toplevel window that may contain tabs, each of which will contain a VTE controlled by a process. */
LuxeTerm * luxeterm_initialize(LXTermWindow * lxtermwin, CommandArguments * arguments)
{
    /* Allocate and initialize the LuxeTerm structure. */
    LuxeTerm * terminal = g_slice_new0(LuxeTerm);
    terminal->parent = lxtermwin;
    terminal->terms = g_ptr_array_new();
    terminal->login_shell = arguments->login_shell;
    terminal->launch_env = arguments->environment ? g_strdupv((gchar **)arguments->environment) : NULL;
    g_ptr_array_add(lxtermwin->windows, terminal);
    terminal->index = terminal->parent->windows->len - 1;
    terminal->scale = 1.0;
    Setting * setting = get_setting();

    /* Create toplevel window. */
    terminal->window = gtk_application_window_new(GTK_APPLICATION(terminal->parent->app));
    g_object_set_data(G_OBJECT(terminal->window), "luxe-terminal", terminal);
    g_signal_connect(terminal->window, "unrealize", G_CALLBACK(terminal_unrealize), terminal);
    gtk_window_set_icon_name(GTK_WINDOW(terminal->window), "org.luxe.Term");
    /* GTK4 windows are RGBA natively; no visual/colormap setup needed. */
    terminal->rgba = TRUE;

    g_object_weak_ref(G_OBJECT(terminal->window), (GWeakNotify) terminal_window_exit, terminal);

    /* Create a vertical box as the child of the toplevel window. */
    terminal->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);

    /* Create the hamburger menu button and register the win.* actions. */
    terminal_menubar_initialize(terminal);

    /* GtkApplicationWindow hosts a header bar (carrying the hamburger
     * menu button) in an GtkBox, never via gtk_window_set_titlebar. */
    GtkWidget * header = gtk_header_bar_new();
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), terminal->menu);
    GtkWidget * toolbar_view = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar_view), header);
    luxe_toolbar_set_content(GTK_BOX(toolbar_view), terminal->box);
    luxe_window_set_content(GTK_WINDOW(terminal->window),
                                       toolbar_view);

    /* Create a notebook as the child of the vertical box. */
    terminal->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(terminal->notebook), TRUE);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(terminal->notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(terminal->notebook), FALSE);
    gtk_box_append(GTK_BOX(terminal->box), terminal->notebook);
    gtk_widget_set_vexpand(terminal->notebook, TRUE);

    /* Initialize tab position. */
    terminal->tab_position = terminal_tab_get_position_id(setting->tab_position);
    terminal_tab_set_position(terminal->notebook, terminal->tab_position);

    /* Connect signals. */
    g_signal_connect(G_OBJECT(terminal->notebook), "switch-page",
        G_CALLBACK(terminal_switch_page_event), terminal);
    g_signal_connect(G_OBJECT(terminal->window), "close-request",
        G_CALLBACK(terminal_close_window_confirmation_request), terminal);

    /* Create the first terminal. */
    gchar * local_working_directory = NULL;
    if (arguments->working_directory == NULL)
    {
        local_working_directory = g_get_current_dir();
    }
    Term * term = terminal_new(
        terminal,
        ((arguments->title != NULL) ? arguments->title : NULL),
        ((arguments->working_directory != NULL) ? arguments->working_directory : local_working_directory),
        terminal->launch_env,
        arguments->command,
        arguments->interactive_shell);
    arguments->command = NULL;
    g_free(local_working_directory);

    /* Set window title. */
    gtk_window_set_title(GTK_WINDOW(terminal->window), gtk_label_get_text(GTK_LABEL(term->label)));

    /* Set the terminal size (vte_terminal_set_size is the GTK4-safe way;
     * window geometry hints no longer exist). */
    if ((arguments->geometry_columns != 0) && (arguments->geometry_rows != 0)) {
        vte_terminal_set_size(VTE_TERMINAL(term->vte), arguments->geometry_columns, arguments->geometry_rows);
    } else {
        vte_terminal_set_size(VTE_TERMINAL(term->vte), setting->geometry_columns, setting->geometry_rows);
    }

    /* Add the first terminal to the notebook and the data structures. */
    gtk_notebook_append_page(GTK_NOTEBOOK(terminal->notebook), term->box, term->tab);
    term->index = gtk_notebook_get_n_pages(GTK_NOTEBOOK(terminal->notebook)) - 1;
    g_ptr_array_add(terminal->terms, term);

    /* Show the window. */
    gtk_window_present(GTK_WINDOW(terminal->window));

    /* NOTE 2026 Jerry Howell: do NOT re-apply settings here.  All settings
     * were already applied to the VTE in terminal_new(), BEFORE the widget
     * was mapped.  Re-applying set_font/set_colors on a MAPPED VTE breaks
     * VTE 0.80's renderer: the terminal then draws its background but no
     * glyphs at all (invisible prompt; MC-style apps still show their own
     * colors).  If the settings ever change at runtime, a full unmap/remap
     * cycle would be needed instead. */
    /* terminal_settings_apply(terminal); */

    if (arguments->tabs != NULL && arguments->tabs[0] != '\0')
    {
        g_auto(GStrv) tabs = g_strsplit(arguments->tabs, ",", -1);
        gboolean first = TRUE;
        for (guint i = 0; tabs[i]; i++) {
            if (!*tabs[i]) continue;
            if (first) {
                term->user_specified_label = TRUE;
                gtk_label_set_text(GTK_LABEL(term->label), tabs[i]);
                gtk_window_set_title(GTK_WINDOW(terminal->window), tabs[i]);
                first = FALSE;
            } else terminal_new_tab(terminal, tabs[i]);
        }
    }

    return terminal;
}

/* Apply new settings to a terminal. */
static void terminal_settings_apply(LuxeTerm *terminal, const TerminalPalette *palette)
{
    /* GTK4 windows are always RGBA-capable. */
    terminal->rgba = TRUE;

    /* Update tab position. */
    terminal->tab_position = terminal_tab_get_position_id(get_setting()->tab_position);
    terminal_tab_set_position(terminal->notebook, terminal->tab_position);

    /* Hide or show menubar. */
    gtk_widget_set_visible(terminal->menu, !get_setting()->hide_menu_bar);

    /* Apply settings to all tabs. */
    guint i;
    for (i = 0; i < terminal->terms->len; i++)
    {
        terminal_settings_apply_to_term(terminal, g_ptr_array_index(terminal->terms, i), palette);
    }

}

/* Apply terminal settings to all tabs in all terminals. */
static void terminal_settings_apply_each(gpointer data, gpointer user_data)
{
    terminal_settings_apply(data, user_data);
}

void terminal_settings_apply_to_all(LuxeTerm * terminal)
{
    /* Accelerators belong to GtkApplication, not to each window or tab. */
    terminal_shortcuts_rebuild(terminal);
    TerminalPalette palette = terminal_current_palette();
    g_ptr_array_foreach(terminal->parent->windows, terminal_settings_apply_each, &palette);
    get_setting()->geometry_change = FALSE;
}

static void terminal_desktop_theme_changed(GObject *settings, GParamSpec *property,
                                           LXTermWindow *context)
{
    (void)settings; (void)property;
    Setting *setting = get_setting();
    if (!setting || !setting->follow_desktop_theme) return;
    TerminalPalette palette = terminal_current_palette();
    for (guint i = 0; i < context->windows->len; i++) {
        LuxeTerm *window = g_ptr_array_index(context->windows, i);
        if (window->closed || !window->terms) continue;
        for (guint j = 0; j < window->terms->len; j++) {
            Term *term = g_ptr_array_index(window->terms, j);
            if (!term->closed)
                vte_terminal_set_colors(VTE_TERMINAL(term->vte), &palette.foreground,
                                        &palette.background, palette.colors, 16);
        }
    }
}

/* GApplication owns authenticated same-user command forwarding and cwd. */
static char *terminal_application_id(const char *display_name)
{
    g_autofree char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, display_name, -1);
    return g_strdup_printf("org.luxe.Term.d%.16s", digest);
}

static gint luxeterm_command_line(GApplication *app, GApplicationCommandLine *line, LXTermWindow *context)
{
    (void)app;
    int argc;
    g_auto(GStrv) argv = g_application_command_line_get_arguments(line, &argc);
    CommandArguments arguments;
    const gchar * const *environment = g_application_command_line_get_environ(line);
    const char *cwd = g_application_command_line_get_cwd(line);
    if (!process_arguments_for_env(argc, argv, &arguments, environment, cwd)) {
        g_strfreev(arguments.command);
        return arguments.exit_status;
    }
    g_autofree char *directory = g_canonicalize_filename(
        arguments.working_directory ? arguments.working_directory : ".",
        g_application_command_line_get_cwd(line));
    arguments.working_directory = directory;
    arguments.environment = environment;
    luxeterm_initialize(context, &arguments);
    return 0;
}
int main(gint argc, gchar **argv)
{
    setlocale(LC_ALL, "");
#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif
    CommandArguments arguments;
    gboolean valid = luxeterm_process_arguments(argc, argv, &arguments);
    g_strfreev(arguments.command);
    if (!valid) return arguments.exit_status;
    luxe_ui_init();
    LXTermWindow context = {0};
    context.windows = g_ptr_array_new();
    GApplicationFlags flags = G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_SEND_ENVIRONMENT;
    if (arguments.no_remote) flags |= G_APPLICATION_NON_UNIQUE;
    g_autofree char *application_id = terminal_application_id(gdk_display_get_name(gdk_display_get_default()));
    context.app = GTK_APPLICATION(luxe_application_new(application_id, flags));
    load_setting();
    g_signal_connect(gtk_settings_get_default(), "notify::gtk-theme-name",
                     G_CALLBACK(terminal_desktop_theme_changed), &context);
    g_signal_connect(context.app, "command-line", G_CALLBACK(luxeterm_command_line), &context);
    int status = g_application_run(G_APPLICATION(context.app), argc, argv);
    while (gtk_application_get_windows(context.app))
        gtk_window_destroy(gtk_application_get_windows(context.app)->data);
    g_signal_handlers_disconnect_by_data(gtk_settings_get_default(), &context);
    g_clear_object(&context.app);
    g_ptr_array_unref(context.windows);
    set_setting(NULL);
    return status;
}
