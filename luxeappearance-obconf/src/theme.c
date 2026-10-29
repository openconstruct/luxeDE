/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   theme.h for ObConf, the configuration tool for Openbox
   Copyright (c) 2003-2007   Dana Jansens
   Copyright (c) 2003        Tim Riley

   Copyright (C) 2010        Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
   Copyright (C) 2026        Jerry Howell

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

/* This file is part of ObConf. It's taken by Hong Jen Yee on
 * 2010-08-07 and some modifications were done to make it a loadable
 * module of LuxeAppearance. */

#include "main.h"
#include "tree.h"
#include "preview_update.h"
#include <glib/gi18n.h>
#include "archive.h"
#include "theme.h"
#include "preview.h"
#include <string.h>
#include <sys/stat.h>

static gboolean mapping = FALSE;

static GList *themes;
static GtkListStore *theme_store;
static GCancellable *theme_scan_cancel;
static guint theme_scan_generation;
/* Directory that currently holds the selected theme; used to pin the
 * file-chooser dialogs to a sensible starting folder. */
static gchar *theme_current_dir;

static void free_theme_list(void)
{
    GList *it;
    for (it = themes; it; it = g_list_next(it))
        g_free(it->data);
    g_list_free(themes);
    themes = NULL;
}

static gboolean valid_theme_name_for_ui(const gchar *name)
{
    const gchar *p;

    if (!name || !*name || strchr(name, '/') ||
        g_strcmp0(name, ".") == 0 || g_strcmp0(name, "..") == 0 ||
        !g_utf8_validate(name, -1, NULL))
        return FALSE;
    for (p = name; *p; ++p) {
        if ((guchar)*p < 0x20 || *p == 0x7f)
            return FALSE;
    }
    return TRUE;
}

static gint compare_theme_names(gconstpointer a, gconstpointer b)
{
    return g_ascii_strcasecmp((const gchar *)a, (const gchar *)b);
}

void theme_cleanup(void)
{
    ++theme_scan_generation;
    if (theme_scan_cancel)
        g_cancellable_cancel(theme_scan_cancel);
    g_clear_object(&theme_scan_cancel);
    free_theme_list();
    g_clear_pointer(&theme_current_dir, g_free);
    theme_store = NULL;
}

/* Forwarded */
static void on_theme_names_selection_changed(GtkTreeSelection *sel,
                                             gpointer data);
void on_install_theme_clicked(GtkButton *w, gpointer data);
void on_theme_archive_clicked(GtkButton *w, gpointer data);

/* End Forwarded */

/* Adw startup activates the xdg-desktop-portal (Settings portal), which
 * flips the file dialogs to the out-of-process portal backend; without
 * an explicit folder that backend opens in its own cwd instead of ours.
 * Always pin the dialogs: the current theme's directory when known,
 * falling back to the user themes dir. */
static void set_initial_theme_folder(GtkFileDialog *dialog)
{
    GFile *folder;

    if (theme_current_dir)
        folder = g_file_new_for_path(theme_current_dir);
    else
        folder = g_file_new_build_filename(g_get_home_dir(), ".themes", NULL);
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);
}

void theme_setup_tab(void)
{
    GtkCellRenderer *render;
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;
    GtkWidget *w;

    mapping = TRUE;

    w = get_widget("theme_names");

    /* No interactive search: its popover trips the
     * gtk_css_node_insert_after assertion (upstream GTK issue
     * GNOME/gtk#4884) on keypress while the view is focused. */
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(w), FALSE);

    /* widget setup */
    theme_store = gtk_list_store_new(1, /* the theme name */ G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(w), GTK_TREE_MODEL(theme_store));

    preview_update_set_tree_view(GTK_TREE_VIEW(w), theme_store);

    g_object_unref (theme_store);

    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(w)),
                                GTK_SELECTION_SINGLE);

    /* text column for the names */
    render = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        ("Name", render, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(w), column);

    /* setup the selection handler */
    select = gtk_tree_view_get_selection(GTK_TREE_VIEW (w));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_SINGLE);
    g_signal_connect (G_OBJECT(select), "changed",
                      G_CALLBACK(on_theme_names_selection_changed),
                      NULL);

    mapping = FALSE;
}

static void on_theme_names_selection_changed(GtkTreeSelection *sel,
                                             gpointer data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    g_autofree gchar *name = NULL;

    if (mapping) return;

    (void)data;
    if(gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &name, -1);
    }

    if(name)
      tree_set_string("theme/name", name);

    preview_update_all();
}

typedef struct {
    GWeakRef window;
} ThemeOperation;

static ThemeOperation *theme_operation_new(GtkWidget *window)
{
    ThemeOperation *operation = g_new0(ThemeOperation, 1);
    g_weak_ref_init(&operation->window, window ? G_OBJECT(window) : NULL);
    return operation;
}

static gboolean theme_operation_current(ThemeOperation *operation)
{
    g_autoptr(GObject) window = g_weak_ref_get(&operation->window);
    return window && window == G_OBJECT(mainwin) && xml_i;
}

static void theme_operation_free(ThemeOperation *operation)
{
    g_weak_ref_clear(&operation->window);
    g_free(operation);
}

static void on_install_theme_response(GObject *source, GAsyncResult *result,
                                      gpointer data)
{
    ThemeOperation *operation = data;
    {
        GError *error = NULL;
        GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source),
                                                  result, &error);
        if (file) {
            gchar *path = g_file_get_path(file);
            if (path && theme_operation_current(operation))
                theme_install(path);
            g_free(path);
            g_object_unref(file);
        }
        if (error && theme_operation_current(operation) && !g_error_matches(error, GTK_DIALOG_ERROR,
                                       GTK_DIALOG_ERROR_DISMISSED))
            g_warning("theme install dialog failed: %s", error->message);
        g_clear_error(&error);
    }
    theme_operation_free(operation);
}

void on_install_theme_clicked(GtkButton *w, gpointer data)
{
    GtkFileDialog *d;
    GtkFileFilter *filter;

    (void)w; (void)data;
    if (!mainwin || !xml_i) return;

    d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, _("Choose an Openbox theme"));
    set_initial_theme_folder(d);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Openbox theme archives"));
    gtk_file_filter_add_pattern(filter, "*.obt");
    //gtk_file_filter_add_pattern(filter, "*.tgz");
    //gtk_file_filter_add_pattern(filter, "*.tar.gz");
    gtk_file_dialog_set_default_filter(d, filter);
    g_object_unref(filter);

    gtk_file_dialog_open(d, GTK_WINDOW(mainwin), NULL,
                         on_install_theme_response, theme_operation_new(mainwin));
    g_object_unref(d);
}

static void on_theme_install_finished(GObject *source, GAsyncResult *result,
                                     gpointer data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gchar *name = NULL;
    (void)source;
    ThemeOperation *operation = data;
    name = archive_install_finish(result, &error);
    gboolean current = theme_operation_current(operation);
    theme_operation_free(operation);
    if (!current) return; /* The originating editor is no longer current. */
    if (!name) {
        obconf_error(error ? error->message : _("The theme could not be installed."), FALSE);
        return;
    }
    tree_set_string("theme/name", name);
    theme_load_all();
}

static void on_theme_archive_response(GObject *source, GAsyncResult *result,
                                      gpointer data)
{
    ThemeOperation *operation = data;
    {
        GError *error = NULL;
        GFile *file = gtk_file_dialog_select_folder_finish(
            GTK_FILE_DIALOG(source), result, &error);
        if (file) {
            gchar *path = g_file_get_path(file);
            if (path && theme_operation_current(operation))
                archive_create(path);
            g_free(path);
            g_object_unref(file);
        }
        if (error && theme_operation_current(operation) && !g_error_matches(error, GTK_DIALOG_ERROR,
                                       GTK_DIALOG_ERROR_DISMISSED))
            g_warning("theme archive dialog failed: %s", error->message);
        g_clear_error(&error);
    }
    theme_operation_free(operation);
}

void on_theme_archive_clicked(GtkButton *w, gpointer data)
{
    GtkFileDialog *d;

    (void)w; (void)data;
    if (!mainwin || !xml_i) return;

    d = gtk_file_dialog_new();
    gtk_file_dialog_set_title(d, _("Choose an Openbox theme"));
    set_initial_theme_folder(d);

    gtk_file_dialog_select_folder(d, GTK_WINDOW(mainwin), NULL,
                                  on_theme_archive_response, theme_operation_new(mainwin));
    g_object_unref(d);
}

void theme_install(const gchar *path)
{
    if (!path || !mainwin || !xml_i)
        return;
    archive_install_async(path, on_theme_install_finished, theme_operation_new(mainwin));
}

typedef struct {
    GPtrArray *roots;
    gchar *selected;
    guint generation;
    GWeakRef window;
} ThemeScan;

static void theme_scan_free(ThemeScan *scan)
{
    g_ptr_array_unref(scan->roots);
    g_free(scan->selected);
    g_weak_ref_clear(&scan->window);
    g_free(scan);
}

static void scan_theme_root(const gchar *root, GPtrArray *names,
                            GCancellable *cancellable)
{
    GDir *dir = g_dir_open(root, 0, NULL);
    const gchar *name;
    guint count = 0;
    if (!dir) return;
    while (count < 4096 && (name = g_dir_read_name(dir))) {
        struct stat theme_st, ob_st, merc_st;
        g_autofree gchar *theme = g_build_filename(root, name, NULL);
        g_autofree gchar *openbox = g_build_filename(theme, "openbox-3", NULL);
        g_autofree gchar *themerc = g_build_filename(openbox, "themerc", NULL);
        if ((count++ & 63u) == 0 && g_cancellable_is_cancelled(cancellable)) break;
        if (!valid_theme_name_for_ui(name) || lstat(theme, &theme_st) != 0 ||
            !S_ISDIR(theme_st.st_mode) || lstat(openbox, &ob_st) != 0 ||
            !S_ISDIR(ob_st.st_mode) || lstat(themerc, &merc_st) != 0 ||
            !S_ISREG(merc_st.st_mode)) continue;
        if (names->len < 8192) g_ptr_array_add(names, g_strdup(name));
    }
    g_dir_close(dir);
}

static gint compare_theme_ptr(gconstpointer a, gconstpointer b)
{
    return compare_theme_names(*(gchar * const *)a, *(gchar * const *)b);
}

static void theme_scan_worker(GTask *task, gpointer source, gpointer task_data,
                              GCancellable *cancellable)
{
    ThemeScan *scan = task_data;
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    (void)source;
    for (guint i = 0; i < scan->roots->len && !g_cancellable_is_cancelled(cancellable); ++i)
        scan_theme_root(g_ptr_array_index(scan->roots, i), names, cancellable);
    if (g_cancellable_is_cancelled(cancellable)) {
        g_ptr_array_unref(names);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Theme scan cancelled");
        return;
    }
    g_ptr_array_sort(names, compare_theme_ptr);
    g_task_return_pointer(task, names, (GDestroyNotify)g_ptr_array_unref);
}

static void theme_scan_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    ThemeScan *scan = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) names = g_task_propagate_pointer(G_TASK(result), &error);
    g_autoptr(GObject) window = g_weak_ref_get(&scan->window);
    GtkWidget *view;
    gint selected_index = -1;
    (void)source;
    if (window && G_OBJECT(mainwin) == window && xml_i && theme_store &&
        scan->generation == theme_scan_generation && names) {
        mapping = TRUE;
        free_theme_list();
        g_clear_pointer(&theme_current_dir, g_free);
        for (guint root_index = 0; root_index < scan->roots->len; ++root_index) {
            const gchar *root = g_ptr_array_index(scan->roots, root_index);
            g_autofree gchar *theme_path = g_build_filename(root, scan->selected, NULL);
            g_autofree gchar *openbox_path = g_build_filename(theme_path, "openbox-3", NULL);
            g_autofree gchar *themerc_path = g_build_filename(openbox_path, "themerc", NULL);
            struct stat theme_st, ob_st, merc_st;
            if (lstat(theme_path, &theme_st) == 0 && S_ISDIR(theme_st.st_mode) &&
                lstat(openbox_path, &ob_st) == 0 && S_ISDIR(ob_st.st_mode) &&
                lstat(themerc_path, &merc_st) == 0 && S_ISREG(merc_st.st_mode)) {
                theme_current_dir = g_strdup(root);
                break;
            }
        }
        gtk_list_store_clear(theme_store);
        for (guint i = 0; i < names->len && i < 8192; ++i) {
            const gchar *name = g_ptr_array_index(names, i);
            if (i && g_ascii_strcasecmp(name, g_ptr_array_index(names, i - 1)) == 0)
                continue;
            themes = g_list_prepend(themes, g_strdup(name));
            GtkTreeIter iter;
            gtk_list_store_append(theme_store, &iter);
            gtk_list_store_set(theme_store, &iter, 0, name, -1);
            if (g_ascii_strcasecmp(scan->selected, name) == 0 && selected_index < 0)
                selected_index = (gint)gtk_tree_model_iter_n_children(GTK_TREE_MODEL(theme_store), NULL) - 1;
        }
        themes = g_list_reverse(themes);
        view = get_widget("theme_names");
        if (selected_index >= 0 && view) {
            GtkTreePath *path = gtk_tree_path_new_from_indices(selected_index, -1);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(view), path, NULL, FALSE);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(view), path, NULL, FALSE, 0.0, 0.0);
            gtk_tree_path_free(path);
        }
        preview_update_all();
        mapping = FALSE;
    }
    if (scan->generation == theme_scan_generation)
        g_clear_object(&theme_scan_cancel);
    theme_scan_free(scan);
}

void theme_load_all(void)
{
    if (!mainwin || !xml_i || !theme_store) return;
    if (theme_scan_cancel) g_cancellable_cancel(theme_scan_cancel);
    g_clear_object(&theme_scan_cancel);
    theme_scan_cancel = g_cancellable_new();
    ThemeScan *scan = g_new0(ThemeScan, 1);
    scan->roots = g_ptr_array_new_with_free_func(g_free);
    scan->selected = tree_get_string("theme/name", "TheBear");
    scan->generation = ++theme_scan_generation;
    g_weak_ref_init(&scan->window, G_OBJECT(mainwin));
    g_ptr_array_add(scan->roots, g_build_filename(g_get_home_dir(), ".themes", NULL));
    GSList *data_dirs = obt_paths_data_dirs(paths);
    for (GSList *it = data_dirs; it && scan->roots->len < 64; it = g_slist_next(it))
        g_ptr_array_add(scan->roots, g_build_filename(it->data, "themes", NULL));
    if (scan->roots->len < 64) g_ptr_array_add(scan->roots, g_strdup(THEMEDIR));
    GTask *task = g_task_new(NULL, theme_scan_cancel, theme_scan_finished, scan);
    g_task_set_task_data(task, scan, NULL);
    g_task_run_in_thread(task, theme_scan_worker);
    g_object_unref(task);
}

