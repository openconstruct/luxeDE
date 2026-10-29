/* Exercise the lock contract used by Luxe Appearance and ObConf writers. */
#include "../../shared/luxe-rcxml-lock.h"

int main(void)
{
    g_autofree gchar *directory = g_dir_make_tmp("luxe-rcxml-lock-XXXXXX", NULL);
    g_autofree gchar *config = NULL;
    g_autoptr(GError) error = NULL;
    g_assert_nonnull(directory);
    config = g_build_filename(directory, "rc.xml", NULL);
    int first = luxe_rcxml_lock_acquire(config, &error);
    g_assert_no_error(error);
    g_assert_cmpint(first, >=, 0);
    int second = luxe_rcxml_lock_acquire(config, &error);
    g_assert_cmpint(second, ==, -1);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error(&error);
    close(first);
    second = luxe_rcxml_lock_acquire(config, &error);
    g_assert_no_error(error);
    g_assert_cmpint(second, >=, 0);
    close(second);
    g_autofree gchar *lock = g_build_filename(directory, ".rc.xml.luxe.lock", NULL);
    g_assert_cmpint(unlink(lock), ==, 0);
    g_assert_cmpint(rmdir(directory), ==, 0);
    return 0;
}
