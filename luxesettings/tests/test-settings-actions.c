/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <glib.h>

#include "settings-actions.h"

static void
test_catalog_is_safe_and_complete(void)
{
    g_assert_cmpuint(luxe_settings_actions_count, >, 0);

    for (guint index = 0; index < luxe_settings_actions_count; index++) {
        const LuxeSettingsAction *action = &luxe_settings_actions[index];

        g_assert_true(luxe_settings_action_is_valid(action));
        g_assert_nonnull(action->argv[0]);
        g_assert_null(g_strstr_len(action->argv[0], -1, " "));
        g_assert_cmpint(action->wait_for_exit,
                        ==, g_str_equal(action->argv[0], "luxepanelctl"));
    }
}

int
main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_assert_false(luxe_settings_action_is_valid(NULL));
    const char *bad_commands[][3] = { { "sh", "-c", NULL }, { "tool;evil", NULL }, { "", NULL }, { "tool", "bad\nargument", NULL } };
    for (guint i = 0; i < G_N_ELEMENTS(bad_commands); i++) {
        LuxeSettingsAction invalid = luxe_settings_actions[0];
        invalid.argv = bad_commands[i];
        g_assert_false(luxe_settings_action_is_valid(&invalid));
    }
    g_test_add_func("/settings/actions/catalog-is-safe-and-complete",
                    test_catalog_is_safe_and_complete);
    return g_test_run();
}
