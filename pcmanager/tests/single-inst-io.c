/* Isolated regression for fragmented and stalled single-instance clients. */
#define SINGLE_INST_CLIENT_TIMEOUT_MS 1000
#include "../src/single-inst.c"
#include <glib/gstdio.h>
#include <fcntl.h>
#include <locale.h>

static gboolean flag;
static char *option_text;
static char **option_files;
static gint option_number = 7;
static int callbacks;
static char *last_cwd;
static SingleInstData *finalize_on_receive;
typedef struct { SingleInstData *data; SingleInstResult result; gint done; } ForwardRequest;

static gpointer forward_request(gpointer pointer)
{
    ForwardRequest *request = pointer;
    request->result = single_inst_init(request->data);
    g_atomic_int_set(&request->done, 1);
    return NULL;
}

static void received(const char *cwd, int screen)
{
    g_assert_cmpint(screen, ==, 1);
    g_free(last_cwd); last_cwd = g_strdup(cwd); callbacks++;
    if(finalize_on_receive) single_inst_finalize(finalize_on_receive);
}
static const GOptionEntry options[] = {
    {"flag", 0, 0, G_OPTION_ARG_NONE, &flag, NULL, NULL},
    {"text", 0, 0, G_OPTION_ARG_STRING, &option_text, NULL, NULL},
    {"number", 0, 0, G_OPTION_ARG_INT, &option_number, NULL, NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &option_files, NULL, NULL},
    {NULL}
};
static void spin(unsigned milliseconds)
{
    gint64 end = g_get_monotonic_time() + milliseconds * 1000;
    do { while (g_main_context_iteration(NULL, FALSE)); g_usleep(1000); }
    while (g_get_monotonic_time() < end);
}
static int connect_client_now(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    g_assert_cmpint(fd, >=, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    g_strlcpy(addr.sun_path, path, sizeof addr.sun_path);
    g_assert_cmpint(connect(fd, (struct sockaddr *)&addr, sizeof addr), ==, 0);
    return fd;
}
static int connect_client(const char *path)
{
    int fd = connect_client_now(path);
    spin(20);
    return fd;
}
static char *frame_request(const char *body)
{
    GString *request = g_string_new(SINGLE_INST_PROTOCOL_HEADER "\n");
    const char *line = body;
    while (*line) {
        const char *end = strchr(line, '\n');
        gsize length = end ? (gsize)(end - line) : strlen(line);
        g_string_append(request, "A:");
        g_string_append_len(request, line, length);
        g_string_append_c(request, '\n');
        if (!end) break;
        line = end + 1;
    }
    g_string_append(request, SINGLE_INST_PROTOCOL_END "\n");
    return g_string_free(request, FALSE);
}
int main(void)
{
    g_assert_nonnull(setlocale(LC_ALL, "C.UTF-8"));
    int credential_pair[2];
    g_assert_cmpint(socketpair(AF_UNIX, SOCK_STREAM, 0, credential_pair), ==, 0);
    g_assert_true(socket_peer_is_current_user(credential_pair[0]));
    close(credential_pair[0]); close(credential_pair[1]);
    g_autofree char *dir = g_dir_make_tmp("pcmanager-ipc-test-XXXXXX", NULL);
    g_assert_nonnull(dir);
    g_setenv("XDG_RUNTIME_DIR", dir, TRUE);
    g_setenv("DISPLAY", ":91", TRUE);
    g_set_prgname("ipc-test");
    SingleInstData server = { .prog_name = "ipc-fixture", .cb = received, .opt_entries = options };
    g_assert_cmpint(single_inst_init(&server), ==, SINGLE_INST_SERVER);
    g_assert_cmpint(fcntl(server.sock, F_GETFD) & FD_CLOEXEC, !=, 0);
    g_assert_cmpint(fcntl(server.sock, F_GETFL) & O_NONBLOCK, !=, 0);
    {
        char name[180], truncated[sizeof(((struct sockaddr_un *)0)->sun_path)];
        memset(name, 'x', sizeof name - 1); name[sizeof name - 1] = '\0';
        g_snprintf(truncated, sizeof truncated, "%s/%s-socket--91-v2", dir, name);
        g_assert_true(g_file_set_contents(truncated, "preserve", -1, NULL));
        SingleInstData oversized = { .prog_name = name, .opt_entries = options };
        g_assert_cmpint(single_inst_init(&oversized), ==, SINGLE_INST_ERROR);
        g_autofree char *contents = NULL;
        g_assert_true(g_file_get_contents(truncated, &contents, NULL, NULL));
        g_assert_cmpstr(contents, ==, "preserve");
        g_assert_cmpint(g_unlink(truncated), ==, 0);
    }
    char path[256]; get_socket_name(&server, path, sizeof path);
    int client = connect_client(path);
    g_assert_nonnull(clients);
    SingleInstClient *peer = clients->data;
    int peer_fd = g_io_channel_unix_get_fd(peer->channel);
    g_assert_cmpint(fcntl(peer_fd, F_GETFD) & FD_CLOEXEC, !=, 0);
    g_assert_cmpint(fcntl(peer_fd, F_GETFL) & O_NONBLOCK, !=, 0);
    g_autofree char *tail = frame_request("/tmp\n1\n--flag\n");
    g_assert_cmpint(write(client, tail, 1), ==, 1);
    spin(60); /* A blocking read here would freeze the main context indefinitely. */
    g_assert_cmpint(callbacks, ==, 0);
    g_assert_cmpint(write(client, tail + 1, strlen(tail) - 1), ==, (ssize_t)strlen(tail) - 1);
    g_assert_cmpint(shutdown(client, SHUT_WR), ==, 0);
    spin(60);
    g_assert_cmpint(callbacks, ==, 1);
    char acknowledgement = 0;
    g_assert_cmpint(read(client, &acknowledgement, 1), ==, 1);
    g_assert_cmpint(acknowledgement, ==, 'A');
    close(client);
    g_assert_cmpint(fcntl(peer_fd, F_GETFD), ==, -1);
    g_assert_cmpint(errno, ==, EBADF);
    g_assert_cmpstr(last_cwd, ==, "/tmp");
    g_assert_true(flag);
    flag = FALSE;
    option_text = g_strdup("original");
    client = connect_client(path);
    g_autofree char *invalid = frame_request("/tmp\n1\n--flag\n--text\nchanged\n--number\nnot-a-number\n");
    g_assert_cmpint(write(client, invalid, strlen(invalid)), ==, (ssize_t)strlen(invalid));
    close(client); spin(60);
    g_assert_cmpint(callbacks, ==, 1);
    g_assert_false(flag);
    g_assert_cmpstr(option_text, ==, "original");
    g_assert_cmpint(option_number, ==, 7);
    client = connect_client(path);
    g_autofree char *invalid_files = frame_request("/tmp\n1\n/tmp/file\n--unknown\n");
    g_assert_cmpint(write(client, invalid_files, strlen(invalid_files)), ==, (ssize_t)strlen(invalid_files));
    close(client); spin(60);
    g_assert_cmpint(callbacks, ==, 1);
    g_assert_null(option_files);
    client = connect_client(path);
    g_autofree char *empty = frame_request("/tmp\n1\n");
    g_assert_cmpint(write(client, empty, strlen(empty)), ==, (ssize_t)strlen(empty));
    g_assert_cmpint(shutdown(client, SHUT_WR), ==, 0); spin(60);
    g_assert_cmpint(callbacks, ==, 2);
    g_assert_cmpint(read(client, &acknowledgement, 1), ==, 1);
    g_assert_cmpint(acknowledgement, ==, 'A');
    close(client);
    g_assert_false(flag);
    g_assert_null(option_files);
    client = connect_client(path);
    const char *truncated = "/tmp\n1\n--flag";
    g_assert_cmpint(write(client, truncated, strlen(truncated)), ==, (ssize_t)strlen(truncated));
    close(client); spin(60);
    g_assert_cmpint(callbacks, ==, 2);
    /* The byte budget applies across clients, not independently per socket. */
    int peers[6];
    char payload[50000]; memset(payload, 'x', sizeof payload);
    for(int i = 0; i < 6; ++i) {
        peers[i] = connect_client(path);
        g_assert_cmpint(write(peers[i], payload, sizeof payload), ==, sizeof payload);
        spin(10);
    }
    g_assert_cmpuint(clients_total_bytes, <=, SINGLE_INST_MAX_TOTAL);
    g_assert_cmpuint(g_list_length(clients), <=, 5);
    for(int i = 0; i < 6; ++i) close(peers[i]);
    spin(50);
    g_assert_null(clients);
    g_assert_cmpuint(clients_total_bytes, ==, 0);
    client = connect_client(path);
    g_assert_cmpint(write(client, "/", 1), ==, 1);
    int idle[31];
    for(int i = 0; i < 31; ++i) {
        idle[i] = connect_client_now(path);
        spin(1);
    }
    g_assert_cmpuint(g_list_length(clients), ==, 32);
    spin(1200);
    g_assert_null(clients);
    g_assert_cmpuint(clients_total_bytes, ==, 0);
    close(client);
    for(int i = 0; i < 31; ++i) close(idle[i]);
    g_clear_pointer(&option_text, g_free);
    option_number = -1;
    flag = FALSE;
    g_clear_pointer(&option_files, g_strfreev);
    gboolean forward_flag = TRUE;
    char *forward_text = g_strdup("two words é");
    char **forward_files = g_new0(char *, 3);
    forward_files[0] = g_strdup("folder é");
    forward_files[1] = g_strdup("file:///tmp/example");
    GOptionEntry forward_options[] = {
        {"flag", 0, 0, G_OPTION_ARG_NONE, &forward_flag, NULL, NULL},
        {"text", 0, 0, G_OPTION_ARG_STRING, &forward_text, NULL, NULL},
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &forward_files, NULL, NULL},
        {NULL}
    };
    SingleInstData forward = { .prog_name = "ipc-fixture", .opt_entries = forward_options, .screen_num = 1 };
    ForwardRequest forward_request_data = { .data = &forward };
    GThread *forward_thread = g_thread_new("ipc-forward", forward_request, &forward_request_data);
    gint64 forward_deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!g_atomic_int_get(&forward_request_data.done) && g_get_monotonic_time() < forward_deadline)
        spin(10);
    g_assert_true(g_atomic_int_get(&forward_request_data.done));
    g_thread_join(forward_thread);
    g_assert_cmpint(forward_request_data.result, ==, SINGLE_INST_CLIENT);
    g_assert_cmpint(forward.sock, ==, -1);
    g_free(forward_text);
    g_strfreev(forward_files);
    int unrelated[2]; g_assert_cmpint(pipe(unrelated), ==, 0);
    single_inst_finalize(&forward);
    g_assert_cmpint(fcntl(unrelated[0], F_GETFD), >=, 0);
    close(unrelated[0]); close(unrelated[1]);
    spin(60);
    g_assert_cmpint(callbacks, ==, 3);
    g_assert_true(flag);
    g_assert_cmpstr(option_text, ==, "two words é");
    g_autofree char *cwd = g_get_current_dir();
    g_autofree char *expected = g_strconcat(cwd, "/folder é", NULL);
    g_assert_nonnull(option_files);
    g_assert_cmpstr(option_files[0], ==, expected);
    g_assert_cmpstr(option_files[1], ==, "file:///tmp/example");
    g_clear_pointer(&option_text, g_free);
    g_clear_pointer(&option_files, g_strfreev);
    client = connect_client(path);
    finalize_on_receive = &server;
    g_autofree char *complete = frame_request("/tmp\n1\n--flag\n");
    g_assert_cmpint(write(client, complete, strlen(complete)), ==, (ssize_t)strlen(complete));
    g_assert_cmpint(shutdown(client, SHUT_WR), ==, 0); spin(60);
    g_assert_cmpint(callbacks, ==, 4);
    g_assert_cmpint(read(client, &acknowledgement, 1), ==, 1);
    g_assert_cmpint(acknowledgement, ==, 'A');
    close(client);
    g_assert_cmpint(server.sock, ==, -1);
    g_assert_null(clients);
    g_assert_cmpuint(clients_total_bytes, ==, 0);
    /* Upgrade compatibility: detect a previous unversioned listener without
     * sending v2 bytes to it, and fail with a clear instruction. */
    {
        SingleInstData previous = { .prog_name = "ipc-fixture" };
        char legacy_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
        get_legacy_socket_name(&previous, legacy_path, sizeof legacy_path);
        int legacy = socket(AF_UNIX, SOCK_STREAM, 0);
        g_assert_cmpint(legacy, >=, 0);
        struct sockaddr_un legacy_addr = { .sun_family = AF_UNIX };
        g_strlcpy(legacy_addr.sun_path, legacy_path, sizeof legacy_addr.sun_path);
        unlink(legacy_path);
        g_assert_cmpint(bind(legacy, (struct sockaddr *)&legacy_addr, sizeof legacy_addr), ==, 0);
        g_assert_cmpint(listen(legacy, 5), ==, 0);
        SingleInstData upgraded = { .prog_name = "ipc-fixture", .cb = received, .opt_entries = options };
        g_assert_cmpint(single_inst_init(&upgraded), ==, SINGLE_INST_ERROR);
        g_assert_cmpint(upgraded.sock, ==, -1);
        int old_connection = accept(legacy, NULL, NULL);
        g_assert_cmpint(old_connection, >=, 0);
        char byte;
        g_assert_cmpint(recv(old_connection, &byte, 1, MSG_DONTWAIT), ==, 0);
        close(old_connection); close(legacy); unlink(legacy_path);
    }
    g_assert_cmpint(g_rmdir(dir), ==, 0);
    g_free(last_cwd);
    g_free(option_text);
    g_strfreev(option_files);
    return 0;
}
