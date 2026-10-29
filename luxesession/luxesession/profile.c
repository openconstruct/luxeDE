/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Session and desktop-environment names are used as path components and as
 * arguments to the XDG autostart helper.  Keep the accepted profile namespace
 * deliberately small instead of treating arbitrary environment data as a
 * relative pathname.
 */
#include "profile.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

gboolean
luxe_profile_component_valid(const gchar *value)
{
    const guchar *p;
    gsize length;

    if (value == NULL)
        return FALSE;
    length = strlen(value);
    if (length == 0 || length > 64)
        return FALSE;
    if (g_str_equal(value, ".") || g_str_equal(value, ".."))
        return FALSE;

    for (p = (const guchar *)value; *p != '\0'; p++)
    {
        if (!(g_ascii_isalnum(*p) || *p == '-' || *p == '_'))
            return FALSE;
    }
    return TRUE;
}

gboolean
luxe_process_start_time(gint pid, guint64 *start_time)
{
    gchar path[64];
    gchar buffer[4096];
    gchar *closing;
    gchar *save = NULL;
    gchar *token;
    guint field = 3;
    FILE *stream;

    if (pid <= 1 || start_time == NULL)
        return FALSE;
    g_snprintf(path, sizeof path, "/proc/%d/stat", pid);
    stream = fopen(path, "r");
    if (stream == NULL)
        return FALSE;
    if (fgets(buffer, sizeof buffer, stream) == NULL) {
        fclose(stream);
        return FALSE;
    }
    fclose(stream);
    closing = strrchr(buffer, ')');
    if (closing == NULL)
        return FALSE;
    token = strtok_r(closing + 1, " ", &save);
    while (token != NULL && field < 22) {
        token = strtok_r(NULL, " ", &save);
        field++;
    }
    if (token == NULL || *token == '\0')
        return FALSE;
    errno = 0;
    {
        gchar *end = NULL;
        guint64 value = g_ascii_strtoull(token, &end, 10);
        if (errno == ERANGE || end == token || *end != '\0')
            return FALSE;
        *start_time = value;
    }
    return TRUE;
}
