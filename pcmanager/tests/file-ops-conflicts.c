/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Headless Step 10b regression test: copy/move batches skip existing
 * destinations without overwriting, continue past failures, and report a
 * summary. Uses plain local paths only (no display, no D-Bus). */
#include "../src/file-ops-gtk4.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <errno.h>
static guint observed_copies;
static guint staging_directories;
static mode_t observed_staging_mode;
static gboolean inject_mkdir_denied;
static const char *inject_destination;
static const char *inject_failure_source;
static gboolean inject_cancellation;
static gboolean inject_attribute_failure;
static gboolean counted_file_copy(GFile *source, GFile *destination, GFileCopyFlags flags,
                                  GCancellable *cancel, GFileProgressCallback progress,
                                  gpointer data, GError **error)
{
    ++observed_copies;
    g_autofree char *source_path = g_file_get_path(source);
    if(inject_failure_source && g_strcmp0(source_path, inject_failure_source) == 0) {
        if(inject_cancellation && cancel) g_cancellable_cancel(cancel);
        g_set_error_literal(error, G_IO_ERROR,
                            inject_cancellation ? G_IO_ERROR_CANCELLED : G_IO_ERROR_FAILED,
                            "Injected nested copy failure");
        return FALSE;
    }
    gboolean result = g_file_copy(source, destination, flags, cancel, progress, data, error);
    if(result && inject_destination) {
        g_assert_cmpint(g_mkdir(inject_destination, 0700), ==, 0);
        g_autofree char *sentinel = g_build_filename(inject_destination, "sentinel", NULL);
        g_assert_true(g_file_set_contents(sentinel, "other writer", -1, NULL));
        inject_destination = NULL;
    }
    return result;
}
static gboolean counted_make_directory(GFile *file, GCancellable *cancel, GError **error)
{
    g_autofree char *name = g_file_get_basename(file);
    gboolean result = g_file_make_directory(file, cancel, error);
    if(result && name && g_str_has_prefix(name, ".pcmanager-copy-partial-")) {
        ++staging_directories;
        g_autofree char *path = g_file_get_path(file);
        GStatBuf statbuf;
        if(path && g_stat(path, &statbuf) == 0) observed_staging_mode = statbuf.st_mode & 0777;
    }
    return result;
}
static int counted_mkdir(const char *path, mode_t mode)
{
    g_autofree char *name = g_path_get_basename(path);
    if(inject_mkdir_denied && g_str_has_prefix(name, ".pcmanager-copy-partial-")) {
        errno = EACCES;
        return -1;
    }
    int result = mkdir(path, mode);
    if(result == 0 && g_str_has_prefix(name, ".pcmanager-copy-partial-")) {
        ++staging_directories;
        GStatBuf statbuf;
        if(g_stat(path, &statbuf) == 0) observed_staging_mode = statbuf.st_mode & 0777;
    }
    return result;
}
static gboolean counted_set_attribute_uint32(GFile *file, const char *attribute,
                                              guint32 value, GFileQueryInfoFlags flags,
                                              GCancellable *cancel, GError **error)
{
    if(inject_attribute_failure && g_str_equal(attribute, G_FILE_ATTRIBUTE_UNIX_MODE)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "Injected metadata-copy failure");
        return FALSE;
    }
    return g_file_set_attribute_uint32(file, attribute, value, flags, cancel, error);
}
#define g_file_copy counted_file_copy
#define g_file_make_directory counted_make_directory
#define g_file_set_attribute_uint32 counted_set_attribute_uint32
#define mkdir counted_mkdir
#include "../src/file-ops-gtk4.c"
#undef g_file_copy
#undef g_file_make_directory
#undef g_file_set_attribute_uint32
#undef mkdir

static GMainLoop *loop;
static gboolean done;
static GError *failure;

static void finished(GObject *o, GAsyncResult *r, gpointer p)
{
    (void)o; (void)p;
    done = pcmgr_file_operation_finish(r, &failure);
    g_main_loop_quit(loop);
}

static gboolean run(PcmgrFileOperation op, GPtrArray *sources, GFile *dest)
{
    g_clear_error(&failure);
    done = FALSE;
    pcmgr_file_operation_async(op, sources, dest, NULL, finished, NULL);
    g_main_loop_run(loop);
    return done;
}

static GPtrArray *paths_to_files(char **paths, int n)
{
    GPtrArray *files = g_ptr_array_new_with_free_func(g_object_unref);
    for (int i = 0; i < n; i++)
        g_ptr_array_add(files, g_file_new_for_path(paths[i]));
    return files;
}

static char *read_all(const char *path)
{
    char *contents = NULL;
    GError *e = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, &e));
    g_assert_no_error(e);
    return contents;
}

static void assert_no_staging(const char *path)
{
    g_autoptr(GDir) directory = g_dir_open(path, 0, NULL);
    g_assert_nonnull(directory);
    const char *name;
    while((name = g_dir_read_name(directory)))
        g_assert_false(g_str_has_prefix(name, ".pcmanager-copy-partial-"));
}

int main(void)
{
    loop = g_main_loop_new(NULL, FALSE);
    GError *e = NULL;
    char *tmp = g_dir_make_tmp("pcmgr-conflicts-XXXXXX", &e);
    g_assert_no_error(e);

    char *srcdir = g_build_filename(tmp, "src", NULL);
    char *dstdir = g_build_filename(tmp, "dst", NULL);
    g_assert_cmpint(g_mkdir_with_parents(srcdir, 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(dstdir, 0700), ==, 0);

    char *names[] = { "a.txt", "b.txt", "c.txt" };
    char *texts[] = { "A-new", "B-new", "C-new" };
    for (int i = 0; i < 3; i++) {
        char *p = g_build_filename(srcdir, names[i], NULL);
        g_assert_true(g_file_set_contents(p, texts[i], -1, &e));
        g_assert_no_error(e);
        g_free(p);
    }
    /* b.txt already exists at the destination with different content. */
    char *existing = g_build_filename(dstdir, "b.txt", NULL);
    g_assert_true(g_file_set_contents(existing, "B-OLD", -1, &e));
    g_assert_no_error(e);

    char *srcpaths[3];
    for (int i = 0; i < 3; i++) srcpaths[i] = g_build_filename(srcdir, names[i], NULL);
    GPtrArray *sources = paths_to_files(srcpaths, 3);
    GFile *dest = g_file_new_for_path(dstdir);

    /* COPY: new files land, the collision is skipped, nothing is overwritten. */
    g_assert_false(run(PCMGR_COPY, sources, dest));
    g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_nonnull(g_strstr_len(failure->message, -1, "1 existing file skipped"));
    g_assert_nonnull(g_strstr_len(failure->message, -1, "b.txt"));
    char *got = read_all(existing);
    g_assert_cmpstr(got, ==, "B-OLD");
    g_free(got);
    char *pa = g_build_filename(dstdir, "a.txt", NULL);
    char *pc = g_build_filename(dstdir, "c.txt", NULL);
    got = read_all(pa); g_assert_cmpstr(got, ==, "A-new"); g_free(got);
    got = read_all(pc); g_assert_cmpstr(got, ==, "C-new"); g_free(got);
    for (int i = 0; i < 3; i++) /* sources intact after copy */
        g_assert_true(g_file_test(srcpaths[i], G_FILE_TEST_EXISTS));
    g_print("PASS: copy batch skips conflicts without overwriting\n");

    /* MOVE: non-conflicting files move, the collision stays put on both sides. */
    char *mnames[] = { "d.txt", "e.txt" };
    char *mtexts[] = { "D-new", "E-new" };
    char *msrc[2], *mdst[2];
    for (int i = 0; i < 2; i++) {
        msrc[i] = g_build_filename(srcdir, mnames[i], NULL);
        g_assert_true(g_file_set_contents(msrc[i], mtexts[i], -1, &e));
        g_assert_no_error(e);
    }
    mdst[0] = g_build_filename(dstdir, "d.txt", NULL);
    mdst[1] = g_build_filename(dstdir, "e.txt", NULL);
    g_assert_true(g_file_set_contents(mdst[1], "E-OLD", -1, &e));
    g_assert_no_error(e);
    GPtrArray *msources = paths_to_files(msrc, 2);
    g_assert_false(run(PCMGR_MOVE, msources, dest));
    g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_nonnull(g_strstr_len(failure->message, -1, "1 existing file skipped"));
    g_assert_nonnull(g_strstr_len(failure->message, -1, "e.txt"));
    g_assert_false(g_file_test(msrc[0], G_FILE_TEST_EXISTS));
    got = read_all(mdst[0]); g_assert_cmpstr(got, ==, "D-new"); g_free(got);
    g_assert_true(g_file_test(msrc[1], G_FILE_TEST_EXISTS));
    got = read_all(mdst[1]); g_assert_cmpstr(got, ==, "E-OLD"); g_free(got);
    g_ptr_array_unref(msources);
    g_print("PASS: move batch skips conflicts without moving or overwriting\n");

    /* Copying a folder into itself still fails instead of recursing. */
    char *sub = g_build_filename(srcdir, "sub", NULL);
    g_assert_cmpint(g_mkdir(sub, 0700), ==, 0);
    GFile *srcdir_file = g_file_new_for_path(srcdir);
    GFile *sub_file = g_file_new_for_path(sub);
    GPtrArray *self = g_ptr_array_new_with_free_func(g_object_unref);
    g_ptr_array_add(self, g_object_ref(srcdir_file));
    g_assert_false(run(PCMGR_COPY, self, sub_file));
    g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_nonnull(g_strstr_len(failure->message, -1, "itself"));
    g_ptr_array_unref(self);
    /* Reject a pre-existing directory before copying any of its children. */
    g_autofree char *collision = g_build_filename(dstdir, "directory-conflict", NULL);
    g_autofree char *sentinel = g_build_filename(collision, "sentinel", NULL);
    g_assert_cmpint(g_mkdir(collision, 0700), ==, 0);
    g_assert_true(g_file_set_contents(sentinel, "previous content", -1, NULL));
    g_autoptr(GFile) collision_file = g_file_new_for_path(collision);
    g_clear_error(&e); observed_copies = 0;
    g_assert_false(copy_tree_full(srcdir_file, collision_file, NULL, &e, NULL, 0, 0));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_assert_cmpuint(observed_copies, ==, 0);
    g_autofree char *preserved = read_all(sentinel);
    g_assert_cmpstr(preserved, ==, "previous content");
    g_assert_cmpint(g_unlink(sentinel), ==, 0);
    g_assert_cmpint(g_rmdir(collision), ==, 0);
    g_assert_cmpint(symlink("missing-target", collision), ==, 0);
    g_clear_error(&e); observed_copies = 0;
    g_assert_false(copy_tree_full(srcdir_file, collision_file, NULL, &e, NULL, 0, 0));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_assert_cmpuint(observed_copies, ==, 0);
    g_assert_true(g_file_test(collision, G_FILE_TEST_IS_SYMLINK));
    g_assert_cmpint(g_unlink(collision), ==, 0);
    /* A target created during copying still wins over the staging rename. */
    g_clear_error(&e); observed_copies = 0; inject_destination = collision;
    g_assert_false(copy_tree_full(srcdir_file, collision_file, NULL, &e, NULL, 0, 0));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_assert_cmpuint(observed_copies, >, 0);
    g_clear_pointer(&preserved, g_free);
    preserved = read_all(sentinel);
    g_assert_cmpstr(preserved, ==, "other writer");
    assert_no_staging(dstdir);
    g_clear_error(&e);
    /* A nested tree is populated within one top-level staging directory. */
    g_autofree char *nested = g_build_filename(sub, "nested", NULL);
    g_autofree char *nested_file = g_build_filename(nested, "content", NULL);
    g_autofree char *nested_link = g_build_filename(nested, "link", NULL);
    g_autofree char *success = g_build_filename(dstdir, "directory-success", NULL);
    g_autofree char *copied_file = g_build_filename(success, "sub", "nested", "content", NULL);
    g_assert_cmpint(g_mkdir(nested, 0700), ==, 0);
    g_assert_true(g_file_set_contents(nested_file, "nested data", -1, NULL));
    g_assert_cmpint(symlink("content", nested_link), ==, 0);
    g_autoptr(GFile) success_file = g_file_new_for_path(success);
    staging_directories = 0; observed_staging_mode = 0;
    mode_t old_umask = umask(0);
    g_assert_true(copy_tree_full(srcdir_file, success_file, NULL, &e, NULL, 0, 0));
    umask(old_umask);
    g_assert_no_error(e);
    g_assert_cmpuint(staging_directories, ==, 1);
    g_assert_cmpuint(observed_staging_mode, ==, 0700);
    g_autofree char *nested_contents = read_all(copied_file);
    g_assert_cmpstr(nested_contents, ==, "nested data");
    g_autofree char *copied_link = g_build_filename(success, "sub", "nested", "link", NULL);
    g_assert_true(g_file_test(copied_link, G_FILE_TEST_IS_SYMLINK));
    g_autofree char *link_target = g_file_read_link(copied_link, NULL);
    g_assert_cmpstr(link_target, ==, "content");
    g_autofree char *copied_dir = g_path_get_dirname(copied_file);
    GStatBuf copied_stat;
    g_assert_cmpint(g_stat(copied_dir, &copied_stat), ==, 0);
    g_assert_cmpuint(copied_stat.st_mode & 0777, ==, 0700);
    g_autofree char *failed = g_build_filename(dstdir, "directory-failed", NULL);
    g_autoptr(GFile) failed_file = g_file_new_for_path(failed);
    inject_mkdir_denied = TRUE; g_clear_error(&e);
    g_assert_false(copy_tree_full(srcdir_file, failed_file, NULL, &e, NULL, 0, 0));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_assert_false(g_file_test(failed, G_FILE_TEST_EXISTS));
    assert_no_staging(dstdir);
    inject_mkdir_denied = FALSE;
    inject_failure_source = nested_file;
    g_clear_error(&e);
    g_assert_false(copy_tree_full(srcdir_file, failed_file, NULL, &e, NULL, 0, 0));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_false(g_file_test(failed, G_FILE_TEST_EXISTS));
    assert_no_staging(dstdir);
    inject_failure_source = NULL;
    inject_attribute_failure = TRUE;
    g_clear_error(&e);
    g_assert_false(copy_tree_full(srcdir_file, failed_file, NULL, &e, NULL, 0, 0));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_false(g_file_test(failed, G_FILE_TEST_EXISTS));
    assert_no_staging(dstdir);
    inject_attribute_failure = FALSE;
    g_autoptr(GCancellable) nested_cancel = g_cancellable_new();
    inject_failure_source = nested_file;
    inject_cancellation = TRUE; g_clear_error(&e);
    g_assert_false(copy_tree_full(srcdir_file, failed_file, nested_cancel, &e, NULL, 0, 0));
    g_assert_error(e, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_false(g_file_test(failed, G_FILE_TEST_EXISTS));
    assert_no_staging(dstdir);
    inject_failure_source = NULL; inject_cancellation = FALSE;
    g_clear_error(&e);
    g_object_unref(srcdir_file); g_object_unref(sub_file);
    g_print("PASS: copy-into-self rejected\n");

    /* A pre-cancelled operation reports cancellation, not a summary. */
    GCancellable *cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_clear_error(&failure);
    done = FALSE;
    pcmgr_file_operation_async(PCMGR_COPY, sources, dest, cancel, finished, NULL);
    g_main_loop_run(loop);
    g_assert_false(done);
    g_assert_error(failure, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_object_unref(cancel);
    g_print("PASS: cancellation preserved\n");

    g_ptr_array_unref(sources);
    g_object_unref(dest);
    for (int i = 0; i < 3; i++) g_free(srcpaths[i]);
    for (int i = 0; i < 2; i++) { g_free(msrc[i]); g_free(mdst[i]); }
    g_free(sub); g_free(pa); g_free(pc); g_free(existing);
    g_free(srcdir); g_free(dstdir);
    /* Leave tmp for inspection on failure; remove on success. */
    char *rm_argv[] = { "rm", "-rf", tmp, NULL };
    (void)g_spawn_sync(NULL, rm_argv, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL);
    g_free(tmp);
    g_main_loop_unref(loop);
    g_print("PASS: file-ops conflicts\n");
    return 0;
}
