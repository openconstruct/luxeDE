/*
 * Copyright (C) 2006-2009 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Andrew Lee <ajqlee@debian.org>
 *               2009 Jürgen Hötzel <juergen@archlinux.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2011-2014 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2012-2014 Giuseppe Penone <giuspen@gmail.com>
 *               2013 Vincenzo di Cicco <enzodicicco@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2014 Vladimír Pýcha <vpycha@gmail.com>
 *               2014 Raimar Bühmann <raimar@buehmann.de>
 *               2014 Andy Balaam <axis3x3@users.sf.net>
 *               2015 Balló György <ballogyor@gmail.com>
 *               2015 Rafał Mużyło <galtgendo@gmail.com>
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

/*
 * Started by Giuseppe Penone <giuspen@gmail.com> merging launchbar and taskbar
 * and adding interoperability between them.
*/

/*
 * Taskbar plugin:
 * 2006.09.10 modified by Hong Jen Yee (PCMan) pcman.tw (AT) gmail.com
 * Following features are added:
 * 1. Add XUrgencyHint support. (Flashing task bar buttons, can be disabled)
 * 2. Raise window when files get dragged over taskbar buttons.
 * 3. Add Restore & Maximize menu items to popup menu of task bar buttons.
 */

//#define DEBUG // killall luxepanel && luxepanel --profile Lubuntu &

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/x11/gdkx.h>
#include <glib/gi18n.h>

#include <libfm/fm.h>

#include "misc.h"
#include "ev.h"
#include "plugin.h"
#define __LUXEPANEL_INTERNALS__
#include "private.h"
#include "task-button.h"
#include "launch-button.h"
#include "icon-grid.h"
#ifndef DISABLE_MENU
# include "menu-policy.h"
#endif
#include "gtk-compat.h"

#define PANEL_ICON_SIZE 24 /* see the private.h */

/* CheckButton is not a ToggleButton in GTK4; the .ui uses CheckButtons */
#define TOGGLE_ACTIVE(w)        gtk_check_button_get_active(GTK_CHECK_BUTTON(w))
#define TOGGLE_SET_ACTIVE(w, n) gtk_check_button_set_active(GTK_CHECK_BUTTON(w), n)

/* PanelIconGrid is a plain GtkWidget now; sibling walk replaces
 * gtk_container_get_children/foreach. */
static GList *ltb_grid_children(GtkWidget * grid)
{
    GList * l = NULL;
    GtkWidget * cw;

    for (cw = gtk_widget_get_first_child(grid); cw; cw = gtk_widget_get_next_sibling(cw))
        l = g_list_append(l, cw);
    return l;
}

/* remove a child from an icon grid (grid owns the sunk reference) */
static void ltb_grid_remove(GtkWidget * grid, GtkWidget * child)
{
    panel_icon_grid_remove(PANEL_ICON_GRID(grid), child);
    g_object_unref(child);
}

/* Column definitions for configuration dialogs. */
enum {
    COL_ICON,
    COL_TITLE,
    COL_ICON_NAME,
    COL_BTN,
    N_COLS
};

typedef enum {
    LAUNCHBAR = 0,  /* GtkComboBox is 0-indexed. */
    TASKBAR,
    LAUNCHTASKBAR
} LtbMode;

typedef struct LaunchTaskBarPlugin LaunchTaskBarPlugin;

/* Private context for taskbar plugin. */
struct LaunchTaskBarPlugin {
    /* LAUNCHBAR */
    GtkWidget *lb_icon_grid;         /* Icon grid managing the container */
    GtkWidget     *p_button_add, *p_button_remove, *p_label_menu_app_exec, *p_label_def_app_exec;
    GtkWidget *last_lb_drag_dest;  /* Last launch button near drag on launchbar */
    PanelIconGridDropPosition last_lb_drag_pos;
    gboolean in_drag;              /* TRUE if some launcher is being dragged */
    FmPath *dragged_launcher;      /* Path of launcher currently being dragged */
    GCancellable *drop_cancel;     /* Invalidated before plugin teardown. */
    int drag_start_x, drag_start_y; /* Drag start coordinates */
    /* TASKBAR */
    GtkWidget * tb_icon_grid;      /* Manager for taskbar buttons */
    int number_of_desktops;        /* Number of desktops, from NET_WM_NUMBER_OF_DESKTOPS */
    int current_desktop;           /* Current desktop, from NET_WM_CURRENT_DESKTOP */
    guint dnd_delay_timer;         /* Timer for drag and drop delay */
    gboolean dnd_task_moving;      /* User is currently moving a task button */
    GtkWidget *dnd_delay_task;     /* Task button to raise on drag timeout */
    int icon_size;                 /* Size of task icons */
    gboolean grouped_tasks;        /* User preference: windows from same task are grouped onto a single button */
    TaskShowFlags flags;        /* User preferences flags */
    int task_width_max;            /* Maximum width of a taskbar button in horizontal orientation */
    int spacing;                   /* Spacing between taskbar buttons */
    guint flash_timeout;        /* Timer for urgency notification */
    gboolean flash_state;       /* One-bit counter to flash taskbar */
    /* COMMON */
#ifndef DISABLE_MENU
    FmPath * path;              /* Current menu item path */
    GtkWidget       *p_menuitem_lock_tbp;
    GtkWidget       *p_menuitem_unlock_tbp;
    GtkWidget       *p_menuitem_new_instance;
    GtkWidget       *p_menuitem_separator;
#endif
    GtkWidget * plugin;                 /* Back pointer to Plugin */
    LuxePanel * panel;                    /* Back pointer to panel */
    config_setting_t * settings;
    Display         *event_dpy;         /* private X connection (GDK4 has no filters) */
    GSource         *event_source;      /* event pump for the private connection */
    GtkWidget       *config_dlg;        /* Configuration dialog */
    GtkNotebook     *p_notebook;
    GtkWidget       *p_notebook_page_launch;
    GtkWidget       *p_notebook_page_task;
    GKeyFile        *p_key_file_special_cases;
    int              mode;
    gboolean         lb_built;
    gboolean         tb_built;
    gboolean         fixed_mode;        /* if mode cannot be changed */
    int              w, h;              /* override GtkBox bug with allocation */
};

#define DRAG_ACTIVE_DELAY    1000
#define TASK_WIDTH_MAX       200
#define ALL_WORKSPACES       -1
#define ICON_ONLY_EXTRA      6      /* Amount needed to have button lay out symmetrically */
#define ICON_BUTTON_TRIM 4      /* Amount needed to have button remain on panel */

static void launchtaskbar_destructor(gpointer user_data);

static LaunchButton *launchbar_exec_bin_exists(LaunchTaskBarPlugin *lb, FmPath *path);
static void launchbar_remove_button(LaunchTaskBarPlugin *ltbp, LaunchButton *btn);

static void taskbar_property_notify_event(LaunchTaskBarPlugin *tb, XEvent *ev);
static void taskbar_configure_notify_event(LaunchTaskBarPlugin * tb, XConfigureEvent * ev);

/* Private X connection pump (gdk_window_add_filter is gone in GDK4). */
typedef struct {
    GSource source;
    GPollFD poll_fd;
    LaunchTaskBarPlugin * tb;
} LtbSource;

static gboolean ltb_source_prepare(GSource *source, gint *timeout)
{
    LtbSource *src = (LtbSource *) source;
    *timeout = -1;
    return src != NULL && src->tb != NULL && src->tb->event_dpy != NULL &&
           XPending(src->tb->event_dpy) > 0;
}

static gboolean ltb_source_check(GSource * source)
{
    LtbSource * src = (LtbSource *) source;

    if (src == NULL || src->tb == NULL ||
        !(src->poll_fd.revents & G_IO_IN) || src->tb->event_dpy == NULL)
        return FALSE;
    return XPending(src->tb->event_dpy) > 0;
}

static gboolean ltb_source_dispatch(GSource * source, GSourceFunc cb, gpointer data)
{
    LtbSource * src = (LtbSource *) source;
    LaunchTaskBarPlugin *tb = src != NULL ? src->tb : NULL;
    Display *dpy = tb != NULL ? tb->event_dpy : NULL;
    XEvent ev;

    while (dpy != NULL && XPending(dpy) > 0)
    {
        XNextEvent(dpy, &ev);
        if (tb->mode == LAUNCHBAR)
            continue;
        if (ev.type == PropertyNotify)
            taskbar_property_notify_event(tb, &ev);
        else if (ev.type == ConfigureNotify)
            taskbar_configure_notify_event(tb, &ev.xconfigure);
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs ltb_source_funcs = {
    ltb_source_prepare,
    ltb_source_check,
    ltb_source_dispatch,
    NULL
};

static void taskbar_redraw(LaunchTaskBarPlugin * tb);
static GdkContentProvider * on_launchbar_drag_prepare(GtkDragSource * source,
                                                      double x, double y,
                                                      LaunchTaskBarPlugin * lb);
static void on_launchbar_drag_begin(GtkDragSource * source, GdkDrag * drag,
                                    LaunchTaskBarPlugin * lb);
static void on_launchbar_drag_end(GtkDragSource * source, GdkDrag * drag,
                                  gboolean delete_data, LaunchTaskBarPlugin * lb);
static gboolean on_launchbar_drop(GtkDropTargetAsync * dest, GdkDrop * drop,
                                  double x, double y, LaunchTaskBarPlugin * lb);
static GdkDragAction on_launchbar_drop_motion(GtkDropTargetAsync * dest, GdkDrop * drop,
                                              double x, double y, LaunchTaskBarPlugin * lb);
static void on_launchbar_drop_leave(GtkDropTargetAsync * dest, GdkDrop * drop,
                                    LaunchTaskBarPlugin * lb);
#if !defined(DISABLE_MENU)
static void ltb_menu_item_action(GSimpleAction * action, GVariant * parameter,
                                 gpointer user_data);
#endif
static void taskbar_net_client_list(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void taskbar_net_current_desktop(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void taskbar_net_number_of_desktops(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void taskbar_net_active_window(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void taskbar_apply_configuration(LaunchTaskBarPlugin * ltbp);
static void taskbar_add_task_button(LaunchTaskBarPlugin * tb, TaskButton * task);
static void taskbar_apply_configuration(LaunchTaskBarPlugin * ltbp);
static void taskbar_add_task_button(LaunchTaskBarPlugin * tb, TaskButton * task);

#define taskbar_reset_menu(tb) if (tb->tb_built) task_button_reset_menu(tb->tb_icon_grid)

#ifndef DISABLE_MENU
static char *task_get_cmdline(Window win, LaunchTaskBarPlugin *ltbp)
{
    GPid pid = get_net_wm_pid(win);
    char proc_path[64];
    gchar *cmdline = NULL;
    gchar *p_char = NULL;

    snprintf(proc_path, sizeof(proc_path),
             G_DIR_SEPARATOR_S "proc" G_DIR_SEPARATOR_S "%lu" G_DIR_SEPARATOR_S "cmdline",
             (gulong)pid);
    g_file_get_contents(proc_path, &cmdline, NULL, NULL);
    if (cmdline)
    {
        p_char = strchr(cmdline, '\n');
        if(p_char != NULL) *p_char = '\0';
        p_char = strrchr(cmdline, G_DIR_SEPARATOR);
        if (p_char != NULL) p_char++;
        else p_char = cmdline;
        if(strcmp(p_char, "python") == 0)
        {
            snprintf(proc_path, sizeof(proc_path),
                     G_DIR_SEPARATOR_S "proc" G_DIR_SEPARATOR_S "%lu" G_DIR_SEPARATOR_S "comm",
                     (gulong)pid);
            g_free(cmdline);
            cmdline = NULL;
            g_file_get_contents(proc_path, &cmdline, NULL, NULL);
            if (cmdline)
            {
                p_char = strchr(cmdline, '\n');
                if(p_char != NULL) *p_char = '\0';
            }
        }
        else
        {
            p_char = g_key_file_get_string(ltbp->p_key_file_special_cases,
                                           "special_cases", p_char, NULL);
            if (p_char != NULL) /* found this key */
            {
                g_free(cmdline);
                cmdline = p_char;
            }
        }
    }
    return cmdline;
}

static FmPath *f_find_menu_launchbutton_recursive(Window win, LaunchTaskBarPlugin *ltbp)
{
    MenuCache *mc;
    guint32 flags;
    GSList *apps, *l;
    size_t len;
    char *exec_bin = task_get_cmdline(win, ltbp);
    const char *exec, *short_exec;
    char *str_path;
    FmPath *path = NULL;

    if (exec_bin == NULL || *exec_bin == '\0')
    {
        g_free(exec_bin);
        return NULL;
    }

    /* FIXME: cache it in Task object */
    mc = panel_menu_cache_new(&flags);
    if (mc == NULL)
    {
        g_free(exec_bin);
        return NULL;
    }
    /* FIXME: if menu plugin wasn't loaded yet we'll get NULL list here */
    apps = menu_cache_list_all_apps(mc);
    if (apps == NULL)
    {
        menu_cache_unref(mc);
        g_free(exec_bin);
        return NULL;
    }
    short_exec = strrchr(exec_bin, '/');
    if (short_exec != NULL)
        short_exec++;
    else
        short_exec = exec_bin;
    len = strlen(short_exec);
    /* the same executable may be used in numerous applications so wild guess
       estimation check for desktop id equal to short_exec+".desktop" first */
    for (l = apps; l; l = l->next)
    {
        exec = menu_cache_item_get_id(MENU_CACHE_ITEM(l->data));
        /* we don't check flags here because user always can manually
           start any app that isn't visible in the desktop menu */
        if (exec != NULL && strncmp(exec, short_exec, len) == 0 && exec[len] == '.')
            break;
    }
    /* if not found then check for non-absolute exec name in application
       since it usually is expanded by application starting functions */
    if (l == NULL) for (l = apps; l; l = l->next)
    {
        exec = menu_cache_app_get_exec(MENU_CACHE_APP(l->data));
        if (exec && exec[0] != '/' && strncmp(exec, short_exec, len) == 0 &&
            (exec[len] == ' ' || exec[len] == 0))
            break;
    }
    /* well, not matched, let try full path, we assume here if application
       starts executable by full path then process cannot have short name */
    if (l == NULL && exec_bin[0] == '/')
    {
        len = strlen(exec_bin);
        for (l = apps; l; l = l->next)
        {
            exec = menu_cache_app_get_exec(MENU_CACHE_APP(l->data));
            if (exec && exec[0] == '/' && strncmp(exec, exec_bin, len) == 0 &&
                (exec[len] == ' ' || exec[len] == 0))
                break;
        }
    }
    if (l)
    {
        str_path = menu_cache_dir_make_path(MENU_CACHE_DIR(l->data));
        if (str_path != NULL && strlen(str_path) > 13)
            path = fm_path_new_relative(fm_path_get_apps_menu(), str_path + 13);
        g_free(str_path);
    }
    g_slist_foreach(apps, (GFunc)menu_cache_item_unref, NULL);
    g_slist_free(apps);
    menu_cache_unref(mc);
    g_debug("f_find_menu_launchbutton_recursive: search '%s' found=%d", exec_bin, (path != NULL));
    g_free(exec_bin);
    return path;
}
#endif

static void launchbar_remove_bootstrap(LaunchTaskBarPlugin *ltbp)
{
    GList *btns = ltb_grid_children(ltbp->lb_icon_grid);
    GList *l;

    for (l = btns; l; l = l->next)
        if (launch_button_get_settings(l->data) == NULL)
        {
            ltb_grid_remove(ltbp->lb_icon_grid, l->data);
            g_debug("launchtaskbar: removed bootstrap button %p", l->data);
        }
    g_list_free(btns);
}

static void launchbar_check_bootstrap(LaunchTaskBarPlugin *lb)
{
    LaunchButton *btn;

    if (panel_icon_grid_get_n_children(PANEL_ICON_GRID(lb->lb_icon_grid)) > 0)
        return;
    btn = launch_button_new(lb->panel, lb->plugin, NULL, NULL);
    panel_icon_grid_add(PANEL_ICON_GRID(lb->lb_icon_grid), GTK_WIDGET(btn));
    g_debug("launchtaskbar: added bootstrap button %p", btn);
}

static void launchbar_remove_launcher(LaunchTaskBarPlugin *ltbp, FmPath *path)
{
    LaunchButton *btn = launchbar_exec_bin_exists(ltbp, path);

    if (btn != NULL)
        launchbar_remove_button(ltbp, btn);
}

/* custom MIME type for launcher moves between panels */
#define LAUNCHER_MIME "application/x-luxepanel-launcher"

/*** GTK4 drag & drop (FmDndDest/gtk_drag_* are gone) ***/

/* insert a launcher at position i in the launchbar grid */
static void launchbar_add_launcher_at(LaunchTaskBarPlugin * lb, FmPath * path, int i)
{
    config_setting_t * s;
    LaunchButton * btn;
    char * path_str;

    s = config_group_add_subgroup(lb->settings, "Button");
    if (fm_path_equal(fm_path_get_scheme_path(path), fm_path_get_apps_menu()))
        config_group_set_string(s, "id", fm_path_get_basename(path));
    else
    {
        path_str = fm_path_to_str(path);
        config_group_set_string(s, "id", path_str);
        g_free(path_str);
    }
    btn = launch_button_new(lb->panel, lb->plugin, path, s);
    if (btn)
    {
        panel_icon_grid_add(PANEL_ICON_GRID(lb->lb_icon_grid), GTK_WIDGET(btn));
        panel_icon_grid_reorder_child(PANEL_ICON_GRID(lb->lb_icon_grid),
                                      GTK_WIDGET(btn), i);
        config_setting_move_elem(s, config_setting_get_parent(s), i);
        luxepanel_config_save(lb->panel);
        launchbar_remove_bootstrap(lb);
    }
    else
        config_setting_destroy(s);
}

/* drag source: called by GtkDragSource to obtain drag content */
static GdkContentProvider * on_launchbar_drag_prepare(GtkDragSource * source,
                                                      double x, double y,
                                                      LaunchTaskBarPlugin * lb)
{
    PanelIconGrid * ig = PANEL_ICON_GRID(lb->lb_icon_grid);
    GtkWidget * btn;
    PanelIconGridDropPosition pos;
    FmFileInfo * fi;
    FmPath * path;
    char * path_str;
    GBytes * bytes;
    GdkContentProvider * prov;

    if (lb->dragged_launcher)
        fm_path_unref(lb->dragged_launcher);
    lb->dragged_launcher = NULL;
    if (!panel_icon_grid_get_dest_at_pos(ig, (int) x, (int) y, &btn, &pos) ||
        !PANEL_IS_LAUNCH_BUTTON(btn) ||
        launch_button_get_settings((LaunchButton *) btn) == NULL)
        return NULL;
    fi = launch_button_get_file_info((LaunchButton *) btn);
    if (fi == NULL)
        return NULL;
    path = fm_path_ref(fm_file_info_get_path(fi));
    lb->dragged_launcher = path;
    path_str = fm_path_to_str(path);
    bytes = g_bytes_new_take(path_str, strlen(path_str));
    prov = gdk_content_provider_new_for_bytes(LAUNCHER_MIME, bytes);
    g_bytes_unref(bytes);
    return prov;
}

/* drag source: set the drag icon from the launcher */
static void on_launchbar_drag_begin(GtkDragSource * source, GdkDrag * drag,
                                    LaunchTaskBarPlugin * lb)
{
    LaunchButton * btn = launchbar_exec_bin_exists(lb, lb->dragged_launcher);

    if (btn)
    {
        FmIcon * icon = launch_button_get_icon(btn);

        if (icon)
        {
            GIcon * gicon = fm_icon_get_gicon(icon);
            if (G_IS_THEMED_ICON(gicon))
            {
                const char * const * names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
                GtkIconTheme * theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
                GtkIconPaintable * pt = gtk_icon_theme_lookup_icon(theme,
                        (const char *) names, names[1] ? (const char **) names : NULL,
                        32, 1, GTK_TEXT_DIR_NONE, 0);
                if (pt != NULL)
                {
                    gtk_drag_source_set_icon(source, GDK_PAINTABLE(pt), 0, 0);
                    g_object_unref(pt);
                }
            }
        }
    }
}

static void on_launchbar_drag_end(GtkDragSource * source, GdkDrag * drag,
                                  gboolean delete_data, LaunchTaskBarPlugin * lb)
{
    if (delete_data && lb->dragged_launcher != NULL)
    {
        launchbar_remove_launcher(lb, lb->dragged_launcher);
        if (lb->mode == LAUNCHBAR)
        {
            /* destroy empty plugin if it is in LAUNCHBAR mode */
            if (panel_icon_grid_get_n_children(PANEL_ICON_GRID(lb->lb_icon_grid)) == 0)
                luxepanel_remove_plugin(lb->panel, lb->plugin);
        }
        else
            launchbar_check_bootstrap(lb);
    }
    if (lb->dragged_launcher)
        fm_path_unref(lb->dragged_launcher);
    lb->dragged_launcher = NULL;
}

/*** drop destination ***/

typedef struct {
    LaunchTaskBarPlugin * lb;
    int x, y;
    GdkDrop * drop;
    GCancellable * cancel;
    GInputStream *stream;
    GByteArray *bytes;
    gboolean is_launcher;
} LtbDropCtx;

static void ltb_drop_finish(LtbDropCtx *ctx, FmPath *path)
{
    if (path != NULL)
    {
        GtkWidget * btn;
        PanelIconGridDropPosition pos;
        int i = 0;

        if (panel_icon_grid_get_dest_at_pos(PANEL_ICON_GRID(ctx->lb->lb_icon_grid),
                                            ctx->x, ctx->y, &btn, &pos))
        {
            i = panel_icon_grid_get_child_position(
                    PANEL_ICON_GRID(ctx->lb->lb_icon_grid), btn);
            if (i >= 0)
            {
                if (pos == PANEL_ICON_GRID_DROP_LEFT_AFTER ||
                    pos == PANEL_ICON_GRID_DROP_RIGHT_AFTER)
                    i++;
            }
            else
                i = 0;
        }
        launchbar_add_launcher_at(ctx->lb, path, i);
        fm_path_unref(path);
    }
    gdk_drop_finish(ctx->drop, (path != NULL) ? GDK_ACTION_COPY : (GdkDragAction) 0);
    g_clear_object(&ctx->stream);
    g_byte_array_unref(ctx->bytes);
    g_object_unref(ctx->drop);
    g_object_unref(ctx->cancel);
    g_free(ctx);
}

static gboolean ltb_drop_append_bytes(LtbDropCtx *ctx, const guint8 *data, gsize size)
{
    if (size > 256 * 1024 - ctx->bytes->len)
        return FALSE;
    if (size != 0)
        g_byte_array_append(ctx->bytes, data, size);
    return TRUE;
}

static FmPath *ltb_drop_parse_payload(const guint8 *data, gsize size,
                                      gboolean is_launcher)
{
    FmPath *path = NULL;
    if (size == 0 || size > 256 * 1024 || memchr(data, '\0', size) != NULL)
        return NULL;
    char *payload = g_strndup((const char *)data, size);
    if (is_launcher) {
        path = fm_path_new_for_str(payload);
    } else {
        char **uris = g_uri_list_extract_uris(payload);
        if (uris != NULL && uris[0] != NULL &&
            g_uri_is_valid(uris[0], G_URI_FLAGS_NONE, NULL))
            path = fm_path_new_for_uri(uris[0]);
        g_strfreev(uris);
    }
    g_free(payload);
    return path;
}

static void on_launchbar_drop_chunk(GObject *obj, GAsyncResult *res,
                                    gpointer user_data)
{
    LtbDropCtx *ctx = user_data;
    GError *error = NULL;
    GBytes *chunk = g_input_stream_read_bytes_finish(G_INPUT_STREAM(obj), res, &error);
    if (chunk == NULL) {
        g_clear_error(&error);
        ltb_drop_finish(ctx, NULL);
        return;
    }
    if (g_cancellable_is_cancelled(ctx->cancel)) {
        g_bytes_unref(chunk);
        ltb_drop_finish(ctx, NULL);
        return;
    }
    gsize size = 0;
    const guint8 *data = g_bytes_get_data(chunk, &size);
    if (!ltb_drop_append_bytes(ctx, data, size)) {
        g_bytes_unref(chunk);
        ltb_drop_finish(ctx, NULL);
        return;
    }
    if (size != 0) {
        g_bytes_unref(chunk);
        g_input_stream_read_bytes_async(ctx->stream,
            MIN(8192, 256 * 1024 + 1 - (gint)ctx->bytes->len),
            G_PRIORITY_DEFAULT, ctx->cancel, on_launchbar_drop_chunk, ctx);
        return;
    }
    g_bytes_unref(chunk);
    FmPath *path = ltb_drop_parse_payload(ctx->bytes->data,
                                          ctx->bytes->len, ctx->is_launcher);
    ltb_drop_finish(ctx, path);
}

static void on_launchbar_drop_stream(GObject *obj, GAsyncResult *res,
                                     gpointer user_data)
{
    LtbDropCtx *ctx = user_data;
    GError *error = NULL;
    const char *mime = NULL;
    ctx->stream = gdk_drop_read_finish(ctx->drop, res, &mime, &error);
    if (ctx->stream == NULL) {
        g_clear_error(&error);
        ltb_drop_finish(ctx, NULL);
        return;
    }
    ctx->is_launcher = g_strcmp0(mime, LAUNCHER_MIME) == 0;
    g_input_stream_read_bytes_async(ctx->stream, 8192, G_PRIORITY_DEFAULT,
        ctx->cancel, on_launchbar_drop_chunk, ctx);
}

/* drop destination: "drop" signal */
static gboolean on_launchbar_drop(GtkDropTargetAsync * dest, GdkDrop * drop,
                                  double x, double y, LaunchTaskBarPlugin * lb)
{
    LtbDropCtx * ctx = g_new0(LtbDropCtx, 1);

    panel_icon_grid_set_drag_dest(PANEL_ICON_GRID(lb->lb_icon_grid), NULL, 0);
    ctx->lb = lb;
    ctx->x = (int) x;
    ctx->y = (int) y;
    ctx->drop = g_object_ref(drop);
    if (!lb->drop_cancel) lb->drop_cancel=g_cancellable_new();
    ctx->cancel=g_object_ref(lb->drop_cancel);
    ctx->bytes = g_byte_array_new();
    const char *mime_types[] = { LAUNCHER_MIME, "text/uri-list", NULL };
    gdk_drop_read_async(drop, mime_types, G_PRIORITY_DEFAULT, ctx->cancel,
                        on_launchbar_drop_stream, ctx);
    return TRUE;
}

/* drop destination: "motion" signal */
static GdkDragAction on_launchbar_drop_motion(GtkDropTargetAsync * dest, GdkDrop * drop,
                                              double x, double y, LaunchTaskBarPlugin * lb)
{
    GtkWidget * btn;
    PanelIconGridDropPosition pos;
    GdkContentFormats * formats = gdk_drop_get_formats(drop);
    GdkDragAction actions = gdk_drop_get_actions(drop);
    GdkDragAction action = 0;

    if (!panel_icon_grid_get_dest_at_pos(PANEL_ICON_GRID(lb->lb_icon_grid),
                                         (int) x, (int) y, &btn, &pos) || !btn)
    {
        panel_icon_grid_set_drag_dest(PANEL_ICON_GRID(lb->lb_icon_grid), NULL, 0);
        return 0;
    }
    lb->last_lb_drag_dest = btn;
    lb->last_lb_drag_pos = pos;
    panel_icon_grid_set_drag_dest(PANEL_ICON_GRID(lb->lb_icon_grid), btn, pos);
    if (gdk_content_formats_contain_mime_type(formats, LAUNCHER_MIME))
        action = actions & GDK_ACTION_MOVE;
    else if (gdk_content_formats_contain_mime_type(formats, "text/uri-list"))
        action = actions & GDK_ACTION_COPY;
    return action;
}

/* drop destination: "leave" signal */
static void on_launchbar_drop_leave(GtkDropTargetAsync * dest, GdkDrop * drop,
                                    LaunchTaskBarPlugin * lb)
{
    panel_icon_grid_set_drag_dest(PANEL_ICON_GRID(lb->lb_icon_grid), NULL, 0);
    lb->last_lb_drag_dest = NULL;
}

static LaunchButton *launchbar_exec_bin_exists(LaunchTaskBarPlugin *lb, FmPath *path)
{
    LaunchButton *ret_val = NULL;
    GList *children, *l;
    FmFileInfo *fi;

    if (!path)
        return NULL;
    children = ltb_grid_children(lb->lb_icon_grid);
    for (l = children; l != NULL; l = l->next)
    {
        fi = launch_button_get_file_info(PANEL_LAUNCH_BUTTON(l->data));
        if (fi && fm_path_equal(path, fm_file_info_get_path(fi)))
        {
            ret_val = l->data;
            break;
        }
    }
    g_list_free(children);
    return ret_val;
}

/* Read the configuration file entry for a launchtaskbar button and create it. */
static gboolean launchbutton_constructor(LaunchTaskBarPlugin * lb, config_setting_t * s)
{
    LaunchButton *btn = NULL;
    const char *str;
    char *str_path = NULL;
    FmPath *path;

    /* Read parameters from the configuration file and validate. */
    if (!config_setting_lookup_string(s, "id", &str) || str[0] == '\0')
        return FALSE;

    /* Build the structures and return. */
    if (str[0] == '~')
    {
        str_path = expand_tilda(str);
        path = fm_path_new_for_path(str_path);
    }
    else if (strchr(str, '/') != NULL)
    {
        path = fm_path_new_for_str(str);
        /* FIXME: check if str contains invalid path */
    }
    else
    {
        str_path = g_strdup_printf("search://menu://applications/?recursive=1&show_hidden=1&name=%s", str);
        path = fm_path_new_for_uri(str_path);
    }
    btn = launch_button_new(lb->panel, lb->plugin, path, s);
    g_free(str_path);
    fm_path_unref(path);
    if (btn)
        panel_icon_grid_add(PANEL_ICON_GRID(lb->lb_icon_grid), GTK_WIDGET(btn));
    return (btn != NULL);
}

/* prototype of this is app_info_create_from_commandline() in libfm */
static gboolean _launchbutton_create_id(LaunchTaskBarPlugin * lb, config_setting_t * s)
{
    const char *icon = NULL, *name, *exec, *path = NULL;
    char *dirname, *filename;
    int fd, terminal = 0;
    gboolean ret = FALSE;

    if (!config_setting_lookup_string(s, "action", &exec) || exec[0] == '\0')
        return FALSE;
    if (!config_setting_lookup_string(s, "tooltip", &name) || name[0] == '\0')
        name = "Launcher"; /* placeholder, XDG requires a non-empty name */
    config_setting_lookup_string(s, "image", &icon);
    config_setting_lookup_string(s, "path", &path);
    config_setting_lookup_int(s, "terminal", &terminal);

    dirname = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    if (g_mkdir_with_parents(dirname, 0700) == 0)
    {
        filename = g_strdup_printf("%s/luxepanel-launcher-XXXXXX.desktop", dirname);
        fd = g_mkstemp (filename);
        if (fd != -1)
        {
            GString* content = g_string_sized_new(256);

            g_string_printf(content,
                "[" G_KEY_FILE_DESKTOP_GROUP "]\n"
                G_KEY_FILE_DESKTOP_KEY_TYPE "=" G_KEY_FILE_DESKTOP_TYPE_APPLICATION "\n"
                G_KEY_FILE_DESKTOP_KEY_NAME "=%s\n"
                G_KEY_FILE_DESKTOP_KEY_EXEC "=%s\n"
                G_KEY_FILE_DESKTOP_KEY_CATEGORIES "=X-LuxePanel;\n",
                name, exec);
            if (icon)
                g_string_append_printf(content, "Icon=%s\n", icon);
            if (terminal)
                g_string_append(content, G_KEY_FILE_DESKTOP_KEY_TERMINAL "=true\n");
            if (path && path[0] == '/')
                g_string_append_printf(content, "Path=%s\n", path);
            close(fd);
            ret = g_file_set_contents(filename, content->str, content->len, NULL);
            if (ret) {
                config_group_set_string(s, "id", filename);
                /* FIXME: is it reasonable to remove obsolete keys too? */
                luxepanel_config_save(lb->panel);
            } else
                g_unlink(filename);
            g_string_free(content, TRUE);
        }
        g_free(filename);
    }
    g_free(dirname);
    if (ret) /* we created it, let use it */
        return launchbutton_constructor(lb, s);
    return FALSE;
}

static void launchtaskbar_constructor_add_default_special_case(LaunchTaskBarPlugin *ltbp, const gchar *tk_exec, const gchar *mb_exec)
{
    g_key_file_set_value(ltbp->p_key_file_special_cases, "special_cases", tk_exec, mb_exec);
}

static void launchtaskbar_constructor_launch(LaunchTaskBarPlugin *ltbp)
{
    config_setting_t *settings;
    guint i = 0;

    if(!ltbp->lb_built)
    {
        ltbp->lb_built = TRUE;
        /* Read parameters from the configuration file. */
        settings = config_setting_get_member(ltbp->settings, "");
        if (settings && config_setting_is_list(settings))
        {
            config_setting_t *s;

            while ((s = config_setting_get_elem(settings, i)) != NULL)
            {
                if (strcmp(config_setting_get_name(s), "Button") != 0)
                {
                    g_warning("launchtaskbar: illegal token %s\n", config_setting_get_name(s));
                    config_setting_destroy(s);
                }
                else if (!launchbutton_constructor(ltbp, s) &&
                         /* try to create desktop id from old-style manual setup */
                         !_launchbutton_create_id(ltbp, s))
                {
                    g_warning( "launchtaskbar: can't init button\n");
                    /* FIXME: show failed id to the user instead */
                    config_setting_destroy(s);
                }
                else /* success, accept the setting */
                    i++;
            }
        }
        if (i == 0)
        {
            /* build bootstrap button */
            launchbar_check_bootstrap(ltbp);
        }
        /* Drag and drop support. */
        {
            GtkDragSource * src = gtk_drag_source_new();
            GtkDropTargetAsync * dst;
            GdkContentFormatsBuilder * builder;
            GdkContentFormats * formats;

            g_signal_connect(src, "prepare", G_CALLBACK(on_launchbar_drag_prepare), ltbp);
            g_signal_connect(src, "drag-begin", G_CALLBACK(on_launchbar_drag_begin), ltbp);
            g_signal_connect(src, "drag-end", G_CALLBACK(on_launchbar_drag_end), ltbp);
            gtk_widget_add_controller(ltbp->lb_icon_grid, GTK_EVENT_CONTROLLER(src));

            builder = gdk_content_formats_builder_new();
            gdk_content_formats_builder_add_mime_type(builder, LAUNCHER_MIME);
            gdk_content_formats_builder_add_mime_type(builder, "text/uri-list");
            formats = gdk_content_formats_builder_free_to_formats(builder);
            dst = gtk_drop_target_async_new(formats, GDK_ACTION_MOVE | GDK_ACTION_COPY);
            g_signal_connect(dst, "drop", G_CALLBACK(on_launchbar_drop), ltbp);
            g_signal_connect(dst, "drag-motion", G_CALLBACK(on_launchbar_drop_motion), ltbp);
            g_signal_connect(dst, "drag-leave", G_CALLBACK(on_launchbar_drop_leave), ltbp);
            gtk_widget_add_controller(ltbp->lb_icon_grid, GTK_EVENT_CONTROLLER(dst));
        }
    }
    gtk_widget_set_visible(ltbp->lb_icon_grid, TRUE);
}

static gboolean flash_window_timeout(gpointer user_data)
{
    LaunchTaskBarPlugin *tb = user_data;

    if (tb == NULL)
        return G_SOURCE_REMOVE;
    tb->flash_state = !tb->flash_state;
    {
        GList * children = ltb_grid_children(tb->tb_icon_grid);
        GList * l;
        for (l = children; l; l = l->next)
            task_button_set_flash_state(PANEL_TASK_BUTTON(l->data), tb->flash_state);
        g_list_free(children);
    }
    return TRUE;
}

static void on_gtk_cursor_blink_time_changed(GObject *gsettings, GParamSpec *pspec,
                                             LaunchTaskBarPlugin *tb)
{
    gint interval;

    if (tb->flash_timeout == 0) /* nothing to do? */
        return;
    g_source_remove(tb->flash_timeout);
    g_object_get(gtk_settings_get_default(), "gtk-cursor-blink-time",
                 &interval, NULL);
    interval = MAX(interval, 200);
    tb->flash_timeout = g_timeout_add(interval / 2, flash_window_timeout, tb);
}

/* Set an urgency timer on a task. */
static void set_timer_on_task(LaunchTaskBarPlugin *tb)
{
    gint interval;

    if (tb->flash_timeout != 0)
        return;
    g_object_get(gtk_settings_get_default(), "gtk-cursor-blink-time", &interval, NULL);
    g_signal_connect(gtk_settings_get_default(),
                     "notify::gtk-cursor-blink-time",
                     G_CALLBACK(on_gtk_cursor_blink_time_changed), tb);
    interval = MAX(interval, 200);
    tb->flash_timeout = g_timeout_add(interval / 2, flash_window_timeout, tb);
}

static void reset_timer_on_task(LaunchTaskBarPlugin *tb)
{
    if (tb->flash_timeout == 0)
        return;
    g_source_remove(tb->flash_timeout);
    tb->flash_timeout = 0;
    g_signal_handlers_disconnect_by_func(gtk_settings_get_default(),
                                         on_gtk_cursor_blink_time_changed, tb);
}

static void launchtaskbar_constructor_task(LaunchTaskBarPlugin *ltbp)
{
    if(!ltbp->tb_built)
    {
        config_setting_t *s = ltbp->settings;
        gint tmp_int;

        ltbp->tb_built = TRUE;

        /* Parse configuration now */
        if (config_setting_lookup_int(s, "tooltips", &tmp_int))
            ltbp->flags.tooltips = (tmp_int != 0);
        if (config_setting_lookup_int(s, "IconsOnly", &tmp_int))
            ltbp->flags.icons_only = (tmp_int != 0);
        if (config_setting_lookup_int(s, "ShowAllDesks", &tmp_int))
            ltbp->flags.show_all_desks = (tmp_int != 0);
        if (config_setting_lookup_int(s, "SameMonitorOnly", &tmp_int))
            ltbp->flags.same_monitor_only = (tmp_int != 0);
        if (config_setting_lookup_int(s, "DisableUpscale", &tmp_int))
            ltbp->flags.disable_taskbar_upscale = (tmp_int != 0);
        config_setting_lookup_int(s, "MaxTaskWidth", &ltbp->task_width_max);
        config_setting_lookup_int(s, "spacing", &ltbp->spacing);
        if (config_setting_lookup_int(s, "UseMouseWheel", &tmp_int))
            ltbp->flags.use_mouse_wheel = (tmp_int != 0);
        if (config_setting_lookup_int(s, "UseUrgencyHint", &tmp_int))
            ltbp->flags.use_urgency_hint = (tmp_int != 0);
        if (config_setting_lookup_int(s, "FlatButton", &tmp_int))
            ltbp->flags.flat_button = (tmp_int != 0);
        if (config_setting_lookup_int(s, "GroupedTasks", &tmp_int))
            ltbp->grouped_tasks = (tmp_int != 0);
        if (config_setting_lookup_int(s, "UseSmallerIcons", &tmp_int))
            ltbp->flags.use_smaller_icons = (tmp_int != 0);

        /* Make container for task buttons as a child of top level widget. */
        ltbp->tb_icon_grid = panel_icon_grid_new(panel_get_orientation(ltbp->panel),
                                                 ((ltbp->flags.icons_only) ? ltbp->icon_size + ICON_ONLY_EXTRA : ltbp->task_width_max),
                                                 ((ltbp->flags.icons_only) ? ltbp->icon_size + ICON_ONLY_EXTRA : ltbp->icon_size + ICON_BUTTON_TRIM),
                                                 ltbp->spacing, 0,
                                                 panel_get_height(ltbp->panel));
        panel_icon_grid_set_constrain_width(PANEL_ICON_GRID(ltbp->tb_icon_grid), TRUE);
        gtk_box_append(GTK_BOX(ltbp->plugin), ltbp->tb_icon_grid);
        if (panel_get_orientation(ltbp->panel) == GTK_ORIENTATION_HORIZONTAL)
            gtk_widget_set_hexpand(ltbp->tb_icon_grid, TRUE);
        else
            gtk_widget_set_vexpand(ltbp->tb_icon_grid, TRUE);

        /* Private X connection + pump: GDK4 has no event filters. */
        ltbp->event_dpy = XOpenDisplay(NULL);
        if (ltbp->event_dpy != NULL)
        {
            LtbSource * src;
            task_button_set_event_display(ltbp->event_dpy);
            src = (LtbSource *) g_source_new(&ltb_source_funcs, sizeof(LtbSource));
            src->tb = ltbp;
            src->poll_fd.fd = ConnectionNumber(ltbp->event_dpy);
            src->poll_fd.events = G_IO_IN;
            g_source_add_poll((GSource *) src, &src->poll_fd);
            g_source_attach((GSource *) src, NULL);
            ltbp->event_source = (GSource *) src;
        }

        /* Connect signals to receive root window events and initialize root window properties. */
        ltbp->number_of_desktops = get_net_number_of_desktops();
        ltbp->current_desktop = get_net_current_desktop();
        g_signal_connect(G_OBJECT(fbev), "current-desktop", G_CALLBACK(taskbar_net_current_desktop), (gpointer) ltbp);
        g_signal_connect(G_OBJECT(fbev), "active-window", G_CALLBACK(taskbar_net_active_window), (gpointer) ltbp);
        g_signal_connect(G_OBJECT(fbev), "number-of-desktops", G_CALLBACK(taskbar_net_number_of_desktops), (gpointer) ltbp);
        g_signal_connect(G_OBJECT(fbev), "client-list", G_CALLBACK(taskbar_net_client_list), (gpointer) ltbp);

        /* GdkScreen::window-manager-changed is gone; every modern WM
         * supports _NET_ACTIVE_WINDOW, so enable it unconditionally. */
        ltbp->flags.use_net_active = TRUE;

#if !defined(DISABLE_MENU)
        /* actions for the task context menu task-to-launcher section */
        {
            static GActionEntry ltb_entries[] = {
                { "add-launcher",     ltb_menu_item_action, NULL, NULL, NULL },
                { "remove-launcher",  ltb_menu_item_action, NULL, NULL, NULL },
                { "new-instance",     ltb_menu_item_action, NULL, NULL, NULL },
            };
            GSimpleActionGroup * group = g_simple_action_group_new();

            g_action_map_add_action_entries(G_ACTION_MAP(group), ltb_entries,
                                            G_N_ELEMENTS(ltb_entries), ltbp);
            g_object_set_data_full(G_OBJECT(ltbp->tb_icon_grid), "ltb-actions",
                                   group, g_object_unref);
            gtk_widget_insert_action_group(ltbp->tb_icon_grid, "ltb",
                                           G_ACTION_GROUP(group));
            /* both disabled until a menu target is set */
            g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "add-launcher")), FALSE);
            g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "remove-launcher")), FALSE);
            g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "new-instance")), FALSE);
        }
#endif
        /* Start blinking timeout if configured */
        if (ltbp->flags.use_urgency_hint)
            set_timer_on_task(ltbp);

        /* Fetch the client list and redraw the taskbar.  Then determine what window has focus. */
        taskbar_net_client_list(NULL, ltbp);
        taskbar_net_active_window(NULL, ltbp);
    }
    gtk_widget_set_visible(ltbp->tb_icon_grid, TRUE);
}

/* Plugin constructor. */
static GtkWidget *_launchtaskbar_constructor(LuxePanel *panel, config_setting_t *settings,
                                             LtbMode mode)
{
    GtkWidget *p;
    LaunchTaskBarPlugin *ltbp;
    int height;

    /* Allocate plugin context and set into Plugin private data pointer. */
    ltbp = g_new0(LaunchTaskBarPlugin, 1);
    ltbp->panel = panel;
    ltbp->settings = settings;
    ltbp->mode = mode;

    /* Initialize to defaults. */
    ltbp->icon_size         = panel_get_icon_size(panel);
    ltbp->flags.tooltips    = TRUE;
    ltbp->flags.icons_only  = FALSE;
    ltbp->flags.show_all_desks = TRUE;
    ltbp->task_width_max    = TASK_WIDTH_MAX;
    ltbp->spacing           = 1;
    ltbp->flags.use_mouse_wheel = TRUE;
    ltbp->flags.use_urgency_hint = TRUE;
    ltbp->flags.use_smaller_icons = TRUE;
    ltbp->grouped_tasks     = FALSE;
    ltbp->fixed_mode        = (mode == LAUNCHBAR) || (mode == TASKBAR);

    /* Special cases key file */
    ltbp->p_key_file_special_cases = g_key_file_new();
    gchar *special_cases_filepath = g_build_filename(g_get_user_config_dir(),
                                                     "luxepanel", "launchtaskbar.cfg", NULL);
    if (!g_key_file_load_from_file(ltbp->p_key_file_special_cases,
                                   special_cases_filepath,
                                   G_KEY_FILE_KEEP_COMMENTS, NULL))
    {
        launchtaskbar_constructor_add_default_special_case(ltbp, "synaptic", "synaptic-pkexec");
        launchtaskbar_constructor_add_default_special_case(ltbp, "soffice.bin", "libreoffice");
        launchtaskbar_constructor_add_default_special_case(ltbp, "x-terminal-emulator", "luxeterm");
        gchar *key_file_data = g_key_file_to_data(ltbp->p_key_file_special_cases, NULL, NULL);
        g_file_set_contents(special_cases_filepath, key_file_data, -1, NULL);
        g_free(key_file_data);
    }
    g_free(special_cases_filepath);

    /* Allocate top level widget and set into Plugin widget pointer. */
    ltbp->plugin = p = panel_box_new(panel, FALSE, 5);
    luxepanel_plugin_set_data(p, ltbp, launchtaskbar_destructor);
    /* Allocate an icon grid manager to manage the container. */
    height = panel_get_height(panel);
    ltbp->lb_icon_grid = panel_icon_grid_new(panel_get_orientation(panel),
                                             ltbp->icon_size, ltbp->icon_size,
                                             3, 0, height);
    gtk_box_append(GTK_BOX(p), ltbp->lb_icon_grid);
    gtk_widget_set_hexpand(ltbp->lb_icon_grid, TRUE);

    /* Read parameters from the configuration file. */
    config_setting_lookup_int(settings, "LaunchTaskBarMode", &ltbp->mode);
    switch (ltbp->mode) {
    case LAUNCHBAR:
        launchtaskbar_constructor_launch(ltbp);
        gtk_widget_set_name(p, "launchbar");
        break;
    default:
        ltbp->mode = LAUNCHTASKBAR; /* reset invalid value */
    case LAUNCHTASKBAR:
        launchtaskbar_constructor_launch(ltbp);
        gtk_widget_set_name(p, "launchtaskbar");
    case TASKBAR:
        launchtaskbar_constructor_task(ltbp);
        if (ltbp->mode == TASKBAR)
            gtk_widget_set_name(p, "taskbar");
    }

    return p;
}

static GtkWidget *launchtaskbar_constructor(LuxePanel *panel, config_setting_t *settings)
{
    return _launchtaskbar_constructor(panel, settings, LAUNCHTASKBAR);
}

static void launchtaskbar_destructor_launch(LaunchTaskBarPlugin *ltbp)
{
    /* do not disconnect handler on child widget - it is already destroyed */
    if (ltbp->dragged_launcher)
        fm_path_unref(ltbp->dragged_launcher);
}

static void launchtaskbar_destructor_task(LaunchTaskBarPlugin *ltbp)
{
    /* Remove the private-connection event pump. */
    if (ltbp->event_source != NULL)
    {
        g_source_destroy(ltbp->event_source);
        g_source_unref(ltbp->event_source);
        ltbp->event_source = NULL;
    }
    task_button_unset_event_display(ltbp->event_dpy);
    if (ltbp->event_dpy != NULL)
    {
        XCloseDisplay(ltbp->event_dpy);
        ltbp->event_dpy = NULL;
    }

    /* Remove root window signal handlers. */
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_current_desktop, ltbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_active_window, ltbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_number_of_desktops, ltbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_client_list, ltbp);

    /* Stop blinking timeout */
    reset_timer_on_task(ltbp);
#ifndef DISABLE_MENU
    if (ltbp->path)
        fm_path_unref(ltbp->path);
#endif

    /* DND delay handler */
    if (ltbp->dnd_delay_timer != 0)
    {
        g_source_remove(ltbp->dnd_delay_timer);
        ltbp->dnd_delay_timer = 0;
    }
    if (ltbp->dnd_delay_task)
        g_object_remove_weak_pointer(G_OBJECT(ltbp->dnd_delay_task), (gpointer *)&ltbp->dnd_delay_task);
}

/* Plugin destructor. */
static void launchtaskbar_destructor(gpointer user_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)user_data;

    /* A pending GDK callback can outlive the plugin allocation. */
    if(ltbp->drop_cancel)g_cancellable_cancel(ltbp->drop_cancel);
    g_clear_object(&ltbp->drop_cancel);

    // TASKBAR
    if(ltbp->tb_built) launchtaskbar_destructor_task(ltbp);

    // LAUNCHBAR
    if(ltbp->lb_built) launchtaskbar_destructor_launch(ltbp);

    // LAUNCHTASKBAR

    /* Deallocate all memory. */
    if (ltbp->p_key_file_special_cases != NULL)
        g_key_file_free(ltbp->p_key_file_special_cases);
    g_free(ltbp);
}

/* pixbuf for an FmIcon (fm_pixbuf_from_icon is libfm-gtk) */
static GdkPixbuf *ltb_pixbuf_from_fm_icon(FmIcon * icon, gint size)
{
    GIcon * gicon;
    GdkPixbuf * pix = NULL;

    if (icon == NULL)
        return NULL;
    gicon = fm_icon_get_gicon(icon);
    if (G_IS_THEMED_ICON(gicon))
    {
        const char * const * names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
        GtkIconTheme * theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
        GtkIconPaintable * pt = gtk_icon_theme_lookup_icon(theme,
                (const char *) names, names[1] ? (const char **) names : NULL,
                size, 1, GTK_TEXT_DIR_NONE, 0);
        if (pt != NULL)
        {
            pix = luxepanel_pixbuf_from_paintable(GDK_PAINTABLE(pt), size);
            g_object_unref(pt);
        }
    }
    return pix;
}

/* libfm-gtk's FmAppMenuView replacement: flat list of menu-cache apps */
enum {
    LTB_AMV_COL_ICON,
    LTB_AMV_COL_NAME,
    LTB_AMV_COL_PATH,
    LTB_AMV_N_COLS
};

static GtkWidget *ltb_app_menu_view_new(void)
{
    GtkListStore * store;
    GtkWidget * view;
    GtkTreeViewColumn * col;
    GtkCellRenderer * render;
    MenuCache * mc;
    guint32 flags;
    GSList * apps, * l;

    store = gtk_list_store_new(LTB_AMV_N_COLS, GDK_TYPE_PIXBUF,
                               G_TYPE_STRING, G_TYPE_POINTER);
    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), FALSE);
    col = gtk_tree_view_column_new();
    render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_add_attribute(col, render, "pixbuf", LTB_AMV_COL_ICON);
    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, render, TRUE);
    gtk_tree_view_column_add_attribute(col, render, "text", LTB_AMV_COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
    g_object_unref(store);

    mc = panel_menu_cache_new(&flags);
    apps = mc != NULL ? menu_cache_list_all_apps(mc) : NULL;
    for (l = apps; l; l = l->next)
    {
        const char * name = menu_cache_item_get_name(MENU_CACHE_ITEM(l->data));
        const char * icon = menu_cache_item_get_icon(MENU_CACHE_ITEM(l->data));
        char *str_path = menu_cache_dir_make_path(MENU_CACHE_DIR(l->data));
        FmPath *path = str_path != NULL && strlen(str_path) > 13
            ? fm_path_new_relative(fm_path_get_apps_menu(), str_path + 13) : NULL;
        GdkPixbuf *pix = luxepanel_load_icon(icon ? icon : "application-x-executable",
                                            16, 16, FALSE);
        GtkTreeIter it;

        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, LTB_AMV_COL_ICON, pix,
                           LTB_AMV_COL_NAME, name, LTB_AMV_COL_PATH, path, -1);
        if (pix)
            g_object_unref(pix);
        if (path != NULL)
            fm_path_unref(path);
        g_free(str_path);
        menu_cache_item_unref(l->data);
    }
    g_slist_free(apps);
    if (mc != NULL)
        menu_cache_unref(mc);
    return view;
}

static FmPath *ltb_app_menu_view_dup_selected_path(GtkTreeView * view)
{
    GtkTreeSelection * sel = gtk_tree_view_get_selection(view);
    GtkTreeModel * model;
    GtkTreeIter it;
    FmPath * path = NULL;

    if (gtk_tree_selection_get_selected(sel, &model, &it))
        gtk_tree_model_get(model, &it, LTB_AMV_COL_PATH, &path, -1);
    return path;
}

static char *ltb_app_menu_view_dup_selected_name(GtkTreeView * view)
{
    GtkTreeSelection * sel = gtk_tree_view_get_selection(view);
    GtkTreeModel * model;
    GtkTreeIter it;
    char * name = NULL;

    if (gtk_tree_selection_get_selected(sel, &model, &it))
        gtk_tree_model_get(model, &it, LTB_AMV_COL_NAME, &name, -1);
    return name;
}

static void _launchbar_configure_add(GtkTreeView *menu_view, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
    FmPath * sel_path = ltb_app_menu_view_dup_selected_path(menu_view);
    LaunchButton * btn;

    if (sel_path != NULL &&
        (btn = launch_button_new(ltbp->panel, ltbp->plugin, sel_path, NULL)) != NULL &&
        launch_button_wait_load(btn))
    {
        GtkListStore * list = GTK_LIST_STORE(gtk_tree_view_get_model(defined_view));
        GtkTreeIter it;
        GdkPixbuf* pix;
        char *path;
        config_setting_t *settings;
        panel_icon_grid_add(PANEL_ICON_GRID(ltbp->lb_icon_grid), GTK_WIDGET(btn));
        gtk_list_store_append(list, &it);
        pix = ltb_pixbuf_from_fm_icon(launch_button_get_icon(btn), PANEL_ICON_SIZE);
        gtk_list_store_set(list, &it,
            COL_ICON, pix,
            COL_TITLE, launch_button_get_disp_name(btn),
            COL_BTN, btn,
            -1);
        if (pix)
            g_object_unref(pix);
        settings = config_group_add_subgroup(ltbp->settings, "Button");
        if (fm_path_equal(fm_path_get_scheme_path(sel_path), fm_path_get_apps_menu()))
            config_group_set_string(settings, "id", fm_path_get_basename(sel_path));
        else
        {
            path = fm_path_to_str(sel_path);
            /* g_debug("*** path '%s'",path); */
            config_group_set_string(settings, "id", path);
            g_free(path);
        }
        launch_button_set_settings(btn, settings);
        luxepanel_config_save(ltbp->panel);
        fm_path_unref(sel_path);
        launchbar_remove_bootstrap(ltbp);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Add" button. */
static void launchbar_configure_add_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "menu_view"));

    _launchbar_configure_add(menu_view, ltbp);
}

static void  launchbar_remove_button(LaunchTaskBarPlugin *ltbp, LaunchButton *btn)
{
    config_setting_destroy(launch_button_get_settings(btn));
    luxepanel_config_save(ltbp->panel);
    ltb_grid_remove(ltbp->lb_icon_grid, GTK_WIDGET(btn));
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Remove" button. */
static void launchbar_configure_remove_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton * btn;
        gtk_tree_model_get(list, &it, COL_BTN, &btn, -1);

        /* We have found a selected button.
         * Remove it from the icon grid, the data structure, and the view. */
        gtk_list_store_remove(GTK_LIST_STORE(list), &it);
        gtk_widget_set_visible(ltbp->p_label_def_app_exec, FALSE);

        launchbar_remove_button(ltbp, btn);
        launchbar_check_bootstrap(ltbp);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Move Up" button. */
static void launchbar_configure_move_up_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton *btn;
        gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_BTN, &btn, -1);
        GtkTreePath * path = gtk_tree_model_get_path(GTK_TREE_MODEL(list), &it);
        if ((gtk_tree_path_get_indices(path)[0] > 0)
        && (gtk_tree_path_prev(path)))
        {
            GtkTreeIter it2;
            if (gtk_tree_model_get_iter(list, &it2, path))
            {
                /* We have found a selected button that can be moved.
                 * Reorder it in the icon grid, the data structure, and the view. */
                int i = gtk_tree_path_get_indices(path)[0];
                config_setting_t *settings = launch_button_get_settings(btn);
                gtk_list_store_move_before(GTK_LIST_STORE(list), &it, &it2);
                panel_icon_grid_reorder_child(PANEL_ICON_GRID(ltbp->lb_icon_grid),
                                              GTK_WIDGET(btn), i);
                config_setting_move_elem(settings,
                                         config_setting_get_parent(settings), i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Move Down" button. */
static void launchbar_configure_move_down_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton *btn;
        gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_BTN, &btn, -1);
        GtkTreePath * path = gtk_tree_model_get_path(GTK_TREE_MODEL(list), &it);
        int n = gtk_tree_model_iter_n_children(list, NULL);
        if (gtk_tree_path_get_indices(path)[0] < (n - 1))
        {
            gtk_tree_path_next(path);
            GtkTreeIter it2;
            if (gtk_tree_model_get_iter( list, &it2, path))
            {
                /* We have found a selected button that can be moved.
                 * Reorder it in the icon grid, the data structure, and the view. */
                int i = gtk_tree_path_get_indices(path)[0];
                config_setting_t *settings = launch_button_get_settings(btn);
                gtk_list_store_move_after(GTK_LIST_STORE(list), &it, &it2);
                panel_icon_grid_reorder_child(PANEL_ICON_GRID(ltbp->lb_icon_grid),
                                              GTK_WIDGET(btn), i);
                config_setting_move_elem(settings,
                                         config_setting_get_parent(settings), i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Initialize the list of existing launchtaskbar buttons when the configuration dialog is shown. */
static void launchbar_configure_initialize_list(LaunchTaskBarPlugin *ltbp, GtkWidget * dlg, GtkTreeView * view)
{
    /* Set the selection mode. */
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view), GTK_SELECTION_BROWSE);

    /* Define a column. */
    GtkTreeViewColumn* col = gtk_tree_view_get_column(view, 0);

    /* Establish the pixbuf column cell renderer. */
    GtkCellRenderer * render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_set_attributes(col, render, "pixbuf", COL_ICON, NULL);

    /* Establish the text column cell renderer. */
    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, render, TRUE);
    gtk_tree_view_column_add_attribute(col, render, "text", COL_TITLE);

    /* Establish the column data types. */
    GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(view));

    /* Initialize from defined launchtaskbar buttons. */
    GList *children = ltb_grid_children(ltbp->lb_icon_grid);
    GList *l;
    for (l = children; l != NULL; l = l->next)
    {
        LaunchButton * btn = (LaunchButton *) l->data;
        GdkPixbuf * pix;
        GtkTreeIter it;
        if (launch_button_get_settings(btn) == NULL) /* bootstrap button */
            continue;
        gtk_list_store_append(list, &it);
        pix = ltb_pixbuf_from_fm_icon(launch_button_get_icon(btn), PANEL_ICON_SIZE);
        gtk_list_store_set(list, &it,
                           COL_ICON, pix,
                           COL_TITLE, launch_button_get_disp_name(btn),
                           COL_BTN, btn,
                           -1);
        if (pix)
            g_object_unref(pix);
    }
    g_list_free(children);
    g_object_set_data(G_OBJECT(dlg), "defined_view", view);
}

static void  plugin_set_expand_status(LaunchTaskBarPlugin *ltbp, gboolean expand_new)
{
    if (panel_get_orientation(ltbp->panel) == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_set_hexpand(ltbp->plugin, expand_new);
    else
        gtk_widget_set_vexpand(ltbp->plugin, expand_new);
}

static void set_config_visibility(LaunchTaskBarPlugin *ltbp)
{
    switch (ltbp->mode) {
    default:
    case LAUNCHTASKBAR:
        gtk_widget_set_visible(ltbp->p_notebook_page_launch, TRUE);
        gtk_widget_set_visible(ltbp->p_notebook_page_task, TRUE);
        gtk_notebook_set_show_tabs(ltbp->p_notebook, TRUE);
        break;
    case TASKBAR:
        gtk_widget_set_visible(ltbp->p_notebook_page_launch, FALSE);
        gtk_widget_set_visible(ltbp->p_notebook_page_task, TRUE);
        gtk_notebook_set_show_tabs(ltbp->p_notebook, FALSE);
        break;
    case LAUNCHBAR:
        gtk_widget_set_visible(ltbp->p_notebook_page_launch, TRUE);
        gtk_widget_set_visible(ltbp->p_notebook_page_task, FALSE);
        gtk_notebook_set_show_tabs(ltbp->p_notebook, FALSE);
    }
}

static void on_combobox_mode_changed(GtkComboBox *p_combobox, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = p_data;
    int new_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(p_combobox));

    if (new_mode < 0 || new_mode == ltbp->mode) /* no change was made */
        return;

    ltbp->mode = new_mode;

    set_config_visibility(ltbp);

    switch (ltbp->mode) {
    case LAUNCHBAR:
        if (ltbp->tb_icon_grid)
            gtk_widget_set_visible(ltbp->tb_icon_grid, FALSE);
        launchtaskbar_constructor_launch(ltbp);
        plugin_set_expand_status(ltbp, FALSE);
        gtk_widget_set_name(ltbp->plugin, "launchbar");
        break;
    case TASKBAR:
        gtk_widget_set_visible(ltbp->lb_icon_grid, FALSE);
        launchtaskbar_constructor_task(ltbp);
        plugin_set_expand_status(ltbp, TRUE);
        gtk_widget_set_name(ltbp->plugin, "taskbar");
        break;
    default:
        ltbp->mode = LAUNCHTASKBAR;
    case LAUNCHTASKBAR:
        launchtaskbar_constructor_launch(ltbp);
        launchtaskbar_constructor_task(ltbp);
        plugin_set_expand_status(ltbp, TRUE);
        gtk_widget_set_name(ltbp->plugin, "launchtaskbar");
        break;
    }

    config_group_set_int(ltbp->settings, "LaunchTaskBarMode", ltbp->mode);
}

static void on_checkbutton_show_tooltips_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.tooltips = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\nltbp->flags.tooltips upd\n");
    config_group_set_int(ltbp->settings, "tooltips", ltbp->flags.tooltips);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_icons_only_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.icons_only = TOGGLE_ACTIVE(p_togglebutton);
    //g_print("\ntb->flags.icons_only upd\n");
    config_group_set_int(ltbp->settings, "IconsOnly", ltbp->flags.icons_only);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_flat_buttons_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.flat_button = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->flags.flat_button upd\n");
    config_group_set_int(ltbp->settings, "FlatButton", ltbp->flags.flat_button);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_show_all_desks_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.show_all_desks = TOGGLE_ACTIVE(p_togglebutton);
    //g_print("\ntb->flags.show_all_desks upd\n");
    config_group_set_int(ltbp->settings, "ShowAllDesks", ltbp->flags.show_all_desks);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_same_monitor_only_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.same_monitor_only = TOGGLE_ACTIVE(p_togglebutton);
    //g_print("\ntb->flags.same_monitor_only upd\n");
    config_group_set_int(ltbp->settings, "SameMonitorOnly", ltbp->flags.same_monitor_only);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_disable_taskbar_upscale_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.disable_taskbar_upscale = TOGGLE_ACTIVE(p_togglebutton);
    //g_print("\ntb->flags.disable_taskbar_upscale upd\n");
    config_group_set_int(ltbp->settings, "DisableUpscale", ltbp->flags.disable_taskbar_upscale);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_use_smaller_icons_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.use_smaller_icons = TOGGLE_ACTIVE(p_togglebutton);
    config_group_set_int(ltbp->settings, "UseSmallerIcons", ltbp->flags.use_smaller_icons);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_mouse_wheel_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.use_mouse_wheel = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->flags.use_mouse_wheel upd\n");
    config_group_set_int(ltbp->settings, "UseMouseWheel", ltbp->flags.use_mouse_wheel);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_urgency_hint_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flags.use_urgency_hint = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->flags.use_urgency_hint upd\n");
    config_group_set_int(ltbp->settings, "UseUrgencyHint", ltbp->flags.use_urgency_hint);
    taskbar_apply_configuration(ltbp);
    /* Start/stop blinking timeout if configured */
    if (ltbp->flags.use_urgency_hint)
        set_timer_on_task(ltbp);
    else
        reset_timer_on_task(ltbp);
}

static void on_checkbutton_grouped_tasks_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->grouped_tasks = TOGGLE_ACTIVE(p_togglebutton);
    //g_print("\ntb->grouped_tasks upd\n");
    config_group_set_int(ltbp->settings, "GroupedTasks", ltbp->grouped_tasks);
    taskbar_apply_configuration(ltbp);
    if (ltbp->grouped_tasks)
    {
        gboolean changed = FALSE;
        GList *children, *this, *l;
        int i = 0;

        children = ltb_grid_children(ltbp->tb_icon_grid);
        /* merge buttons with the same class into first of that class */
        while ((this = g_list_nth(children, i++)))
        {
            for (l = this->next; l; l = l->next)
                if (task_button_merge(this->data, l->data))
                    changed = TRUE;
            if (changed)
            {
                /* some button was consumed, need to reload buttons list */
                g_list_free(children);
                children = ltb_grid_children(ltbp->tb_icon_grid);
                changed = FALSE;
            }
        }
        g_list_free(children);
    }
    else
    {
        TaskButton *old_btn, *new_btn;
        GList *children, *this;
        int i;

        children = ltb_grid_children(ltbp->tb_icon_grid);
        /* split each button starting from last one */
        for (this = g_list_last(children); this; this = this->prev)
        {
            old_btn = this->data;
            i = panel_icon_grid_get_child_position(PANEL_ICON_GRID(ltbp->tb_icon_grid),
                                                   GTK_WIDGET(old_btn));
            while ((new_btn = task_button_split(old_btn)))
            {
                /* insert rest before old_btn */
                taskbar_add_task_button(ltbp, new_btn);
                panel_icon_grid_reorder_child(PANEL_ICON_GRID(ltbp->tb_icon_grid),
                                              GTK_WIDGET(new_btn), i);
                /* continue split with the rest */
                old_btn = new_btn;
            }
        }
        g_list_free(children);
    }
}

static void on_spinbutton_max_width_value_changed(GtkSpinButton *p_spinbutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->task_width_max = gtk_spin_button_get_value(p_spinbutton);
    //g_print("\ntb->task_width_max upd\n");
    config_group_set_int(ltbp->settings, "MaxTaskWidth", ltbp->task_width_max);
    taskbar_apply_configuration(ltbp);
}

static void on_spinbutton_spacing_value_changed(GtkSpinButton *p_spinbutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->spacing = gtk_spin_button_get_value(p_spinbutton);
    //g_print("\ntb->spacing upd\n");
    config_group_set_int(ltbp->settings, "spacing", ltbp->spacing);
    taskbar_apply_configuration(ltbp);
}

static void on_defined_view_double_click(GtkGestureClick * gesture, gint n_press,
                                         gdouble x, gdouble y, LaunchTaskBarPlugin * lb)
{
    if (n_press == 2 && gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == 1)
        g_signal_emit_by_name(lb->p_button_remove, "clicked");
}

static void on_defined_view_cursor_changed(GtkTreeView *p_treeview, gpointer p_data)
{
    gboolean  label_set = FALSE;
    LaunchTaskBarPlugin *lb = (LaunchTaskBarPlugin *)p_data;
    GtkTreeIter  tree_iter_sel;
    GtkTreeModel* p_treemodel = gtk_tree_view_get_model(p_treeview);
    GtkTreeSelection *p_treeselection = gtk_tree_view_get_selection(p_treeview);
    if(gtk_tree_selection_get_selected(p_treeselection,
                                       (GtkTreeModel **)(&p_treemodel),
                                       &tree_iter_sel))
    {
        LaunchButton * p_btn;
        FmFileInfo *fi;
        gtk_tree_model_get(p_treemodel, &tree_iter_sel, COL_BTN, &p_btn, -1);
        if ((p_btn != NULL) && ((fi = launch_button_get_file_info(p_btn)) != NULL))
        {
            GString *p_gstring = g_string_new("");
            g_string_printf(p_gstring, "<i>%s</i>", fm_file_info_get_disp_name(fi));
            gtk_label_set_markup(GTK_LABEL(lb->p_label_def_app_exec), p_gstring->str);
            g_string_free(p_gstring, TRUE/*free also gstring->str*/);
            label_set = TRUE;
        }
    }
    gtk_widget_set_visible(lb->p_label_def_app_exec, label_set);
    gtk_widget_set_sensitive(lb->p_button_remove, label_set);
}

static void on_menu_view_cursor_changed(GtkTreeView *p_treeview, gpointer p_data)
{
    gboolean  label_set = FALSE;
    LaunchTaskBarPlugin *lb = (LaunchTaskBarPlugin *)p_data;
    char *desc = ltb_app_menu_view_dup_selected_name(p_treeview);

    if (desc)
    {
        GString *p_gstring = g_string_new("");
        g_string_printf(p_gstring, "<i>%s</i>", desc);
        gtk_label_set_markup(GTK_LABEL(lb->p_label_menu_app_exec), p_gstring->str);
        g_string_free(p_gstring, TRUE/*free also gstring->str*/);
        label_set = TRUE;
    }
    g_free(desc);
    gtk_widget_set_visible(lb->p_label_menu_app_exec, label_set);
    gtk_widget_set_sensitive(lb->p_button_add, label_set);
}

static void on_menu_view_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                                       GtkTreeViewColumn *column,
                                       LaunchTaskBarPlugin *ltbp)
{
    _launchbar_configure_add(tree_view, ltbp);
}

/* FIXME: add support for global hotkeys for launchers */

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *launchtaskbar_configure(LuxePanel *panel, GtkWidget *p)
{
    LaunchTaskBarPlugin *ltbp = luxepanel_plugin_get_data(p);

    {
        GtkWidget *dlg, *btn, *defined_view, *menu_view, *menu_view_window;
        GtkBuilder *builder = gtk_builder_new();
        GObject *object;

        gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/launchtaskbar.ui", NULL);
        dlg = (GtkWidget *)gtk_builder_get_object(builder, "dlg");
        panel_apply_icon(GTK_WINDOW(dlg));

        defined_view = (GtkWidget *)gtk_builder_get_object(builder, "defined_view");
        menu_view_window = (GtkWidget*)gtk_builder_get_object(builder, "menu_view_window");
        if (menu_view_window == NULL) /* fallback for old glade file */
        {
            menu_view_window = (GtkWidget*)gtk_builder_get_object(builder, "scroll2");
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(menu_view_window), NULL);
        }
        menu_view = ltb_app_menu_view_new();
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(menu_view_window), menu_view);
        ltbp->p_label_def_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_def_app_exec");
        ltbp->p_label_menu_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_menu_app_exec");

        /* Connect signals. */
        ltbp->p_button_add = (GtkWidget *)gtk_builder_get_object(builder, "button_add");
        g_signal_connect(ltbp->p_button_add, "clicked", G_CALLBACK(launchbar_configure_add_button), ltbp);

        ltbp->p_button_remove = (GtkWidget *)gtk_builder_get_object(builder, "button_remove");
        g_signal_connect(ltbp->p_button_remove, "clicked", G_CALLBACK(launchbar_configure_remove_button), ltbp);

        btn = (GtkWidget *)gtk_builder_get_object(builder, "button_up");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_up_button), ltbp);

        btn = (GtkWidget *)gtk_builder_get_object(builder, "button_down");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_down_button), ltbp);

        /* FIXME: add a button 'New' with launcher creation dialog */

        {
            GtkGesture * gesture = gtk_gesture_click_new();
            g_signal_connect(gesture, "pressed", G_CALLBACK(on_defined_view_double_click), ltbp);
            gtk_widget_add_controller(defined_view, GTK_EVENT_CONTROLLER(gesture));
        }
        g_signal_connect(defined_view, "cursor-changed", G_CALLBACK(on_defined_view_cursor_changed), ltbp);
        g_signal_connect(menu_view, "cursor-changed", G_CALLBACK(on_menu_view_cursor_changed), ltbp);
        g_signal_connect(menu_view, "row-activated", G_CALLBACK(on_menu_view_row_activated), ltbp);

        ltbp->p_notebook = GTK_NOTEBOOK(gtk_builder_get_object(builder, "notebook"));
        ltbp->p_notebook_page_launch = gtk_notebook_get_nth_page(ltbp->p_notebook, 0);
        ltbp->p_notebook_page_task = gtk_notebook_get_nth_page(ltbp->p_notebook, 1);
        set_config_visibility(ltbp);
	object = gtk_builder_get_object(builder, "combobox_mode");
        gtk_combo_box_set_active(GTK_COMBO_BOX(object), ltbp->mode);
	g_signal_connect(object, "changed",
			G_CALLBACK(on_combobox_mode_changed), ltbp);

#define SETUP_TOGGLE_BUTTON(button,member) \
        object = gtk_builder_get_object(builder, #button); \
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(object), ltbp->flags.member); \
        g_signal_connect(object, "toggled", G_CALLBACK(on_##button##_toggled), ltbp)

        SETUP_TOGGLE_BUTTON(checkbutton_show_tooltips, tooltips);
        SETUP_TOGGLE_BUTTON(checkbutton_icons_only, icons_only);
        SETUP_TOGGLE_BUTTON(checkbutton_flat_buttons, flat_button);
        SETUP_TOGGLE_BUTTON(checkbutton_show_all_desks, show_all_desks);
        SETUP_TOGGLE_BUTTON(checkbutton_same_monitor_only, same_monitor_only);
        SETUP_TOGGLE_BUTTON(checkbutton_mouse_wheel, use_mouse_wheel);
        SETUP_TOGGLE_BUTTON(checkbutton_urgency_hint, use_urgency_hint);
        SETUP_TOGGLE_BUTTON(checkbutton_disable_taskbar_upscale, disable_taskbar_upscale);
        //SETUP_TOGGLE_BUTTON(checkbutton_use_smaller_icons, use_smaller_icons);
#undef SETUP_TOGGLE_BUTTON
        object = gtk_builder_get_object(builder, "checkbutton_grouped_tasks");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(object), ltbp->grouped_tasks);
        g_signal_connect(object, "toggled", G_CALLBACK(on_checkbutton_grouped_tasks_toggled), ltbp);
        /* FIXME: for transitional period, turn into SETUP_TOGGLE_BUTTON later */
        object = gtk_builder_get_object(builder, "checkbutton_use_smaller_icons");
        if (object)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(object), ltbp->flags.use_smaller_icons);
            g_signal_connect(object, "toggled", G_CALLBACK(on_checkbutton_use_smaller_icons_toggled), ltbp);
        }

#define SETUP_SPIN_BUTTON(button,member) \
        object = gtk_builder_get_object(builder, #button); \
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(object), ltbp->member); \
        g_signal_connect(object, "value-changed", \
                         G_CALLBACK(on_##button##_value_changed), ltbp)

        SETUP_SPIN_BUTTON(spinbutton_max_width, task_width_max);
        SETUP_SPIN_BUTTON(spinbutton_spacing, spacing);
#undef SETUP_SPIN_BUTTON

        ltbp->config_dlg = dlg;

        /* Initialize the tree view contents. */
        launchbar_configure_initialize_list(ltbp, dlg, GTK_TREE_VIEW(defined_view));
        g_object_set_data(G_OBJECT(dlg), "menu_view", menu_view);

        gtk_widget_set_visible(ltbp->p_label_menu_app_exec, FALSE);
        gtk_widget_set_visible(ltbp->p_label_def_app_exec, FALSE);
        gtk_widget_set_sensitive(ltbp->p_button_add, FALSE);
        gtk_widget_set_sensitive(ltbp->p_button_remove, FALSE);
        if (ltbp->fixed_mode)
        {
            object = gtk_builder_get_object(builder, "hbox_mode");
            if (object)
            {
                GtkWidget * parent = gtk_widget_get_parent(GTK_WIDGET(object));
                gtk_box_remove(GTK_BOX(parent), GTK_WIDGET(object));
                g_object_unref(GTK_WIDGET(object)); /* was floating-owned? builder holds */
            }
            if (ltbp->mode == LAUNCHBAR)
                gtk_window_set_title(GTK_WINDOW(ltbp->config_dlg),
                                     _("Application Launch Bar"));
            else
                gtk_window_set_title(GTK_WINDOW(ltbp->config_dlg),
                                     _("Task Bar (Window List)"));
        }

        g_object_unref(builder);
    }
    return ltbp->config_dlg;
}

/* Callback when panel configuration changes. */
static void launchtaskbar_panel_configuration_changed(LuxePanel *panel, GtkWidget *p)
{
    /* Set orientation into the icon grid. */
    LaunchTaskBarPlugin *ltbp = luxepanel_plugin_get_data(p);
    int new_icon_size = panel_get_icon_size(panel);
    int height = panel_get_height(panel);

    if (ltbp->lb_built)
        panel_icon_grid_set_geometry(PANEL_ICON_GRID(ltbp->lb_icon_grid),
                                     panel_get_orientation(panel),
                                     new_icon_size, new_icon_size,
                                     3, 0, height);

    /* Redraw all the labels.  Icon size or font color may have changed. */
    if (ltbp->tb_built)
    {
        ltbp->icon_size = new_icon_size;
        panel_icon_grid_set_geometry(PANEL_ICON_GRID(ltbp->tb_icon_grid),
            panel_get_orientation(panel),
            ((ltbp->flags.icons_only) ? ltbp->icon_size + ICON_ONLY_EXTRA : ltbp->task_width_max),
            ((ltbp->flags.icons_only) ? ltbp->icon_size + ICON_ONLY_EXTRA : ltbp->icon_size + ICON_BUTTON_TRIM),
            ltbp->spacing, 0, height);
        taskbar_reset_menu(ltbp);
        taskbar_redraw(ltbp);
    }
}

static gboolean launchtaskbar_control(GtkWidget *p, const char *cmd)
{
    LaunchTaskBarPlugin *ltbp = luxepanel_plugin_get_data(p);

    if (ltbp->mode == LAUNCHBAR || ltbp->mode == LAUNCHTASKBAR)
    {
        if (strncmp(cmd, "add ", 4) == 0)
        {
            config_setting_t *s;

            s = config_group_add_subgroup(ltbp->settings, "Button");
            config_group_set_string(s, "id", &cmd[4]);
            if (launchbutton_constructor(ltbp, s))
            {
                launchbar_remove_bootstrap(ltbp);
                luxepanel_config_save(ltbp->panel);
                return TRUE;
            }
            else
                config_setting_destroy(s);
        }
    }
    return FALSE;
}

/* Redraw all tasks in the taskbar. */
static void taskbar_redraw(LaunchTaskBarPlugin * tb)
{
    GList *children = ltb_grid_children(tb->tb_icon_grid);
    GList *l;
    guint mon = panel_get_monitor(tb->panel);
    guint icon_size = panel_get_icon_size(tb->panel);

    if (tb->flags.use_smaller_icons)
        icon_size -= 4;
    for (l = children; l; l = l->next)
        task_button_update(l->data, tb->current_desktop, tb->number_of_desktops,
                           mon, icon_size, tb->flags);
    g_list_free(children);
}

/* Determine if a task should be visible given its NET_WM_STATE. */
static gboolean accept_net_wm_state(NetWMState * nws)
{
    return ( ! (nws->skip_taskbar));
}

/* Determine if a task should be visible given its NET_WM_WINDOW_TYPE. */
static gboolean accept_net_wm_window_type(NetWMWindowType * nwwt)
{
    return ( ! ((nwwt->desktop) || (nwwt->dock) || (nwwt->splash)));
}

/* Set the class associated with a task. */
static char *task_get_class(Window win)
{
    /* Read the WM_CLASS property. */
    XClassHint ch;
    ch.res_name = NULL;
    ch.res_class = NULL;
    char *res_class = NULL;
    XGetClassHint(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), win, &ch);

    /* If the res_name was returned, free it.  We make no use of it at this time. */
    if (ch.res_name != NULL)
    {
        XFree(ch.res_name);
    }

    /* If the res_class was returned, process it.
     * This identifies the application that created the window and is the basis for taskbar grouping. */
    if (ch.res_class != NULL)
    {
        /* Convert the class to UTF-8 and enter it in the class table. */
        res_class = g_locale_to_utf8(ch.res_class, -1, NULL, NULL, NULL);
        XFree(ch.res_class);
    }
    return res_class;
}

/* Look up a task in the task list. */
static TaskButton *task_lookup(LaunchTaskBarPlugin * tb, Window win)
{
    TaskButton *task = NULL;
    GList *children, *l;
    children = ltb_grid_children(tb->tb_icon_grid);
    for (l = children; l; l = l->next)
        if (task_button_has_window(l->data, win))
        {
            task = l->data;
            break;
        }
    g_list_free(children);
    return task;
}

#ifndef DISABLE_MENU
static void  on_menuitem_lock_tbp_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void  on_menuitem_unlock_tbp_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void  on_menuitem_new_instance_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb);
#endif

static void on_task_menu_built(GtkWidget *unused, GMenu *menu, LaunchTaskBarPlugin *tb)
{
#ifndef DISABLE_MENU
    /* task-to-launcher items; visibility is via action enablement */
    GMenu * section = g_menu_new();

    g_menu_append(section, _("A_dd to Launcher"), "ltb.add-launcher");
    g_menu_append(section, _("Rem_ove from Launcher"), "ltb.remove-launcher");
    g_menu_append(section, _("_New Instance"), "ltb.new-instance");
    if (panel_is_at_bottom(tb->panel))
        g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    else
        g_menu_prepend_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);
#endif
}

static void on_task_menu_target_set(TaskButton *btn, gulong win, LaunchTaskBarPlugin *ltbp)
{
#ifndef DISABLE_MENU
            if(ltbp->mode == LAUNCHTASKBAR)
            {
                FmPath *path = f_find_menu_launchbutton_recursive(win, ltbp);
                LaunchButton *btn = launchbar_exec_bin_exists(ltbp, path);
                /* FIXME: shouldn't we make file info at task button creation? */
                GSimpleActionGroup * group =
                    g_object_get_data(G_OBJECT(ltbp->tb_icon_grid), "ltb-actions");
                GSimpleAction * act;
                if (group != NULL)
                {
                    act = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "add-launcher"));
                    if (act != NULL)
                        g_simple_action_set_enabled(act, btn != NULL || path != NULL);
                    act = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "remove-launcher"));
                    if (act != NULL)
                        g_simple_action_set_enabled(act, btn != NULL);
                    act = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "new-instance"));
                    if (act != NULL)
                        g_simple_action_set_enabled(act, path != NULL);
                }
                if (ltbp->path)
                    fm_path_unref(ltbp->path);
                ltbp->path = path;
            }
            else
            {
                GSimpleActionGroup * group =
                    g_object_get_data(G_OBJECT(ltbp->tb_icon_grid), "ltb-actions");
                GSimpleAction * act;
                if (group != NULL)
                {
                    act = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "add-launcher"));
                    if (act != NULL)
                        g_simple_action_set_enabled(act, FALSE);
                    act = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "remove-launcher"));
                    if (act != NULL)
                        g_simple_action_set_enabled(act, FALSE);
                    act = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(group), "new-instance"));
                    if (act != NULL)
                        g_simple_action_set_enabled(act, FALSE);
                }
            }
#endif
}

/* Handler for "drag-motion" timeout. */
static gboolean taskbar_button_drag_motion_timeout(LaunchTaskBarPlugin * tb)
{
    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;

    task_button_raise_window(PANEL_TASK_BUTTON(tb->dnd_delay_task), CurrentTime);
    tb->dnd_delay_timer = 0;
    return FALSE;
}

/* GTK4 task-button drag&drop: per-button GtkDragSource (same-grid
 * reorder) + GtkDropTargetAsync (reorder + raise-on-hover timer). */
#define TASK_BUTTON_MIME "application/x-luxepanel-taskbutton"

/* drag source: identify the dragged button by its address */
static GdkContentProvider * task_button_drag_prepare(GtkDragSource * source,
                                                     double x, double y,
                                                     TaskButton * task)
{
    GBytes * bytes;
    GdkContentProvider * prov;
    guintptr addr = (guintptr) task;

    bytes = g_bytes_new(&addr, sizeof(addr));
    prov = gdk_content_provider_new_for_bytes(TASK_BUTTON_MIME, bytes);
    g_bytes_unref(bytes);
    return prov;
}

static void task_button_drop_read_done(GObject * obj, GAsyncResult * res,
                                       gpointer user_data);

/* drop target: reorder if the dragged button belongs to the same grid,
 * otherwise arm the raise-on-hover timer */
static GdkDragAction task_button_drop_motion(GtkDropTargetAsync * dest,
                                             GdkDrop * drop,
                                             double x, double y,
                                             LaunchTaskBarPlugin * tb)
{
    GtkWidget * widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(dest));
    GdkContentFormats * formats = gdk_drop_get_formats(drop);

    if (gdk_content_formats_contain_mime_type(formats, TASK_BUTTON_MIME) &&
        tb->dnd_delay_task == NULL)
    {
        tb->dnd_task_moving = TRUE;
        return gdk_drop_get_actions(drop) & GDK_ACTION_MOVE;
    }
    tb->dnd_task_moving = FALSE;
    /* Prevent excessive motion notification. */
    if (tb->dnd_delay_timer == 0)
        tb->dnd_delay_timer = g_timeout_add(DRAG_ACTIVE_DELAY,
                (GSourceFunc) taskbar_button_drag_motion_timeout, tb);
    if (tb->dnd_delay_task != widget)
    {
        if (tb->dnd_delay_task)
            g_object_remove_weak_pointer(G_OBJECT(tb->dnd_delay_task), (gpointer *)&tb->dnd_delay_task);
        tb->dnd_delay_task = widget;
        g_object_add_weak_pointer(G_OBJECT(widget), (gpointer *)&tb->dnd_delay_task);
    }
    return 0;
}

typedef struct {
    LaunchTaskBarPlugin * tb;
    GtkWidget * widget;
} TaskDropCtx;

static gboolean task_button_drop(GtkDropTargetAsync * dest, GdkDrop * drop,
                                 double x, double y, LaunchTaskBarPlugin * tb)
{
    GtkWidget * widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(dest));

    tb->dnd_task_moving = FALSE;
    /* Cancel the timer if set. */
    if (tb->dnd_delay_timer != 0)
    {
        g_source_remove(tb->dnd_delay_timer);
        tb->dnd_delay_timer = 0;
    }
    /* same-grid reorder: read the button address back */
    if (gdk_content_formats_contain_mime_type(gdk_drop_get_formats(drop), TASK_BUTTON_MIME))
    {
        TaskDropCtx * ctx = g_new0(TaskDropCtx, 1);

        ctx->tb = tb;
        ctx->widget = widget;
        gdk_drop_read_value_async(drop, G_TYPE_BYTES, G_PRIORITY_DEFAULT, NULL,
                                  task_button_drop_read_done, ctx);
        return TRUE;
    }
    return FALSE;
}

static void task_button_drop_read_done(GObject * obj, GAsyncResult * res,
                                       gpointer user_data)
{
    TaskDropCtx * ctx = user_data;
    GdkDrop * drop = GDK_DROP(obj);
    GError * error = NULL;
    const GValue * val = gdk_drop_read_value_finish(drop, res, &error);
    GBytes * bytes = (val != NULL && G_VALUE_HOLDS(val, G_TYPE_BYTES))
                     ? g_value_get_boxed(val) : NULL;

    if (bytes != NULL)
    {
        gsize size;
        gconstpointer data = g_bytes_get_data(bytes, &size);
        if (data != NULL && size == sizeof(guintptr))
        {
            guintptr addr;
            GtkWidget * drag_source;

            memcpy(&addr, data, sizeof(addr));
            if (PANEL_IS_TASK_BUTTON((TaskButton *) addr))
                drag_source = GTK_WIDGET(addr);
            else
                drag_source = NULL;
            if (drag_source != NULL &&
                gtk_widget_get_parent(drag_source) == gtk_widget_get_parent(ctx->widget) &&
                drag_source != ctx->widget)
            {
                PanelIconGrid *ig = PANEL_ICON_GRID(ctx->tb->tb_icon_grid);
                gint i = panel_icon_grid_get_child_position(ig, ctx->widget);
                panel_icon_grid_reorder_child(ig, drag_source, i);
            }
        }
        gdk_drop_finish(drop, GDK_ACTION_MOVE);
    }
    else
        gdk_drop_finish(drop, (GdkDragAction) 0);
    g_clear_error(&error);
    g_free(ctx);
}

/* Handler for "enter" event from taskbar button.  This indicates that the cursor position has entered the button. */
static void taskbar_button_enter(GtkEventControllerMotion * m, LaunchTaskBarPlugin * tb)
{
    tb->dnd_task_moving = FALSE;
}

/* Build graphic elements needed for a task button. */
static void taskbar_add_task_button(LaunchTaskBarPlugin * tb, TaskButton * task)
{
    /* Allocate a toggle button as the top level widget. */
    panel_icon_grid_add(PANEL_ICON_GRID(tb->tb_icon_grid), GTK_WIDGET(task));

    /* Connect signals to the button. */
    /* handle menu callbacks */
    g_signal_connect(G_OBJECT(task), "menu-built",
                     (GCallback)on_task_menu_built, tb);
    g_signal_connect(G_OBJECT(task), "menu-target-set",
                     (GCallback)on_task_menu_target_set, tb);
    /* drag & drop on task buttons */
    {
        GtkDragSource * src = gtk_drag_source_new();
        GtkDropTargetAsync * dst;
        GdkContentFormatsBuilder * builder;
        GdkContentFormats * formats;
        GtkEventController * motion = gtk_event_controller_motion_new();

        g_signal_connect(src, "prepare", G_CALLBACK(task_button_drag_prepare), task);
        gtk_widget_add_controller(GTK_WIDGET(task), GTK_EVENT_CONTROLLER(src));

        builder = gdk_content_formats_builder_new();
        gdk_content_formats_builder_add_mime_type(builder, TASK_BUTTON_MIME);
        formats = gdk_content_formats_builder_free_to_formats(builder);
        dst = gtk_drop_target_async_new(formats, GDK_ACTION_MOVE);
        g_signal_connect(dst, "drag-motion", G_CALLBACK(task_button_drop_motion), tb);
        g_signal_connect(dst, "drop", G_CALLBACK(task_button_drop), tb);
        gtk_widget_add_controller(GTK_WIDGET(task), GTK_EVENT_CONTROLLER(dst));

        g_signal_connect(motion, "enter", G_CALLBACK(taskbar_button_enter), tb);
        gtk_widget_add_controller(GTK_WIDGET(task), motion);
    }
}

/* add win to tb, using list of task buttons */
static void taskbar_add_new_window(LaunchTaskBarPlugin * tb, Window win, GList *list)
{
    gchar *res_class = task_get_class(win);
    TaskButton *task;

    if (!tb->grouped_tasks || res_class == NULL)
        list = NULL;
    else for (; list; list = list->next)
        if (task_button_add_window(list->data, win, res_class))
            break;
    if (list != NULL)
        return; /* some button accepted it, done */

    task = task_button_new(win, tb->current_desktop, tb->number_of_desktops,
                           tb->panel, res_class, tb->flags);
    taskbar_add_task_button(tb, task);
}

/*****************************************************
 * handlers for NET actions                          *
 *****************************************************/

/* Handler for "client-list" event from root window listener. */
static void taskbar_net_client_list(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    /* Get the NET_CLIENT_LIST property. */
    int client_count;
    Window * client_list = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST, XA_WINDOW, &client_count);
    if (client_list != NULL)
    {
        GList *children = ltb_grid_children(tb->tb_icon_grid), *l;
        /* Remove windows from the task list that are not present in the NET_CLIENT_LIST. */
        for (l = children; l; l = l->next)
            task_button_update_windows_list(l->data, client_list, client_count);
        g_list_free(children);
        children = ltb_grid_children(tb->tb_icon_grid);
        /* Loop over client list, correlating it with task list. */
        int i;
        for (i = 0; i < client_count; i++)
        {
            /* Search for the window in the task list.  Set up context to do an insert right away if needed. */
            for (l = children; l; l = l->next)
            {
                if (task_button_has_window(l->data, client_list[i]))
                    break;
            }

            /* Task is not in task list. */
            if (l == NULL)
            {
                /* Evaluate window state and window type to see if it should be in task list. */
                NetWMWindowType nwwt;
                NetWMState nws;
                get_net_wm_state(client_list[i], &nws);
                get_net_wm_window_type(client_list[i], &nwwt);
                if ((accept_net_wm_state(&nws))
                && (accept_net_wm_window_type(&nwwt)))
                {
                    /* Allocate and initialize new task structure. */
                    taskbar_add_new_window(tb, client_list[i], children);
                    g_list_free(children);
                    children = ltb_grid_children(tb->tb_icon_grid);
                }
            }
        }
        g_list_free(children);
        XFree(client_list);
    }

    else /* clear taskbar */
    {
        GList * children = ltb_grid_children(tb->tb_icon_grid);
        GList * l;
        for (l = children; l; l = l->next)
            ltb_grid_remove(tb->tb_icon_grid, l->data);
        g_list_free(children);
    }
}

/* Handler for "current-desktop" event from root window listener. */
static void taskbar_net_current_desktop(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    /* Store the local copy of current desktops.  Redisplay the taskbar. */
    tb->current_desktop = get_net_current_desktop();
    taskbar_redraw(tb);
}

/* Handler for "number-of-desktops" event from root window listener. */
static void taskbar_net_number_of_desktops(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    /* Store the local copy of number of desktops.  Recompute the popup menu and redisplay the taskbar. */
    tb->number_of_desktops = get_net_number_of_desktops();
    taskbar_reset_menu(tb);
    taskbar_redraw(tb);
}

/* Handler for "active-window" event from root window listener. */
static void taskbar_net_active_window(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    /* Get the window that has focus. */
    Window * f = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_ACTIVE_WINDOW, XA_WINDOW, 0);

    {
        GList * children = ltb_grid_children(tb->tb_icon_grid);
        GList * l;
        for (l = children; l; l = l->next)
            task_button_window_focus_changed(l->data, f);
        g_list_free(children);
    }
    if (f != NULL)
        XFree(f);
}

/* Handle PropertyNotify event.
 * http://tronche.com/gui/x/icccm/
 * http://standards.freedesktop.org/wm-spec/wm-spec-1.4.html */
static void taskbar_property_notify_event(LaunchTaskBarPlugin *tb, XEvent *ev)
{
    /* State may be PropertyNewValue, PropertyDeleted. */
    if (((XPropertyEvent*) ev)->state == PropertyNewValue)
    {
        Atom at = ev->xproperty.atom;
        Window win = ev->xproperty.window;
        if (win != GDK_ROOT_WINDOW())
        {
            /* Look up task structure by X window handle. */
            TaskButton * tk = task_lookup(tb, win);
            if (tk != NULL)
            {
                /* Install an error handler that ignores BadWindow.
                 * We frequently get a PropertyNotify event on deleted windows. */
                XErrorHandler previous_error_handler = XSetErrorHandler(panel_handle_x_error_swallow_BadWindow_BadDrawable);

                /* Dispatch on atom. */
                if (at == a_NET_WM_STATE)
                {
                    /* Window changed EWMH state. */
                    NetWMState nws;
                    get_net_wm_state(win, &nws);
                    if ( ! accept_net_wm_state(&nws))
                        task_button_drop_window(tk, win, FALSE);
                    /* else
                        task_button_window_state_changed(tk, win, nws); */
                }
                else if (at == a_NET_WM_WINDOW_TYPE)
                {
                    /* Window changed EWMH window type. */
                    NetWMWindowType nwwt;
                    get_net_wm_window_type(win, &nwwt);
                    if ( ! accept_net_wm_window_type(&nwwt))
                        task_button_drop_window(tk, win, FALSE);
                }
                else if (at == XA_WM_CLASS && tb->grouped_tasks
                         && task_button_drop_window(tk, win, TRUE))
                {
                    GList *children = ltb_grid_children(tb->tb_icon_grid);
                    /* if Window was not single window of that class then
                       add it to another class or make another button */
                    taskbar_add_new_window(tb, win, children);
                    g_list_free(children);
                }
                else
                {
                    /* simply notify button, it will handle the event */
                    task_button_window_xprop_changed(tk, win, at);
                }

                XSetErrorHandler(previous_error_handler);
            }
        }
    }
}

/* Handle ConfigureNotify events */
static void taskbar_configure_notify_event(LaunchTaskBarPlugin * tb, XConfigureEvent * ev)
{
    /* If the same_monitor_only option is set and the window is on a different
       monitor than before, redraw the taskbar */
    TaskButton *task;

    if (ev->window != GDK_ROOT_WINDOW())
    {
        task = task_lookup(tb, ev->window);
        if (task)
        {
            /* Deleted windows seem to get ConfigureNotify events too. */
            XErrorHandler previous_error_handler = XSetErrorHandler(panel_handle_x_error_swallow_BadWindow_BadDrawable);

            /* Monitor might be changed so button might need update */
            task_button_window_reconfigured(task, ev->window);

            XSetErrorHandler(previous_error_handler);
        }
    }
}

#if !defined(DISABLE_MENU)
static void ltb_menu_item_action(GSimpleAction * action, GVariant * parameter,
                                 gpointer user_data)
{
    LaunchTaskBarPlugin * tb = user_data;
    const char * name = g_action_get_name(G_ACTION(action));

    if (strcmp(name, "add-launcher") == 0)
        on_menuitem_lock_tbp_clicked(NULL, tb);
    else if (strcmp(name, "remove-launcher") == 0)
        on_menuitem_unlock_tbp_clicked(NULL, tb);
    else
        on_menuitem_new_instance_clicked(NULL, tb);
}
#endif

#ifndef DISABLE_MENU
static void  on_menuitem_lock_tbp_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchButton *btn;
    char *path;
    config_setting_t *settings;

    if (tb->path)
    {
        /* Create a button and add settings for it */
        path = fm_path_to_str(tb->path);
        /* g_debug("*** path '%s'",path); */
        settings = config_group_add_subgroup(tb->settings, "Button");
        config_group_set_string(settings, "id", path);
        g_free(path);
        btn = launch_button_new(tb->panel, tb->plugin, tb->path, settings);
        if (btn)
        {
            panel_icon_grid_add(PANEL_ICON_GRID(tb->lb_icon_grid), GTK_WIDGET(btn));
        }
        else
            config_setting_destroy(settings);
        luxepanel_config_save(tb->panel);
    }
}

static void  on_menuitem_unlock_tbp_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    launchbar_remove_launcher(tb, tb->path);
    launchbar_check_bootstrap(tb);
}

static void  on_menuitem_new_instance_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    if (tb->path)
    {
        luxepanel_launch_path(tb->panel, tb->path);
    }
}
#endif

/* Callback from configuration dialog mechanism to apply the configuration. */
static void taskbar_apply_configuration(LaunchTaskBarPlugin *ltbp)
{
    taskbar_redraw(ltbp);
}

static GtkWidget *launchbar_constructor(LuxePanel *panel, config_setting_t *settings)
{
    return _launchtaskbar_constructor(panel, settings, LAUNCHBAR);
}

static GtkWidget *taskbar_constructor(LuxePanel *panel, config_setting_t *settings)
{
    return _launchtaskbar_constructor(panel, settings, TASKBAR);
}

static LuxePanelPluginInit _launchbar_init = {
    .name = N_("Application Launch Bar"),
    .description = N_("Bar with buttons to launch application"),

    .new_instance = launchbar_constructor,
    .config = launchtaskbar_configure,
    .reconfigure = launchtaskbar_panel_configuration_changed,
    .control = launchtaskbar_control
};

static LuxePanelPluginInit _taskbar_init = {
    .name = N_("Task Bar (Window List)"),
    .description = N_("Taskbar shows all opened windows and allow to iconify them, shade or get focus"),

    .expand_available = TRUE,
    .expand_default = TRUE,

    .new_instance = taskbar_constructor,
    .config = launchtaskbar_configure,
    .reconfigure = launchtaskbar_panel_configuration_changed,
    .control = launchtaskbar_control
};

static void launchtaskbar_init(void)
{
    luxepanel_register_plugin_type("launchbar", &_launchbar_init);
    luxepanel_register_plugin_type("taskbar", &_taskbar_init);
}

/* Plugin descriptor. */
LuxePanelPluginInit luxepanel_static_plugin_launchtaskbar = {
    .name = N_("Application Launch and Task Bar"),
    .description = N_("Bar with buttons to launch application and/or show all opened windows"),

    .expand_available = TRUE,
    .expand_default = TRUE,

    .init = launchtaskbar_init,
    .new_instance = launchtaskbar_constructor,
    .config = launchtaskbar_configure,
    /* .update_context_menu = launchtaskbar_update_context_menu, */
    .reconfigure = launchtaskbar_panel_configuration_changed,
    .control = launchtaskbar_control
};

/* vim: set sw=4 sts=4 et : */
