/*
 * Copyright (C) 2006 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006 Jim Huang <jserv.tw@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2016 Andriy Grytsenko <andrej@rep.kiev.ua>
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

/* Remote controller of luxepanel */

#include "luxepanelctl.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

static Display* dpy;

/* Must match CLIPBOARD_NAME in main.c: single-instance X selection. */
#define LUXEPANEL_SELECTION "LUXEPANEL_SELECTION"

static int luxepanel_profile_has_panels(const char *name);

static const char usage[] =
        "\nluxepanelctl - LuxePanel Controller\n"
        "Usage: luxepanelctl [--profile NAME] <command>\n\n"
        "Available commands:\n"
        "menu\t\t\tshow system menu\n"
        "run\t\t\tshow run dialog\n"
        "config\t\t\tshow configuration dialog\n"
        "restart\t\t\trestart luxepanel (respawns it if not running)\n"
        "exit\t\t\texit luxepanel\n"
        "command <plugin> <cmd>\tsend a command to a plugin\n\n"
        "If no --profile is given, restart respawn prefers $LUXEPANEL_PROFILE,\n"
        "then the LXDE profile when it has panels configured.\n";

static int get_cmd( const char* cmd )
{
    if( ! strcmp( cmd, "menu") )
        return LUXEPANEL_CMD_SYS_MENU;
    else if( ! strcmp( cmd, "run") )
        return LUXEPANEL_CMD_RUN;
    else if( ! strcmp( cmd, "config") )
        return LUXEPANEL_CMD_CONFIG;
    else if( ! strcmp( cmd, "restart") )
        return LUXEPANEL_CMD_RESTART;
    else if( ! strcmp( cmd, "exit") )
        return LUXEPANEL_CMD_EXIT;
    else if( ! strcmp( cmd, "command") )
        return LUXEPANEL_CMD_COMMAND;
    return -1;
}

/* format: either "<edge>" or "<num>:<edge>" */
static int valid_profile_name(const char *name)
{
    const unsigned char *p;
    if (name == NULL || *name == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
        return 0;
    for (p = (const unsigned char *)name; *p != '\0'; p++)
        if (!(isalnum(*p) || *p == '-' || *p == '_' || *p == '.'))
            return 0;
    return 1;
}

static int parse_id(const char *arg, int *mon)
{
    char *end;
    long lmon = strtoul(arg, &end, 10);

    if (*end == ':')
        arg = (const char *)end + 1;
    else
        lmon = 0;
    *mon = lmon;
    if (strcmp(arg, "top") == 0)
        return EDGE_TOP;
    if (strcmp(arg, "bottom") == 0)
        return EDGE_BOTTOM;
    if (strcmp(arg, "left") == 0)
        return EDGE_LEFT;
    if (strcmp(arg, "right") == 0)
        return EDGE_RIGHT;
    return EDGE_NONE;
}

int main( int argc, char** argv )
{
    char *display_name = (char *)getenv("DISPLAY");
    XEvent ev;
    Window root;
    Atom cmd_atom;
    Atom sel_atom;
    int cmd;
    int cmd_index = 1;
    const char *profile = NULL;      /* explicit --profile, if given */
    int panel_alive = 0;
    /* int restart; */
    /* target of message, it's XClientMessageEvent::b[1]
     * valid only if XClientMessageEvent::b[0] == LUXEPANEL_CMD_COMMAND */
    uint8_t target;

    /* Optional leading --profile NAME / -p NAME so a respawned panel
     * keeps the right profile (this session uses LXDE). */
    if (argc >= 4 && (strcmp(argv[1], "--profile") == 0 || strcmp(argv[1], "-p") == 0))
    {
        if (!valid_profile_name(argv[2]))
        {
            fprintf(stderr, "luxepanelctl: invalid profile name\n");
            return 1;
        }
        profile = argv[2];
        cmd_index = 3;
    }

    if( argc < cmd_index + 1 )
    {
        printf( usage );
        return 1;
    }

    /*
    if( restart = !strcmp( argv[1], "restart" ) )
        argv[1] = "exit";
    */

    if( ( cmd = get_cmd( argv[cmd_index] ) ) == -1 )
    {
        printf( usage );
        return 1;
    }

    if (cmd == LUXEPANEL_CMD_COMMAND && argc < cmd_index + 3)
    {
        printf( usage );
        return 1;
    }

    dpy = XOpenDisplay(display_name);
    if (dpy == NULL) {
        printf("Cant connect to display: %s\n", display_name);
        exit(1);
    }

    /* Liveness probe: luxepanel owns LUXEPANEL_SELECTION while running
     * (see check_main_lock() in main.c). XSendEvent to the root window
     * succeeds even with no receiver, so without this check "restart"
     * after a crash silently does nothing and exits 0. */
    sel_atom = XInternAtom(dpy, LUXEPANEL_SELECTION, False);
    panel_alive = (XGetSelectionOwner(dpy, sel_atom) != None);

    if (!panel_alive)
    {
        if (cmd == LUXEPANEL_CMD_RESTART)
        {
            /* Dead panel: respawn it directly instead of sending a
             * ClientMessage nobody will receive. */
            const char *spawn_profile = profile;
            char *owned_profile = NULL;
            pid_t pid;

            XCloseDisplay(dpy);
            dpy = NULL;
            if (spawn_profile == NULL)
            {
                spawn_profile = getenv("LUXEPANEL_PROFILE");
                if (spawn_profile != NULL && *spawn_profile == '\0')
                    spawn_profile = NULL;
            }
            if (spawn_profile == NULL && luxepanel_profile_has_panels("LXDE"))
                spawn_profile = "LXDE";
            if (spawn_profile != NULL && !valid_profile_name(spawn_profile))
            {
                fprintf(stderr, "luxepanelctl: invalid respawn profile\n");
                return 1;
            }
            if (spawn_profile != NULL)
                owned_profile = strdup(spawn_profile);

            pid = fork();
            if (pid < 0)
            {
                fprintf(stderr, "luxepanelctl: no running luxepanel and fork failed: %s\n",
                        strerror(errno));
                free(owned_profile);
                return 1;
            }
            if (pid == 0)
            {
                /* Child: detach and exec a fresh panel. The new panel
                 * must not inherit our stdio: a caller capturing our
                 * output would otherwise wait forever for EOF held open
                 * by the long-lived panel process. */
                int nullfd;
                if (setsid() < 0)
                    _exit(127);
                nullfd = open("/dev/null", O_RDWR);
                if (nullfd >= 0)
                {
                    dup2(nullfd, STDIN_FILENO);
                    dup2(nullfd, STDOUT_FILENO);
                    dup2(nullfd, STDERR_FILENO);
                    if (nullfd > STDERR_FILENO)
                        close(nullfd);
                }
                if (owned_profile != NULL)
                {
                    execlp("luxepanel", "luxepanel",
                           "--profile", owned_profile, (char *)NULL);
                    execv("/usr/bin/luxepanel", (char *[]) {
                        (char *)"luxepanel", (char *)"--profile",
                        owned_profile, NULL });
                }
                else
                {
                    execlp("luxepanel", "luxepanel", (char *)NULL);
                    execv("/usr/bin/luxepanel", (char *[]) {
                        (char *)"luxepanel", NULL });
                }
                _exit(127);
            }
            printf("luxepanelctl: no running luxepanel, started new instance%s%s%s\n",
                   owned_profile ? " (profile " : "",
                   owned_profile ? owned_profile : "",
                   owned_profile ? ")" : "");
            free(owned_profile);
            return 0;
        }
        fprintf(stderr,
                "luxepanelctl: no running luxepanel (nothing owns %s); "
                "message not delivered. Use 'luxepanelctl restart' to start it.\n",
                LUXEPANEL_SELECTION);
        XCloseDisplay(dpy);
        return 1;
    }

    root = DefaultRootWindow(dpy);
    cmd_atom = XInternAtom(dpy, "_LUXEPANEL_CMD", False);
    memset(&ev, '\0', sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = root;
    ev.xclient.message_type = cmd_atom;
    ev.xclient.format = 8;

    ev.xclient.data.b[0] = cmd;

    if (cmd == LUXEPANEL_CMD_COMMAND)
    {
        int i = cmd_index + 1;
        if (argc > cmd_index + 3 && strncmp(argv[cmd_index + 1], "--panel=", 8) == 0)
        {
            int monitor;
            int edge = parse_id(argv[cmd_index + 1] + 8, &monitor);
            /* edge: EDGE_NONE ..., monitor: 0 - none, 1...8 - selected */
            target = ((edge & 0x7) << 4) + (monitor & 0xf);
            i++;
        }
        else
            target = (EDGE_NONE << 4) + 0; /* edge: none, monitor: none */
        ev.xclient.data.b[1] = target;
        snprintf(&ev.xclient.data.b[2], 18, "%s\t%s", argv[i], argv[i+1]);
    }

    XSendEvent(dpy, root, False,
               SubstructureRedirectMask|SubstructureNotifyMask, &ev);
    XSync(dpy, False);
    XCloseDisplay(dpy);

    if (cmd == LUXEPANEL_CMD_RESTART)
        printf("luxepanelctl: restart requested (panel is running)\n");
    return 0;
}

/* TRUE if ~/.config/luxepanel/<name>/panels holds at least one real
 * panel file (editor backups ending in ~ are ignored, mirroring
 * _start_panels_from_dir()). Used to pick a sane respawn profile. */
static int luxepanel_profile_has_panels(const char *name)
{
    const char *home = getenv("HOME");
    char path[1024];
    DIR *dir;
    struct dirent *ent;
    int found = 0;

    if (home == NULL || !valid_profile_name(name))
        return 0;
    if (snprintf(path, sizeof path, "%s/.config/luxepanel/%s/panels",
                 home, name) >= (int)sizeof path)
        return 0;
    dir = opendir(path);
    if (dir == NULL)
        return 0;
    while ((ent = readdir(dir)) != NULL)
    {
        if (ent->d_name[0] == '.')
            continue;
        if (strchr(ent->d_name, '~') != NULL)
            continue;
        found = 1;
        break;
    }
    closedir(dir);
    return found;
}

