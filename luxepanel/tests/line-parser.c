/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include <glib.h>
#include <string.h>

#include "private.h"

static void
test_long_line_is_bounded(void)
{
    line parsed = {0};
    char input[512];
    char *cursor = input;

    input[0] = 'x';
    input[1] = '=';
    memset(input + 2, 'y', sizeof(input) - 4);
    input[sizeof(input) - 2] = '\n';
    input[sizeof(input) - 1] = '\0';
    parsed.len = 8;

    g_assert_cmpint(luxepanel_get_line(&cursor, &parsed), ==, LINE_VAR);
    g_assert_cmpuint(strlen(parsed.str), <, (gsize)parsed.len);
    g_assert_cmpstr(parsed.str, ==, "x");
    g_assert_cmpstr(parsed.t[1], ==, "yyyyy");
    g_assert_true(*cursor == '\0');
}

static void
test_variables_and_groups(void)
{
    line parsed = {0};
    char input[] = "  Plugin {\nname=value\n}\n";
    char *cursor = input;

    parsed.len = sizeof(parsed.str);
    g_assert_cmpint(luxepanel_get_line(&cursor, &parsed), ==, LINE_BLOCK_START);
    g_assert_cmpstr(parsed.t[0], ==, "Plugin");
    g_assert_cmpint(luxepanel_get_line(&cursor, &parsed), ==, LINE_VAR);
    g_assert_cmpstr(parsed.t[0], ==, "name");
    g_assert_cmpstr(parsed.t[1], ==, "value");
    g_assert_cmpint(luxepanel_get_line(&cursor, &parsed), ==, LINE_BLOCK_END);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/line-parser/long-line", test_long_line_is_bounded);
    g_test_add_func("/line-parser/legacy-groups", test_variables_and_groups);
    return g_test_run();
}
