/*
 * Copyright (C) 2006-2009 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Jürgen Hötzel <juergen@archlinux.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2011-2014 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2013 Vincenzo di Cicco <enzodicicco@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2014-2019 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2014 Andy Balaam <axis3x3@users.sf.net>
 *               2015 Balló György <ballogyor@gmail.com>
 *               2015 Rafał Mużyło <galtgendo@gmail.com>
 *               2018 Mamoru TASAKA <mtasaka@fedoraproject.org>
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

#include "task-button.h"
#include "task-icon-property.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/x11/gdkx.h>
#include <glib/gi18n.h>
#include <cairo-xlib.h>

#include "plugin.h"
#include "misc.h"
#include "icon-grid.h"
#include "icon.xpm"
#include "gtk-compat.h"
#define __LUXEPANEL_INTERNALS__
#include "private.h"

#define ALL_WORKSPACES       -1

/* Private X connections used to watch task windows (GDK4 ignores events
 * on non-GDK windows of the GDK connection). Each taskbar plugin owns one;
 * they are tracked in a registry so destroying one plugin does not leave
 * the surviving taskbars selecting input on a display GDK ignores. */
static GList * task_event_dpy_list = NULL;

void task_button_set_event_display(Display * dpy)
{
    if (dpy != NULL)
    {
        if (g_list_find(task_event_dpy_list, dpy) == NULL)
            task_event_dpy_list = g_list_prepend(task_event_dpy_list, dpy);
        return;
    }
}

void task_button_unset_event_display(Display * dpy)
{
    task_event_dpy_list = g_list_remove(task_event_dpy_list, dpy);
}

/* Xlib error handlers have no closure; park state in statics. */
static gboolean task_xerror = FALSE;
static Display * task_xerror_dpy = NULL;
/* Previously installed handler, saved by task_xtrap_push() so
 * task_xtrap_pop() restores it instead of clearing to NULL (which
 * permanently destroyed the panel's global X error handler). */
static XErrorHandler task_saved_handler = NULL;
static gint task_xtrap_depth = 0;

static int task_error_handler(Display * dpy, XErrorEvent * ev)
{
    if (dpy == task_xerror_dpy)
        task_xerror = TRUE;
    return 0;
}

static void task_xtrap_push(void)
{
    Display * xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    if (task_xtrap_depth == 0)
    {
        task_xerror = FALSE;
        task_xerror_dpy = xdisplay;
        task_saved_handler = XSetErrorHandler(task_error_handler);
    }
    task_xtrap_depth++;
}

static gboolean task_xtrap_pop(void)
{
    Display * xdisplay = task_xerror_dpy;
    gboolean had_error;

    if (xdisplay != NULL)
        XSync(xdisplay, False);
    had_error = task_xerror;
    if (task_xtrap_depth > 0)
        task_xtrap_depth--;
    if (task_xtrap_depth == 0)
    {
        XSetErrorHandler(task_saved_handler);
        task_saved_handler = NULL;
        task_xerror_dpy = NULL;
        task_xerror = FALSE;
    }
    return had_error;
}

static Atom a_NET_WM_STATE_MAXIMIZED_VERT;
static Atom a_NET_WM_STATE_MAXIMIZED_HORZ;

/* gtk_widget_destroy is gone; buttons live in a PanelIconGrid */
static void task_button_destroy_widget(TaskButton * button)
{
    GtkWidget * parent = gtk_widget_get_parent(GTK_WIDGET(button));

    if (parent != NULL && PANEL_IS_ICON_GRID(parent))
    {
        panel_icon_grid_remove(PANEL_ICON_GRID(parent), GTK_WIDGET(button));
    }
    else if (parent == NULL)
        g_object_unref(GTK_WIDGET(button));
}

/* -----------------------------------------------------------------------------
 * Class data
 */

/* individual task data */
typedef struct
{
    Window win;                             /* X window ID */
    gint desktop;                           /* Desktop that contains task, needed to switch to it on Raise */
    gint monitor;                           /* Monitor that the window is on or closest to */
    char * name;                            /* Taskbar label when normal, from WM_NAME or NET_WM_NAME */
    GdkPixbuf * icon;           /* the taskbar icon */
    GtkWidget * menu_item;      /* if menu_list exists then it's an item in it */
    Atom name_source;                       /* Atom that is the source of taskbar label */
    Atom image_source;                      /* Atom that is the source of taskbar icon */
    unsigned int visible :1;    /* TRUE if window is shown in taskbar */
    unsigned int focused                :1; /* True if window has focus */
    unsigned int iconified              :1; /* True if window is iconified, from WM_STATE */
    unsigned int urgency                :1; /* True if window has an urgency hint, from WM_HINTS */
} TaskDetails;

/* widget data */
struct _TaskButton
{
    GtkToggleButton parent;
    char * res_class;                      /* Class name */
    GtkWidget * image;                      /* Icon for task, child of button */
    GtkWidget * label;                      /* Label for task, child of button */
    LuxePanel * panel;            /* points to panel (grandparent widget) */
    TaskDetails * last_focused; /* points to details of last focused task */
    GMenu * menu_list;          /* list of tasks on menu activation */
    Window menu_target;         /* window which activated menu */
    guint n_visible;            /* number of windows that are shown */
    guint idle_loader;          /* id of icons loader */
    GList * details;            /* details for each window, TaskDetails */
    gint desktop;               /* Current desktop of the button */
    gint n_desktops;            /* total number of desktops */
    gint monitor;               /* current monitor for the panel */
    guint icon_size;            /* Current value from last update */
    TaskShowFlags flags;        /* flags to show */
    unsigned int set_bold :1;   /* flat buttons only: TRUE if set bold */
    unsigned int visible :1;    /* TRUE if any window shown on current desktop */
    unsigned int same_name :1;  /* TRUE if all visible windows have the same name */
    unsigned int entered_state :1; /* TRUE if cursor is inside taskbar button */
    unsigned int has_flash :1;  /* used by task_button_set_flash_state() */
};

enum {
    MENU_BUILT,
    MENU_TARGET_SET,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void task_update_icon(TaskButton *task, TaskDetails *details, Atom source);

/* -----------------------------------------------------------------------------
 * Internal functions
 */

/* Determine which monitor a given window is associated with */
static gint get_window_monitor(Window win)
{
    GdkDisplay * display = gdk_display_get_default();
    Display * xdisplay;
    XWindowAttributes attr;
    GListModel * monitors;
    guint i, n;
    int x, y;
    Window child;

    if (!luxepanel_display_usable(display))
        return 0;
    if (!GDK_IS_X11_DISPLAY(display))
        return 0;
    xdisplay = gdk_x11_display_get_xdisplay(display);
    if (xdisplay == NULL)
        return 0;
    task_xtrap_push();
    {
        gboolean attrs_ok = XGetWindowAttributes(xdisplay, win, &attr);

        if (attrs_ok)
            XTranslateCoordinates(xdisplay, win, DefaultRootWindow(xdisplay),
                                  0, 0, &x, &y, &child);
    }
    {
        gboolean had_xerror = task_xtrap_pop();

        if (had_xerror)
            return 0;
    }    monitors = gdk_display_get_monitors(display);
    if (!G_IS_LIST_MODEL(monitors))
        return 0;
    n = g_list_model_get_n_items(monitors);
    for (i = 0; i < n; i++)
    {
        GdkMonitor * mon = GDK_MONITOR(g_list_model_get_item(monitors, i));
        GdkRectangle r;
        if (mon == NULL)
            continue;
        gdk_monitor_get_geometry(mon, &r);
        g_object_unref(mon);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
            return (gint) i;
    }
    return 0;
}

/* Determine if the "urgency" hint is set on a window. */
static gboolean task_has_urgency(Window win)
{
    gboolean result = FALSE;
    XWMHints * hints;
    task_xtrap_push();
    hints = (XWMHints *) get_xaproperty(win, XA_WM_HINTS, XA_WM_HINTS, 0);
    if (task_xtrap_pop() && hints != NULL)
    {
        XFree(hints);
        hints = NULL;
    }
    if (hints != NULL)
    {
        if (hints->flags & XUrgencyHint)
            result = TRUE;
        XFree(hints);
    }
    //FIXME: also test _NET_WM_STATE_DEMANDS_ATTENTION flag in _NET_WM_STATE
    return result;
}

/* Returns TRUE if change name affects button name */
static gboolean task_set_names(TaskDetails *tk, Atom source)
{
    char * name = NULL;
    gboolean had_xerror;
    task_xtrap_push();

    /* Try _NET_WM_VISIBLE_NAME, which supports UTF-8.
     * If it is set, the window manager is displaying it as the window title. */
    if ((source == None) || (source == a_NET_WM_VISIBLE_NAME))
    {
        name = get_utf8_property(tk->win,  a_NET_WM_VISIBLE_NAME);
        if (name != NULL)
            tk->name_source = a_NET_WM_VISIBLE_NAME;
    }

    /* Try _NET_WM_NAME, which supports UTF-8, but do not overwrite _NET_WM_VISIBLE_NAME. */
    if ((name == NULL)
    && ((source == None) || (source == a_NET_WM_NAME))
    && ((tk->name_source == None) || (tk->name_source == a_NET_WM_NAME) || (tk->name_source == XA_WM_NAME)))
    {
        name = get_utf8_property(tk->win,  a_NET_WM_NAME);
        if (name != NULL)
            tk->name_source = a_NET_WM_NAME;
    }

    /* Try WM_NAME, which supports only ISO-8859-1, but do not overwrite _NET_WM_VISIBLE_NAME or _NET_WM_NAME. */
    if ((name == NULL)
    && ((source == None) || (source == XA_WM_NAME))
    && ((tk->name_source == None) || (tk->name_source == XA_WM_NAME)))
    {
        name = get_textproperty(tk->win,  XA_WM_NAME);
        if (name != NULL)
            tk->name_source = XA_WM_NAME;
    }

    /* A dead window raises BadWindow instead of returning data;
     * drop any partial result and keep the previous name. */
    had_xerror = task_xtrap_pop();
    if (had_xerror && name != NULL)
    {
        g_free(name);
        name = NULL;
    }

    /* Set the name into the task context, and also on the tooltip. */
    if (name != NULL)
    {
        if (g_strcmp0(name, tk->name) != 0)
        {
            g_free(tk->name);
            tk->name = name;
            return TRUE;
        }
        g_free(name);
    }
    return FALSE;
}

static gboolean task_is_visible(TaskButton *b, TaskDetails *task)
{
    /* Not on same monitor */
    if (b->flags.same_monitor_only && b->monitor != task->monitor && b->monitor >= 0)
        return FALSE;

    /* Desktop placement. */
    return ((task->desktop == ALL_WORKSPACES) ||
            (task->desktop == b->desktop) ||
            (b->flags.show_all_desks) ||
            (b->flags.use_urgency_hint && task->urgency));
}

static TaskDetails *task_details_for_window(TaskButton *button, Window win)
{
    TaskDetails *details = g_slice_new0(TaskDetails);
    GdkDisplay *display = gdk_display_get_default();
    /* NOTE
     * 1. the extended mask is sum of taskbar and pager needs
     * see bug [ 940441 ] pager loose track of windows
     *
     * Do not change event mask to gtk windows spawned by this gtk client
     * this breaks gtk internals */
    /* GDK4 ignores events on non-GDK windows; use the taskbars' private
     * connections. Selecting on every registered connection keeps each
     * taskbar's pump alive even when another taskbar plugin is destroyed
     * (its connection previously died with the selected inputs). */
    if (task_event_dpy_list != NULL)
    {
        for (GList *l = task_event_dpy_list; l != NULL; l = l->next)
        {
            XSelectInput((Display *) l->data, win,
                         PropertyChangeMask | StructureNotifyMask);
            XFlush((Display *) l->data);
        }
    }
    else
    {
        XSelectInput(gdk_x11_display_get_xdisplay(display), win,
                     PropertyChangeMask | StructureNotifyMask);
        XFlush(gdk_x11_display_get_xdisplay(display));
    }

    /* fetch task details */
    details->win = win;
    task_xtrap_push();
    details->desktop = get_net_wm_desktop(win);
    if (task_xtrap_pop())
        details->desktop = 0;
    details->monitor = get_window_monitor(win);
    task_set_names(details, None);
    task_update_icon(button, details, None);
    details->urgency = task_has_urgency(win);
    task_xtrap_push();
    details->iconified = (get_wm_state(win) == IconicState);
    if (task_xtrap_pop())
        details->iconified = FALSE;
    // FIXME: may want _NET_WM_STATE check
    // FIXME: check if task is focused
    /* check task visibility by flags */
    details->visible = task_is_visible(button, details);
    return details;
}

static void free_task_details(TaskDetails *details)
{
    g_free(details->name);
    if (details->icon)
        g_object_unref(details->icon);
    g_slice_free(TaskDetails, details);
}

static TaskDetails *task_details_lookup(TaskButton *task, Window win)
{
    GList *l;

    for (l = task->details; l; l = l->next)
        if (((TaskDetails *)l->data)->win == win)
            return l->data;
    return NULL;
}

static inline TaskButton *get_menu_task_button(GtkWidget *taskbar)
{
    return g_object_get_data(G_OBJECT(taskbar), "task-button-current");
}

/* Handler for maximize/unmaximize. Taken from WNCK */
static void do_maximize(Window xwindow, gboolean set)
{
    Xclimsg(xwindow,
            a_NET_WM_STATE, set ? a_NET_WM_STATE_ADD : a_NET_WM_STATE_REMOVE,
            a_NET_WM_STATE_MAXIMIZED_VERT, a_NET_WM_STATE_MAXIMIZED_HORZ,
            1 /* application */, 0);
}

/* the "task.*" actions of the shared task menu live on the parent widget */
static void menu_raise_window(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    GtkWidget *taskbar = user_data;
    TaskButton *tb = get_menu_task_button(taskbar);
    if (tb == NULL || tb->menu_target == None)
        return;
    TaskDetails *tk = task_details_lookup(tb, tb->menu_target);
    if (tk == NULL)
        return;

    if ((tk->desktop != ALL_WORKSPACES) && (tk->desktop != tb->desktop))
        Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, tk->desktop, 0, 0, 0, 0);
    XMapRaised(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), tk->win);
}

static void menu_restore_window(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    TaskButton *tb = get_menu_task_button(user_data);
    if (tb == NULL || tb->menu_target == None)
        return;
    do_maximize(tb->menu_target, FALSE);
}

static void menu_maximize_window(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    TaskButton *tb = get_menu_task_button(user_data);
    if (tb == NULL || tb->menu_target == None)
        return;
    do_maximize(tb->menu_target, TRUE);
}

static void menu_iconify_window(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    TaskButton *tb = get_menu_task_button(user_data);
    if (tb == NULL || tb->menu_target == None)
        return;
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    XIconifyWindow(xdisplay, tb->menu_target, DefaultScreen(xdisplay));
}

static void menu_move_to_workspace(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    TaskButton *tb = get_menu_task_button(user_data);
    if (tb == NULL || tb->menu_target == None)
        return;
    int num = g_variant_get_int32(parameter);
    Xclimsg(tb->menu_target, a_NET_WM_DESKTOP, num, 0, 0, 0, 0);
}

static void menu_close_window(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    TaskButton *tb = get_menu_task_button(user_data);
    if (tb == NULL || tb->menu_target == None)
        return;
    Xclimsgwm(tb->menu_target, a_WM_PROTOCOLS, a_WM_DELETE_WINDOW);
}

static gboolean task_button_window_do_release_event(GtkWidget *tb, TaskDetails *task,
                                                    guint button, guint32 time);

/* activate a listed window from the grouped-task popup */
static void menu_window_activate(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    GtkWidget *taskbar = user_data;
    TaskButton *tb = get_menu_task_button(taskbar);
    if (tb == NULL)
        return;
    Window win = (Window) g_variant_get_uint64(parameter);
    TaskDetails *task = task_details_lookup(tb, win);

    if (task != NULL)
        task_button_window_do_release_event(GTK_WIDGET(tb), task, 1, 0);
}

static void menu_close_all_windows(GSimpleAction * action, GVariant * parameter, gpointer user_data)
{
    GtkWidget *taskbar = user_data;
    TaskButton *tb = get_menu_task_button(taskbar);
    if (tb == NULL)
        return;
    GList *l;

    for (l = tb->details; l; l = l->next)
    {
        TaskDetails *tk = l->data;
        if (tk->visible)
            Xclimsgwm(tk->win, a_WM_PROTOCOLS, a_WM_DELETE_WINDOW);
    }
}

/* register the shared "task.*" action group on the parent widget */
static void task_button_ensure_actions(GtkWidget * parent)
{
    GSimpleActionGroup * group = g_object_get_data(G_OBJECT(parent), "task-button-actions");

    if (group != NULL)
        return;
    group = g_simple_action_group_new();
    {
        GSimpleAction * act;
        static GActionEntry entries[] = {
            { "raise",          menu_raise_window },
            { "restore",        menu_restore_window },
            { "maximize",       menu_maximize_window },
            { "iconify",        menu_iconify_window },
            { "close",          menu_close_window },
            { "close-all",      menu_close_all_windows },
        };
        g_action_map_add_action_entries(G_ACTION_MAP(group), entries,
                                        G_N_ELEMENTS(entries), parent);
        act = g_simple_action_new("move-workspace", G_VARIANT_TYPE("i"));
        g_signal_connect(act, "activate", G_CALLBACK(menu_move_to_workspace), parent);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_object_unref(act);
        act = g_simple_action_new("window", G_VARIANT_TYPE("t"));
        g_signal_connect(act, "activate", G_CALLBACK(menu_window_activate), parent);
        g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));
        g_object_unref(act);
    }
    g_object_set_data_full(G_OBJECT(parent), "task-button-actions",
                           group, g_object_unref);
    gtk_widget_insert_action_group(parent, "task", G_ACTION_GROUP(group));
}

/* Handler for "activate" event on Raise item of right-click menu for task buttons. */

/* pixbuf -> GIcon for menu items (GdkPixbuf is not a GIcon) */
static GIcon *task_menu_icon_from_pixbuf(GdkPixbuf * pixbuf)
{
    GIcon * icon = NULL;
    gchar * buf = NULL;
    gsize size;
    GError * error = NULL;

    if (gdk_pixbuf_save_to_buffer(pixbuf, &buf, &size, "png", &error, NULL))
    {
        GBytes * bytes = g_bytes_new_take(buf, size);
        icon = g_bytes_icon_new(bytes);
        g_bytes_unref(bytes);
    }
    else
    {
        g_error_free(error);
        g_free(buf);
    }
    return icon;
}

/* Make right-click menu for task buttons.
 * This depends on number of desktops and edge. */
static GMenu *taskbar_make_menu(TaskButton *tb)
{
    GMenu *menu = g_menu_new();
    GMenu *section;
    GMenu *close_section;

    section = g_menu_new();
    g_menu_append(section, _("_Raise"), "task.raise");
    g_menu_append(section, _("R_estore"), "task.restore");
    g_menu_append(section, _("Ma_ximize"), "task.maximize");
    g_menu_append(section, _("Ico_nify"), "task.iconify");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    /* FIXME: if WM is Openbox then add "Window special parameters" submenu */

    /* If multiple desktops are supported, add menu items to select them. */
    if (tb->n_desktops > 1)
    {
        char label[128];
        GMenu * workspace_menu = g_menu_new();
        GMenu * mv_menu;
        int i;

        /* Loop over all desktops. */
        for (i = 1; i <= tb->n_desktops; i++)
        {
            GMenuItem * item;
            /* For the first 9 desktops, allow the desktop number as a keyboard shortcut. */
            if (i <= 9)
            {
                g_snprintf(label, sizeof(label), _("Workspace _%d"), i);
            }
            else
            {
                g_snprintf(label, sizeof(label), _("Workspace %d"), i);
            }
            item = g_menu_item_new(label, NULL);
            g_menu_item_set_action_and_target(item, "task.move-workspace", "i", i - 1);
            g_menu_append_item(workspace_menu, item);
            g_object_unref(item);
        }

        /* Add "move to all workspaces" item.  This causes the window to be visible no matter what desktop is active. */
        {
            GMenuItem * item = g_menu_item_new(_("_All workspaces"), NULL);
            g_menu_item_set_action_and_target(item, "task.move-workspace", "i", (gint) ALL_WORKSPACES);
            g_menu_append_item(workspace_menu, item);
            g_object_unref(item);
        }

        /* FIXME: add "Current workspace" item, active if not on a current */

        /* Add Move to Workspace menu item as a submenu. */
        mv_menu = g_menu_new();
        g_menu_append_submenu(mv_menu, _("_Move to Workspace"), G_MENU_MODEL(workspace_menu));
        g_menu_append_section(menu, NULL, G_MENU_MODEL(mv_menu));
        g_object_unref(mv_menu);
        g_object_unref(workspace_menu);
    }

    /* Extend the menu by callbacks */
    g_signal_emit(tb, signals[MENU_BUILT], 0, menu);

    /* Add Close menu item.  By popular demand, we place this menu item closest to the cursor. */
    close_section = g_menu_new();
    g_menu_append(close_section, _("_Close Window"), "task.close");
    if (panel_is_at_bottom(tb->panel))
        g_menu_append_section(menu, NULL, G_MENU_MODEL(close_section));
    else
        g_menu_prepend_section(menu, NULL, G_MENU_MODEL(close_section));
    g_object_unref(close_section);

    return menu;
}

static GMenu *get_task_button_menu(TaskButton *tb, TaskDetails *task)
{
    GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(tb));
    GMenu *menu = g_object_get_data(G_OBJECT(parent), "task-button-menu");

    if (menu == NULL)
    {
        /* this GMenu is built on demand on the parent widget */
        menu = taskbar_make_menu(tb);
        g_object_set_data_full(G_OBJECT(parent), "task-button-menu",
                               menu, g_object_unref);
    }
    g_object_set_data(G_OBJECT(parent), "task-button-current", tb);
    /* save current choice for our callbacks */
    tb->menu_target = task->win;
    /* notify menu makers about current choise */
    g_signal_emit(tb, signals[MENU_TARGET_SET], 0, (gulong)task->win);
    /* FIXME: gray out workspace where window is on (popovers from GMenu
     * cannot set per-item sensitivity; actions stay enabled) */

    return menu;
}

/* Do the proper steps to raise a window.
 * This means removing it from iconified state and bringing it to the front.
 * We also switch the active desktop and viewport if needed. */
static void task_raise_window(TaskButton *tb, TaskDetails *tk, guint32 time)
{
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    task_xtrap_push();

    /* Change desktop if needed. */
    if ((tk->desktop != ALL_WORKSPACES) && (tk->desktop != tb->desktop))
        Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, tk->desktop, 0, 0, 0, 0);

    /* Raise the window.  We can use NET_ACTIVE_WINDOW if the window manager supports it.
     * Otherwise, do it the old way with XMapRaised and XSetInputFocus. */
    if (tb->flags.use_net_active)
        Xclimsg(tk->win, a_NET_ACTIVE_WINDOW, 2, time, 0, 0, 0);
    else
    {
        XMapRaised(xdisplay, tk->win);

        /* There is a race condition between the X server actually executing the XMapRaised and this code executing XSetInputFocus.
         * If the window is not viewable, the XSetInputFocus will fail with BadMatch. */
        XWindowAttributes attr;
        XGetWindowAttributes(xdisplay, tk->win, &attr);
        if (attr.map_state == IsViewable)
            XSetInputFocus(xdisplay, tk->win, RevertToNone, time);
    }

    /* Change viewport if needed. */
    XWindowAttributes xwa;
    XGetWindowAttributes(xdisplay, tk->win, &xwa);
    Xclimsg(tk->win, a_NET_DESKTOP_VIEWPORT, xwa.x, xwa.y, 0, 0, 0);

    task_xtrap_pop();
}

static gboolean task_button_window_do_release_event(GtkWidget *tb, TaskDetails *task,
                                                    guint button, guint32 time)
{
    if (button == 1)
    {
        Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
        /* Left button.
         * If the task is iconified, raise it.
         * If the task is not iconified and has focus, iconify it.
         * If the task is not iconified and does not have focus, raise it. */
        if (task->iconified)
            task_raise_window(PANEL_TASK_BUTTON(tb), task, time);
        else if (task->focused)
            XIconifyWindow(xdisplay, task->win, DefaultScreen(xdisplay));
        else
            task_raise_window(PANEL_TASK_BUTTON(tb), task, time);
    }
    else if (button == 2)
    {
        /* Middle button.  Toggle the shaded state of the window. */
        Xclimsg(task->win, a_NET_WM_STATE,
                a_NET_WM_STATE_TOGGLE,
                a_NET_WM_STATE_SHADED,
                0, 0, 0);
    }
    return TRUE;
}

static void assemble_gui(TaskButton *self)
{
    /* Create a box to contain the application icon and window title. */
    GtkWidget * container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);

    /* Add the image to contain the application icon to the box. */
    gtk_widget_set_halign(self->image, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(self->image, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(container), self->image);

    /* Add the label to contain the window title to the box. */
    gtk_widget_set_halign(self->label, GTK_ALIGN_START);
    gtk_widget_set_valign(self->label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(self->label, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(self->label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(container), self->label);

    /* Add the box to the button. */
    gtk_button_set_child(GTK_BUTTON(self), container);
    gtk_widget_set_visible(self->label, !self->flags.icons_only);
}

static void map_xwindow_animation(GtkWidget *widget, Window win, GtkAllocation *alloc)
{
    /* Tell WM to set iconifying animation the window into the task button */
    if (gtk_widget_get_realized(widget))
    {
        int x, y;
        gulong data[4];

        /* Get the coordinates of the button (absolute, via toplevel). */
        {
            GtkWidget * top = GTK_WIDGET(gtk_widget_get_root(widget));
            GdkSurface * surf;
            Display * dpy;
            Window root;
            gdouble ax = 0, ay = 0;
            int ox = 0, oy = 0;

            if (top == NULL ||
                !gtk_widget_translate_coordinates(widget, top, 0, 0, &ax, &ay))
                return;
            surf = gtk_native_get_surface(GTK_NATIVE(top));
            if (surf == NULL)
                return;
            dpy = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surf));
            XTranslateCoordinates(dpy, gdk_x11_surface_get_xid(surf),
                                  DefaultRootWindow(dpy), 0, 0, &ox, &oy, &root);
            x = ox + (int) ax;
            y = oy + (int) ay;
        }

        /* Send a NET_WM_ICON_GEOMETRY property change on the window. */
        data[0] = x;
        data[1] = y;
        data[2] = alloc->width;
        data[3] = alloc->height;
        XChangeProperty(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), win,
                        XInternAtom(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                                    "_NET_WM_ICON_GEOMETRY", False),
                        XA_CARDINAL, 32, PropModeReplace, (guchar *) &data, 4);
    }
}

/* Get a pixbuf from a pixmap.
 * Originally from libwnck, Copyright (C) 2001 Havoc Pennington. */
static GdkPixbuf * _wnck_gdk_pixbuf_get_from_pixmap(G_GNUC_UNUSED void *screen, Pixmap xpixmap, Window win, int width, int height)
{
  cairo_surface_t *surface;
  GdkPixbuf *pixbuf = NULL;
  Display *xdisplay;
  XWindowAttributes attrs;

  surface = NULL;
  xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

  task_xtrap_push();

  if (!XGetWindowAttributes (xdisplay, win, &attrs))
    goto TRAP_POP;

  if (attrs.depth == 1)
    {
      surface = cairo_xlib_surface_create_for_bitmap (xdisplay,
                                                      xpixmap,
                                                      attrs.screen,
                                                      width,
                                                      height);
    }
  else
    {
      surface = cairo_xlib_surface_create (xdisplay,
                                           xpixmap,
                                           attrs.visual,
                                           width, height);
    }

  pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, width, height);
  cairo_surface_destroy (surface);

TRAP_POP:
  /* The client may have freed its icon pixmap or window mid-read; pixels read
   * through a failed request are garbage, so fall back to the default icon. */
  if (task_xtrap_pop())
    {
      g_debug("task button: icon pixmap vanished while reading it");
      g_clear_object(&pixbuf);
    }

  return pixbuf;
}

/* Apply a mask to a pixbuf.
 * Originally from libwnck, Copyright (C) 2001 Havoc Pennington. */
static GdkPixbuf * apply_mask(GdkPixbuf * pixbuf, GdkPixbuf * mask)
{
    /* Initialize. */
    int w = MIN(gdk_pixbuf_get_width(mask), gdk_pixbuf_get_width(pixbuf));
    int h = MIN(gdk_pixbuf_get_height(mask), gdk_pixbuf_get_height(pixbuf));
    GdkPixbuf * with_alpha = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);
    guchar * dst = gdk_pixbuf_get_pixels(with_alpha);
    guchar * src = gdk_pixbuf_get_pixels(mask);
    int dst_stride = gdk_pixbuf_get_rowstride(with_alpha);
    int src_stride = gdk_pixbuf_get_rowstride(mask);

    /* Loop to do the work. */
    int i;
    for (i = 0; i < h; i += 1)
    {
        int j;
        for (j = 0; j < w; j += 1)
        {
            guchar * s = src + i * src_stride + j * 3;
            guchar * d = dst + i * dst_stride + j * 4;

            /* s[0] == s[1] == s[2], they are 255 if the bit was set, 0 otherwise. */
            d[3] = ((s[0] == 0) ? 0 : 255); /* 0 = transparent, 255 = opaque */
        }
    }

    return with_alpha;
}

/* Get an icon from the window manager for a task, and scale it to a specified size. */
static GdkPixbuf * get_wm_icon(Window task_win, guint required_width,
                               guint required_height, Atom source,
                               Atom * current_source, TaskButton * tb)
{
    /* The result. */
    GdkPixbuf * pixmap = NULL;
    Atom possible_source = None;
    int result = -1;
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    task_xtrap_push();

    if ((source == None) || (source == a_NET_WM_ICON))
    {
        /* The wire property uses 32-bit CARDINALs; Xlib expands each item
         * to a native long in its returned buffer on 64-bit systems. */

        /* Get the window property _NET_WM_ICON, if possible. */
        gulong nitems=0;
        /* 4 MiB on the wire, at most 8 MiB of Xlib storage on LP64. */
        gulong *data=task_icon_read_property(xdisplay,task_win,a_NET_WM_ICON,
                                            XA_CARDINAL,1048576,3,&nitems);
        result=data ? Success : -1;

        /* If the result is usable, extract the icon from it. */
        if (result == Success)
        {
            /* Get the largest icon available, unless there is one that is the desired size. */
            /* FIXME: should we try to find an icon whose size is closest to
             * required_width and required_height to reduce unnecessary resizing? */
            gulong max_w = 0;
            gulong max_h = 0;
            gulong *max_icon=task_icon_select(data,nitems,required_width,required_height,&max_w,&max_h);

            /* If an icon was extracted, convert it to a pixbuf.
             * Its size is max_w and max_h. */
            if (max_icon != NULL)
            {
                /* Allocate enough space for the pixel data. */
                gulong len = max_w * max_h;
                guchar * pixdata = g_new(guchar, len * 4);

                /* Loop to convert the pixel data. */
                guchar * p = pixdata;
                gulong i;
                for (i = 0; i < len; p += 4, i += 1)
                {
                    guint argb = max_icon[i];
                    guint rgba = (argb << 8) | (argb >> 24);
                    p[0] = rgba >> 24;
                    p[1] = (rgba >> 16) & 0xff;
                    p[2] = (rgba >> 8) & 0xff;
                    p[3] = rgba & 0xff;
                }

                /* Initialize a pixmap with the pixel data. */
                pixmap = gdk_pixbuf_new_from_data(
                    pixdata,
                    GDK_COLORSPACE_RGB,
                    TRUE, 8,    /* has_alpha, bits_per_sample */
                    max_w, max_h, max_w * 4,
                    (GdkPixbufDestroyNotify) g_free,
                    NULL);
                possible_source = a_NET_WM_ICON;
            }
            else
                result = -1;

            /* Free the X property data. */
            XFree(data);
        }
    }

    /* No icon available from _NET_WM_ICON.  Next try WM_HINTS, but do not overwrite _NET_WM_ICON. */
    if ((result != Success) && (*current_source != a_NET_WM_ICON)
    && ((source == None) || (source != a_NET_WM_ICON)))
    {
        XWMHints * hints = XGetWMHints(xdisplay, task_win);
        result = (hints != NULL) ? Success : -1;
        Pixmap xpixmap = None;
        Pixmap xmask = None;
        Window win = None;

        if (result == Success)
        {
            /* WM_HINTS is available.  Extract the X pixmap and mask. */
            if ((hints->flags & IconPixmapHint))
                xpixmap = hints->icon_pixmap;
            if ((hints->flags & IconMaskHint))
                xmask = hints->icon_mask;
            XFree(hints);
            if (xpixmap != None)
            {
                result = Success;
                possible_source = XA_WM_HINTS;
            }
            else
                result = -1;
        }

        if (result != Success)
        {
            /* No icon available from _NET_WM_ICON or WM_HINTS.  Next try KWM_WIN_ICON. */
            gulong nitems=0;
            Atom kwin_win_icon_atom = XInternAtom(xdisplay, "KWM_WIN_ICON", False);
            gulong *icons=task_icon_read_property(xdisplay,task_win,kwin_win_icon_atom,
                                                 kwin_win_icon_atom,2,2,&nitems);
            result=icons ? Success : -1;

            /* If the result is usable, extract the X pixmap and mask from it. */
            if (result == Success)
            {
                xpixmap = icons[0];
                xmask = icons[1];
                if (xpixmap != None)
                {
                    result = Success;
                    possible_source = kwin_win_icon_atom;
                }
                else
                    result = -1;
                XFree(icons);
            }
        }

        /* If we have an X pixmap, get its geometry.*/
        unsigned int w, h;
        if (result == Success)
        {
            int unused;
            unsigned int unused_2;
            result = XGetGeometry(
                xdisplay, xpixmap,
                &win, &unused, &unused, &w, &h, &unused_2, &unused_2) ? Success : -1;
            if(result==Success && !task_icon_geometry_allowed(w,h)) result=-1;
        }

        /* If we have an X pixmap and its geometry, convert it to a GDK pixmap. */
        if (result == Success)
        {
            pixmap = _wnck_gdk_pixbuf_get_from_pixmap(NULL, xpixmap, win, w, h);
            result = ((pixmap != NULL) ? Success : -1);
        }

        /* If we have success, see if the result needs to be masked.
         * Failures here are implemented as nonfatal. */
        if ((result == Success) && (xmask != None))
        {
            Window win;
            int unused;
            unsigned int unused_2;
            if (XGetGeometry(
                xdisplay, xmask,
                &win, &unused, &unused, &w, &h, &unused_2, &unused_2) && task_icon_geometry_allowed(w,h))
            {
                /* Convert the X mask to a GDK pixmap. */
                GdkPixbuf * mask = _wnck_gdk_pixbuf_get_from_pixmap(NULL, xmask, win, w, h);
                if (mask != NULL)
                {
                    /* Apply the mask. */
                    GdkPixbuf * masked_pixmap = apply_mask(pixmap, mask);
                    g_object_unref(G_OBJECT(pixmap));
                    g_object_unref(G_OBJECT(mask));
                    pixmap = masked_pixmap;
                }
            }
        }
    }

    task_xtrap_pop();

    /* If we got a pixmap, scale it and return it. */
    if (pixmap == NULL)
        return NULL;
    else
    {
        GdkPixbuf * ret;

        *current_source = possible_source;
        if (tb->flags.disable_taskbar_upscale)
        {
            guint w = gdk_pixbuf_get_width (pixmap);
            guint h = gdk_pixbuf_get_height (pixmap);
            if (w <= required_width || h <= required_height)
                return pixmap;
        }
        ret = gdk_pixbuf_scale_simple(pixmap, required_width, required_height,
                                      GDK_INTERP_BILINEAR);
        g_object_unref(pixmap);
        return ret;
    }
}

/* Update the icon of a task. */
static void _task_update_icon(TaskButton *task, TaskDetails *details, Atom source)
{
    GdkPixbuf *pixbuf = NULL;

    if (source == a_NET_ACTIVE_WINDOW && details != NULL)
        pixbuf = details->icon; /* use cached icon */

    /* Get the icon from the window's hints. */
    if (details != NULL && pixbuf == NULL)
    {
        pixbuf = get_wm_icon(details->win, task->icon_size, task->icon_size,
                             source, &details->image_source, task);
        if (pixbuf)
        {
            /* replace old cached image */
            if (details->icon)
                g_object_unref(details->icon);
            details->icon = g_object_ref_sink(pixbuf);
        }
        else
            /* use cached icon if available */
            pixbuf = details->icon;
    }

    /* If that fails, and we have no other icon yet, return the fallback icon. */
    if ((pixbuf == NULL)
        && ((source == None) || (details->image_source == None)))
    {
        GObject *parent = G_OBJECT(gtk_widget_get_parent(GTK_WIDGET(task)));

        /* Establish the fallback task icon.  This is used when no other icon is available. */
        pixbuf = g_object_get_data(parent, "task-fallback-pixbuf");
        if (pixbuf == NULL)
        {
            pixbuf = gdk_pixbuf_new_from_xpm_data((const char **) icon_xpm);
            if (pixbuf != NULL)
                g_object_set_data_full(parent, "task-fallback-pixbuf",
                                       g_object_ref_sink(pixbuf), g_object_unref);
        }
    }

    if (pixbuf != NULL)
    {
        GdkTexture * tex = gdk_texture_new_for_pixbuf(pixbuf);
        gtk_image_set_from_paintable(GTK_IMAGE(task->image), GDK_PAINTABLE(tex));
        g_object_unref(tex);
    }
}

static gboolean task_update_icon_idle(gpointer user_data)
{
    TaskButton *task;
    GList *l;
    TaskDetails *details;

    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;
    task = user_data;
    task->idle_loader = 0;
    for (l = task->details; l; l = l->next)
    {
        details = l->data;
        if (details->icon == NULL)
            _task_update_icon(task, details, None);
    }
    return FALSE;
}

static void task_update_icon(TaskButton *task, TaskDetails *details, Atom source)
{
    if (source != None || (details && details->icon))
        _task_update_icon(task, details, source);
    else if (task->idle_loader == 0)
        task->idle_loader = g_timeout_add_full(G_PRIORITY_LOW, 20,
                                               task_update_icon_idle,
                                               task, NULL);
}

/* Draw the label and tooltip on a taskbar button. */
static void task_draw_label(TaskButton *tb, gboolean bold_style, gboolean force)
{
    GString *str;
    gboolean old_bold = !!tb->set_bold;

    if (!force && old_bold == bold_style) /* nothing to do */
        return;
    if (tb->flags.icons_only) /* no label to show */
        return;

    tb->set_bold = bold_style;
    str = g_string_sized_new(32);
    if (!tb->visible)
        g_string_append_c(str, '[');
    if (tb->n_visible > 1)
        g_string_append_printf(str, "(%d) ", tb->n_visible);
    if (!tb->same_name || !tb->last_focused || !tb->last_focused->name)
        g_string_append(str, tb->res_class);
    else
        g_string_append(str, tb->last_focused->name);
    if (!tb->visible)
        g_string_append_c(str, ']');

    if (force && tb->flags.tooltips)
        gtk_widget_set_tooltip_text(GTK_WIDGET(tb), str->str);

    luxepanel_draw_label_text(tb->panel, tb->label, str->str, bold_style, 1,
                            tb->flags.flat_button);

    g_string_free(str, TRUE);
}

/* conventional macro */
#define task_redraw_label(b) task_draw_label(b, (b->flags.flat_button && b->entered_state), TRUE)

/* update task->visible, task->n_visible, task->same_name
   also update task->last_focused if it was NULL
   returns TRUE if button's label would need update */
static gboolean task_update_visibility(TaskButton *task)
{
    guint old_n_visible = task->n_visible;
    gboolean old_visible = !!task->visible;
    gboolean old_same_name = !!task->same_name;
    gboolean old_last_focused = (task->last_focused != NULL && task->last_focused->visible);
    GList *l;
    TaskDetails *details, *first_visible = NULL;

    task->same_name = TRUE;
    task->visible = FALSE;
    task->n_visible = 0;
    for (l = task->details; l; l = l->next)
    {
        details = l->data;
        details->visible = task_is_visible(task, details);
        if (!details->visible)
            continue;
        if (details->monitor == task->monitor && !details->iconified)
            /* window is visible on the current desktop */
            task->visible = TRUE;
        /* Compute the visible name.  If all visible windows have the same title, use that.
         * Otherwise, use the class name.  This follows WNCK. */
        if (first_visible == NULL)
            first_visible = details;
        else if (task->same_name
                 && g_strcmp0(first_visible->name, details->name) != 0)
            task->same_name = FALSE;
        task->n_visible++;
        if (task->last_focused == NULL || !task->last_focused->visible)
            task->last_focused = details;
    }
    if (!task->n_visible && old_n_visible)
    {
        /* task button became invisible */
        gtk_widget_set_visible(GTK_WIDGET(task), FALSE);
        return FALSE;
    }
    else if (task->n_visible && !old_n_visible)
        /* task button became visible */
        gtk_widget_set_visible(GTK_WIDGET(task), TRUE);
    return (task->n_visible != old_n_visible || /* n_visible changed */
            (!task->visible) == old_visible || /* visible changed */
            (!task->same_name) == old_same_name || /* visible name changed */
            (task->same_name && !old_last_focused)); /* visible name unavailable */
}

/* -----------------------------------------------------------------------------
 * Class implementation
 */
G_DEFINE_TYPE(TaskButton, task_button, GTK_TYPE_TOGGLE_BUTTON)

static void task_button_finalize(GObject *object)
{
    TaskButton *self = (TaskButton *)object;

    /* free all data */
    g_free(self->res_class);
    if (self->menu_list)
        g_object_remove_weak_pointer(G_OBJECT(self->menu_list),
                                     (void **)&self->menu_list);
    if (self->idle_loader)
        g_source_remove(self->idle_loader);
    g_list_free_full(self->details, (GDestroyNotify)free_task_details);

    G_OBJECT_CLASS(task_button_parent_class)->finalize(object);
}

/* right-click on the button: popup the context (or grouped) menu */
static void task_button_pressed(GtkGestureClick * gesture, gint n_press,
                                gdouble x, gdouble y, TaskButton * tb)
{
    GtkWidget * widget = GTK_WIDGET(tb);
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

    /* Own the pointer sequence before GtkButton's internal gesture claims
     * it, otherwise our release callback never activates the window. */
    if (button >= 1 && button <= 3)
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    if (button == 3) /* Right click */
    {
        GMenu * menu;
        task_button_ensure_actions(gtk_widget_get_parent(widget));
        if (tb->n_visible > 1)
        {
            /* This is grouped-task representative, meaning that there is a class
             * with at least two windows. */
            menu = g_menu_new();
            g_menu_append(menu, _("_Close all windows"), "task.close-all");
            g_object_set_data(G_OBJECT(gtk_widget_get_parent(widget)),
                              "task-button-current", tb);
        }
        else
        {
            /* Not a grouped-task representative, or entered from the grouped-task popup menu. */
            menu = get_task_button_menu(tb, tb->last_focused);
        }
        /* show it */
        luxepanel_show_popup_menu_at_widget(menu, widget);
        if (tb->n_visible > 1)
            g_object_unref(menu); /* transient menu, not shared */
    }
}

/* left/middle click on the button */
static void task_button_released(GtkGestureClick * gesture, gint n_press,
                                 gdouble x, gdouble y, TaskButton * tb)
{
    GtkWidget * widget = GTK_WIDGET(tb);
    TaskButton *tb2 = PANEL_TASK_BUTTON(widget);
    TaskDetails *task;
    GList *l;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    guint32 time = gtk_event_controller_get_current_event_time(GTK_EVENT_CONTROLLER(gesture));

    if (x < 0 || y < 0 || x >= gtk_widget_get_width(widget) ||
        y >= gtk_widget_get_height(widget))
        return; /* A release outside the task button must not activate it. */

    else if (tb2->n_visible > 1)
    {
        /* This is grouped-task representative, meaning that there is a class
         * with at least two windows. */
        if (button == 1) /* Left click */
        {
            GMenu * menu = g_menu_new();
            task_button_ensure_actions(gtk_widget_get_parent(widget));
            g_object_set_data(G_OBJECT(gtk_widget_get_parent(widget)),
                              "task-button-current", tb2);
            if (tb2->menu_list != NULL)
            {
                g_object_remove_weak_pointer(G_OBJECT(tb2->menu_list),
                                             (void **)&tb2->menu_list);
                tb2->menu_list = NULL;
            }
            /* Bring up a popup menu listing all the class members. */
            for (l = tb2->details; l; l = l->next)
            {
                task = l->data;
                if (task->visible)
                {
                    /* The menu item has the name, or the iconified name, and
                     * the icon of the application window. */
                    GMenuItem * item;
                    char * name = task->iconified ? g_strdup_printf("[%s]", task->name) : NULL;
                    item = g_menu_item_new(name ? name : task->name, NULL);
                    g_free(name);
                    g_menu_item_set_action_and_target(item, "task.window", "t",
                                                      (guint64) task->win);
                    if (task->icon)
                    {
                        GIcon * icon = task_menu_icon_from_pixbuf(task->icon);
                        if (icon != NULL)
                        {
                            g_menu_item_set_icon(item, icon);
                            g_object_unref(icon);
                        }
                    }
                    g_menu_append_item(menu, item);
                    g_object_unref(item);
                }
            }
            /* Show the menu.  The popover positions itself near the button. */
            luxepanel_show_popup_menu_at_widget(menu, widget);
            g_object_unref(menu);
            /* keep a weak pointer for flash-state bookkeeping */
            tb2->menu_list = menu;
            g_object_add_weak_pointer(G_OBJECT(tb2->menu_list), (void **)&tb2->menu_list);
        }
    }
    else
    {
        /* Not a grouped-task representative, or entered from the grouped-task popup menu. */
        if (tb2->last_focused != NULL)
            task_button_window_do_release_event(widget, tb2->last_focused, button, time);
    }
}

static void task_button_enter(GtkEventControllerMotion * m, gdouble x, gdouble y, TaskButton * tb)
{
    tb->entered_state = TRUE;
    task_draw_label(tb, tb->flags.flat_button, FALSE);
}

static void task_button_leave(GtkEventControllerMotion * m, TaskButton * tb)
{
    tb->entered_state = FALSE;
    task_draw_label(tb, FALSE, FALSE);
}

static void task_button_scrolled(GtkEventControllerScroll * c,
                                 gdouble dx, gdouble dy, TaskButton * tb)
{
    if (tb->flags.use_mouse_wheel && tb->n_visible == 1 && tb->last_focused != NULL)
    {
        GdkScrollDirection dir = gdk_scroll_event_get_direction(
            gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(c)));
        guint32 time = gtk_event_controller_get_current_event_time(GTK_EVENT_CONTROLLER(c));
        if ((dir == GDK_SCROLL_UP) || (dir == GDK_SCROLL_LEFT))
            task_raise_window(tb, tb->last_focused, time);
        else
        {
            Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
            XIconifyWindow(xdisplay, tb->last_focused->win, DefaultScreen(xdisplay));
        }
    }
}

static void task_button_size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
    TaskButton *tb = PANEL_TASK_BUTTON(widget);
    GList *l;
    GtkAllocation alloc;

    /* Pass it to the GtkToggleButton handler first */
    GTK_WIDGET_CLASS(task_button_parent_class)->size_allocate(widget, width, height, baseline);

    /* Set iconifying animation for all related windows into this button */
    if (gtk_widget_get_realized(widget))
    {
        alloc.x = 0;
        alloc.y = 0;
        alloc.width = width;
        alloc.height = height;
        for (l = tb->details; l; l = l->next)
            map_xwindow_animation(widget, ((TaskDetails *)l->data)->win, &alloc);
    }
}

static void task_button_class_init(TaskButtonClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->finalize = task_button_finalize;
    widget_class->size_allocate = task_button_size_allocate;

    /**
     * Signal TaskButton::menu-built is emitted when GtkMenu is built
     * by TaskButton on its parent widget. Connected callback therefore
     * can add own menu items with handlers.
     */
    signals[MENU_BUILT] = g_signal_new ("menu-built",
                                    G_TYPE_FROM_CLASS(klass),
                                    G_SIGNAL_RUN_FIRST,
                                    G_STRUCT_OFFSET(TaskButtonClass, menu_built),
                                    NULL, NULL,
                                    g_cclosure_marshal_VOID__OBJECT,
                                    G_TYPE_NONE, 1, G_TYPE_MENU);

    /**
     * Signal TaskButton::menu-target-set is emitted when TaskButton
     * activated menu popup against some task. If any items were added
     * in TaskButton::menu-built callback, their visibility should be
     * managed in this callback, or all them will be visible by default.
     */
    signals[MENU_TARGET_SET] = g_signal_new ("menu-target-set",
                                    G_TYPE_FROM_CLASS(klass),
                                    G_SIGNAL_RUN_FIRST,
                                    G_STRUCT_OFFSET(TaskButtonClass, menu_target_set),
                                    NULL, NULL,
                                    g_cclosure_marshal_VOID__ULONG,
                                    G_TYPE_NONE, 1, G_TYPE_ULONG);

    a_NET_WM_STATE_MAXIMIZED_VERT = XInternAtom(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                                                "_NET_WM_STATE_MAXIMIZED_VERT", False);
    a_NET_WM_STATE_MAXIMIZED_HORZ = XInternAtom(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                                                "_NET_WM_STATE_MAXIMIZED_HORZ", False);
}

static void task_button_init(TaskButton *self)
{
    {
        GtkGesture * gesture = gtk_gesture_click_new();
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
        g_signal_connect(gesture, "pressed", G_CALLBACK(task_button_pressed), self);
        g_signal_connect(gesture, "released", G_CALLBACK(task_button_released), self);
        gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(gesture));
    }
    {
        GtkEventController * motion = gtk_event_controller_motion_new();
        g_signal_connect(motion, "enter", G_CALLBACK(task_button_enter), self);
        g_signal_connect(motion, "leave", G_CALLBACK(task_button_leave), self);
        gtk_widget_add_controller(GTK_WIDGET(self), motion);
    }
    {
        GtkEventController * scroll = gtk_event_controller_scroll_new(
            GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
        g_signal_connect(scroll, "scroll", G_CALLBACK(task_button_scrolled), self);
        gtk_widget_add_controller(GTK_WIDGET(self), scroll);
    }
    gtk_widget_set_focusable(GTK_WIDGET(self), FALSE);
}

/* -----------------------------------------------------------------------------
 * Interface functions
 */

/* creates new button and sets rendering options */
TaskButton *task_button_new(Window win, gint desk, gint desks, LuxePanel *panel,
                            const char *res_class, TaskShowFlags flags)
{
    TaskButton *self = g_object_new(PANEL_TYPE_TASK_BUTTON, NULL);
    gtk_widget_add_css_class(GTK_WIDGET(self), "luxe-task-button");
    if (flags.flat_button)
        gtk_widget_add_css_class(GTK_WIDGET(self), "flat");

    /* remember data */
    self->desktop = desk;
    self->n_desktops = desks;
    self->panel = panel;
    self->monitor = panel_get_monitor(panel);
    self->icon_size = panel_get_icon_size(panel);
    if (flags.use_smaller_icons)
        self->icon_size -= 4;
    self->res_class = g_strdup(res_class);
    self->flags = flags;
    /* create empty image and label */
    self->image = gtk_image_new();
    self->label = gtk_label_new(NULL);
    /* append the window and set icon/label by that */
    task_button_add_window(self, win, self->res_class);
    /* and now let assemble all widgets we got */
    assemble_gui(self);
    /* and finally set visibility on it */
    gtk_widget_set_visible(GTK_WIDGET(self), self->n_visible > 0);
    return self;
}

gboolean task_button_has_window(TaskButton *button, Window win)
{
    GList *l;

    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button), FALSE);

    for (l = button->details; l; l = l->next)
        if (((TaskDetails *)l->data)->win == win)
            return TRUE;
    return FALSE;
}

/* removes windows from button, that are missing in list */
void task_button_update_windows_list(TaskButton *button, Window *list, gint n)
{
    GList *l, *next;
    TaskDetails *details;
    gint i;
    gboolean has_removed = FALSE;

    g_return_if_fail(PANEL_IS_TASK_BUTTON(button));

    for (l = button->details; l; )
    {
        next = l->next;
        details = l->data;
        for (i = 0; i < n; i++)
            if (list[i] == details->win)
                break;
        if (i >= n) /* not found, remove details now */
        {
            button->details = g_list_delete_link(button->details, l);
            free_task_details(details);
            if (button->last_focused == details)
                button->last_focused = NULL;
            has_removed = TRUE;
        }
        l = next; /* go next details */
    }
    if (button->details == NULL) /* all windows were deleted */
    {
        task_button_reset_menu(gtk_widget_get_parent(GTK_WIDGET(button)));
        task_button_destroy_widget(button);
    }
    else if (has_removed && task_update_visibility(button))
        task_redraw_label(button);
    // FIXME: test if need to update menu
}

/* returns TRUE if found and updated */
gboolean task_button_window_xprop_changed(TaskButton *button, Window win, Atom atom)
{
    TaskDetails *details;

    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button), FALSE);

    details = task_details_lookup(button, win);
    if (details == NULL)
        return FALSE;

    /* Dispatch on atom. */
    if (atom == a_NET_WM_DESKTOP)
    {
        /* Window changed desktop. */
        task_xtrap_push();
        details->desktop = get_net_wm_desktop(win);
        if (task_xtrap_pop())
            details->desktop = 0;
        details->visible = task_is_visible(button, details);
        if (task_update_visibility(button))
            task_redraw_label(button);
    }
    else if ((atom == XA_WM_NAME) || (atom == a_NET_WM_NAME) || (atom == a_NET_WM_VISIBLE_NAME))
    {
        /* Window changed name. */
        if (task_set_names(details, atom))
            task_redraw_label(button);
    }
    else if (atom == XA_WM_CLASS)
    {
        /* Read the WM_CLASS property. */
        XClassHint ch;
        gchar *res_class;

        ch.res_name = NULL;
        ch.res_class = NULL;
        task_xtrap_push();
        XGetClassHint(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), win, &ch);
        if (task_xtrap_pop())
        {
            /* Dead window: drop any partial result. */
            if (ch.res_name != NULL)
            {
                XFree(ch.res_name);
                ch.res_name = NULL;
            }
            if (ch.res_class != NULL)
            {
                XFree(ch.res_class);
                ch.res_class = NULL;
            }
        }
        if (ch.res_name != NULL)
            XFree(ch.res_name);
        if (ch.res_class != NULL)
        {
            res_class = g_locale_to_utf8(ch.res_class, -1, NULL, NULL, NULL);
            XFree(ch.res_class);
            if (res_class != NULL)
            {
                g_free(button->res_class);
                button->res_class = res_class;
                if (!button->same_name)
                    task_redraw_label(button);
            }
        }
    }
    else if (atom == a_WM_STATE)
    {
        /* Window changed state. */
        task_xtrap_push();
        details->iconified = (get_wm_state(win) == IconicState);
        if (task_xtrap_pop())
            details->iconified = FALSE;
        details->visible = task_is_visible(button, details);
        if (task_update_visibility(button))
            task_redraw_label(button);
    }
    else if (atom == XA_WM_HINTS)
    {
        gboolean has_urgency = details->urgency;

        details->urgency = task_has_urgency(win);
        if (!has_urgency && details->urgency && button->flags.use_urgency_hint)
        {
            /* gained urgency, update the button */
            details->visible = task_is_visible(button, details);
            task_update_visibility(button);
            if (details->visible)
                button->last_focused = details;
            task_redraw_label(button);
        }
        /* Window changed "window manager hints".
         * Some windows set their WM_HINTS icon after mapping. */
        task_update_icon(button, details, atom);
    }
    else if (atom == a_NET_WM_ICON)
    {
        /* Window changed EWMH icon. */
        task_update_icon(button, details, atom);
    }
    /* else
    {
        char *ev_name = XGetAtomName(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), atom);
        g_debug("got event for me: %s", ev_name);
        XFree(ev_name);
    } */

    return TRUE;
}

gboolean task_button_window_focus_changed(TaskButton *button, Window *win)
{
    GList *l;
    TaskDetails *details;
    gboolean res = FALSE;

    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button), FALSE);

    for (l = button->details; l; l = l->next)
    {
        details = l->data;
        if (win && details->win == *win)
        {
            res = TRUE;
            details->focused = TRUE;
            button->last_focused = details;
        }
        else
            details->focused = FALSE;
    }
    if (res)
    {
        /* for no flat buttons we have to reflect focus by button state */
        if (!button->flags.flat_button)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
        /* if focus changed that means button widgets may need update */
        task_update_icon(button, button->last_focused, a_NET_ACTIVE_WINDOW);
        task_redraw_label(button);
        // FIXME: test if need to update menu
    }
    else
    {
        /* if no focus on any button window then button may need style update */
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
        // FIXME: test if need to update menu
    }
    return res;
}

/* update internal data */
gboolean task_button_window_reconfigured(TaskButton *button, Window win)
{
    gint old_mon, new_mon;
    TaskDetails *details;

    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button), FALSE);

    details = task_details_lookup(button, win);
    if (details == NULL)
        return FALSE;

    /* If the same_monitor_only option is set and the window is on a different
       monitor than before, redraw the task button */
    old_mon = details->monitor;
    new_mon = get_window_monitor(details->win);
    details->monitor = new_mon;

    if (button->flags.same_monitor_only
        && (old_mon == button->monitor || new_mon == button->monitor))
    {
        details->visible = task_is_visible(button, details);
        task_update_visibility(button);
        task_redraw_label(button);
        // FIXME: test if need to update menu
    }
    return TRUE;
}

/* updates rendering options */
void task_button_update(TaskButton *button, gint desk, gint desks,
                        gint mon, guint icon_size, TaskShowFlags flags)
{
    gboolean changed = FALSE, changed_icon = FALSE, changed_label = FALSE;

    g_return_if_fail(PANEL_IS_TASK_BUTTON(button));

    if (button->desktop != desk
        || button->monitor != mon
        || button->flags.show_all_desks != flags.show_all_desks
        || button->flags.same_monitor_only != flags.same_monitor_only)
        changed = TRUE;
    if (button->n_desktops != desks)
        task_button_reset_menu(gtk_widget_get_parent(GTK_WIDGET(button)));
    if (button->icon_size != icon_size
        || button->flags.disable_taskbar_upscale != flags.disable_taskbar_upscale)
        changed_icon = TRUE;
    if (button->flags.flat_button != flags.flat_button)
        changed_label = TRUE;
    if (button->flags.icons_only != flags.icons_only)
    {
        changed_label = !flags.icons_only;
        gtk_widget_set_visible(button->label, changed_label);
    }
    if (button->flags.flat_button != flags.flat_button)
    {
        if (flags.flat_button)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
            gtk_widget_add_css_class(GTK_WIDGET(button), "flat");
        }
        else
            gtk_widget_remove_css_class(GTK_WIDGET(button), "flat");
    }
    button->desktop = desk;
    button->n_desktops = desks;
    button->monitor = mon;
    button->icon_size = icon_size;
    button->flags = flags;

    if (changed)
    {
        if (task_update_visibility(button))
            changed_label = TRUE;
        // FIXME: test if need to update menu
    }
    if (changed_label)
        task_redraw_label(button);
    if (changed_icon)
        task_update_icon(button, button->last_focused, None);
}

/* updates state for flashing buttons, including menu list */
void task_button_set_flash_state(TaskButton *button, gboolean state)
{
    gboolean has_flash = FALSE, m_state;
    GList *l;
    TaskDetails *details;

    g_return_if_fail(PANEL_IS_TASK_BUTTON(button));

    for (l = button->details; l; l = l->next)
    {
        details = l->data;
        if (button->flags.use_urgency_hint && details->urgency)
        {
            has_flash = TRUE;
            m_state = state;
        }
        else
            m_state = FALSE;
    }
    /* Set state on the button and redraw. */
    if (button->flags.flat_button)
    {
        if (has_flash || button->has_flash)
        {
            if (!has_flash)
                state = button->entered_state;
            task_draw_label(button, state, FALSE); /* we have to redraw bold text state */
        }
    }
    button->has_flash = has_flash;
}

/* adds task only if it's the same class */
gboolean task_button_add_window(TaskButton *button, Window win, const char *cl)
{
    TaskDetails *details;
    GtkAllocation alloc;

    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button), FALSE);

    if (g_strcmp0(button->res_class, cl) != 0)
        return FALSE;
    /* fetch task details */
    details = task_details_for_window(button, win);
    button->details = g_list_append(button->details, details);
    /* redraw label on the button if need */
    if (details->visible)
    {
        if (task_update_visibility(button))
            task_redraw_label(button);
        // FIXME: test if need to update menu
    }
    gtk_widget_get_allocation(GTK_WIDGET(button), &alloc);
    map_xwindow_animation(GTK_WIDGET(button), win, &alloc);
    return TRUE;
}

gboolean task_button_drop_window(TaskButton *button, Window win, gboolean leave_last)
{
    GList *l;
    TaskDetails *details;
    gboolean was_last_focused;

    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button), FALSE);

    if (leave_last && g_list_length(button->details) <= 1)
        return FALSE;
    for (l = button->details; l; l = l->next)
        if (((TaskDetails *)l->data)->win == win)
            break;
    if (l == NULL) /* not our window */
        return FALSE;
    if (g_list_length(button->details) == 1)
    {
        /* this was last window, destroy the button */
        task_button_destroy_widget(button);
        return TRUE;
    }
    details = l->data;
    button->details = g_list_delete_link(button->details, l);
    was_last_focused = (button->last_focused == details);
    if (was_last_focused)
        button->last_focused = NULL;
    if (details->visible)
    {
        task_update_visibility(button);
        if (was_last_focused)
            task_update_icon(button, button->last_focused, None);
        task_redraw_label(button);
        // FIXME: test if need to update menu
    }
    /* bug SF#823: menu may be still opened for this window (menu items
     * are model-driven now, nothing to destroy individually) */
    free_task_details(details);
    return TRUE;
}

/* leaves only last task in button and returns a copy containing rest */
TaskButton *task_button_split(TaskButton *button)
{
    TaskButton *sibling;
    GList *llast;

    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button), NULL);

    if (g_list_length(button->details) < 2)
        return NULL;
    sibling = g_object_new(PANEL_TYPE_TASK_BUTTON, NULL);
    gtk_widget_add_css_class(GTK_WIDGET(sibling), "luxe-task-button");
    if (button->flags.flat_button)
        gtk_widget_add_css_class(GTK_WIDGET(sibling), "flat");
    sibling->res_class = g_strdup(button->res_class);
    sibling->panel = button->panel;
    sibling->image = gtk_image_new();
    sibling->label = gtk_label_new(NULL);
    llast = g_list_last(button->details);
    sibling->details = g_list_remove_link(button->details, llast);
    button->details = llast;
    if (button->last_focused != llast->data)
    {
        /* focused item migrated to sibling */
        sibling->last_focused = button->last_focused;
        button->last_focused = NULL;
    }
    sibling->desktop = button->desktop;
    sibling->n_desktops = button->n_desktops;
    sibling->monitor = button->monitor;
    sibling->icon_size = button->icon_size;
    sibling->flags = button->flags;
    task_update_visibility(button);
    task_update_visibility(sibling);
    /* force redraw icons and labels on buttons */
    if (button->n_visible > 0)
    {
        task_update_icon(button, button->last_focused, None);
        task_draw_label(button, FALSE, TRUE);
    }
    if (sibling->n_visible > 0)
    {
        task_update_icon(sibling, button->last_focused, None);
        task_draw_label(sibling, FALSE, TRUE);
    }
    assemble_gui(sibling);
    // FIXME: test if need to update menu
    return sibling;
}

/* merges buttons if they are the same class */
gboolean task_button_merge(TaskButton *button, TaskButton *sibling)
{
    g_return_val_if_fail(PANEL_IS_TASK_BUTTON(button) && PANEL_IS_TASK_BUTTON(sibling), FALSE);

    if (g_strcmp0(button->res_class, sibling->res_class) != 0)
        return FALSE;
    /* move data lists from sibling appending to button */
    button->details = g_list_concat(button->details, sibling->details);
    sibling->details = NULL;
    /* update visibility */
    button->n_visible += sibling->n_visible;
    button->visible = (button->visible | sibling->visible);
    /* eliminate sibling widget now */
    task_button_destroy_widget(sibling);
    /* redraw label on the button */
    task_redraw_label(button);
    // FIXME: test if need to update menu
    return TRUE;
}

/* single-instance-menu management, should be called on button parent widget */
void task_button_reset_menu(GtkWidget *parent)
{
    gpointer menu = g_object_get_data(G_OBJECT(parent), "task-button-menu");

    if (menu)
    {
        /* the GMenu is simply dropped; popovers close on their own */
        g_object_set_data(G_OBJECT(parent), "task-button-menu", NULL);
    }
    g_object_set_data(G_OBJECT(parent), "task-button-current", NULL);
}

/* request for a minimized window to raise */
void task_button_raise_window(TaskButton *button, guint32 time)
{
    if (!PANEL_IS_TASK_BUTTON(button))
        return;
    if (button->details)
        task_raise_window(button, button->details->data, time);
}
