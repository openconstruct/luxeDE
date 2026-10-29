/* Copyright 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
 * Run only with --isolated and a fresh XDG_DATA_HOME inside dbus-run-session. */
#include "../src/file-ops-gtk4.h"
#include <glib/gstdio.h>
static GMainLoop *loop;static gboolean done;static GError *failure;
static void finished(GObject *o,GAsyncResult *r,gpointer p){done=pcmgr_file_operation_finish(r,&failure);g_main_loop_quit(loop);}
int main(int argc,char **argv){
 g_assert_cmpint(argc,==,2);g_assert_cmpstr(argv[1],==,"--isolated");
 const char *base=g_getenv("XDG_DATA_HOME");g_assert_nonnull(base);char *basename=g_path_get_basename(base);g_assert_true(g_str_has_prefix(basename,"pcmgr-trash-test-"));g_free(basename);
 GFile *trash=g_file_new_for_uri("trash:///");GError *e=NULL;
 GFileEnumerator *en=g_file_enumerate_children(trash,"standard::name",0,NULL,&e);g_assert_no_error(e);g_assert_nonnull(en);g_assert_null(g_file_enumerator_next_file(en,NULL,&e));g_assert_no_error(e);g_object_unref(en);
 char *keep=g_build_filename(base,"keep",NULL);g_file_set_contents(keep,"keep",-1,NULL);
 char *dir=g_build_filename(base,"folder",NULL);g_mkdir(dir,0700);char *nested=g_build_filename(dir,"nested",NULL);g_file_set_contents(nested,"fixture",-1,NULL);
 char *link=g_build_filename(base,"link",NULL);GFile *lf=g_file_new_for_path(link);g_assert_true(g_file_make_symbolic_link(lf,keep,NULL,&e));g_file_trash(lf,NULL,&e);g_assert_no_error(e);
 GFile *df=g_file_new_for_path(dir);g_assert_true(g_file_trash(df,NULL,&e));g_assert_no_error(e);
 loop=g_main_loop_new(NULL,FALSE);GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);
 pcmgr_file_operation_async(PCMGR_EMPTY_TRASH,NULL,NULL,cancel,finished,NULL);g_main_loop_run(loop);g_assert_false(done);g_assert_error(failure,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&failure);g_object_unref(cancel);
 pcmgr_file_operation_async(PCMGR_EMPTY_TRASH,NULL,NULL,NULL,finished,NULL);g_main_loop_run(loop);g_assert_no_error(failure);g_assert_true(done);g_assert_true(g_file_test(keep,G_FILE_TEST_EXISTS));
 en=g_file_enumerate_children(trash,"standard::name",0,NULL,&e);g_assert_no_error(e);g_assert_null(g_file_enumerator_next_file(en,NULL,&e));g_assert_no_error(e);g_object_unref(en);
 pcmgr_file_operation_async(PCMGR_EMPTY_TRASH,NULL,NULL,NULL,finished,NULL);g_main_loop_run(loop);g_assert_no_error(failure);g_assert_true(done);
 g_print("PASS: empty Trash, nested directory, symlink target preserved, cancellation, already empty\n");return 0;
}
