/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include "../src/run-completion.h"
static GMutex gate_lock;
static GCond gate_cond;
static gboolean release_first;
static gint started;
static GSList *fixture_scan(const char *path,gint *cancel)
{
    (void)path;
    int job=g_atomic_int_add(&started,1);
    if(job==0){g_mutex_lock(&gate_lock);while(!release_first)g_cond_wait(&gate_cond,&gate_lock);g_mutex_unlock(&gate_lock);}
    return g_atomic_int_get(cancel)?NULL:g_slist_prepend(NULL,g_strdup("fixture-command"));
}
#define luxepanel_scan_path fixture_scan
#include "../src/gtk-run.c"
#undef luxepanel_scan_path
void x11_root_watch_window(Window window){(void)window;}
int main(void)
{
    gtk_init();
    GtkEntry *first=GTK_ENTRY(gtk_entry_new()),*second=GTK_ENTRY(gtk_entry_new());
    g_object_ref_sink(first);g_object_ref_sink(second);
    setup_auto_complete(first);
    gint64 deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while(!g_atomic_int_get(&started)&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_cmpint(g_atomic_int_get(&started),==,1);
    g_atomic_int_set(&thread_data->cancel,TRUE);
    setup_auto_complete(second);
    g_assert_cmpuint(g_slist_length(thread_jobs),==,1);
    g_assert_true(pending_completion_entry==second);
    g_assert_cmpint(g_atomic_int_get(&started),==,1);
    GtkEntry *third=GTK_ENTRY(gtk_entry_new());g_object_ref_sink(third);
    for(int i=0;i<20;i++)setup_auto_complete(i%2?second:third);
    g_assert_cmpuint(g_slist_length(thread_jobs),==,1);g_assert_true(pending_completion_entry==second);
    GtkWidget *dialog=gtk_window_new();g_object_ref_sink(dialog);win=dialog;
    cancel_run_dialog(dialog);g_assert_null(pending_completion_entry);
    gtk_window_destroy(GTK_WINDOW(dialog));g_object_unref(dialog);g_object_unref(third);
    setup_auto_complete(second);
    g_mutex_lock(&gate_lock);release_first=TRUE;g_cond_broadcast(&gate_cond);g_mutex_unlock(&gate_lock);
    while((thread_jobs||pending_completion_entry)&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
    g_assert_null(thread_jobs);g_assert_null(pending_completion_entry);
    g_assert_cmpint(g_atomic_int_get(&started),==,2);
    g_assert_null(gtk_entry_get_completion(first));
    g_assert_nonnull(gtk_entry_get_completion(second));
    g_object_unref(first);g_object_unref(second);
    return 0;
}
