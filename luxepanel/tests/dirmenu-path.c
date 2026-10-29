/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "misc.h"
static GtkWidget *fixture_icon(LuxePanel *panel,const gchar *name,gint size,const char *fallback)
{(void)panel;(void)name;(void)size;(void)fallback;return gtk_image_new();}
#define luxepanel_image_new_for_icon fixture_icon
#include "../plugins/dirmenu.c"
#undef luxepanel_image_new_for_icon
void x11_root_watch_window(Window window){(void)window;}
int main(void)
{
    gtk_init();
    g_autofree char *root=g_dir_make_tmp("luxepanel-dirmenu-XXXXXX",NULL);
    const char invalid_name[]={'b',(char)0xff,'x',0};
    g_autofree char *invalid_path=g_build_filename(root,invalid_name,NULL);
    g_autofree char *first=g_build_filename(root,"alpha",NULL);
    g_assert_cmpint(g_mkdir(invalid_path,0700),==,0);g_assert_cmpint(g_mkdir(first,0700),==,0);
    DirMenuPlugin dm={0};dm.path=root;dm.list=gtk_list_box_new();g_object_ref_sink(dm.list);dm.scan_state=dirmenu_scan_state_new(dm.list);
    dirmenu_populate(&dm);
    gint64 deadline=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time()<deadline) {
        guint rows=0;
        for(GtkWidget *row=gtk_widget_get_first_child(dm.list);row;row=gtk_widget_get_next_sibling(row)) rows++;
        if(rows>=5) break;
        g_main_context_iteration(NULL,TRUE);
    }
    GtkWidget *first_row=gtk_widget_get_first_child(dm.list);
    g_assert_cmpstr(g_object_get_data(G_OBJECT(first_row),"dirmenu-path"),==,first);
    GtkWidget *second_row=gtk_widget_get_next_sibling(first_row);
    const char *row_path=g_object_get_data(G_OBJECT(second_row),"dirmenu-path");
    g_assert_nonnull(row_path);g_assert_cmpmem(row_path,strlen(row_path),invalid_path,strlen(invalid_path));
    GtkWidget *box=gtk_list_box_row_get_child(GTK_LIST_BOX_ROW(second_row));
    GtkWidget *label=gtk_widget_get_next_sibling(gtk_widget_get_first_child(box));
    g_assert_true(g_utf8_validate(gtk_label_get_text(GTK_LABEL(label)),-1,NULL));
    dirmenu_navigate(&dm,first);
    dirmenu_navigate(&dm,root);
    dirmenu_navigate(&dm,invalid_path);
    g_assert_true(dm.scan_state->running);
    g_assert_cmpstr(dm.scan_state->pending_path,==,invalid_path);
    deadline=g_get_monotonic_time()+5*G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time()<deadline) {
        guint rows=0;
        for(GtkWidget *row=gtk_widget_get_first_child(dm.list);row;row=gtk_widget_get_next_sibling(row)) rows++;
        if(rows>=4) break;
        g_main_context_iteration(NULL,TRUE);
    }
    g_assert_cmpmem(dm.curpath,strlen(dm.curpath),invalid_path,strlen(invalid_path));
    dirmenu_scan_state_unref(dm.scan_state);dirmenu_clear_rows(&dm);g_clear_pointer(&dm.curpath,g_free);g_object_unref(dm.list);
    g_assert_cmpint(g_rmdir(invalid_path),==,0);g_assert_cmpint(g_rmdir(first),==,0);g_assert_cmpint(g_rmdir(root),==,0);
    return 0;
}
