/*
 *      command.c - xrandr command builder for LuxeRandR
 *
 *      Moved verbatim from lxrandr.c so the command-building logic can be
 *      unit tested without starting the GTK application.
 *
 *      Copyright (C) 2008 Hong Jen Yee(PCMan) <pcman.tw@gmail.com>
 *      Copyright (C) 2011 Julien Lavergne <julien.lavergne@gmail.com>
 *      Copyright (C) 2014 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
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

#include "command.h"

GSList* monitors = NULL;
Monitor* LVDS = NULL;

static gboolean token_is_simple(const char *token)
{
    for (const unsigned char *p=(const unsigned char *)token; *p; ++p)
        if (!(g_ascii_isalnum(*p) || *p=='-' || *p=='_' || *p=='.' || *p=='+' || *p=='x')) return FALSE;
    return TRUE;
}

static gboolean token_valid(const char *token)
{
    if (!token || !*token || *token=='-' || !g_utf8_validate(token,-1,NULL)) return FALSE;
    for (const char *p=token; *p;) {
        gunichar ch=g_utf8_get_char(p);
        if (g_unichar_iscntrl(ch)) return FALSE;
        p=g_utf8_next_char(p);
    }
    return TRUE;
}

static gboolean append_command_value(GString *cmd,const char *token,gboolean desktop_exec)
{
    if (!token_valid(token)) return FALSE;
    g_string_append_c(cmd, ' ');
    if (token_is_simple(token)) {
        g_string_append(cmd,token);
    } else if (!desktop_exec) {
        g_autofree char *quoted=g_shell_quote(token);
        g_string_append(cmd,quoted);
    } else {
        g_string_append_c(cmd,'"');
        for (const char *p=token;*p;p++) {
            if (*p=='%' ) g_string_append(cmd,"%%");
            else {
                if (*p=='"' || *p=='\\' || *p=='`' || *p=='$') g_string_append_c(cmd,'\\');
                g_string_append_c(cmd,*p);
            }
        }
        g_string_append_c(cmd,'"');
    }
    return TRUE;
}

gboolean append_xrandr_token(GString *cmd,const char *token)
{ return append_command_value(cmd,token,FALSE); }

/* This produces direct xrandr arguments, which are also valid in a Desktop
 * Entry Exec key. Connector and mode names originate with the X server, so
 * reject anything outside the small token alphabet above. */
static GString* build_command(gboolean restore,gboolean desktop_exec)
{
    GSList* l;
    GString *cmd = g_string_sized_new(1024);

    g_string_assign(cmd, "xrandr");

    Monitor *fixed = LVDS && LVDS->try_mode >= 0 ? LVDS : NULL;
    for (l = monitors; !fixed && l; l = l->next)
        if (((Monitor *)l->data)->try_mode >= 0)
            fixed = l->data;
    if (!fixed)
        goto invalid_token;
    Monitor *anchor = fixed;

    for( l = monitors; l; l = l->next )
    {
        Monitor* m = (Monitor*)l->data;
        if (m->try_placement < PLACEMENT_DEFAULT || m->try_placement > PLACEMENT_BELOW)
            goto invalid_token;
        g_string_append(cmd, " --output");
        if (!append_command_value(cmd, m->name,desktop_exec))
            goto invalid_token;

        /* if the monitor is turned on */
        if (m->try_mode >= 0)
        {
            if( m->try_mode < 1 ) /* auto resolution */
            {
                g_string_append( cmd, " --auto" );
            }
            else
            {
                char** mode_line = g_slist_nth_data( m->mode_lines, m->try_mode - 1 );

                g_string_append(cmd, " --mode");
                if (!mode_line || !mode_line[0] || !append_command_value(cmd, mode_line[0],desktop_exec))
                    goto invalid_token;

                if( m->try_rate >= 1 ) /* not auto refresh rate */
                {
                    int n_tokens = 0;
                    /* Zero-mode outputs have no usable mode_lines, and a
                     * stale try_rate past the NULL terminator would read
                     * out of bounds, so count entries before indexing. */
                    while (mode_line[n_tokens] != NULL)
                        n_tokens++;
                    g_string_append(cmd, " --rate");
                    if (m->try_rate >= n_tokens || !mode_line[m->try_rate] ||
                        !append_command_value(cmd, mode_line[m->try_rate],desktop_exec))
                        goto invalid_token;
                }
            }
            /* Anchor selection: the laptop panel (LVDS/eDP) is the fixed
             * reference while it stays enabled (external-only mode must
             * not anchor to the disabled LVDS); otherwise the first
             * enabled output is fixed so a disabled-anchor is never used
             * as a --*-of target. Further outputs anchor to the nearest
             * preceding enabled output, falling back to the fixed one, so
             * 3+ extended outputs chain instead of stacking on one anchor. */
            if (restore || (m->has_position && m->try_placement == m->placement)) {
                if (!m->has_position) goto invalid_token;
                g_string_append_printf(cmd, " --pos %dx%d", m->x, m->y);
            }
            else if (m != fixed)
            {
                switch (m->try_placement)
                {
                case PLACEMENT_RIGHT:
                    g_string_append(cmd, " --right-of");
                    break;
                case PLACEMENT_ABOVE:
                    g_string_append(cmd, " --above");
                    break;
                case PLACEMENT_LEFT:
                    g_string_append(cmd, " --left-of");
                    break;
                case PLACEMENT_BELOW:
                    g_string_append(cmd, " --below");
                    break;
                case PLACEMENT_DEFAULT:
                    g_string_append(cmd, " --same-as");
                }
                if (!append_command_value(cmd, anchor->name,desktop_exec))
                    goto invalid_token;
                anchor = m;
            }
        }
        else    /* turn off */
            g_string_append( cmd, " --off" );
    }
    return cmd;

invalid_token:
    g_warning("Refusing unsafe or unavailable XRandR connector, mode, or rate name");
    g_string_free(cmd, TRUE);
    return NULL;
}

GString* get_command_xrandr_info(void) { return build_command(FALSE,FALSE); }
GString* get_command_xrandr_restore(void) { return build_command(TRUE,FALSE); }
GString* get_desktop_exec_xrandr_info(void) { return build_command(FALSE,TRUE); }

void free_monitors(void)
{
    GSList *l;

    for (l = monitors; l; l = l->next)
    {
        Monitor *m = (Monitor*)l->data;
        GSList *k;

        g_free(m->name);
        for (k = m->mode_lines; k; k = k->next)
            g_strfreev((gchar**)k->data);
        g_slist_free(m->mode_lines);
        g_free(m);
    }
    g_slist_free(monitors);
    monitors = NULL;
    LVDS = NULL;
}
