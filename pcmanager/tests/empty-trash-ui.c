/* Copyright 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/browser-gtk4.c"
static void pump(void){gint64 until=g_get_monotonic_time()+400000;while(g_get_monotonic_time()<until){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}}
int main(void){gtk_init();fm_init(NULL);pcmgr_browser_setup("empty-trash-test",NULL);
 const char *paths[]={g_get_home_dir(),NULL};pcmgr_browser_open(paths,NULL,TRUE);pump();Browser *b=browsers->data;
 g_assert_false(gtk_widget_get_visible(b->empty_trash));GFile *trash=g_file_new_for_uri("trash:///");navigate(current_tab(b),trash,TRUE);pump();g_assert_true(gtk_widget_get_visible(b->empty_trash));
 g_action_group_activate_action(G_ACTION_GROUP(b->actions),"empty-trash",NULL);pump();GListModel *wins=gtk_window_get_toplevels();GtkDialog *dialog=NULL;
 for(guint i=0;i<g_list_model_get_n_items(wins);i++){GtkWidget *w=g_list_model_get_item(wins,i);if(GTK_IS_MESSAGE_DIALOG(w))dialog=GTK_DIALOG(w);else g_object_unref(w);}
 g_assert_nonnull(dialog);g_assert_true(gtk_window_get_default_widget(GTK_WINDOW(dialog))==gtk_dialog_get_widget_for_response(dialog,GTK_RESPONSE_CANCEL));
 gtk_dialog_response(dialog,GTK_RESPONSE_CANCEL);g_object_unref(dialog);pump();g_assert_null(b->operation);gtk_window_close(GTK_WINDOW(b));pump();g_object_unref(trash);fm_finalize();g_print("PASS: Trash-only button, confirmation, Cancel default, cancellation leaves operation idle\n");return 0;}
