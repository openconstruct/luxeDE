/*
 * Copyright (C) 2006-2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell
 *               2006 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2013 Henry Gebhardt <hsggebhardt@gmail.com>
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

#include <stdlib.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <libfm/fm.h>
#include <string.h>

#include "misc.h"
#include "plugin.h"

#define DIRMENU_MAX_SCANNED_ENTRIES 4096
#define DIRMENU_MAX_DIRECTORY_ROWS 256

/* Temporary for sort of directory names. */
typedef struct _directory_name {
    char * directory_name;
    char * directory_name_collate_key;
    char * path;
} DirectoryName;

typedef struct {
    GPtrArray *directories;
    gboolean truncated;
    char *error_message;
} DirMenuScan;

typedef struct {
    gint refs;
    GWeakRef list;
    gboolean alive;
    gboolean running;
    guint generation;
    char *pending_path;
    GCancellable *cancellable;
} DirMenuScanState;

static void directory_name_free(gpointer data)
{
    DirectoryName *item=data;
    g_free(item->directory_name);g_free(item->directory_name_collate_key);g_free(item->path);g_free(item);
}
static gint directory_name_compare(gconstpointer a,gconstpointer b)
{
    const DirectoryName *left=*(DirectoryName *const *)a,*right=*(DirectoryName *const *)b;
    int order=strcmp(left->directory_name_collate_key,right->directory_name_collate_key);
    return order ? order : strcmp(left->path,right->path);
}

/* Private context for directory menu plugin. */
typedef struct {
    LuxePanel * panel; /* The panel and settings are required to apply config */
    config_setting_t * settings;
    char * image;			/* Icon for top level widget */
    char * path;			/* Top level path for widget */
    char * name;			/* User's label for widget */
    GtkWidget * popover;		/* The menu popover */
    GtkWidget * list;			/* GtkListBox with items */
    char * curpath;			/* Directory currently displayed */
    DirMenuScanState *scan_state;
} DirMenuPlugin;

static void dirmenu_destructor(gpointer user_data);
static gboolean dirmenu_apply_configuration(gpointer user_data);

/* row kinds stored in the "kind" qdata of list rows */
#define DIRMENU_ROW_UP        1
#define DIRMENU_ROW_DIR       2
#define DIRMENU_ROW_OPEN      3
#define DIRMENU_ROW_TERMINAL  4

/* row label quark */
static GQuark dirmenu_row_quark = 0;
static GQuark dirmenu_kind_quark = 0;
static GQuark dirmenu_path_quark = 0;

/* Navigate the popover to the specified directory. */
static void dirmenu_navigate(DirMenuPlugin * dm, const char * path);
static GtkWidget *dirmenu_make_row(int kind, const char *icon, const char *label, const char *path);
static void dirmenu_add_separator(GtkWidget *list);

static void dirmenu_scan_free(DirMenuScan *scan)
{
    if (scan == NULL) return;
    g_clear_pointer(&scan->directories, g_ptr_array_unref);
    g_free(scan->error_message);
    g_free(scan);
}

static void dirmenu_scan_worker(GTask *task, gpointer source, gpointer task_data,
                                GCancellable *cancellable)
{
    const char *path = task_data;
    DirMenuScan *scan = g_new0(DirMenuScan, 1);
    GError *error = NULL;
    GDir *dir;
    guint scanned = 0;
    (void)source;
    scan->directories = g_ptr_array_new_with_free_func(directory_name_free);
    dir = g_dir_open(path, 0, &error);
    if (dir == NULL) {
        scan->error_message = g_strdup(error != NULL ? error->message : "Unable to read directory");
        g_clear_error(&error);
    } else {
        const char *name;
        while (!g_cancellable_is_cancelled(cancellable) &&
               (name = g_dir_read_name(dir)) != NULL) {
            if (++scanned > DIRMENU_MAX_SCANNED_ENTRIES) {
                scan->truncated = TRUE;
                break;
            }
            if (name[0] == '.') continue;
            char *full = g_build_filename(path, name, NULL);
            if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
                if (scan->directories->len >= DIRMENU_MAX_DIRECTORY_ROWS) {
                    scan->truncated = TRUE;
                    g_free(full);
                    break;
                }
                DirectoryName *item = g_new0(DirectoryName, 1);
                item->directory_name = g_filename_display_name(name);
                item->directory_name_collate_key = g_utf8_collate_key(item->directory_name, -1);
                item->path = full;
                g_ptr_array_add(scan->directories, item);
            } else {
                g_free(full);
            }
        }
        g_dir_close(dir);
        g_ptr_array_sort(scan->directories, directory_name_compare);
    }
    if (g_cancellable_is_cancelled(cancellable)) {
        dirmenu_scan_free(scan);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                "Directory scan was cancelled");
    } else {
        g_task_return_pointer(task, scan, (GDestroyNotify)dirmenu_scan_free);
    }
}

typedef struct {
    DirMenuScanState *state;
    char *path;
    guint generation;
} DirMenuScanRequest;

static DirMenuScanState *dirmenu_scan_state_ref(DirMenuScanState *state)
{
    g_atomic_int_inc(&state->refs);
    return state;
}

static void dirmenu_scan_state_unref(DirMenuScanState *state)
{
    if (!g_atomic_int_dec_and_test(&state->refs)) return;
    g_weak_ref_clear(&state->list);
    g_clear_object(&state->cancellable);
    g_free(state->pending_path);
    g_free(state);
}

static DirMenuScanState *dirmenu_scan_state_new(GtkWidget *list)
{
    DirMenuScanState *state = g_new0(DirMenuScanState, 1);
    state->refs = 1;
    state->alive = TRUE;
    g_weak_ref_init(&state->list, G_OBJECT(list));
    return state;
}

static void dirmenu_scan_request_free(DirMenuScanRequest *request)
{
    dirmenu_scan_state_unref(request->state);
    g_free(request->path);
    g_free(request);
}

static void dirmenu_scan_start(DirMenuScanState *state);

static void dirmenu_scan_complete(GObject *source, GAsyncResult *result, gpointer user_data)
{
    DirMenuScanRequest *request = user_data;
    DirMenuScanState *state = request->state;
    GError *error = NULL;
    DirMenuScan *scan = g_task_propagate_pointer(G_TASK(result), &error);
    GtkWidget *list = g_weak_ref_get(&state->list);
    (void)source;

    if (list != NULL && state->alive && request->generation == state->generation && scan != NULL) {
        if (scan->error_message != NULL)
            gtk_list_box_append(GTK_LIST_BOX(list), dirmenu_make_row(0, NULL, _("Unable to read directory"), NULL));
        for (guint i = 0; i < scan->directories->len; i++) {
            DirectoryName *item = g_ptr_array_index(scan->directories, i);
            gtk_list_box_append(GTK_LIST_BOX(list), dirmenu_make_row(DIRMENU_ROW_DIR, "folder", item->directory_name, item->path));
        }
        if (scan->truncated)
            gtk_list_box_append(GTK_LIST_BOX(list), dirmenu_make_row(0, NULL, _("Some entries are omitted because this directory is large."), NULL));
        dirmenu_add_separator(list);
        gtk_list_box_append(GTK_LIST_BOX(list), dirmenu_make_row(DIRMENU_ROW_OPEN, "folder-open", _("Open"), NULL));
        gtk_list_box_append(GTK_LIST_BOX(list), dirmenu_make_row(DIRMENU_ROW_TERMINAL, "utilities-terminal", _("Open in _Terminal"), NULL));
        luxepanel_plugin_show_contents(list);
    }
    g_clear_object(&list);
    dirmenu_scan_free(scan);
    g_clear_error(&error);
    state->running = FALSE;
    g_clear_object(&state->cancellable);
    if (state->alive && state->pending_path != NULL)
        dirmenu_scan_start(state);
    dirmenu_scan_request_free(request);
}

static void dirmenu_scan_start(DirMenuScanState *state)
{
    DirMenuScanRequest *request;
    GTask *task;
    char *path;

    if (!state->alive || state->running || state->pending_path == NULL) return;
    path = g_steal_pointer(&state->pending_path);
    state->running = TRUE;
    state->cancellable = g_cancellable_new();
    request = g_new0(DirMenuScanRequest, 1);
    request->state = dirmenu_scan_state_ref(state);
    request->path = g_strdup(path);
    request->generation = state->generation;
    task = g_task_new(NULL, state->cancellable, dirmenu_scan_complete, request);
    g_task_set_task_data(task, path, g_free);
    g_task_run_in_thread(task, dirmenu_scan_worker);
    g_object_unref(task);
}

/* Remove all rows from the list. */
static void dirmenu_clear_rows(DirMenuPlugin * dm)
{
    GtkWidget * child;

    if (dm->list == NULL)
        return;
    while ((child = gtk_widget_get_first_child(dm->list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(dm->list), child);
}

/* Directory rows are per-open state: drop them when the popover closes so
 * a long-lived panel does not retain them between opens (rebuilt on show). */
static void dirmenu_popover_closed(GtkPopover * pop, DirMenuPlugin * dm)
{
    (void)pop;
    dm->scan_state->generation++;
    g_clear_pointer(&dm->scan_state->pending_path, g_free);
    if (dm->scan_state->cancellable != NULL)
        g_cancellable_cancel(dm->scan_state->cancellable);
    dirmenu_clear_rows(dm);
}

/* Create a row widget with an optional icon and label. */
static GtkWidget * dirmenu_make_row(int kind,
                                    const char * icon, const char * label,
                                    const char * path)
{
    GtkWidget * row = gtk_list_box_row_new();
    GtkWidget * box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    if (dirmenu_row_quark == 0)
    {
        dirmenu_row_quark = g_quark_from_static_string("DirMenuRow");
        dirmenu_kind_quark = g_quark_from_static_string("DirMenuKind");
        dirmenu_path_quark = g_quark_from_static_string("DirMenuPath");
    }

    g_object_set_qdata(G_OBJECT(row), dirmenu_kind_quark, GINT_TO_POINTER(kind));
    if (path != NULL)
        g_object_set_data_full(G_OBJECT(row), "dirmenu-path", g_strdup(path), g_free);

    if (kind == DIRMENU_ROW_DIR)
        gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name(icon));
    if (label != NULL)
    {
        GtkWidget * lbl = gtk_label_new(label);

        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(lbl, TRUE);
        gtk_box_append(GTK_BOX(box), lbl);
    }
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 2);
    gtk_widget_set_margin_bottom(box, 2);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    return row;
}

/* Add a separator row. */
static void dirmenu_add_separator(GtkWidget *list)
{
    GtkWidget * row = gtk_list_box_row_new();

    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row),
                               gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_list_box_append(GTK_LIST_BOX(list), row);
}

/* Populate the list with the subdirectories of the current path. */
static void dirmenu_populate(DirMenuPlugin *dm)
{
    const char *path = dm->curpath != NULL ? dm->curpath : dm->path;
    DirMenuScanState *state = dm->scan_state;
    state->generation++;
    g_free(state->pending_path);
    state->pending_path = g_strdup(path);
    if (state->cancellable != NULL)
        g_cancellable_cancel(state->cancellable);
    dirmenu_clear_rows(dm);
    if (g_strcmp0(path, dm->path) != 0)
        gtk_list_box_append(GTK_LIST_BOX(dm->list),
                            dirmenu_make_row(DIRMENU_ROW_UP, NULL, "..", NULL));

    dirmenu_scan_start(state);
}

/* Navigate the popover to the specified directory. */
static void dirmenu_navigate(DirMenuPlugin * dm, const char * path)
{
    g_free(dm->curpath);
    dm->curpath = g_strdup(path);
    dirmenu_populate(dm);
}

/* Handler for "row-activated" on the list. */
static void dirmenu_row_activated(GtkListBox * list, GtkListBoxRow * row,
                                  DirMenuPlugin * dm)
{
    int kind;
    const char * path;

    (void)list;
    if (row == NULL)
        return;
    kind = GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(row), dirmenu_kind_quark));
    path = (const char *)g_object_get_data(G_OBJECT(row), "dirmenu-path");

    switch (kind)
    {
    case DIRMENU_ROW_UP:
        {
            char * parent = g_path_get_dirname(dm->curpath);
            dirmenu_navigate(dm, parent);
            g_free(parent);
        }
        break;

    case DIRMENU_ROW_DIR:
        dirmenu_navigate(dm, path);
        break;

    case DIRMENU_ROW_OPEN:
        {
            FmPath * fmpath = fm_path_new_for_str(dm->curpath);
            gtk_popover_popdown(GTK_POPOVER(dm->popover));
            luxepanel_launch_path(dm->panel, fmpath);
            fm_path_unref(fmpath);
        }
        break;

    case DIRMENU_ROW_TERMINAL:
        gtk_popover_popdown(GTK_POPOVER(dm->popover));
        fm_terminal_launch(dm->curpath, NULL);
        break;

    default:
        break;
    }
}

/* Create the popover used as the directory menu. */
static void dirmenu_create_popover(DirMenuPlugin * dm, GtkWidget * widget)
{
    GtkWidget * scroll;

    dm->list = gtk_list_box_new();
    dm->scan_state = dirmenu_scan_state_new(dm->list);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(dm->list), GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(dm->list), TRUE);
    g_signal_connect(dm->list, "row-activated", G_CALLBACK(dirmenu_row_activated), dm);

    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 350);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), dm->list);

    dm->popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(dm->popover), scroll);
    gtk_widget_set_size_request(dm->popover, 280, -1);
    g_signal_connect(dm->popover, "closed", G_CALLBACK(dirmenu_popover_closed), dm);
    gtk_widget_set_parent(dm->popover, widget);
}

/* Show the directory menu. */
static void dirmenu_show_menu(GtkWidget * widget, DirMenuPlugin * dm)
{
    /* Reset to the top level path. */
    g_free(dm->curpath);
    dm->curpath = g_strdup(dm->path);
    dirmenu_populate(dm);

    gtk_popover_popup(GTK_POPOVER(dm->popover));
}

/* Handler for "button-press-event" on top level widget. */
static gboolean dirmenu_button_press_event(GtkWidget * widget, GdkEvent * event, LuxePanel * p)
{
    DirMenuPlugin * dm = luxepanel_plugin_get_data(widget);

    (void)p;
    if (gdk_event_get_event_type(event) == GDK_BUTTON_PRESS &&
        gdk_button_event_get_button(event) == 1)
    {
        dirmenu_show_menu(widget, dm);
        return TRUE;
    }
    return FALSE;
}

/* Handler for middle-button release on the plugin button. */
static void dirmenu_middle_released(GtkGestureClick * gesture, int n_press,
                                    double x, double y, DirMenuPlugin * dm)
{
    (void)n_press; (void)x; (void)y;
    if (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture)) == 2)
        fm_terminal_launch(dm->path, NULL);
}

/* Plugin constructor. */
static GtkWidget *dirmenu_constructor(LuxePanel *panel, config_setting_t *settings)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    DirMenuPlugin * dm = g_new0(DirMenuPlugin, 1);
    GtkWidget * p;
    const char *str;

    /* Load parameters from the configuration file. */
    if (config_setting_lookup_string(settings, "image", &str))
        dm->image = g_strdup(str);
    if (config_setting_lookup_string(settings, "path", &str))
        dm->path = expand_tilda(str);
    else
        dm->path = g_strdup(fm_get_home_dir());
    if (config_setting_lookup_string(settings, "name", &str))
        dm->name = g_strdup(str);

    /* Save construction pointers */
    dm->panel = panel;
    dm->settings = settings;

    /* Allocate top level widget and set into Plugin widget pointer.
     * It is not known why, but the button text will not draw if it is edited from empty to non-empty
     * unless this strategy of initializing it with a non-empty value first is followed. */
    p = luxepanel_button_new_for_icon(panel,
                            ((dm->image != NULL) ? dm->image : "file-manager"),
                            NULL, "Temp");
    luxepanel_plugin_set_data(p, dm, dirmenu_destructor);

    /* Create the popover menu and attach it to the button. */
    dirmenu_create_popover(dm, p);

    /* Middle-button release opens a terminal. */
    {
        GtkGestureClick * click = GTK_GESTURE_CLICK(gtk_gesture_click_new());

        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        g_signal_connect(click, "released", G_CALLBACK(dirmenu_middle_released), dm);
        gtk_widget_add_controller(p, GTK_EVENT_CONTROLLER(click));
    }

    /* Initialize the widget. */
    dirmenu_apply_configuration(p);

    /* Show the widget and return. */
    return p;
}

/* Plugin destructor. */
static void dirmenu_destructor(gpointer user_data)
{
    DirMenuPlugin * dm = (DirMenuPlugin *)user_data;

    dm->scan_state->alive = FALSE;
    g_clear_pointer(&dm->scan_state->pending_path, g_free);
    if (dm->scan_state->cancellable != NULL)
        g_cancellable_cancel(dm->scan_state->cancellable);
    dirmenu_clear_rows(dm);
    if (dm->popover != NULL && gtk_widget_get_parent(dm->popover) != NULL)
        gtk_widget_unparent(dm->popover);
    dirmenu_scan_state_unref(dm->scan_state);
    g_free(dm->curpath);

    /* Deallocate all memory. */
    g_free(dm->image);
    g_free(dm->path);
    g_free(dm->name);
    g_free(dm);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static gboolean dirmenu_apply_configuration(gpointer user_data)
{
    GtkWidget * p = user_data;
    DirMenuPlugin * dm = luxepanel_plugin_get_data(p);
    char * path = dm->path;

    /* Normalize path */
    if (path == NULL)
        dm->path = g_strdup(fm_get_home_dir());
    else if (path[0] == '~')
    {
        dm->path = expand_tilda(path);
        g_free(path);
    }

    /* Save configuration */
    config_group_set_string(dm->settings, "path", dm->path);
    config_group_set_string(dm->settings, "name", dm->name);
    config_group_set_string(dm->settings, "image", dm->image);

    luxepanel_button_set_icon(p, ((dm->image != NULL) ? dm->image : "file-manager"), -1);
    luxepanel_button_set_label(p, dm->name);

    gtk_widget_set_tooltip_text(p, dm->path);
    return FALSE;
}

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *dirmenu_configure(LuxePanel *panel, GtkWidget *p)
{
    DirMenuPlugin * dm = luxepanel_plugin_get_data(p);
    return luxepanel_generic_config_dlg(_("Directory Menu"),
        panel, dirmenu_apply_configuration, p,
        _("Directory"), &dm->path, CONF_TYPE_DIRECTORY_ENTRY,
        _("Label"), &dm->name, CONF_TYPE_STR,
        _("Icon"), &dm->image, CONF_TYPE_FILE_ENTRY,
        NULL);
}

/* Plugin descriptor. */
LuxePanelPluginInit luxepanel_static_plugin_dirmenu = {
    .name = N_("Directory Menu"),
    .description = N_("Browse directory tree via menu (Author = PCMan)"),

    .new_instance = dirmenu_constructor,
    .config = dirmenu_configure,
    .button_press_event = dirmenu_button_press_event
};
