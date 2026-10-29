/* Verify that the installed desktop entry passes the selected URI to Frame. */
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>

int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 2);
    g_autoptr(GError) error = NULL;
    g_autofree char *dir = g_dir_make_tmp("frame-desktop-test-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *program = g_build_filename(dir, "frame", NULL);
    g_autofree char *output = g_build_filename(dir, "argument", NULL);
    g_autofree char *image = g_build_filename(dir, "photo é space.png", NULL);
    g_assert_true(g_file_set_contents(program, "#!/bin/sh\nprintf '%s' \"$1\" > \"$FRAME_TEST_OUTPUT\"\n", -1, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_chmod(program, 0700), ==, 0);
    g_assert_true(g_file_set_contents(image, "fixture", -1, &error));
    g_assert_no_error(error);
    g_setenv("PATH", dir, TRUE);
    g_setenv("FRAME_TEST_OUTPUT", output, TRUE);
    g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new_from_filename(argv[1]);
    g_assert_nonnull(info);
    g_assert_cmpstr(g_app_info_get_commandline(G_APP_INFO(info)), ==, "frame %u");
    g_autoptr(GFile) file = g_file_new_for_path(image);
    GList one = { file, NULL, NULL };
    g_assert_true(g_app_info_launch(G_APP_INFO(info), &one, NULL, &error));
    g_assert_no_error(error);
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!g_file_test(output, G_FILE_TEST_EXISTS) && g_get_monotonic_time() < deadline)
        g_usleep(10000);
    g_autofree char *actual = NULL;
    g_assert_true(g_file_get_contents(output, &actual, NULL, &error));
    g_assert_no_error(error);
    /* GIO may supply a local path for %u; both forms identify the same file. */
    g_autoptr(GFile) received = g_file_new_for_commandline_arg(actual);
    g_assert_true(g_file_equal(file, received));
    g_assert_cmpint(g_unlink(output), ==, 0);
    g_assert_true(g_app_info_launch(G_APP_INFO(info), NULL, NULL, &error));
    g_assert_no_error(error);
    deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!g_file_test(output, G_FILE_TEST_EXISTS) && g_get_monotonic_time() < deadline)
        g_usleep(10000);
    g_clear_pointer(&actual, g_free);
    g_assert_true(g_file_get_contents(output, &actual, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(actual, ==, "");
    g_unlink(output); g_unlink(image); g_unlink(program); g_rmdir(dir);
    return 0;
}
