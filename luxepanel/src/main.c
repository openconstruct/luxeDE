/*
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Jürgen Hötzel <juergen@archlinux.org>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Lajos Kamocsay <lajos@panka.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Jack Chen <speed.up08311990@gmail.com>
 *               2012 Rafał Mużyło <galtgendo@gmail.com>
 *               2012 Michael Rawson <michaelrawson76@gmail.com>
 *               2012 Julien Lavergne <julien.lavergne@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2013 peadaredwards <peadaredwards@users.sourceforge.net>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *
 * This file is a part of LXPanel project.
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
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <gdk/x11/gdkx.h>
#include <libfm/fm.h>
#include "x11-hotkey.h"
#include "renderer-init.h"
#include "luxe-runtime-dir.h"

#define __LUXEPANEL_INTERNALS__

#include "private.h"
#include "misc.h"

#include "luxepanelctl.h"
#include "gtk-compat.h"
#include "dbg.h"
#include "space.h"

static gchar *cfgfile = NULL;
static gchar version[] = VERSION;
static int config = 0;

static gboolean is_restarting = FALSE;
static Window panel_lock_owner = None;

Command commands[] = {
    //{ "configure", N_("Preferences"), configure },
    { "run", N_("Run"), gtk_run },
    { "restart", N_("Restart"), restart },
    { "logout", N_("Logout"), logout },
    { NULL, NULL },
};

static GMainLoop *panel_main_loop = NULL;

void restart(void)
{
    ENTER;
    is_restarting = TRUE;

    if (panel_main_loop != NULL)
        g_main_loop_quit(panel_main_loop);
    RET();
}

/* Trust boundary for _LUXEPANEL_CMD: X11 ClientMessages carry no sender
 * authentication.  Any client able to open this X display (same user,
 * or any host/user granted X access) can send root-window messages that
 * arrive here, so this channel is same-user-trust only.  We therefore
 * allowlist opcodes, sanity-check arguments, and rate-limit floods, but
 * make no attempt to identify the sender.  A mutually-authenticated
 * channel (e.g. D-Bus) is deliberately out of scope: keep the X11 path. */

/* Loose token bucket: bursts of up to 20 commands per second pass (normal
 * use is a single message); sustained floods are dropped with a throttled
 * warning so a hostile or buggy client cannot spin restart/config loops. */
static gboolean luxepanel_cmd_rate_limit(void)
{
    static gint64 window = 0;
    static int count = 0;
    static gint64 last_warn = 0;
    gint64 now = g_get_monotonic_time();

    if (now - window > G_USEC_PER_SEC)
    {
        window = now;
        count = 0;
    }
    if (++count <= 20)
        return TRUE;
    if (now - last_warn > 5 * G_USEC_PER_SEC)
    {
        last_warn = now;
        g_warning("_LUXEPANEL_CMD flood ignored (rate limit)");
    }
    return FALSE;
}

/* Plugin type names come from our own registry keys; restrict to a safe
 * charset so a forged message cannot smuggle path or format characters. */
static gboolean luxepanel_cmd_plugin_name_ok(const char * s)
{
    if (s == NULL || *s == '\0')
        return FALSE;
    for (; *s != '\0'; s++)
        if (!g_ascii_isalnum(*s) && *s != '_' && *s != '-' && *s != '+')
            return FALSE;
    return TRUE;
}

/* Plugin control strings are short single-line tokens (e.g. ADD/DEL or a
 * plugin verb); reject empty strings and embedded control characters. */
static gboolean luxepanel_cmd_arg_ok(const char * s)
{
    if (s == NULL || *s == '\0')
        return FALSE;
    for (; *s != '\0'; s++)
        if (g_ascii_iscntrl(*s))
            return FALSE;
    return TRUE;
}

static void process_client_msg ( XClientMessageEvent* ev )
{
    int cmd;
    guint8 selector;

    if (ev == NULL || ev->format != 8)
        return;
    selector = (guint8)ev->data.b[0];
    cmd = (int)selector;
    int monitor;
    int edge;
    char *plugin_type;
    char *command;
    /* Opcode allowlist: only the enum values luxepanelctl can send. */
    if (cmd < LUXEPANEL_CMD_SYS_MENU || cmd > LUXEPANEL_CMD_COMMAND)
    {
        g_debug("ignoring _LUXEPANEL_CMD with unknown opcode %d", cmd);
        return;
    }
    if (!luxepanel_cmd_rate_limit())
        return;
    switch( cmd )
    {
#ifndef DISABLE_MENU
        case LUXEPANEL_CMD_SYS_MENU:
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                LuxePanel* p = (LuxePanel*)l->data;

                if (p->priv->box == NULL)
                    continue;
                for (GtkWidget *iter = gtk_widget_get_first_child(p->priv->box);
                     iter != NULL; iter = gtk_widget_get_next_sibling(iter))
                {
                    const LuxePanelPluginInit *init = PLUGIN_CLASS(iter);
                    if (init != NULL && init->show_system_menu)
                        /* queue to show system menu */
                        init->show_system_menu(iter);
                }
            }
            break;
        }
#endif
        case LUXEPANEL_CMD_RUN:
            gtk_run();
            break;
        case LUXEPANEL_CMD_CONFIG:
            {
            LuxePanel * p = ((all_panels != NULL) ? all_panels->data : NULL);
            if (p != NULL)
                panel_configure(p, 0);
            }
            break;
        case LUXEPANEL_CMD_RESTART:
            restart();
            break;
        case LUXEPANEL_CMD_EXIT:
            if (panel_main_loop != NULL)
                g_main_loop_quit(panel_main_loop);
            break;
        case LUXEPANEL_CMD_COMMAND:
            selector = (guint8)ev->data.b[1];
            monitor = (selector & 0xf) - 1; /* 0 for no monitor */
            edge = (selector >> 4) & 0x7;
            if ((selector & 0x80) != 0)
                /* some extension, not supported yet */
                break;
            if (edge > EDGE_BOTTOM)
                break;
            plugin_type = g_strndup(&ev->data.b[2], 18);
            command = strchr(plugin_type, '\t');
            if (command) do /* use do{}while(0) to enable break */
            {
                LuxePanel *p;
                GSList *l;
                const LuxePanelPluginInit *init;
                GtkWidget *plugin = NULL;

                *command++ = '\0';
                if (!luxepanel_cmd_plugin_name_ok(plugin_type) ||
                    !luxepanel_cmd_arg_ok(command))
                    break;
                /* find the panel by monitor and edge */
                for (l = all_panels; l; l = l->next)
                {
                    p = (LuxePanel*)l->data;
                    if (p->priv->box == NULL) /* inactive panel */
                        continue;
                    if (monitor >= 0 && p->priv->monitor != monitor)
                        continue;
                    if (edge == EDGE_NONE || p->priv->edge == edge)
                        break;
                }
                if (l == NULL) /* match not found */
                    break;
                /* find the plugin */
                init = g_hash_table_lookup(luxepanel_get_all_types(), plugin_type);
                if (init == NULL) /* no such plugin known */
                    break;
                for (GtkWidget *iter = gtk_widget_get_first_child(p->priv->box);
                     iter != NULL; iter = gtk_widget_get_next_sibling(iter))
                {
                    if (init == PLUGIN_CLASS(iter))
                    {
                        plugin = iter;
                        break;
                    }
                }
                /* test for built-in commands ADD and DEL */
                if (strcmp(command, "ADD") == 0)
                {
                    if (plugin == NULL)
                    {
                        config_setting_t *cfg;

                        cfg = config_group_add_subgroup(config_root_setting(p->priv->config),
                                                        "Plugin");
                        if (cfg == NULL)
                            break;
                        config_group_set_string(cfg, "type", plugin_type);
                        plugin = luxepanel_add_plugin(p, plugin_type, cfg, -1);
                        if (plugin == NULL) /* failed to create */
                            config_setting_destroy(cfg);
                    }
                }
                else if (strcmp(command, "DEL") == 0)
                {
                    if (plugin != NULL)
                        luxepanel_remove_plugin(p, plugin);
                }
                /* send the command */
                else if (plugin && init->control)
                    init->control(plugin, command);
            } while(0);
            g_free(plugin_type);
            break;
    }
}

/* GTK4 has no X event filters; a private X connection + GSource pumps
 * root-window property changes and client messages. */
static void panel_root_event(XEvent *ev);

static Display *root_watch_dpy = NULL;
static GSource *root_watch_source = NULL;

typedef struct
{
    GSource source;
    GPollFD poll_fd;
} RootWatchSource;

static gboolean root_watch_check(GSource *source)
{
    RootWatchSource *src = (RootWatchSource *)source;
    if (root_watch_dpy == NULL || !(src->poll_fd.revents & G_IO_IN))
        return FALSE;
    return XPending(root_watch_dpy) > 0;
}

static gboolean root_watch_prepare(GSource *source, gint *timeout)
{
    *timeout = -1;
    return root_watch_dpy != NULL && XPending(root_watch_dpy) > 0;
}

static gboolean root_watch_dispatch(GSource *source, GSourceFunc cb, gpointer data)
{
    while (root_watch_dpy != NULL && XPending(root_watch_dpy) > 0)
    {
        XEvent ev;
        XNextEvent(root_watch_dpy, &ev);
        panel_root_event(&ev);
    }
    return TRUE;
}

static GSourceFuncs root_watch_funcs = {
    root_watch_prepare, root_watch_check, root_watch_dispatch, NULL, NULL, NULL
};

/* NOTE: private X connections are intentionally not shared.  The panel
 * holds several (root watcher here, resize watcher in panel.c, tray
 * manager selection in plugins/tray.c, taskbar event pump in
 * plugins/launchtaskbar.c, hotkey grabs in src/x11-hotkey.c, plus
 * per-plugin ones such as kbled/xkb).  Merging them into one Display
 * is not clearly safe: Xlib is not thread-safe without global locking,
 * each owner selects different event masks (merging masks would deliver
 * foreign events to every pump), each owns protocol state tied to its
 * connection (selections, grabs, save-sets), and each destructor closes
 * its own Display (a shared one would couple all lifetimes).  GDK4 also
 * forbids reusing its X connection for raw pumps.  So the private
 * connections remain; sharing would need a multiplexing dispatcher and
 * shared-lifetime design, out of scope for a hardening pass. */
static void root_watch_init(void)
{
    RootWatchSource *src;

    root_watch_dpy = XOpenDisplay(NULL);
    if (root_watch_dpy == NULL)
        return;
    XSelectInput(root_watch_dpy, DefaultRootWindow(root_watch_dpy),
                 PropertyChangeMask | SubstructureNotifyMask);
    XFlush(root_watch_dpy);
    src = (RootWatchSource *) g_source_new(&root_watch_funcs, sizeof(RootWatchSource));
    src->poll_fd.fd = ConnectionNumber(root_watch_dpy);
    src->poll_fd.events = G_IO_IN;
    g_source_add_poll((GSource *) src, &src->poll_fd);
    root_watch_source = (GSource *)src;
    g_source_attach(root_watch_source, NULL);
    g_source_unref(root_watch_source);
}

void x11_root_watch_window(Window w)
{
    if (root_watch_dpy != NULL && w != None)
        XSelectInput(root_watch_dpy, w, StructureNotifyMask);
}

void x11_root_watch_cleanup(void)
{
    if (root_watch_source != NULL)
    {
        g_source_destroy(root_watch_source);
        g_source_unref(root_watch_source);
        root_watch_source = NULL;
    }
    if (root_watch_dpy != NULL)
    {
        XSelectInput(root_watch_dpy, DefaultRootWindow(root_watch_dpy), NoEventMask);
        XCloseDisplay(root_watch_dpy);
        root_watch_dpy = NULL;
    }
}

static void panel_root_event(XEvent *ev)
{
    Atom at;
    Window win;

    ENTER;
    if (ev->type == PropertyNotify || ev->type == ConfigureNotify)
        DBG("win = 0x%x\n", ev->xany.window);
    if (ev->type != PropertyNotify )
    {
        /* private client message from luxepanelctl */
        if( ev->type == ClientMessage && ev->xclient.message_type == a_LUXEPANEL_CMD )
        {
            process_client_msg( (XClientMessageEvent*)ev );
        }
        else if( ev->type == DestroyNotify )
        {
            if (fbev != NULL)
                fb_ev_emit_destroy( fbev, ((XDestroyWindowEvent*)ev)->window );
        }
        return;
    }

    at = ev->xproperty.atom;
    win = ev->xproperty.window;
    if (win == GDK_ROOT_WINDOW())
    {
        if (at == a_NET_CLIENT_LIST)
        {
            if (fbev != NULL)
                fb_ev_emit(fbev, EV_CLIENT_LIST);
        }
        else if (at == a_NET_CURRENT_DESKTOP)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((LuxePanel*)l->data)->priv->curdesk = get_net_current_desktop();
            if (fbev != NULL)
                fb_ev_emit(fbev, EV_CURRENT_DESKTOP);
        }
        else if (at == a_NET_NUMBER_OF_DESKTOPS)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((LuxePanel*)l->data)->priv->desknum = get_net_number_of_desktops();
            if (fbev != NULL)
                fb_ev_emit(fbev, EV_NUMBER_OF_DESKTOPS);
        }
        else if (at == a_NET_DESKTOP_NAMES)
        {
            if (fbev != NULL)
                fb_ev_emit(fbev, EV_DESKTOP_NAMES);
        }
        else if (at == a_NET_ACTIVE_WINDOW)
        {
            if (fbev != NULL)
                fb_ev_emit(fbev, EV_ACTIVE_WINDOW);
        }
        else if (at == a_NET_CLIENT_LIST_STACKING)
        {
            if (fbev != NULL)
                fb_ev_emit(fbev, EV_CLIENT_LIST_STACKING);
        }
        else if (at == a_XROOTPMAP_ID)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                _panel_queue_update_background((LuxePanel*)l->data);
        }
        else
            return;

        return;
    }
    return;
}

/* The same for new plugins type - they will be not unloaded by FmModule */
#define REGISTER_STATIC_MODULE(pc) do { \
    extern LuxePanelPluginInit luxepanel_static_plugin_##pc; \
    luxepanel_register_plugin_type(#pc, &luxepanel_static_plugin_##pc); } while (0)

/* Initialize the static plugins. */
static void init_static_plugins(void)
{
#ifdef STATIC_SEPARATOR
    REGISTER_STATIC_MODULE(separator);
#endif

#ifdef STATIC_LAUNCHTASKBAR
    REGISTER_STATIC_MODULE(launchtaskbar);
#endif

#ifdef STATIC_DCLOCK
    REGISTER_STATIC_MODULE(dclock);
#endif

#ifdef STATIC_WINCMD
    REGISTER_STATIC_MODULE(wincmd);
#endif

#ifdef STATIC_DIRMENU
    REGISTER_STATIC_MODULE(dirmenu);
#endif

#ifdef STATIC_PAGER
    REGISTER_STATIC_MODULE(pager);
#endif

#ifdef STATIC_TRAY
    REGISTER_STATIC_MODULE(tray);
#endif

    REGISTER_STATIC_MODULE(notifications);

#ifndef DISABLE_MENU
#ifdef STATIC_MENU
    REGISTER_STATIC_MODULE(menu);
#endif
#endif
}

static gboolean valid_profile_name(const char *name)
{
    const char *p;
    if (name == NULL || *name == '\0')
        return FALSE;
    for (p = name; *p != '\0'; p++)
        if (!(g_ascii_isalnum(*p) || *p == '-' || *p == '_' || *p == '.'))
            return FALSE;
    return !(g_str_equal(name, ".") || g_str_equal(name, ".."));
}

static void
usage()
{
    g_print(_("LuxePanel %s - lightweight GTK4 panel for the Luxe desktop\n"), version);
    g_print(_("Command line options:\n"));
    g_print(_(" --help      -- print this help and exit\n"));
    g_print(_(" --version   -- print version and exit\n"));
//    g_print(_(" --log <number> -- set log level 0-5. 0 - none 5 - chatty\n"));
//    g_print(_(" --configure -- launch configuration utility\n"));
    g_print(_(" --profile name -- use specified profile\n"));
    g_print("\n");
    g_print(_(" -h  -- same as --help\n"));
    g_print(_(" -p  -- same as --profile\n"));
    g_print(_(" -v  -- same as --version\n"));
 //   g_print(_(" -C  -- same as --configure\n"));
    g_print(_("\nBased on LXPanel. Upstream project: http://lxde.org/\n\n"));
}

/* Lightweight lock related functions - X clipboard hacks */

#define CLIPBOARD_NAME "LUXEPANEL_SELECTION"

/*
 * Lightweight version for checking single instance.
 * Try and own the CLIPBOARD_NAME X selection instead of using file
 * manipulation (GtkClipboard is gone in GTK4; raw Xlib selection).
 *
 * Returns TRUE if successfully retrieved and FALSE otherwise.
 */
static gboolean check_main_lock()
{
    gboolean retval = FALSE;
    Atom atom;
    Display *xdisplay;
    Window owner;

    if (!luxepanel_display_usable(gdk_display_get_default()))
        return FALSE;
    xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    atom = XInternAtom(xdisplay, CLIPBOARD_NAME, False);

    XGrabServer(xdisplay);

    if (XGetSelectionOwner(xdisplay, atom) != None)
        goto out;

    owner = XCreateSimpleWindow(xdisplay, DefaultRootWindow(xdisplay),
                                -100, -100, 1, 1, 0, 0, 0);
    XSetSelectionOwner(xdisplay, atom, owner, CurrentTime);
    if (XGetSelectionOwner(xdisplay, atom) == owner)
    {
        panel_lock_owner = owner;
        retval = TRUE;
    }
    else
    {
        XDestroyWindow(xdisplay, owner);
    }

out:
    XUngrabServer (xdisplay);
    XFlush (xdisplay);

    return retval;
}

static void release_main_lock(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (panel_lock_owner != None && luxepanel_display_usable(display))
    {
        Display *dpy = GDK_DISPLAY_XDISPLAY(display);
        Atom atom = XInternAtom(dpy, CLIPBOARD_NAME, False);
        XSetSelectionOwner(dpy, atom, None, CurrentTime);
        XDestroyWindow(dpy, panel_lock_owner);
        XFlush(dpy);
        panel_lock_owner = None;
    }
}
#undef CLIPBOARD_NAME

static void _start_panels_from_dir(const char *panel_dir)
{
    GDir* dir = g_dir_open( panel_dir, 0, NULL );
    const gchar* name;

    if( ! dir )
    {
        return;
    }

    while((name = g_dir_read_name(dir)) != NULL)
    {
        char* panel_config = g_build_filename( panel_dir, name, NULL );
        if (strchr(panel_config, '~') == NULL)    /* Skip editor backup files in case user has hand edited in this directory */
        {
            LuxePanel* panel = panel_new( panel_config, name );
            if( panel )
                all_panels = g_slist_prepend( all_panels, panel );
        }
        g_free( panel_config );
    }
    g_dir_close( dir );
}

static gboolean start_all_panels( )
{
    char *panel_dir;
    const gchar * const * dir;

    /* try user panels */
    panel_dir = _user_config_file_name("panels", NULL);
    _start_panels_from_dir(panel_dir);
    g_free(panel_dir);
    if (all_panels != NULL)
        return TRUE;
    /* else try XDG fallbacks */
    dir = g_get_system_config_dirs();
    if (dir) while (dir[0])
    {
        panel_dir = _system_config_file_name(dir[0], "panels");
        _start_panels_from_dir(panel_dir);
        g_free(panel_dir);
        if (all_panels != NULL)
            return TRUE;
        dir++;
    }
    /* last try at old fallback for compatibility reasons */
    panel_dir = _old_system_config_file_name("panels");
    _start_panels_from_dir(panel_dir);
    g_free(panel_dir);
    return all_panels != NULL;
}

static void _ensure_user_config_dirs(void)
{
    char *dir = g_build_filename(g_get_user_config_dir(), "luxepanel", cprofile,
                                 "panels", NULL);

    /* make sure the private profile and panels dir exists */
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
}

int main(int argc, char *argv[], char *env[])
{
    int i;
    const char* desktop_name;

    luxe_restore_runtime_dir();
    setlocale(LC_CTYPE, "");

#if !GLIB_CHECK_VERSION(2, 32, 0)
    g_thread_init(NULL);
#endif
/*    gdk_threads_init();
    gdk_threads_enter(); */

    luxepanel_init_gtk();
    /* luxepanel is X11-only: exit cleanly (not SEGV) if GDK opened a
     * non-X11 display, e.g. under a Wayland session (2026 Jerry Howell) */
    {
        GdkDisplay *gdpy = gdk_display_get_default();

        if (gdpy == NULL || !GDK_IS_X11_DISPLAY(gdpy))
        {
            g_critical( "luxepanel requires X11; run with GDK_BACKEND=x11 under Wayland" );
            exit( 1 );
        }
    }

#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    XSetLocaleModifiers("");
    XSetErrorHandler((XErrorHandler) panel_handle_x_error);

    resolve_atoms();

    desktop_name = g_getenv("XDG_CURRENT_DESKTOP");
    /* X-Luxe retains LXDE menu compatibility. Resolve this before the
     * asynchronous menu cache has loaded its custom desktop names. */
    if (desktop_name != NULL)
    {
        gchar **desktops = g_strsplit(desktop_name, ":", -1);
        for (guint n = 0; desktops[n] != NULL; n++)
            if (g_str_equal(desktops[n], "X-Luxe") ||
                g_str_equal(desktops[n], "LXDE"))
                is_in_lxde = TRUE;
        g_strfreev(desktops);
    }

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            exit(0);
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("LuxePanel %s\n", version);
            exit(0);
        } else if (!strcmp(argv[i], "--log")) {
            i++;
            if (i == argc) {
                g_critical( "luxepanel: missing log level");
                usage();
                exit(1);
            } else {
                /* deprecated */
            }
        } else if (!strcmp(argv[i], "--configure") || !strcmp(argv[i], "-C")) {
            config = 1;
        } else if (!strcmp(argv[i], "--profile") || !strcmp(argv[i], "-p")) {
            i++;
            if (i == argc) {
                g_critical( "luxepanel: missing profile name");
                usage();
                exit(1);
            } else {
                if (!valid_profile_name(argv[i]))
                {
                    g_critical("luxepanel: invalid profile name");
                    usage();
                    exit(1);
                }
                cprofile = g_strdup(argv[i]);
            }
        } else {
            printf("luxepanel: unknown option - %s\n", argv[i]);
            usage();
            exit(1);
        }
    }

    /* Check for duplicated luxepanel instances */
    if (!check_main_lock() && !config) {
        printf("There is already an instance of LuxePanel.  Now to exit\n");
        exit(1);
    }

    _ensure_user_config_dirs();

    /* Add our own icons to the search path of icon theme */
    gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()),
                                   PACKAGE_DATA_DIR "/images");

    fbev = fb_ev_new();

    is_restarting = FALSE;

    /* init LibFM */
    /* libfm-gtk is dropped: fm_init() initializes the libfm core
     * (config, icon cache, paths, mime types, ... -- fm_gtk_init()
     * used to call it; a bare fm_config_new() leaves e.g. the icon
     * cache uninitialized and crashes with GLib criticals). */
    fm_init(NULL);

    /* prepare modules data */
    luxepanel_prepare_modules();
    luxepanel_register_plugin_type("space", &_luxepanel_static_plugin_space);
    init_static_plugins();

    load_global_config();

    /* private-connection root watcher replaces the GDK event filter */
    root_watch_init();

    if( G_UNLIKELY( ! start_all_panels() ) )
        g_warning( "Config files are not found.\n" );
/*
 * FIXME: configure??
    if (config)
        configure();
*/
    panel_main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(panel_main_loop);
    g_main_loop_unref(panel_main_loop);
    panel_main_loop = NULL;

    x11_root_watch_cleanup();
    /* Stop the private geometry watcher before the display is torn down. */
    panel_resize_watcher_stop();

    /* destroy all panels */
    g_slist_foreach( all_panels, (GFunc) gtk_window_destroy, NULL );
    g_slist_free( all_panels );
    all_panels = NULL;
    g_free( cfgfile );

    free_global_config();

    luxepanel_unload_modules();

    /* gdk_threads_leave(); */

    g_object_unref(fbev);
    release_main_lock();

    if (!is_restarting)
        return 0;
    if (strchr(argv[0], G_DIR_SEPARATOR))
        execve(argv[0], argv, env);
    else
        execve(g_find_program_in_path(argv[0]), argv, env);
    return 1;
}
