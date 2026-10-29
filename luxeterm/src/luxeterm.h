/**
 *      Copyright (C) 2026 Jerry Howell
 *      Copyright 2008 Fred Chien <cfsghost@gmail.com>
 *      Copyright (c) 2010 LxDE Developers, see the file AUTHORS for details.
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

#ifndef LUXETERM_H
#define LUXETERM_H

#include "setting.h"

/* steal from tilda-0.09.6/src/tilda_terminal.c:36 */
#define DINGUS1 "(((news|telnet|nttp|file|http|ftp|https)://)|(www|ftp)[-A-Za-z0-9]*\\.)[-A-Za-z0-9\\.]+(:[0-9]*)?"
#define DINGUS2 "(((news|telnet|nttp|file|http|ftp|https)://)|(www|ftp)[-A-Za-z0-9]*\\.)[-A-Za-z0-9\\.]+(:[0-9]*)?/[-A-Za-z0-9_\\$\\.\\+\\!\\*\\(\\),;:@&=\\?/~\\#\\%]*[^]'\\.}>\\) ,\\\"]"

/* Top level application context. */
typedef struct _lxtermwindow {
    GPtrArray * windows;            /* Array of pointers to LuxeTerm structures */
    GtkApplication * app;           /* The GtkApplication owning the windows */
} LXTermWindow;

/* Representative of a toplevel window. */
typedef struct _luxeterm {
    LXTermWindow * parent;          /* Back pointer to top level context */
    gint index;                 /* Index of this element in parent->windows */
    GtkWidget * window;             /* Toplevel window */
    GtkWidget * box;                /* Vertical box, child of toplevel window */
    GtkWidget * menu;               /* Hamburger menu button, child of the header bar */
    GtkWidget * notebook;           /* Notebook, child of vertical box */
    GPtrArray * terms;              /* Array of pointers to Term structures */
    gboolean rgba;              /* Always TRUE on GTK4 (windows are RGBA natively) */
    GdkRGBA background;             /* User preference background color */
    GdkRGBA foreground;             /* User preference foreground color */
    gint tab_position;              /* Tab position as an integer value */
    gboolean login_shell;           /* Terminal will spawn login shells */
    gchar **launch_env;             /* Environment of the window's invocation. */
    gdouble scale;                  /* Terminal scale */
    gboolean closed;
    gboolean confirming;
    GtkWindow *confirm_dialog;     /* Weak; dismissed when a child exits. */
} LuxeTerm;

/* Representative of a tab within a toplevel window. */
typedef struct _term {
    LuxeTerm * parent;            /* Back pointer to LuxeTerm */
    gint index;                 /* Index of this element in parent->terms */
    GtkWidget * tab;                /* Toplevel of the tab (a GtkBox holding label + close button) */
    GtkWidget * label;              /* Label of the tab, child of the toplevel */
    gboolean user_specified_label;      /* User did "Name Tab", so we will never overwrite this with the window title */
    GtkWidget * close_button;           /* Close button for the tab, child of the toplevel */
    GtkWidget * box;                /* Horizontal box, child of notebook */
    GtkWidget * vte;                /* VteTerminal, child of horizontal box */
    GtkWidget * scrollbar;          /* Scroll bar, child of horizontal box */
    GPid pid;                                   /* Process ID of the process that has this as its terminal */
    gint references;                            /* Owned by the tab and any pending async callbacks */
    gboolean interactive_shell;                 /* Can compare its PTY foreground group with its shell group. */
    GCancellable *spawn_cancel;                 /* Cancels a spawn when its tab closes */
    gchar * matched_url;
    gulong exit_handler_id;
    gboolean closed;
    GtkWidget *popup;
} Term;

/* Output of luxeterm_process_arguments. */
typedef struct _command_arguments {
    char * executable;              /* Value of argv[0]; points into argument vector */
    gchar * * command;              /* Value of -e, --command; memory allocated by glib */
    int geometry_columns;           /* Value of --geometry */
    int geometry_rows;
    char * title;               /* Value of -t, -T, --title; points into argument vector */
    char * tabs;                /* Value of --tab; points into argument vector */
    char * working_directory;           /* Value of --working-directory; points into argument vector */
    const gchar * const *environment;    /* Borrowed until initialization returns. */
    gboolean login_shell;           /* Terminal will spawn login shells */
    gboolean no_remote;
    gboolean interactive_shell;
    int exit_status;
} CommandArguments;

extern gboolean luxeterm_process_arguments(gint argc, gchar * * argv, CommandArguments * arguments);
extern LuxeTerm * luxeterm_initialize(LXTermWindow * lxtermwin, CommandArguments * arguments);
extern void terminal_settings_apply_to_all(LuxeTerm * terminal);

#endif
