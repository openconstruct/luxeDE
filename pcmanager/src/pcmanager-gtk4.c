/*
 *      pcmanager-gtk4.c - GTK4 implementation of pcmanager.
 *
 *      GTK4 file browser and X11 desktop manager.
 *
 *      Copyright 2026 Jerry Howell <jerr@luxe.desktop>
 *      Based on PCMan file manager code
 *      Copyright 2009 - 2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
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

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop-gtk4.h"
#include "single-inst.h"
#include "browser-gtk4.h"
#include <libfm/fm.h>

static gboolean daemon_mode = FALSE;
static gboolean show_desktop = FALSE;
static gboolean desktop_off = FALSE;
static gboolean new_window = FALSE;
static char **files = NULL;
static gboolean no_desktop = FALSE;
static gint show_pref = -1;
static gboolean desktop_pref = FALSE;
static char *set_wallpaper = NULL;
static char *wallpaper_mode = NULL;
static char *profile = NULL;

static GOptionEntry opt_entries[] =
{
    { "profile", 'p', 0, G_OPTION_ARG_STRING, &profile, N_("Name of configuration profile"), N_("PROFILE") },
    { "daemon-mode", 'd', 0, G_OPTION_ARG_NONE, &daemon_mode, N_("Run PCManager as a daemon"), NULL },
    { "no-desktop", '\0', 0, G_OPTION_ARG_NONE, &no_desktop, N_("No function. Just to be compatible with nautilus"), NULL },
    { "desktop", '\0', 0, G_OPTION_ARG_NONE, &show_desktop, N_("Launch desktop manager"), NULL },
    { "desktop-off", '\0', 0, G_OPTION_ARG_NONE, &desktop_off, N_("Turn off desktop manager if it's running"), NULL },
    { "desktop-pref", '\0', 0, G_OPTION_ARG_NONE, &desktop_pref, N_("Open desktop preference dialog"), NULL },
    { "set-wallpaper", 'w', 0, G_OPTION_ARG_FILENAME, &set_wallpaper, N_("Set desktop wallpaper from image FILE"), N_("FILE") },
    { "wallpaper-mode", '\0', 0, G_OPTION_ARG_STRING, &wallpaper_mode, N_("Set mode of desktop wallpaper. MODE=(color|stretch|fit|crop|center|tile|screen)"), N_("MODE") },
    { "show-pref", '\0', 0, G_OPTION_ARG_INT, &show_pref, N_("Open Preferences dialog on the page N"), N_("N") },
    { "new-win", 'n', 0, G_OPTION_ARG_NONE, &new_window, N_("Open a new window"), NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &files, N_("Files or folders to open"), N_("FILES") },
    { NULL }
};

static GtkApplication *app = NULL;
static char *activate_cwd = NULL;
static gboolean ipc_running = FALSE;
static gboolean browser_initialized = FALSE;

static void quit_if_idle(void)
{
    if (app && !daemon_mode && !pcmgr_desktop_running() && !pcmgr_browser_count())
        g_application_quit(G_APPLICATION(app));
}
static void ensure_browser(void)
{
    if (browser_initialized) return;
    fm_init(NULL);
    pcmgr_browser_setup(profile, G_CALLBACK(quit_if_idle));
    browser_initialized = TRUE;
}
static void single_inst_cb(const char *cwd, int screen_num)
{
    (void)screen_num;
    gboolean command = show_desktop || desktop_off || desktop_pref || set_wallpaper || wallpaper_mode || show_pref >= 0;
    if (show_desktop && !pcmgr_desktop_running()) pcmgr_desktop_manager_init(profile);
    if (desktop_off && pcmgr_desktop_running()) pcmgr_desktop_manager_finalize();
    if (set_wallpaper || wallpaper_mode) pcmgr_set_wallpaper(set_wallpaper, wallpaper_mode);
    if (desktop_pref) {
        if (!pcmgr_desktop_running()) pcmgr_desktop_manager_init(profile);
        pcmgr_desktop_preferences();
    }
    if (show_pref >= 0) {
        ensure_browser();
        pcmgr_browser_preferences();
    }
    if (files || (!command && (!daemon_mode || ipc_running)) || new_window) {
        ensure_browser();
        pcmgr_browser_open((const char * const *)files, cwd, new_window);
    }
    show_desktop = desktop_off = desktop_pref = new_window = FALSE;
    g_clear_pointer(&set_wallpaper, g_free); g_clear_pointer(&wallpaper_mode, g_free);
    g_clear_pointer(&files, g_strfreev); show_pref = -1;
    quit_if_idle();
}
static void on_activate(GApplication *gapp, gpointer data)
{
    (void)gapp; (void)data;
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    char *cwd = activate_cwd ? g_strdup(activate_cwd) : g_get_current_dir();
    single_inst_cb(cwd, 0);
    g_free(cwd);
    /* Later IPC arrivals run with the loop live, so daemon-mode plain
     * launches open a window then (matching the old main-loop check). */
    ipc_running = TRUE;
    quit_if_idle();
}

int main(int argc, char **argv)
{
    GError *err = NULL;
    SingleInstData inst;
    GOptionContext *ctx;

#ifdef ENABLE_NLS
    bindtextdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif

    ctx = g_option_context_new(" - file and desktop manager (GTK4)");
    g_option_context_add_main_entries(ctx, opt_entries, GETTEXT_PACKAGE);
    if (!g_option_context_parse(ctx, &argc, &argv, &err))
    {
        g_print("%s\n", err->message);
        g_error_free(err);
        g_option_context_free(ctx);
        return 1;
    }
    g_option_context_free(ctx);

    /* Keep the legacy Frame commands available before the browser starts, but
     * finish finite commands and continue when the caller also requested a
     * browser window or preferences. */
    if (show_desktop || desktop_off || desktop_pref || set_wallpaper || wallpaper_mode) {
        if (wallpaper_mode && !g_str_equal(wallpaper_mode, "stretch")) {
            g_printerr("pcmanager: Frame supports only stretch wallpaper mode (requested: %s)\n", wallpaper_mode);
            return 2;
        }
        if (wallpaper_mode && !set_wallpaper) {
            g_printerr("pcmanager: --wallpaper-mode requires --set-wallpaper FILE\n");
            return 2;
        }
        const char *option = desktop_pref ? "--wallpaper-settings" :
            desktop_off ? "--wallpaper-stop" : set_wallpaper ? "--set-wallpaper" : "--wallpaper-daemon";
        const char *arguments[] = {"frame", option, set_wallpaper, NULL};
        g_autoptr(GSubprocess) child = g_subprocess_newv(arguments, G_SUBPROCESS_FLAGS_NONE, &err);
        if (!child) {g_printerr("%s\n",err->message); g_clear_error(&err); return 1;}
        gboolean finite_command = desktop_pref || desktop_off || set_wallpaper;
        gboolean wait_ok = TRUE;
        if (finite_command && !g_subprocess_wait_check(child, NULL, &err)) {
            g_printerr("Frame command failed: %s\n", err->message);
            g_clear_error(&err);
            wait_ok = FALSE;
        }
        gboolean open_browser = files || new_window || show_pref >= 0;
        if (!open_browser) return wait_ok ? 0 : 1;
        if (!wait_ok) return 1;
        show_desktop = desktop_off = desktop_pref = FALSE;
        g_clear_pointer(&set_wallpaper, g_free);
        g_clear_pointer(&wallpaper_mode, g_free);
    }
    luxe_ui_init();

    /* ensure that there is only one instance of pcmanager. */
    inst.prog_name = "pcmanager";
    inst.cb = single_inst_cb;
    /* skip first-instance-only options (profile, daemon-mode, no-desktop):
     * they must not be forwarded through IPC */
    inst.opt_entries = opt_entries + 3;
    inst.screen_num = 0;
    switch (single_inst_init(&inst))
    {
        case SINGLE_INST_CLIENT: /* we're not the first instance. */
            single_inst_finalize(&inst);
            return 0;
        case SINGLE_INST_ERROR: /* error happened. */
            single_inst_finalize(&inst);
            return 1;
        case SINGLE_INST_SERVER:
            break;
    }

    g_free(activate_cwd);
    activate_cwd = g_get_current_dir();
    /* NON_UNIQUE: single-instance stays with the private socket above
     * (which forwards argv); GApplication uniqueness would swallow
     * remote commands without forwarding them. */
    app = GTK_APPLICATION(luxe_application_new("org.luxe.PcManager", G_APPLICATION_NON_UNIQUE));
    pcmgr_browser_set_application(GTK_APPLICATION(app));
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    /* Hold so daemon/desktop modes with zero browser windows stay alive;
     * quit_if_idle() quits explicitly when fully idle. */
    g_application_hold(G_APPLICATION(app));
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_clear_object(&app);
    g_free(activate_cwd); activate_cwd = NULL;
    pcmgr_desktop_manager_finalize();
    single_inst_finalize(&inst);
    if (browser_initialized) fm_finalize();
    return status;
}
