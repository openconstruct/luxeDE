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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include "../luxesession/profile.h"
#include "safe-file.h"

enum {
    COL_ENABLED,
    COL_ICON,
    COL_NAME,
    COL_COMMENT,
    COL_DESKTOP_ID,
    COL_SRC_FILE,
    COL_FLAGS,
    N_COLS
};

gchar *lxsession_autostart_save_source(const gchar *config_dir,
                                        const gchar *desktop_id,
                                        const gchar *source_file)
{
    if (config_dir && desktop_id) {
        gchar *override = g_build_filename(config_dir, "autostart", desktop_id, NULL);
        if (g_file_test(override, G_FILE_TEST_EXISTS))
            return override;
        g_free(override);
    }
    return g_strdup(source_file);
}

enum {
    NONE = 0,
    NOT_SHOW_IN = 1 << 0,
    ONLY_SHOW_IN = 1 << 1,
    ORIGINALLY_ENABLED = 1 << 15
};

const char grp[]= "Desktop Entry";
GtkListStore* autostart_list = NULL;

gboolean is_desktop_file_enabled(GKeyFile* kf, int *flags, const char* session_name)
{
    char** not_show_in;
    char** only_show_in;
    gsize n, i;

    *flags = 0;

    not_show_in = g_key_file_get_string_list(kf, grp, "NotShowIn", &n, NULL);
    if( not_show_in )
    {
        *flags |= NOT_SHOW_IN;
        for( i = 0; i < n; ++i )
        {
            if(strcmp(not_show_in[i], session_name) == 0)
            {
                g_strfreev(not_show_in);
                return FALSE;
            }
        }
        g_strfreev(not_show_in);
    }

    only_show_in = g_key_file_get_string_list(kf, grp, "OnlyShowIn", &n, NULL);
    if( only_show_in )
    {
        *flags |= ONLY_SHOW_IN;
        for( i = 0; i < n; ++i )
            if(strcmp(only_show_in[i], session_name) == 0)
                break;
        g_strfreev(only_show_in);
        if( i >= n )
            return FALSE;
    }

    return TRUE;
}

gboolean is_desktop_file_valid(GKeyFile* kf)
{
    char* tmp;
    if( g_key_file_get_boolean(kf, grp, "Hidden", NULL) )
        return FALSE;

    if( (tmp = g_key_file_get_string(kf, grp, "Type", NULL)) != NULL )
    {
        if( strcmp(tmp, "Application") )
        {
            g_free(tmp);
            return FALSE;
        }
        g_free(tmp);
    }

    if( (tmp = g_key_file_get_string(kf, grp, "TryExec", NULL)) != NULL )
    {
        char* prg = g_find_program_in_path(tmp);
        g_free(tmp);
        if(!prg)
            return FALSE;
        g_free(prg);
    }
    return TRUE;
}

void get_autostart_files_in_dir( GHashTable* hash, const char* session_name, const char* base_dir )
{
    char* dir_path = g_build_filename( base_dir, "autostart", NULL );
    GDir* dir = g_dir_open( dir_path, 0, NULL );
    if( dir )
    {
        char *path;
        const char *name;

        while( name = g_dir_read_name(dir) )
        {
            if(g_str_has_suffix(name, ".desktop"))
            {
                path = g_build_filename( dir_path, name, NULL );
                g_hash_table_replace( hash, g_strdup(name), path );
            }
        }
        g_dir_close( dir );
    }
    g_free( dir_path );
}

void add_autostart_file(char* desktop_id, char* file, GKeyFile* kf)
{
    GtkTreeIter it;

    const char* session_name_local = NULL;

    session_name_local = g_getenv("XDG_CURRENT_DESKTOP");
    if(!session_name_local)
    {
        session_name_local = g_getenv("DESKTOP_SESSION");
        if( G_UNLIKELY(!session_name_local) )
            session_name_local = "LXDE";
    }

    if( lxsession_key_file_load_bounded( kf, file, NULL ) )
    {
        if( is_desktop_file_valid(kf) )
        {
            char* name = g_key_file_get_locale_string(kf, grp, "Name", NULL, NULL);
            char* icon = g_key_file_get_locale_string(kf, grp, "Icon", NULL, NULL);
            char* comment = g_key_file_get_locale_string(kf, grp, "Comment", NULL, NULL);
            int flags;
            gboolean enabled = is_desktop_file_enabled(kf, &flags, session_name_local);
            if( enabled )
                flags |= ORIGINALLY_ENABLED;
            gtk_list_store_append(autostart_list, &it);
            gtk_list_store_set( autostart_list, &it,
                                COL_ENABLED, enabled,
                                COL_NAME, name,
                                /* COL_ICON, pix, */
                                COL_COMMENT, comment,
                                COL_DESKTOP_ID, desktop_id,
                                COL_SRC_FILE, file,
                                COL_FLAGS, flags,
                                -1 );
            g_free(name);
            g_free(icon);
            g_free(comment);
        }
    }
}

void load_autostart(const char* session_name)
{
    const char* const *dirs = g_get_system_config_dirs();
    const char* const *dir;
    GHashTable* hash = g_hash_table_new_full( g_str_hash, g_str_equal, g_free, g_free );

    /* get system-wide autostart files */
    for( dir = dirs; *dir; ++dir )
        get_autostart_files_in_dir( hash, session_name, *dir );

    /* get user-specific autostart files */
    get_autostart_files_in_dir( hash, session_name, g_get_user_config_dir() );

    if( g_hash_table_size( hash ) > 0 )
    {
        GKeyFile* kf = g_key_file_new();
        g_hash_table_foreach( hash, (GHFunc)add_autostart_file, kf );
        g_key_file_free( kf );
    }
    g_hash_table_destroy( hash );
}

void reload_autostart(GtkTreeView* view, const char* session_name)
{
    gtk_list_store_clear(autostart_list);
    load_autostart(session_name);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(autostart_list));
}

/* FIXME:
 * If the system-wide desktop file can meet our needs,
 * remove the user-specific one instead of changing its key values. */
static gboolean update_session_list(GKeyFile *kf, const char *key,
                                    const char *session_name, gboolean include,
                                    gboolean create, GError **error)
{
    gboolean present = g_key_file_has_key(kf, grp, key, NULL);
    if (!present && !create) return TRUE;

    gsize count = 0;
    g_auto(GStrv) original = present
        ? g_key_file_get_string_list(kf, grp, key, &count, error) : NULL;
    if (present && original == NULL) return FALSE;

    g_autoptr(GPtrArray) values = g_ptr_array_new_with_free_func(g_free);
    gboolean found = FALSE;
    for (gsize i = 0; i < count; i++) {
        gboolean matches = g_str_equal(original[i], session_name);
        if (matches) {
            if (!include || found) continue;
            found = TRUE;
        }
        g_ptr_array_add(values, g_strdup(original[i]));
    }
    if (include && !found) g_ptr_array_add(values, g_strdup(session_name));

    if (values->len == 0) {
        if (present && !g_key_file_remove_key(kf, grp, key, error)) return FALSE;
    } else {
        g_key_file_set_string_list(kf, grp, key,
            (const gchar * const *)values->pdata, values->len);
    }
    return TRUE;
}

gboolean update_enable_state(GKeyFile *kf, gboolean enabled,
                             const char *session_name, GError **error)
{
    gboolean has_only_show = g_key_file_has_key(kf, grp, "OnlyShowIn", NULL);
    if (!update_session_list(kf, "NotShowIn", session_name,
                             !enabled, !enabled, error))
        return FALSE;
    if (has_only_show && !update_session_list(kf, "OnlyShowIn", session_name,
                                               enabled, FALSE, error))
        return FALSE;
    return TRUE;
}

gboolean save_autostart(const char* session_name, GError **error)
{
    GtkTreeIter it;
    GKeyFile* kf;
    char* dirname;
    if( ! gtk_tree_model_get_iter_first(GTK_TREE_MODEL(autostart_list), &it) )
        return TRUE;

    /* create user autostart dir */
    dirname = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    if (g_mkdir_with_parents(dirname, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create autostart directory: %s", g_strerror(errno));
        g_free(dirname);
        return FALSE;
    }
    g_free(dirname);

    /* update desktop files in autostart dir */
    kf = g_key_file_new();
    do
    {
        int flags;
        gboolean enabled;
        gtk_tree_model_get( GTK_TREE_MODEL(autostart_list), &it,
                            COL_ENABLED, &enabled,
                            COL_FLAGS, &flags, -1);

        /* enabled state is changed */
        if( enabled != !!(flags & ORIGINALLY_ENABLED) )
        {
            char* desktop_id, *src_file;
            gtk_tree_model_get( GTK_TREE_MODEL(autostart_list), &it,
                                COL_DESKTOP_ID, &desktop_id,
                                COL_SRC_FILE, &src_file,
                                -1);

            char *source_path = lxsession_autostart_save_source(
                g_get_user_config_dir(), desktop_id, src_file);

            /* load the source desktop file */
            {
                gchar *source_data = NULL;
                gsize source_length = 0;
                GError *source_error = NULL;
                g_key_file_free(kf);
                kf = g_key_file_new();
                if (desktop_id == NULL || src_file == NULL ||
                    !lxsession_read_bounded_file(source_path, 1024U * 1024U,
                                                &source_data, &source_length,
                                                &source_error) ||
                    !g_key_file_load_from_data(kf, source_data, source_length,
                                               G_KEY_FILE_KEEP_TRANSLATIONS,
                                               NULL))
                {
                    g_clear_error(&source_error);
                    g_free(source_data);
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Cannot read autostart entry %s", source_path ? source_path : "(unknown)");
                    g_free(source_path);
                    g_free(desktop_id);
                    g_free(src_file);
                    g_key_file_free(kf);
                    return FALSE;
                }
                g_clear_error(&source_error);
                g_free(source_data);
            }
            {
                char* file, *data;
                gsize len;
                /* update enabled state */
                GError *state_error = NULL;
                if (!update_enable_state(kf, enabled, session_name, &state_error)) {
                    g_propagate_prefixed_error(error, state_error,
                                               "Cannot update autostart entry %s: ", desktop_id);
                    g_free(source_path);
                    g_free(desktop_id);
                    g_free(src_file);
                    g_key_file_free(kf);
                    return FALSE;
                }
                data = g_key_file_to_data(kf, &len, NULL);
                file = g_build_filename(  g_get_user_config_dir(), "autostart", desktop_id, NULL );
                /* save it to user-specific autostart dir */
                g_debug("src:%s, save to: %s", source_path, file);
                GError *write_error = NULL;
                if( lxsession_atomic_write_file(file, data, len, &write_error) )
                {
                    int new_flags = 0;
                    is_desktop_file_enabled(kf, &new_flags, session_name);
                    if( enabled )
                        new_flags |= ORIGINALLY_ENABLED;
                    gtk_list_store_set(autostart_list, &it,
                                       COL_SRC_FILE, file,
                                       COL_FLAGS, new_flags,
                                       -1);
                }
                else {
                    g_propagate_prefixed_error(error, write_error,
                                               "Cannot save autostart entry %s: ", desktop_id);
                    g_free(file);
                    g_free(data);
                    g_free(source_path);
                    g_free(desktop_id);
                    g_free(src_file);
                    g_key_file_free(kf);
                    return FALSE;
                }
                g_free(file);
                g_free(data);
            }
            g_free(source_path);
            g_free(desktop_id);
            g_free(src_file);
        }
    }while( gtk_tree_model_iter_next(GTK_TREE_MODEL(autostart_list), &it) );
    g_key_file_free(kf);
    return TRUE;
}

void on_enable_toggled(GtkCellRendererToggle* render,
                              char* tp_str, gpointer user_data)
{
    (void)render; (void)user_data;
    GtkTreePath* tp = gtk_tree_path_new_from_string(tp_str);
    GtkTreeIter it;
    gboolean previous = FALSE;
    if( gtk_tree_model_get_iter(GTK_TREE_MODEL(autostart_list), &it, tp) )
    {
        gtk_tree_model_get(GTK_TREE_MODEL(autostart_list), &it, COL_ENABLED, &previous, -1 );
        gtk_list_store_set(autostart_list, &it, COL_ENABLED, !previous, -1 );
    }

    const char* session_name = NULL;
    session_name = g_getenv("XDG_CURRENT_DESKTOP");
    if(!session_name)
    {
        session_name = g_getenv("DESKTOP_SESSION");
        if( G_UNLIKELY(!session_name) )
            session_name = "LXDE";
    }

    GError *error = NULL;
    if (!save_autostart(session_name, &error)) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_window_destroy), dialog);
        gtk_widget_show(dialog);
        g_clear_error(&error);
        /* Restore the checkbox to the persisted state when the write fails. */
        if (gtk_tree_model_get_iter(GTK_TREE_MODEL(autostart_list), &it, tp)) {
            gtk_list_store_set(autostart_list, &it, COL_ENABLED, previous, -1);
        }
    }

    gtk_tree_path_free(tp);
}

void init_list_view( GtkTreeView* view )
{

    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    autostart_list = gtk_list_store_new(N_COLS,
                                        G_TYPE_BOOLEAN,
                                        GDK_TYPE_PIXBUF,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING,
                                        G_TYPE_STRING,
                                        G_TYPE_INT );

    render = gtk_cell_renderer_toggle_new();
    col = gtk_tree_view_column_new_with_attributes(_("Enabled"), render, "active", COL_ENABLED, NULL );
    gtk_tree_view_append_column(view, col);
    g_signal_connect(render, "toggled", G_CALLBACK(on_enable_toggled), NULL);

    render = gtk_cell_renderer_pixbuf_new();
    col = gtk_tree_view_column_new_with_attributes(_("Application"), render, "pixbuf", COL_ICON, NULL );
    gtk_tree_view_append_column(view, col);

    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start( col, render, TRUE );
    gtk_tree_view_column_set_attributes( col, render, "text", COL_NAME, NULL );
/*
    Remove the comment, make the windows huge
    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(_("Comment"), render, "text", COL_COMMENT, NULL );
    gtk_tree_view_append_column(view, col);
*/
}

GtkListStore* get_autostart_list ()
{
    return autostart_list;
}

static char* command_desktop_id(const char* command)
{
    char* digest;
    if (command == NULL || *command == '\0')
        return g_strdup("luxe-user-application.desktop");
    digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, command, -1);
    char** argv = NULL;
    char* base = NULL;
    char* slug;
    char* id;
    int argc = 0;

    if( g_shell_parse_argv(command, &argc, &argv, NULL) && argc > 0 )
        base = g_path_get_basename(argv[0]);
    else
        base = g_strdup("application");
    slug = g_ascii_strdown(base, -1);
    for( char* pos = slug; *pos; ++pos )
        if( !g_ascii_isalnum(*pos) && *pos != '-' )
            *pos = '-';
    id = g_strdup_printf("luxe-user-%s-%.10s.desktop", slug, digest);
    g_strfreev(argv);
    g_free(base);
    g_free(slug);
    g_free(digest);
    return id;
}

gboolean add_autostart_command(const char* command, const char* session_name,
                               GError** error)
{
    char* stripped = g_strdup(command ? command : "");
    char* directory;
    char* desktop_id;
    char* path;
    char* display_name = NULL;
    char** argv = NULL;
    int argc = 0;
    GKeyFile* key_file;
    char* data;
    gsize length;
    const char* sessions[1];
    gboolean saved;

    g_strstrip(stripped);
    if (!luxe_profile_component_valid(session_name))
        session_name = "X-Luxe";
    if( !*stripped || strpbrk(stripped, "\r\n") )
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            _("Enter a command to start at login."));
        g_free(stripped);
        return FALSE;
    }
    if( !g_shell_parse_argv(stripped, &argc, &argv, error) || argc == 0 )
    {
        g_free(stripped);
        g_strfreev(argv);
        return FALSE;
    }

    display_name = g_path_get_basename(argv[0]);
    directory = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    if( g_mkdir_with_parents(directory, 0700) != 0 )
    {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    _("Could not create %s"), directory);
        g_free(stripped); g_strfreev(argv); g_free(display_name); g_free(directory);
        return FALSE;
    }
    desktop_id = command_desktop_id(stripped);
    path = g_build_filename(directory, desktop_id, NULL);
    key_file = g_key_file_new();
    g_key_file_set_string(key_file, grp, "Type", "Application");
    g_key_file_set_string(key_file, grp, "Name", display_name);
    g_key_file_set_string(key_file, grp, "Comment", _("Added in LuxeSession Settings"));
    g_key_file_set_string(key_file, grp, "Exec", stripped);
    sessions[0] = (session_name && *session_name) ? session_name : "X-Luxe";
    g_key_file_set_string_list(key_file, grp, "OnlyShowIn", sessions, 1);
    g_key_file_set_boolean(key_file, grp, "X-GNOME-Autostart-enabled", TRUE);
    data = g_key_file_to_data(key_file, &length, NULL);
    saved = lxsession_atomic_write_file(path, data, length, error);

    g_free(data);
    g_key_file_free(key_file);
    g_free(path);
    g_free(desktop_id);
    g_free(directory);
    g_free(display_name);
    g_strfreev(argv);
    g_free(stripped);
    return saved;
}

gboolean remove_selected_autostart(GtkTreeView* view, const char* session_name,
                                   GError** error)
{
    GtkTreeSelection* selection = gtk_tree_view_get_selection(view);
    GtkTreeModel* model = NULL;
    GtkTreeIter iter;
    char* source = NULL;
    char* desktop_id = NULL;
    char* user_directory;
    char* canonical_source;
    char* canonical_directory;
    gboolean removed = FALSE;
    gboolean previous_enabled = FALSE;

    if( !gtk_tree_selection_get_selected(selection, &model, &iter) )
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            _("Select an autostart entry to remove."));
        return FALSE;
    }
    gtk_tree_model_get(model, &iter,
                       COL_SRC_FILE, &source,
                       COL_DESKTOP_ID, &desktop_id,
                       COL_ENABLED, &previous_enabled,
                       -1);
    if (source == NULL || desktop_id == NULL || *desktop_id == '\0')
    {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            _("Invalid autostart entry."));
        g_free(source);
        g_free(desktop_id);
        return FALSE;
    }
    user_directory = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    canonical_source = g_canonicalize_filename(source, NULL);
    canonical_directory = g_canonicalize_filename(user_directory, NULL);

    gboolean has_system_source = FALSE;
    const char* const* system_dirs = g_get_system_config_dirs();
    for( const char* const* dir = system_dirs; *dir && !has_system_source; ++dir )
    {
        char* candidate = g_build_filename(*dir, "autostart", desktop_id, NULL);
        has_system_source = g_file_test(candidate, G_FILE_TEST_IS_REGULAR);
        g_free(candidate);
    }

    if( g_str_has_prefix(canonical_source, canonical_directory) &&
        canonical_source[strlen(canonical_directory)] == G_DIR_SEPARATOR &&
        !has_system_source )
    {
        if( g_remove(canonical_source) == 0 )
            removed = TRUE;
        else
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        _("Could not remove %s"), canonical_source);
    }
    else
    {
        /* System entries cannot be deleted.  Removing one means disabling it
         * for this desktop through a per-user XDG override. */
        gtk_list_store_set(autostart_list, &iter, COL_ENABLED, FALSE, -1);
        if (save_autostart(session_name, error)) removed = TRUE;
        else gtk_list_store_set(autostart_list, &iter, COL_ENABLED, previous_enabled, -1);
    }

    g_free(canonical_directory);
    g_free(canonical_source);
    g_free(user_directory);
    g_free(desktop_id);
    g_free(source);
    return removed;
}
