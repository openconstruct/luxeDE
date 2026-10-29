/*
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell <jerr@luxe.desktop>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Jürgen Hötzel <juergen@archlinux.org>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Lajos Kamocsay <lajos@panka.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Jack Chen <speed.up08311990@gmail.com>
 *               2012 Rafał Mużyło <galtgendo@gmail.com>
 *               2012 Michael Rawson <michaelrawson76@gmail.com>
 *               2012 Julien Lavergne <julien.lavergne@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2013 peadaredwards <peadaredwards@gmail.com>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *
 * This file is a part of LXPanel project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* The small line reader/writer used by the legacy plugin configuration
 * format.  It lives in its own translation unit so the production parser
 * can be tested without linking the panel's display and plugin machinery. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "private.h"

static int
buf_gets(char *buf, int len, char **fp)
{
    char *p;
    gsize count = 0;

    if (buf == NULL || len <= 0 || fp == NULL || !(p = *fp) || *p == '\0')
    {
        if (buf != NULL && len > 0)
            buf[0] = '\0';
        return 0;
    }

    while (*p != '\0')
    {
        /* Keep counting the source line, but never write past the caller's
         * fixed-size buffer.  The old implementation incremented i after
         * the limit and then wrote the terminator out of bounds. */
        if (count < (gsize)len)
            buf[count] = *p;
        count++;
        if (*p == '\n')
        {
            p++;
            break;
        }
        p++;
    }

    buf[MIN(count, (gsize)len - 1)] = '\0';
    *fp = p;
    return count > G_MAXINT ? G_MAXINT : (int)count;
}

int
luxepanel_put_line(FILE *fp, const char *format, ...)
{
    static int indent = 0;
    int i, ret;
    va_list args;

    if (fp == NULL || format == NULL)
        return 0;

    if (strchr(format, '}') != NULL && indent > 0)
        --indent;

    for (i = 0; i < indent; ++i)
        fputs("    ", fp);

    va_start(args, format);
    ret = vfprintf(fp, format, args);
    va_end(args);

    if (strchr(format, '{') != NULL)
        ++indent;
    fputc('\n', fp);
    return ret < 0 ? -1 : ret + 1;
}

int
luxepanel_get_line(char **fp, line *s)
{
    gchar *tmp, *tmp2;

    if (s == NULL)
        return LINE_NONE;
    s->type = LINE_NONE;
    if (fp == NULL)
        return s->type;

    while (buf_gets(s->str, s->len, fp))
    {
        g_strstrip(s->str);

        if (s->str[0] == '#' || s->str[0] == '\0')
            continue;
        if (!g_ascii_strcasecmp(s->str, "}"))
        {
            s->type = LINE_BLOCK_END;
            break;
        }

        s->t[0] = s->str;
        for (tmp = s->str; *tmp != '\0' && g_ascii_isalnum(*tmp); tmp++)
            ;
        for (tmp2 = tmp; *tmp2 != '\0' && g_ascii_isspace(*tmp2); tmp2++)
            ;
        if (*tmp2 == '=')
        {
            for (++tmp2; *tmp2 != '\0' && g_ascii_isspace(*tmp2); tmp2++)
                ;
            s->t[1] = tmp2;
            *tmp = '\0';
            s->type = LINE_VAR;
        }
        else if (*tmp2 == '{')
        {
            *tmp = '\0';
            s->type = LINE_BLOCK_START;
        }
        else
        {
            g_warning("parser: unknown token: '%c'", *tmp2);
        }
        break;
    }
    return s->type;
}
