/*
 *      single-inst.c: simple IPC mechanism for single instance app
 *
 *      Copyright 2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *      Copyright 2012-2018 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
 *      Copyright (C) 2026 Jerry Howell
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "single-inst.h"

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>

#define SINGLE_INST_MAX_REQUEST (64 * 1024)
#define SINGLE_INST_MAX_ARGS 256
#define SINGLE_INST_MAX_CLIENTS 32
#define SINGLE_INST_MAX_TOTAL (256 * 1024)
#define SINGLE_INST_PROTOCOL_HEADER "LUXE-PCMANAGER-IPC/2"
#define SINGLE_INST_PROTOCOL_END "LUXE-PCMANAGER-END/2"
#ifndef SINGLE_INST_CLIENT_TIMEOUT_MS
#define SINGLE_INST_CLIENT_TIMEOUT_MS 15000
#endif
#ifndef SINGLE_INST_SEND_TIMEOUT_MS
#define SINGLE_INST_SEND_TIMEOUT_MS 2000
#endif

typedef struct _SingleInstClient SingleInstClient;
struct _SingleInstClient
{
    GIOChannel* channel;
    char* cwd;
    int screen_num;
    GPtrArray* argv;
    const GOptionEntry* opt_entries;
    SingleInstCallback callback;
    guint watch;
    guint timeout;
    gboolean invalid;
    gboolean protocol_started;
    gboolean protocol_complete;
    gsize total_bytes;
    GString* line;
};

static GList* clients = NULL;
static gsize clients_total_bytes = 0;

static void get_socket_name(SingleInstData* data, char* buf, int len);
static void get_legacy_socket_name(SingleInstData* data, char* buf, int len);
static gboolean on_server_socket_event(GIOChannel* ioc, GIOCondition cond, gpointer data);
static gboolean on_client_socket_event(GIOChannel* ioc, GIOCondition cond, gpointer user_data);

static void single_inst_client_free(SingleInstClient* client)
{
    g_io_channel_shutdown(client->channel, FALSE, NULL);
    g_io_channel_unref(client->channel);
    if(client->watch) g_source_remove(client->watch);
    if(client->timeout) g_source_remove(client->timeout);
    clients_total_bytes -= client->total_bytes;
    g_string_free(client->line, TRUE);
    g_free(client->cwd);
    g_ptr_array_free(client->argv, TRUE);
    g_slice_free(SingleInstClient, client);
    /* g_debug("free client"); */
}

/* Protocol v2 has explicit header/end records; EOF alone never commits input. */
static gboolean request_value_bounded(const char* value)
{
    gsize length = 0;
    if(!value) return FALSE;
    while(length <= SINGLE_INST_MAX_REQUEST && value[length]) ++length;
    return length <= SINGLE_INST_MAX_REQUEST;
}

static gboolean append_request_line(GString* request, const char* value, guint* lines)
{
    if(!request_value_bounded(value) || *lines >= SINGLE_INST_MAX_ARGS + 1) return FALSE;
    g_autofree char *escaped = g_strescape(value, NULL);
    gsize length = strlen(escaped);
    if(length > SINGLE_INST_MAX_REQUEST || request->len + length + 3 > SINGLE_INST_MAX_TOTAL)
        return FALSE;
    g_string_append(request, "A:");
    g_string_append_len(request, escaped, length);
    g_string_append_c(request, '\n');
    ++*lines;
    return TRUE;
}

static gboolean pass_args_to_existing_instance(const GOptionEntry* opt_entries, int screen_num, int sock)
{
    GString *request = g_string_sized_new(1024);
    g_autofree char *cwd = g_get_current_dir();
    g_autofree char *screen = g_strdup_printf("%d", screen_num);
    guint lines = 0;
    g_string_append(request, SINGLE_INST_PROTOCOL_HEADER "\n");
    gboolean ok = screen_num >= 0 && append_request_line(request, cwd ? cwd : ".", &lines) &&
                  append_request_line(request, screen, &lines);
    for(const GOptionEntry *ent = opt_entries; ok && ent->long_name; ++ent) {
        g_autofree char *option = g_strconcat("--", ent->long_name, NULL);
        switch(ent->arg) {
        case G_OPTION_ARG_NONE:
            if(*(gboolean*)ent->arg_data) ok = append_request_line(request, option, &lines);
            break;
        case G_OPTION_ARG_STRING:
        case G_OPTION_ARG_FILENAME: {
            const char *str = *(char**)ent->arg_data;
            if(str && *str) {
                ok = append_request_line(request, option, &lines);
                if(ok && g_str_has_prefix(str, "--")) ok = append_request_line(request, "--", &lines);
                if(ok) ok = append_request_line(request, str, &lines);
            }
            break;
        }
        case G_OPTION_ARG_INT: {
            gint value = *(gint*)ent->arg_data;
            if(value >= 0) {
                g_autofree char *number = g_strdup_printf("%d", value);
                ok = append_request_line(request, option, &lines) && append_request_line(request, number, &lines);
            }
            break;
        }
        case G_OPTION_ARG_STRING_ARRAY:
        case G_OPTION_ARG_FILENAME_ARRAY: {
            char **strv = *(char***)ent->arg_data;
            if(strv && *strv) {
                if(*ent->long_name) ok = append_request_line(request, option, &lines);
                for(; ok && *strv; ++strv) {
                    const char *str = *strv;
                    if(!request_value_bounded(str)) { ok = FALSE; break; }
                    g_autofree char *scheme = g_uri_parse_scheme(str);
                    g_autofree char *expanded = NULL;
                    if(str[0] == '~' && str[1] == '/' && g_getenv("HOME")) {
                        if(!request_value_bounded(g_getenv("HOME"))) { ok = FALSE; break; }
                        expanded = g_strconcat(g_getenv("HOME"), str + 1, NULL);
                    }
                    else if(!(str[0] == '~' && str[1] == '\0') && !scheme && str[0] != '/')
                        expanded = g_strconcat(cwd ? cwd : ".", "/", str, NULL);
                    ok = append_request_line(request, expanded ? expanded : str, &lines);
                }
            }
            break;
        }
        case G_OPTION_ARG_DOUBLE: {
            char number[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_dtostr(number, sizeof number, *(gdouble*)ent->arg_data);
            ok = append_request_line(request, option, &lines) && append_request_line(request, number, &lines);
            break;
        }
        case G_OPTION_ARG_INT64: {
            g_autofree char *number = g_strdup_printf("%" G_GINT64_FORMAT, *(gint64*)ent->arg_data);
            ok = append_request_line(request, option, &lines) && append_request_line(request, number, &lines);
            break;
        }
        case G_OPTION_ARG_CALLBACK: /* This protocol does not forward callbacks. */
            break;
        }
    }
    if(ok) g_string_append(request, SINGLE_INST_PROTOCOL_END "\n");
    gint64 deadline = g_get_monotonic_time() + (gint64)SINGLE_INST_SEND_TIMEOUT_MS * 1000;
    gsize sent = 0;
    while(ok && sent < request->len) {
        if(g_get_monotonic_time() >= deadline) { ok = FALSE; break; }
        ssize_t n = send(sock, request->str + sent, request->len - sent, MSG_DONTWAIT | MSG_NOSIGNAL);
        if(n > 0) { sent += (gsize)n; continue; }
        if(n < 0 && errno == EINTR) continue;
        if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            gint64 remaining = deadline - g_get_monotonic_time();
            if(remaining <= 0) { ok = FALSE; break; }
            struct pollfd pfd = { .fd = sock, .events = POLLOUT };
            int ready = poll(&pfd, 1, (int)((remaining + 999) / 1000));
            if(ready < 0 && errno == EINTR) continue;
            if(ready <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) { ok = FALSE; break; }
            continue;
        }
        ok = FALSE;
    }
    if(ok && shutdown(sock, SHUT_WR) != 0) ok = FALSE;
#ifndef SINGLE_INST_TEST_NO_ACK_WAIT
    while(ok) {
        gint64 remaining = deadline - g_get_monotonic_time();
        if(remaining <= 0) { ok = FALSE; break; }
        struct pollfd pfd = { .fd = sock, .events = POLLIN };
        int ready = poll(&pfd, 1, (int)((remaining + 999) / 1000));
        if(ready < 0 && errno == EINTR) continue;
        if(ready <= 0 || (pfd.revents & (POLLERR | POLLNVAL)) ||
           ((pfd.revents & POLLHUP) && !(pfd.revents & POLLIN))) { ok = FALSE; break; }
        char response = 0;
        ssize_t n = recv(sock, &response, 1, MSG_DONTWAIT);
        if(n == 1) { ok = response == 'A'; break; }
        if(n == 0) { ok = FALSE; break; }
        if(errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) { ok = FALSE; break; }
    }
#endif
    close(sock);
    g_string_free(request, TRUE);
    return ok;
}

static int connect_existing(int sock, const struct sockaddr *addr, socklen_t length)
{
    int result = connect(sock, addr, length);
    if(result == 0 || errno != EINPROGRESS) return result;
    gint64 deadline = g_get_monotonic_time() + (gint64)SINGLE_INST_SEND_TIMEOUT_MS * 1000;
    for(;;) {
        gint64 remaining = deadline - g_get_monotonic_time();
        if(remaining <= 0) { errno = ETIMEDOUT; return -1; }
        struct pollfd pfd = { .fd = sock, .events = POLLOUT };
        int ready = poll(&pfd, 1, (int)((remaining + 999) / 1000));
        if(ready < 0 && errno == EINTR) continue;
        if(ready < 0) return -1;
        if(ready == 0) { errno = ETIMEDOUT; return -1; }
        int error = 0;
        socklen_t error_length = sizeof error;
        if(getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &error_length) < 0) return -1;
        if(error) { errno = error; return -1; }
        return 0;
    }
}

static gboolean socket_peer_is_current_user(int fd)
{
#ifdef SO_PEERCRED
    struct { pid_t pid; uid_t uid; gid_t gid; } credentials;
    socklen_t length = sizeof credentials;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0 &&
           length == sizeof credentials && credentials.uid == getuid();
#else
    (void)fd;
    return FALSE; /* Do not forward paths/options without peer authentication. */
#endif
}

/* A v1 server cannot parse the framed v2 request. Probe the old endpoint
 * without sending data so upgrades fail clearly instead of starting a second
 * browser process or corrupting the old server's option state. */
static gboolean legacy_server_present(SingleInstData *data)
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    get_legacy_socket_name(data, addr.sun_path, sizeof addr.sun_path);
    if(addr.sun_path[0] == '\0') return FALSE;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) return FALSE;
    int flags = fcntl(fd, F_GETFL);
    if(flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd); return FALSE;
    }
#ifdef SUN_LEN
    socklen_t length = SUN_LEN(&addr);
#else
    socklen_t length = strlen(addr.sun_path) + sizeof(addr.sun_family);
#endif
    gboolean present = FALSE;
    if(connect_existing(fd, (struct sockaddr *)&addr, length) == 0)
        present = TRUE;
    else if(errno != ECONNREFUSED && errno != ENOENT)
        present = TRUE; /* timeout/permission/other uncertainty: fail closed */
    close(fd);
    return present;
}

/**
 * single_inst_init
 * @data: data filled by caller
 * Return value: result from initialization
 *
 * Initializes single instance IPC, verifies if it's first instance and
 * returns result of initialization.
 * Data passed to single_inst_init() should be kept intact until next
 * mandatory call to single_inst_finalize().
 */
SingleInstResult single_inst_init(SingleInstData* data)
{
    struct sockaddr_un addr;
    int addr_len;
    int ret;
    int reuse;
    char *dir_sep;

    data->io_channel = NULL;
    data->io_watch = 0;
    if((data->sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        return SINGLE_INST_ERROR;
    if(fcntl(data->sock, F_SETFD, FD_CLOEXEC) < 0) {
        close(data->sock); data->sock = -1;
        return SINGLE_INST_ERROR;
    }
    int flags = fcntl(data->sock, F_GETFL);
    if(flags < 0 || fcntl(data->sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(data->sock); data->sock = -1;
        return SINGLE_INST_ERROR;
    }

    /* FIXME: use abstract socket? */
    addr.sun_family = AF_UNIX;
    get_socket_name(data, addr.sun_path, sizeof(addr.sun_path));
    if (addr.sun_path[0] == '\0' || strlen(addr.sun_path) >= sizeof(addr.sun_path)) {
        close(data->sock);
        data->sock = -1;
        return SINGLE_INST_ERROR;
    }
#ifdef SUN_LEN
    addr_len = SUN_LEN(&addr);
#else
    addr_len = strlen(addr.sun_path) + sizeof(addr.sun_family);
#endif

    /* try to connect to existing instance */
    if(connect_existing(data->sock, (struct sockaddr*)&addr, addr_len) == 0)
    {
        if(!socket_peer_is_current_user(data->sock)) {
            close(data->sock);
            data->sock = -1;
            g_printerr("Refusing to forward a request to a PCManager socket owned by another user.\n");
            return SINGLE_INST_ERROR;
        }
        /* connected successfully, pass args in opt_entries to server process as argv and exit. */
        gboolean sent = pass_args_to_existing_instance(data->opt_entries, data->screen_num, data->sock);
        data->sock = -1; /* The sender owns and closes it. */
        if(!sent) g_printerr("Could not forward the request to the running PCManager.\n");
        return sent ? SINGLE_INST_CLIENT : SINGLE_INST_ERROR;
    }
    if (errno != ECONNREFUSED && errno != ENOENT) {
        close(data->sock);
        data->sock = -1;
        return SINGLE_INST_ERROR;
    }
    if(legacy_server_present(data)) {
        close(data->sock);
        data->sock = -1;
        g_printerr("A previous PCManager version is still running. Close it and retry.\n");
        return SINGLE_INST_ERROR;
    }
    /* There is no existing server, and we are in the first instance. */
    unlink(addr.sun_path); /* delete stale socket file if it exists. */

    /* root-instance issue: /root/.cache might not exist, see
       https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=874753 */
    dir_sep = strrchr(addr.sun_path, '/');
    if (dir_sep)
    {
        *dir_sep = '\0';
        g_mkdir_with_parents(addr.sun_path, 0700);
        *dir_sep = '/';
    }

    reuse = 1;
    ret = setsockopt( data->sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse) );
    if(ret || bind(data->sock, (struct sockaddr*)&addr, addr_len) == -1)
        return SINGLE_INST_ERROR;

    data->io_channel = g_io_channel_unix_new(data->sock);
    if(data->io_channel == NULL)
        return SINGLE_INST_ERROR;

    g_io_channel_set_encoding(data->io_channel, NULL, NULL);
    g_io_channel_set_buffered(data->io_channel, FALSE);

    if(listen(data->sock, 5) == -1)
        return SINGLE_INST_ERROR;

    data->io_watch = g_io_add_watch(data->io_channel,
                                    G_IO_IN|G_IO_ERR|G_IO_HUP,
                                    (GIOFunc)on_server_socket_event, data);
    return SINGLE_INST_SERVER;
}

/**
 * single_inst_finalize
 * @data: data filled by call to single_inst_init()
 *
 * Terminates single instance IPC started by single_inst_init(). Should be
 * always called after single_inst_init().
 */
void single_inst_finalize(SingleInstData* data)
{
    if(data->sock >=0)
    {
        close(data->sock);
        data->sock = -1;

        if(data->io_channel)
        {
            char sock_path[256];

            /* disconnect all clients */
            if(clients)
            {
                for(GList *item = clients; item; item = item->next)
                    single_inst_client_free(item->data);
                g_list_free(clients);
                clients = NULL;
            }

            if(data->io_watch)
            {
                g_source_remove(data->io_watch);
                data->io_watch = 0;
            }
            g_io_channel_unref(data->io_channel);
            data->io_channel = NULL;
            /* remove the file */
            get_socket_name(data, sock_path, 256);
            unlink(sock_path);
        }
    }
}

static inline gboolean parse_args(SingleInstClient* client)
{
    GOptionContext* ctx = g_option_context_new("");
    int argc = client->argv->len;
    char** argv = g_new(char*, argc + 1);
    memcpy(argv, client->argv->pdata, sizeof(char*) * argc);
    argv[argc] = NULL;
    g_option_context_add_main_entries(ctx, client->opt_entries, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(ctx, &argc, &argv, &error)) {
        g_warning("Ignoring malformed PCManager IPC request: %s", error->message);
        g_clear_error(&error);
        g_free(argv);
        g_option_context_free(ctx);
        return FALSE;
    }
    g_free(argv);
    g_option_context_free(ctx);
    int fd = g_io_channel_unix_get_fd(client->channel);
    if(send(fd, "A", 1, MSG_DONTWAIT | MSG_NOSIGNAL) != 1)
        return FALSE;
    if(client->callback)
        client->callback(client->cwd, client->screen_num);
    return TRUE;
}

static gboolean single_inst_client_timeout(gpointer user_data)
{
    SingleInstClient* client = user_data;
    client->timeout = 0;
    clients = g_list_remove(clients, client);
    single_inst_client_free(client);
    return G_SOURCE_REMOVE;
}

static gboolean single_inst_client_line(SingleInstClient* client)
{
    const char *line = client->line->str;
    const char *value;
    if(client->protocol_complete) return FALSE;
    if(!client->line->len) return TRUE;
    if(!client->protocol_started) {
        if(strcmp(line, SINGLE_INST_PROTOCOL_HEADER) != 0) return FALSE;
        client->protocol_started = TRUE;
        g_string_truncate(client->line, 0);
        return TRUE;
    }
    if(strcmp(line, SINGLE_INST_PROTOCOL_END) == 0) {
        client->protocol_complete = TRUE;
        g_string_truncate(client->line, 0);
        return TRUE;
    }
    if(!g_str_has_prefix(line, "A:")) return FALSE;
    value = line + 2;
    if(!client->cwd) client->cwd = g_strcompress(value);
    else if(client->screen_num == -1) {
        char *end = NULL;
        errno = 0;
        long parsed = strtol(value, &end, 10);
        if(errno || *end != '\0' || parsed < 0 || parsed > INT_MAX) return FALSE;
        client->screen_num = (int)parsed;
    } else {
        if(client->argv->len >= SINGLE_INST_MAX_ARGS) {
            g_warning("Ignoring PCManager IPC request with too many arguments");
            return FALSE;
        }
        g_ptr_array_add(client->argv, g_strcompress(value));
    }
    g_string_truncate(client->line, 0);
    return TRUE;
}

static gboolean on_client_socket_event(GIOChannel* ioc, GIOCondition cond, gpointer user_data)
{
    SingleInstClient* client = user_data;
    gboolean eof = FALSE;
    size_t processed = 0;
    int fd = g_io_channel_unix_get_fd(ioc);
    if(cond & (G_IO_ERR | G_IO_NVAL)) client->invalid = TRUE;
    while(!client->invalid && processed < 16384) {
        char buffer[4096];
        ssize_t count = read(fd, buffer, sizeof buffer);
        if(count < 0 && errno == EINTR) continue;
        if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if(count < 0) { client->invalid = TRUE; break; }
        if(count == 0) { eof = TRUE; break; }
        processed += (size_t)count;
        for(ssize_t i = 0; i < count; ++i) {
            unsigned char ch = (unsigned char)buffer[i];
            if(client->total_bytes >= SINGLE_INST_MAX_TOTAL || clients_total_bytes >= SINGLE_INST_MAX_TOTAL) {
                g_warning("Ignoring oversized PCManager IPC request");
                client->invalid = TRUE; break;
            }
            ++client->total_bytes; ++clients_total_bytes;
            if(ch == '\n') {
                if(!single_inst_client_line(client)) { client->invalid = TRUE; break; }
            } else if(ch < 0x20 || client->line->len >= SINGLE_INST_MAX_REQUEST) {
                g_warning("Ignoring oversized PCManager IPC request or invalid control character");
                client->invalid = TRUE; break;
            } else g_string_append_c(client->line, (char)ch);
        }
    }
    if(eof || client->invalid) {
        clients = g_list_remove(clients, client);
        client->watch = 0; /* GLib removes the currently dispatched source. */
        if(client->timeout) { g_source_remove(client->timeout); client->timeout = 0; }
        if(eof && !client->invalid && client->line->len == 0 && client->protocol_started &&
           client->protocol_complete && client->cwd && client->screen_num >= 0)
            parse_args(client);
        single_inst_client_free(client);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_server_socket_event(GIOChannel* ioc, GIOCondition cond, gpointer user_data)
{
    SingleInstData* data = user_data;

    if ( cond & G_IO_IN )
    {
        int client_sock = accept(g_io_channel_unix_get_fd(ioc), NULL, 0);
        if(client_sock != -1)
        {
            int flags = fcntl(client_sock, F_GETFL);
            if(flags < 0 || fcntl(client_sock, F_SETFL, flags | O_NONBLOCK) < 0 ||
               fcntl(client_sock, F_SETFD, FD_CLOEXEC) < 0) {
                close(client_sock);
                return TRUE;
            }
            if (g_list_length(clients) >= SINGLE_INST_MAX_CLIENTS) {
                close(client_sock);
                return TRUE;
            }
            SingleInstClient* client = g_slice_new0(SingleInstClient);
            client->channel = g_io_channel_unix_new(client_sock);
            g_io_channel_set_encoding(client->channel, NULL, NULL);
            client->screen_num = -1;
            client->argv = g_ptr_array_new_with_free_func(g_free);
            client->line = g_string_sized_new(1024);
            client->callback = data->cb;
            client->opt_entries = data->opt_entries;
            g_ptr_array_add(client->argv, g_strdup(g_get_prgname()));
            client->watch = g_io_add_watch(client->channel, G_IO_IN|G_IO_ERR|G_IO_HUP,
                                           on_client_socket_event, client);
            client->timeout = g_timeout_add(SINGLE_INST_CLIENT_TIMEOUT_MS, single_inst_client_timeout, client);
            clients = g_list_prepend(clients, client);
            /* g_debug("accept new client"); */
        }
        else
            g_debug("accept() failed!\n%s", g_strerror(errno));
    }

    if(cond & (G_IO_ERR|G_IO_HUP))
    {
        single_inst_finalize(data);
        single_inst_init(data);
        return FALSE;
    }

    return TRUE;
}

static void get_socket_name_version(SingleInstData* data, char* buf, int len, const char *version)
{
    const char *dpy = g_getenv("DISPLAY");
    const char *runtime_dir = NULL;
    char *host = NULL;
    gint64 parsed_display = 0;
    int dpynum = 0;
    int written;

    if (len <= 0)
        return;

    if (dpy != NULL) {
        const char *p = strrchr(dpy, ':');
        if (p != NULL && p[1] != '\0') {
            gchar *end = NULL;
            parsed_display = g_ascii_strtoll(p + 1, &end, 10);
            gboolean valid_suffix = *end == '\0';
            if (*end == '.' && end[1] != '\0') {
                valid_suffix = TRUE;
                for (const char *q = end + 1; *q; ++q)
                    if (!g_ascii_isdigit(*q)) { valid_suffix = FALSE; break; }
            }
            if (end != p + 1 && valid_suffix && parsed_display >= 0 && parsed_display <= G_MAXINT)
                dpynum = (int)parsed_display;
            host = g_strndup(dpy, (gsize)(p - dpy));
            for (const char *q = host; *q != '\0'; ++q) {
                if (!(g_ascii_isalnum(*q) || *q == '.' || *q == '-' || *q == '_')) {
                    g_free(host);
                    host = g_strdup("display");
                    break;
                }
            }
        }
    }
#if GLIB_CHECK_VERSION(2, 28, 0)
    runtime_dir = g_get_user_runtime_dir();
    if (runtime_dir == NULL || *runtime_dir == '\0')
        runtime_dir = g_get_tmp_dir();
    written = g_snprintf(buf, len, "%s/%s-socket-%s-%d%s", runtime_dir,
                         data->prog_name, host ? host : "", dpynum, version);
#else
    written = g_snprintf(buf, len, "%s/.%s-socket-%s-%d-%s%s",
                         g_get_tmp_dir(), data->prog_name, host ? host : "",
                         dpynum, g_get_user_name(), version);
#endif
    g_free(host);
    if (written < 0 || written >= len)
        buf[0] = '\0'; /* Never bind or unlink a truncated socket name. */
}

static void get_socket_name(SingleInstData* data, char* buf, int len)
{
    get_socket_name_version(data, buf, len, "-v2");
}

static void get_legacy_socket_name(SingleInstData* data, char* buf, int len)
{
    get_socket_name_version(data, buf, len, "");
}
