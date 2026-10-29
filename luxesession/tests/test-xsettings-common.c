/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "xsettings-common.h"

#include <glib.h>
#include <string.h>

int main(void)
{
    XSettingsSetting source = {0};
    XSettingsSetting *copy;
    XSettingsList *list = NULL;
    gchar *long_name = g_strnfill(XSETTINGS_MAX_NAME + 2, 'n');
    gchar *long_value = g_strnfill(XSETTINGS_MAX_STRING + 2, 'v');

    g_assert_null(xsettings_setting_copy(NULL));
    g_assert_cmpint(xsettings_setting_equal(NULL, &source), ==, 0);
    g_assert_cmpint(xsettings_list_insert(NULL, &source), ==, XSETTINGS_FAILED);
    g_assert_null(xsettings_list_lookup(list, NULL));
    g_assert_cmpint(xsettings_list_delete(NULL, "name"), ==, XSETTINGS_FAILED);

    source.name = "Net/ThemeName";
    source.type = XSETTINGS_TYPE_STRING;
    source.data.v_string = (char *)"Adwaita";
    copy = xsettings_setting_copy(&source);
    g_assert_nonnull(copy);
    g_assert_cmpint(xsettings_setting_equal(&source, copy), !=, 0);
    g_assert_cmpint(xsettings_list_insert(&list, copy), ==, XSETTINGS_SUCCESS);
    g_assert_cmpint(xsettings_list_insert(&list, copy), ==, XSETTINGS_DUPLICATE_ENTRY);
    g_assert_nonnull(xsettings_list_lookup(list, "Net/ThemeName"));

    source.name = long_name;
    source.data.v_string = (char *)"value";
    g_assert_null(xsettings_setting_copy(&source));
    source.name = "bounded";
    source.data.v_string = long_value;
    g_assert_null(xsettings_setting_copy(&source));

    xsettings_list_free(list);
    g_free(long_name);
    g_free(long_value);
    return 0;
}
