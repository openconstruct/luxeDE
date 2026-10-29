/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real socket-name derivation: host part kept when it is a
 * plain name, replaced when it carries unsafe characters. */
#include "../src/single-inst.c"

static void expect(const char *display, const char *suffix)
{
    SingleInstData data = { 0 };
    char buf[512];

    data.prog_name = "pcmanager";
    g_setenv("XDG_RUNTIME_DIR", "/run/user/test", TRUE);
    if (display)
        g_setenv("DISPLAY", display, TRUE);
    else
        g_unsetenv("DISPLAY");
    get_socket_name(&data, buf, sizeof buf);
    if (!g_str_has_suffix(buf, suffix)) {
        g_printerr("DISPLAY=%s: got %s, want suffix %s\n", display ? display : "(unset)", buf, suffix);
        exit(1);
    }
}

int main(void)
{
    expect(":0", "/pcmanager-socket--0-v2");
    expect("localhost:10", "/pcmanager-socket-localhost-10-v2");
    expect("localhost:10.0", "/pcmanager-socket-localhost-10-v2");
    expect(":12.1", "/pcmanager-socket--12-v2");
    expect("host.example-1:3", "/pcmanager-socket-host.example-1-3-v2");
    expect("bad/../host:2", "/pcmanager-socket-display-2-v2");
    expect("a b:4", "/pcmanager-socket-display-4-v2");
    expect(NULL, "/pcmanager-socket--0-v2");
    {
        SingleInstData data = { .prog_name = "pcmanager" };
        char buf[512];
        g_setenv("XDG_RUNTIME_DIR", "/run/user/test", TRUE);
        g_setenv("DISPLAY", ":0.0", TRUE);
        get_legacy_socket_name(&data, buf, sizeof buf);
        g_assert_cmpstr(buf, ==, "/run/user/test/pcmanager-socket--0");
    }
    {
        SingleInstData data = { 0 };
        char long_name[180];
        char buf[sizeof(((struct sockaddr_un *)0)->sun_path)];
        memset(long_name, 'x', sizeof long_name - 1);
        long_name[sizeof long_name - 1] = '\0';
        data.prog_name = long_name;
        get_socket_name(&data, buf, sizeof buf);
        g_assert_cmpstr(buf, ==, "");
        g_assert_cmpint(single_inst_init(&data), ==, SINGLE_INST_ERROR);
        g_assert_cmpint(data.sock, ==, -1);
        /* Exactly sizeof(sun_path)-1 bytes fits; one additional byte does not. */
        size_t prefix = strlen(g_get_user_runtime_dir()) + 1;
        size_t suffix = strlen("-socket--0-v2");
        size_t max_name = sizeof(buf) - 1 - prefix - suffix;
        memset(long_name, 'y', max_name + 1);
        long_name[max_name] = '\0';
        get_socket_name(&data, buf, sizeof buf);
        g_assert_cmpuint(strlen(buf), ==, sizeof(buf) - 1);
        long_name[max_name] = 'y'; long_name[max_name + 1] = '\0';
        get_socket_name(&data, buf, sizeof buf);
        g_assert_cmpstr(buf, ==, "");
    }
    puts("PASS: single-instance socket names keep safe hosts and replace unsafe ones");
    return 0;
}
