/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXEPANEL_RUN_COMPLETION_H
#define LUXEPANEL_RUN_COMPLETION_H
#include <glib.h>

#define LUXEPANEL_MAX_PATH_SCAN_ENTRIES 32768
#define LUXEPANEL_MAX_COMPLETION_COMMANDS 2048

/* Returned strings belong to the caller. Empty PATH components mean cwd. */
static GSList *
luxepanel_scan_path(const char *path, gint *cancel)
{
    GSList *list=NULL;
    if(!path)return NULL;
    g_auto(GStrv) directories=g_strsplit(*path?path:".",":",-1);
    g_autoptr(GHashTable) seen=g_hash_table_new(g_str_hash,g_str_equal);
    guint scanned=0, found=0;
    gboolean truncated=FALSE;
    for(guint i=0;directories[i] && !g_atomic_int_get(cancel) && !truncated;i++) {
        const char *directory=*directories[i]?directories[i]:".";
        g_autoptr(GDir) dir=g_dir_open(directory,0,NULL);
        if(!dir)continue;
        const char *name;
        while(!g_atomic_int_get(cancel) && (name=g_dir_read_name(dir))) {
            if(++scanned>LUXEPANEL_MAX_PATH_SCAN_ENTRIES) {
                truncated=TRUE;
                break;
            }
            if(!g_utf8_validate(name,-1,NULL) || g_hash_table_contains(seen,name))continue;
            g_autofree char *file=g_build_filename(directory,name,NULL);
            if(g_file_test(file,G_FILE_TEST_IS_REGULAR) && g_file_test(file,G_FILE_TEST_IS_EXECUTABLE)) {
                char *copy=g_strdup(name);
                g_hash_table_add(seen,copy);list=g_slist_prepend(list,copy);
                if(++found>=LUXEPANEL_MAX_COMPLETION_COMMANDS) {
                    truncated=TRUE;
                    break;
                }
            }
        }
    }
    if(truncated)
        g_debug("Run completion stopped at its bounded PATH scan limit");
    return list;
}
#endif
