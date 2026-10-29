/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Include the production implementation: no mirrored parser or serializer. */
#define main luxehotkey_program_main
#include "../src/luxehotkey.c"
#undef main

static void test_actions(void)
{
    const char *cases[] = {
        "Execute:command=terminal:startupnotify=yes",
        "Execute:command=a\\:b\\&c\\=d\\\\e&Focus:one=1:two=2",
        "name\\:with\\&symbols:opt\\=name=value",
        "Execute:command=", "Focus"
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        GError *error = NULL;
        GList *actions = actions_from_str(cases[i], &error);
        g_assert_no_error(error);
        char *wire = actions_to_str(actions);
        g_assert_cmpstr(wire, ==, cases[i]);
        g_free(wire);
        free_actions(actions);
    }
    const char *invalid[] = { "", "=value", "Focus:", "Focus&", "Focus::bad" };
    for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
        GError *error = NULL;
        g_assert_null(actions_from_str(invalid[i], &error));
        g_assert_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS);
        g_clear_error(&error);
    }
}

static void test_validation(void)
{
    g_assert_true(validate_key("<Primary><Shift>Tab", NULL));
    const char *bare[] = { "Super_L", "Meta_L", "Hyper_L", "Shift_L", "Control_L",
        "Alt_L", "ISO_Level3_Shift", "a", "Tab", "Escape", "BackSpace", "XF86Search" };
    for (guint i = 0; i < G_N_ELEMENTS(bare); i++)
        g_assert_true(validate_key(bare[i], NULL));
    g_assert_false(validate_key("<Control>", NULL));
    g_assert_false(validate_key("<Control", NULL));
    g_assert_false(validate_key("--", NULL));
    g_assert_false(validate_key("<NoSuchModifier>a", NULL));
    g_assert_false(validate_key("<ControlBogus>a", NULL));
    g_assert_false(validate_key("a<Control>", NULL));
    g_assert_false(validate_key("<Control>NotARealKeysym", NULL));
    g_assert_true(validate_key("<Control>XF86AudioMute", NULL));
    g_assert_true(luxe_shortcut_equal("<Alt><Primary>c", "<Control><Alt>c"));
    g_assert_true(luxe_shortcut_equal("<Control><Control>a", "<Ctrl>a"));
    g_assert_false(luxe_shortcut_equal("<Alt>a", "<Control>a"));
    g_assert_false(validate_exec(" \t", NULL));
    g_assert_true(validate_exec("terminal --working-directory /tmp", NULL));
    g_assert_true(numeric_value_matches("-20", "#"));
    g_assert_true(numeric_value_matches("50%", "%"));
    g_assert_false(numeric_value_matches("", "#"));
    g_assert_false(numeric_value_matches("50%junk", "%"));
    g_assert_false(numeric_value_matches("999999999999999999999999", "#"));
}

static void test_app_command_options(void)
{
    GError *error = NULL;
    char *exec = NULL;
    GList *options = NULL;
    g_assert_true(parse_app_spec("/opt/tool\\&helper --flag", NULL,
                                 &exec, &options, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(exec, ==, "/opt/tool&helper --flag");
    g_assert_null(options);
    g_free(exec);

    g_assert_true(parse_app_spec("terminal&startupnotify=yes", NULL,
                                 &exec, &options, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(exec, ==, "terminal");
    g_assert_nonnull(options);
    g_free(exec); free_actions(options);

    g_assert_true(parse_app_spec("echo&is-an-executable", "startupnotify=yes",
                                 &exec, &options, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(exec, ==, "echo&is-an-executable");
    g_assert_nonnull(options);
    g_free(exec); free_actions(options);

    g_assert_false(parse_app_spec("terminal&", NULL, &exec, &options, &error));
    g_assert_error(error, LXKEYS_ERROR, LXKEYS_BAD_ARGS);
    g_assert_null(exec); g_assert_null(options); g_clear_error(&error);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    LXKEYS_ERROR = g_quark_from_static_string("luxehotkey-error");
    g_test_add_func("/actions/roundtrip", test_actions);
    g_test_add_func("/actions/validation", test_validation);
    g_test_add_func("/actions/app-command-options", test_app_command_options);
    return g_test_run();
}
