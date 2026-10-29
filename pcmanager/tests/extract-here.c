/* Copyright 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/browser-gtk4.c"
static void pump(void){gint64 until=g_get_monotonic_time()+100000;while(g_get_monotonic_time()<until){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}}
int main(int argc,char **argv){g_assert_cmpint(argc,==,2);gtk_init();fm_init(NULL);pcmgr_browser_setup("extract-test",NULL);const char *paths[]={argv[1],NULL};pcmgr_browser_open(paths,NULL,TRUE);Browser *b=browsers->data;Tab *tab=current_tab(b);
 for(int i=0;i<50;i++){pump();if(!gtk_directory_list_is_loading(tab->directory))break;}
 g_assert_false(extractable(tab));
 for(guint i=0;i<g_list_model_get_n_items(G_LIST_MODEL(tab->sorted));i++){
  GFileInfo *info=g_list_model_get_item(G_LIST_MODEL(tab->sorted),i);
  if(g_str_has_suffix(g_file_info_get_name(info),".zip")){gtk_selection_model_select_item(GTK_SELECTION_MODEL(tab->selection),i,TRUE);g_object_unref(info);break;}g_object_unref(info);
 }
 g_assert_true(extractable(tab));g_action_group_activate_action(G_ACTION_GROUP(b->actions),"extract-here",NULL);
 char *output=g_build_filename(argv[1],"extracted.txt",NULL);for(int i=0;i<100&&!g_file_test(output,G_FILE_TEST_EXISTS);i++)pump();
 char *text=NULL;g_assert_true(g_file_get_contents(output,&text,NULL,NULL));g_assert_cmpstr(text,==,"extraction works\n");g_free(text);g_free(output);
 for(int i=0;i<10;i++)pump();gtk_window_close(GTK_WINDOW(b));pump();g_print("PASS: archive selection and real Xarchiver extraction with shell characters in archive path\n");return 0;}
