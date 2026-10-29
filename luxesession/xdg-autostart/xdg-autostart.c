/*
 *      autostart.c - Handle autostart spec of freedesktop.org
 *
 *      Copyright 2008 PCMan <pcman.tw@gmail.com>
 *      Copyright (C) 2026 Jerry Howell
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

#include "xdg-autostart.h"

#include <glib.h>
#include <gio/gdesktopappinfo.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DESKTOP_FILE (1024U * 1024U)
#define MAX_EXEC_LENGTH (16U * 1024U)

static const char DesktopEntry[] = "Desktop Entry";
const char* de_name = NULL;

/* Luxe runs the session as X-Luxe (see start-luxe) while many upstream and
 * generated entries still declare LXDE.  Treat the two names as compatible so
 * those entries are not silently skipped. */
static gboolean de_name_matches( const char* entry_name )
{
    if( entry_name == NULL || de_name == NULL )
        return FALSE;
    if( 0 == strcmp( de_name, entry_name ) )
        return TRUE;
    if( 0 == strcmp( de_name, "X-Luxe" ) && 0 == strcmp( entry_name, "LXDE" ) )
        return TRUE;
    if( 0 == strcmp( de_name, "LXDE" ) && 0 == strcmp( entry_name, "X-Luxe" ) )
        return TRUE;
    return FALSE;
}

static gboolean load_desktop_keyfile_bounded(GKeyFile *kf, const char *path)
{
    int fd;
    struct stat st;
    gchar *data;
    gsize total = 0;
    gboolean ok = FALSE;

    if (kf == NULL || path == NULL)
        return FALSE;
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return FALSE;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        st.st_size < 0 || (guint64)st.st_size > MAX_DESKTOP_FILE)
        goto done;
    data = g_malloc0((gsize)st.st_size + 1);
    while (total < (gsize)st.st_size)
    {
        ssize_t n = read(fd, data + total, (gsize)st.st_size - total);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            g_free(data);
            goto done;
        }
        total += (gsize)n;
    }
    if (memchr(data, '\0', total) != NULL)
    {
        g_free(data);
        goto done;
    }
    ok = g_key_file_load_from_data(kf, data, total, G_KEY_FILE_NONE, NULL);
    g_free(data);

done:
    close(fd);
    return ok;
}

static void launch_autostart_file( const char* desktop_id, const char* desktop_file, GKeyFile* kf)
{
    (void)desktop_id;
    if( load_desktop_keyfile_bounded(kf, desktop_file) )
    {
        char* exec;
        char** only_show_in, **not_show_in;
        gsize n;

        if( g_key_file_get_boolean( kf, DesktopEntry, "Hidden", NULL ) )
            return;

        /* check if this desktop entry is desktop-specific */
        only_show_in = g_key_file_get_string_list( kf, DesktopEntry, "OnlyShowIn", &n, NULL );
        if( only_show_in )
        {
            /* The format of this list is like:  OnlyShowIn=GNOME;XFCE */
            gsize i = 0;
            for( i = 0; i < n; ++i )
            {
                /* Only start this program if we are in the "OnlyShowIn" list */
                if( de_name_matches( only_show_in[ i ] ) )
                    break;
            }
            if( i >= n )    /* our session name is not found in the list */
            {
                g_strfreev( only_show_in );
                return;   /* read next desktop file */
            }
            g_strfreev( only_show_in );
        }
		else /* OnlyShowIn and NotShowIn cannot be set at the same time. */
		{
			/* check if this desktop entry is not allowed in our session */
			not_show_in = g_key_file_get_string_list( kf, DesktopEntry, "NotShowIn", &n, NULL );
			if( not_show_in )
			{
				/* The format of this list is like:  NotShowIn=KDE;IceWM */
				gsize i = 0;
				for( i = 0; i < n; ++i )
				{
					/* Only start this program if we are in the "OnlyShowIn" list */
					if( de_name_matches( not_show_in[ i ] ) )
						break;
				}
				if( i < n )    /* our session name is found in the "NotShowIn" list */
				{
					g_strfreev( not_show_in );
					return;   /* read next desktop file */
				}
				g_strfreev( not_show_in );
			}
		}

        exec = g_key_file_get_string( kf, DesktopEntry, "TryExec", NULL );
        if( G_UNLIKELY(exec) ) /* If we are asked to tryexec first */
        {
            if (exec[0] == '\0' || strlen(exec) > MAX_EXEC_LENGTH)
            {
                g_free(exec);
                return;
            }
            if( ! g_path_is_absolute( exec ) )
            {
                char* full = g_find_program_in_path( exec );
                g_free( exec );
                exec = full;
            }
            /* If we cannot match the TryExec key with an installed executable program */
            if (exec == NULL || !g_file_test( exec, G_FILE_TEST_IS_EXECUTABLE ))
            {
                g_free( exec );
                return;   /* bypass this desktop file, and read next */
            }
            g_free( exec );
        }

        /* get the real command line */
        exec = g_key_file_get_string( kf, DesktopEntry, "Exec", NULL );
        if( G_LIKELY(exec) )
        {
            GError *error = NULL;
            if (strlen(exec) > MAX_EXEC_LENGTH)
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                    "Desktop entry Exec field exceeds the size limit");
            GDesktopAppInfo *app = error ? NULL : g_desktop_app_info_new_from_filename(desktop_file);
            if (app == NULL && error == NULL)
                g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                    "Desktop entry cannot be launched");
            if (app != NULL) {
                GAppLaunchContext *context = g_app_launch_context_new();
                if (!g_app_info_launch(G_APP_INFO(app), NULL, context, &error))
                    g_warning("xdg-autostart: failed to launch %s: %s",
                              desktop_file, error ? error->message : "unknown error");
                g_object_unref(context);
                g_object_unref(app);
            } else if (error != NULL) {
                g_warning("xdg-autostart: invalid Exec in %s: %s", desktop_file, error->message);
            }
            g_clear_error(&error);
            g_free(exec);
        }
    }
}

static void get_autostart_files_in_dir( GHashTable* hash, const char* base_dir )
{
    char* dir_path;
    if (hash == NULL || base_dir == NULL || base_dir[0] == '\0')
        return;
    dir_path = g_build_filename( base_dir, "autostart", NULL );
    GDir* dir = g_dir_open( dir_path, 0, NULL );

    if( dir )
    {
        char *path;
        const char *name;

        while( (name = g_dir_read_name( dir )) != NULL )
        {
            if(g_str_has_suffix(name, ".desktop"))
            {
                if (g_hash_table_contains( hash, name))
                {
                    /* desktop file already exist in a higher directory, do nothing */
                }
                else
                {
                    path = g_build_filename( dir_path, name, NULL );
                    // printf("%s\n", path);
                    g_hash_table_replace( hash, g_strdup(name), path );
                }
            }
        }
        g_dir_close( dir );
    }
    g_free( dir_path );
}

void xdg_autostart( const char* de_name_arg )
{
    const char* const *dirs = g_get_system_config_dirs();
    const char* const *dir;
    GHashTable* hash = g_hash_table_new_full( g_str_hash, g_str_equal, g_free, g_free );
    de_name = (de_name_arg != NULL && de_name_arg[0] != '\0') ? de_name_arg : "LXDE";

    /* get user-specific autostart files */
    get_autostart_files_in_dir( hash, g_get_user_config_dir() );

     /* get system-wide autostart files */
	for( dir = dirs; *dir; ++dir )
		get_autostart_files_in_dir( hash, *dir );

    if( g_hash_table_size( hash ) > 0 )
    {
        GKeyFile* kf = g_key_file_new();
        /* GHashTable iteration order is nondeterministic; launch in sorted
         * basename order so session startup order is stable. */
        GList* names = g_hash_table_get_keys( hash );
        GList* l;
        names = g_list_sort( names, (GCompareFunc) strcmp );
        for( l = names; l; l = l->next )
        {
            const char* id = (const char*) l->data;
            launch_autostart_file( id, g_hash_table_lookup( hash, id ), kf );
        }
        g_list_free( names );
        g_key_file_free( kf );
    }

    g_hash_table_destroy( hash );
}
