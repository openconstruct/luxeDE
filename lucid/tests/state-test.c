#include "state.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>
static void roundtrip(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("lucid-test-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = lucid_session_path(dir, "../../日本語 / work");
    g_assert_true(g_str_has_prefix(path, dir));
    g_assert_null(strstr(path + strlen(dir), ".."));
    GKeyFile *state = g_key_file_new();
    g_key_file_set_integer(state,"Session","version",1);
    g_key_file_set_integer(state,"Session","count",1);
    g_key_file_set_string(state,"Document 0","text","a\n# café\n\\escape\t\n");
    g_assert_true(lucid_state_write(state,path,&error)); g_assert_no_error(error);
    GKeyFile *copy = lucid_state_read(path,&error); g_assert_no_error(error);
    char *text = g_key_file_get_string(copy,"Document 0","text",NULL);
    g_assert_cmpstr(text,==,"a\n# café\n\\escape\t\n");
    struct stat st; g_stat(path,&st); g_assert_cmpint(st.st_mode & 0777,==,0600);
    g_key_file_set_string(state,"Document 0","text","replacement");
    g_assert_true(lucid_state_write(state,path,&error)); g_assert_no_error(error);
    g_key_file_unref(copy); copy=lucid_state_read(path,&error); g_assert_no_error(error);
    g_free(text); text=g_key_file_get_string(copy,"Document 0","text",NULL); g_assert_cmpstr(text,==,"replacement");
    g_free(text); g_key_file_unref(copy); g_key_file_unref(state); g_remove(path);g_rmdir(dir);g_free(path);g_free(dir);
}
static void invalid(void)
{
    GError *error=NULL;char *dir=g_dir_make_tmp("lucid-invalid-XXXXXX",&error);g_assert_no_error(error);
    char *path=lucid_session_path(dir,"invalid");
    g_file_set_contents(path,"[Session]\nversion=99\ncount=1\n",-1,NULL);
    g_assert_null(lucid_state_read(path,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);
    g_file_set_contents(path,"[Session]\nversion=1\ncount=-1\n",-1,NULL);
    g_assert_null(lucid_state_read(path,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&error);
    g_remove(path);g_rmdir(dir);g_free(path);g_free(dir);
}
static void preserve(void)
{
    g_autofree char *dir = g_dir_make_tmp("lucid-preserve-XXXXXX", NULL);
    g_autoptr(GKeyFile) state = g_key_file_new();
    g_key_file_set_integer(state, "Session", "version", 1);
    g_key_file_set_integer(state, "Session", "count", 1);
    g_key_file_set_string(state, "Document 0", "text", "retained edits");
    g_autoptr(GError) error = NULL;
    g_autofree char *path = lucid_state_preserve(state, dir, &error);
    g_assert_no_error(error); g_assert_nonnull(path);
    struct stat info;
    g_assert_cmpint(lstat(path, &info), ==, 0);
    g_assert_cmpuint(info.st_mode & 0777, ==, 0600);
    g_autofree char *again = lucid_state_preserve(state, dir, &error);
    g_assert_no_error(error); g_assert_cmpstr(again, ==, path);
    GDir *files = g_dir_open(dir, 0, NULL);
    g_assert_nonnull(g_dir_read_name(files)); g_assert_null(g_dir_read_name(files)); g_dir_close(files);
    g_autoptr(GKeyFile) other = lucid_state_read(path, NULL);
    g_key_file_set_string(other, "Document 0", "text", "external replacement");
    g_assert_true(lucid_state_write(other, path, NULL));
    g_assert_null(lucid_state_preserve(state, dir, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS); g_clear_error(&error);
    g_autoptr(GKeyFile) retained = lucid_state_read(path, NULL);
    g_autofree char *text = g_key_file_get_string(retained, "Document 0", "text", NULL);
    g_assert_cmpstr(text, ==, "external replacement");
    g_unlink(path);
    g_assert_cmpint(symlink("missing-target", path), ==, 0);
    g_assert_null(lucid_state_preserve(state, dir, &error)); g_assert_nonnull(error); g_clear_error(&error);
    g_unlink(path); g_rmdir(dir);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/session/roundtrip",roundtrip);g_test_add_func("/session/invalid",invalid);g_test_add_func("/session/preserve",preserve);return g_test_run();}
