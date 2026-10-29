/* A second instance must not hang behind a server that stops reading. */
#define SINGLE_INST_SEND_TIMEOUT_MS 250
#include "../src/single-inst.c"
#include <sys/wait.h>
#include <signal.h>

typedef struct {
    const GOptionEntry *entries;
    int screen, sock;
    gboolean result;
} SendRequest;

static gpointer send_complete_request(gpointer data)
{
    SendRequest *request = data;
    request->result = pass_args_to_existing_instance(request->entries, request->screen, request->sock);
    return NULL;
}

int main(void)
{
    int pair[2];
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
    int size = 4096;
    g_assert_cmpint(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof size), ==, 0);
    g_autofree char *text = g_strnfill(50000, 'x');
    GOptionEntry entries[] = {{"text", 0, 0, G_OPTION_ARG_STRING, &text, NULL, NULL}, {NULL}};
    pid_t child = fork();
    g_assert_cmpint(child, >=, 0);
    if(child == 0) {
        close(pair[1]);
        gboolean sent = pass_args_to_existing_instance(entries, 0, pair[0]);
        _exit(sent ? 2 : 0);
    }
    close(pair[0]);
    gint64 deadline = g_get_monotonic_time() + 1000000;
    int status = 0;
    pid_t result;
    do {
        result = waitpid(child, &status, WNOHANG);
        if(result == child) break;
        g_usleep(10000);
    } while(g_get_monotonic_time() < deadline);
    if(result != child) {
        kill(child, SIGKILL); waitpid(child, &status, 0);
        g_printerr("Second instance blocked on an unread socket\n");
        close(pair[1]);
        return 1;
    }
    close(pair[1]);
    g_assert_true(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    g_free(text); text = g_strdup("hello\nworld");
    char *files[] = {"folder with space", "file:///tmp/remote", NULL};
    char **file_values = files;
    GOptionEntry complete[] = {
        {"text", 0, 0, G_OPTION_ARG_STRING, &text, NULL, NULL},
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &file_values, NULL, NULL},
        {NULL}
    };
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
    SendRequest request = { complete, 1, pair[0], FALSE };
    GThread *sender = g_thread_new("ipc-sender", send_complete_request, &request);
    GString *wire = g_string_new(NULL);
    char buf[1024]; ssize_t n;
    const char *end_record = SINGLE_INST_PROTOCOL_END "\n";
    while((n = read(pair[1], buf, sizeof buf)) > 0) {
        g_string_append_len(wire, buf, n);
        if (g_str_has_suffix(wire->str, end_record)) break;
    }
    g_assert_cmpstr(g_strrstr(wire->str, end_record), !=, NULL);
    g_assert_cmpint(write(pair[1], "A", 1), ==, 1);
    g_thread_join(sender);
    g_assert_true(request.result);
    close(pair[1]);
    g_auto(GStrv) parts = g_strsplit(wire->str, "\n", -1);
    g_autofree char *cwd = g_get_current_dir();
    g_assert_cmpstr(parts[0], ==, SINGLE_INST_PROTOCOL_HEADER);
    g_autofree char *decoded_cwd = g_strcompress(parts[1] + 2);
    g_autofree char *decoded_text = g_strcompress(parts[4] + 2);
    g_autofree char *decoded_path = g_strcompress(parts[5] + 2);
    g_autofree char *expected_path = g_strconcat(cwd, "/folder with space", NULL);
    g_assert_cmpstr(decoded_cwd, ==, cwd);
    g_assert_cmpstr(parts[2], ==, "A:1");
    g_assert_cmpstr(parts[3], ==, "A:--text");
    g_assert_cmpstr(decoded_text, ==, text);
    g_assert_cmpstr(decoded_path, ==, expected_path);
    g_assert_cmpstr(parts[6], ==, "A:file:///tmp/remote");
    g_string_free(wire, TRUE);
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
    close(pair[1]);
    g_assert_false(pass_args_to_existing_instance(complete, 1, pair[0])); /* No SIGPIPE. */
    g_free(text); text = g_strnfill(SINGLE_INST_MAX_REQUEST + 1, 'x');
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), ==, 0);
    g_assert_false(pass_args_to_existing_instance(complete, 1, pair[0]));
    g_assert_cmpint(read(pair[1], buf, sizeof buf), ==, 0);
    close(pair[1]);
    /* A full listening backlog must not make connect() block either. */
    g_autofree char *dir = g_dir_make_tmp("pcmanager-connect-test-XXXXXX", NULL);
    g_assert_nonnull(dir);
    g_setenv("XDG_RUNTIME_DIR", dir, TRUE);
    g_setenv("DISPLAY", ":91", TRUE);
    SingleInstData data = { .prog_name = "blocked-fixture", .opt_entries = complete, .screen_num = 1 };
    char path[256]; get_socket_name(&data, path, sizeof path);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    g_strlcpy(address.sun_path, path, sizeof address.sun_path);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert_cmpint(listener, >=, 0);
    g_assert_cmpint(bind(listener, (struct sockaddr *)&address, sizeof address), ==, 0);
    g_assert_cmpint(listen(listener, 1), ==, 0);
    int waiting[16], count = 0;
    for(; count < 16; ++count) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        g_assert_cmpint(fd, >=, 0);
        int flags = fcntl(fd, F_GETFL);
        g_assert_cmpint(fcntl(fd, F_SETFL, flags | O_NONBLOCK), ==, 0);
        if(connect(fd, (struct sockaddr *)&address, sizeof address) < 0) {
            g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
            close(fd); break;
        }
        waiting[count] = fd;
    }
    g_assert_cmpint(count, >, 0);
    g_assert_cmpint(count, <, 16);
    child = fork();
    g_assert_cmpint(child, >=, 0);
    if(child == 0) {
        SingleInstResult result = single_inst_init(&data);
        _exit(result == SINGLE_INST_ERROR && data.sock == -1 ? 0 : 2);
    }
    deadline = g_get_monotonic_time() + 1000000;
    do {
        result = waitpid(child, &status, WNOHANG);
        if(result == child) break;
        g_usleep(10000);
    } while(g_get_monotonic_time() < deadline);
    if(result != child) {
        kill(child, SIGKILL); waitpid(child, &status, 0);
        g_printerr("Second instance blocked connecting to a full backlog\n");
        return 1;
    }
    g_assert_true(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    g_assert_true(g_file_test(path, G_FILE_TEST_EXISTS));
    for(int i = 0; i < count; ++i) close(waiting[i]);
    close(listener);
    g_assert_cmpint(unlink(path), ==, 0);
    g_assert_cmpint(rmdir(dir), ==, 0);
    return 0;
}
