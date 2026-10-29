/* Regression checks run only inside a fresh temporary directory. */
#include "file-ops-gtk4.h"
#include <glib/gstdio.h>
#include <unistd.h>
static GMainLoop *loop;
static GError *result_error;
static gboolean result_ok;
static void done(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object; (void)data;
    result_ok = pcmgr_file_operation_finish(result, &result_error); g_main_loop_quit(loop);
}
static gboolean run(PcmgrFileOperation op, const char *source, const char *dest, gboolean cancel)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_object_unref);
    if (source) g_ptr_array_add(files, g_file_new_for_path(source));
    GFile *target = dest ? g_file_new_for_path(dest) : NULL;
    GCancellable *c = g_cancellable_new(); if (cancel) g_cancellable_cancel(c);
    g_clear_error(&result_error);
    pcmgr_file_operation_async(op, files, target, c, done, NULL); g_main_loop_run(loop);
    g_ptr_array_unref(files); g_clear_object(&target); g_object_unref(c); return result_ok;
}
int main(void)
{
    GError *error = NULL; char *root = g_dir_make_tmp("pcmanager-ops-XXXXXX", &error); g_assert_no_error(error);
    loop = g_main_loop_new(NULL, FALSE);
    char *source = g_build_filename(root, "source", NULL), *dest = g_build_filename(root, "destination", NULL);
    g_assert_true(run(PCMGR_MKDIR, NULL, source, FALSE)); g_assert_true(run(PCMGR_MKDIR, NULL, dest, FALSE));
    char *file = g_build_filename(source, "hello world.txt", NULL);
    g_assert_true(run(PCMGR_NEW_FILE, NULL, file, FALSE));
    g_assert_true(g_file_set_contents(file, "original", -1, &error)); g_assert_no_error(error);
    char *link = g_build_filename(source, "loop", NULL); g_assert_cmpint(symlink(".", link), ==, 0);
    g_assert_true(run(PCMGR_COPY, source, dest, FALSE));
    char *copied = g_build_filename(dest, "source", "hello world.txt", NULL), *contents = NULL;
    g_assert_true(g_file_get_contents(copied, &contents, NULL, &error)); g_assert_cmpstr(contents, ==, "original"); g_free(contents);
    char *copied_link = g_build_filename(dest, "source", "loop", NULL); g_assert_true(g_file_test(copied_link, G_FILE_TEST_IS_SYMLINK));
    g_assert_false(run(PCMGR_COPY, source, dest, FALSE)); g_assert_error(result_error, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_assert_false(run(PCMGR_COPY, source, source, FALSE)); g_assert_error(result_error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    char *alias = g_build_filename(root, "alias", NULL); g_assert_cmpint(symlink(source, alias), ==, 0);
    g_assert_false(run(PCMGR_COPY, source, alias, FALSE)); g_assert_error(result_error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    char *cancelled = g_build_filename(root, "cancelled", NULL);
    g_assert_false(run(PCMGR_MKDIR, NULL, cancelled, TRUE)); g_assert_error(result_error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_false(g_file_test(cancelled, G_FILE_TEST_EXISTS));
    char *renamed = g_build_filename(source, "renamed.txt", NULL);
    g_assert_true(run(PCMGR_RENAME, file, renamed, FALSE)); g_assert_false(g_file_test(file, G_FILE_TEST_EXISTS));
    g_assert_true(run(PCMGR_MOVE, renamed, dest, FALSE)); g_assert_false(g_file_test(renamed, G_FILE_TEST_EXISTS));
    char *moved = g_build_filename(dest, "renamed.txt", NULL);
    g_assert_true(g_file_get_contents(moved, &contents, NULL, &error)); g_assert_cmpstr(contents, ==, "original"); g_free(contents);
    g_print("PASS: create, recursive copy, symlinks, collision refusal, descendant/alias refusal, cancellation, rename, move\n");
    /* Remove precisely the test fixtures; never recurse through a symlink. */
    g_unlink(moved); g_unlink(copied); g_unlink(copied_link); g_unlink(link); g_unlink(alias);
    char *copy_dir = g_build_filename(dest, "source", NULL); g_rmdir(copy_dir); g_rmdir(dest); g_rmdir(source); g_rmdir(root);
    g_free(copy_dir); g_free(moved); g_free(renamed); g_free(cancelled); g_free(alias); g_free(copied_link);
    g_free(copied); g_free(link); g_free(file); g_free(source); g_free(dest); g_free(root);
    g_clear_error(&result_error); g_main_loop_unref(loop); return 0;
}
