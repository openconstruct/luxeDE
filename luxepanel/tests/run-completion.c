/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <glib/gstdio.h>
#include <unistd.h>
#include "../src/run-completion.h"

static void executable(const char *path)
{g_assert_true(g_file_set_contents(path,"#!/bin/sh\n",-1,NULL));g_assert_cmpint(g_chmod(path,0755),==,0);}
int main(void)
{
    g_autofree char *old=g_get_current_dir(),*root=g_dir_make_tmp("panel-path-XXXXXX",NULL);
    g_autofree char *a=g_build_filename(root,"a",NULL),*b=g_build_filename(root,"b",NULL);
    g_assert_cmpint(g_mkdir(a,0700),==,0);g_assert_cmpint(g_mkdir(b,0700),==,0);
    g_autofree char *a1=g_build_filename(a,"same",NULL),*b1=g_build_filename(b,"same",NULL);
    g_autofree char *other=g_build_filename(b,"other",NULL),*current=g_build_filename(root,"current",NULL);
    g_autofree char *nonexec=g_build_filename(a,"nonexec",NULL),*folder=g_build_filename(a,"executable-folder",NULL);
    g_autofree char *link=g_build_filename(a,"link",NULL);
    executable(a1);executable(b1);executable(other);executable(current);
    g_assert_true(g_file_set_contents(nonexec,"x",-1,NULL));g_assert_cmpint(g_chmod(nonexec,0644),==,0);
    g_assert_cmpint(g_mkdir(folder,0755),==,0);g_assert_cmpint(symlink(a1,link),==,0);
    g_assert_cmpint(g_chdir(root),==,0);
    g_autofree char *search=g_strdup_printf("%s:%s::%s",a,b,a);gint canceled=0;
    GSList *names=luxepanel_scan_path(search,&canceled);
    g_assert_cmpuint(g_slist_length(names),==,4);
    const char *expected[]={"same","other","current","link"};
    for(guint i=0;i<G_N_ELEMENTS(expected);i++)g_assert_nonnull(g_slist_find_custom(names,expected[i],(GCompareFunc)g_strcmp0));
    g_assert_null(g_slist_find_custom(names,"executable-folder",(GCompareFunc)g_strcmp0));
    g_assert_null(g_slist_find_custom(names,"nonexec",(GCompareFunc)g_strcmp0));
    g_slist_free_full(names,g_free);
    canceled=1;g_assert_null(luxepanel_scan_path(search,&canceled));
    canceled=0;names=luxepanel_scan_path("",&canceled);g_assert_nonnull(g_slist_find_custom(names,"current",(GCompareFunc)g_strcmp0));g_slist_free_full(names,g_free);
    g_assert_cmpint(g_chdir(old),==,0);
    g_assert_cmpint(g_unlink(a1),==,0);g_assert_cmpint(g_unlink(b1),==,0);g_assert_cmpint(g_unlink(other),==,0);
    g_assert_cmpint(g_unlink(current),==,0);g_assert_cmpint(g_unlink(nonexec),==,0);g_assert_cmpint(g_unlink(link),==,0);
    g_assert_cmpint(g_rmdir(folder),==,0);g_assert_cmpint(g_rmdir(a),==,0);g_assert_cmpint(g_rmdir(b),==,0);g_assert_cmpint(g_rmdir(root),==,0);
    return 0;
}
