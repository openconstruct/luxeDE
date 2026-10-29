/*
 * Copyright (C) 2014 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2014 Henry Gebhardt <hsggebhardt@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "conf.h"
#include "private.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Panel configuration is user data, but it is still an untrusted input
 * boundary: a damaged file must not turn into an unbounded allocation or a
 * negative seek/size calculation.  Real panel files are tiny; the generous
 * limit leaves room for legacy plugin settings while keeping parser failures
 * deterministic. */
#define CONFIG_MAX_SIZE (1024U * 1024U)
#define CONFIG_READ_CHUNK (64U * 1024U)

struct _config_setting_t
{
    config_setting_t *next;
    config_setting_t *parent;
    PanelConfType type;
    PanelConfSaveHook hook;
    gpointer hook_data;
    char *name;
    union {
        gint num; /* for integer or boolean */
        gchar *str; /* for string */
        config_setting_t *first; /* for group or list */
    };
};

struct _PanelConf
{
    config_setting_t *root;
};

static config_setting_t *_config_setting_t_new(config_setting_t *parent, int index,
                                               const char *name, PanelConfType type)
{
    config_setting_t *s;
    s = g_slice_new0(config_setting_t);
    s->type = type;
    s->name = g_strdup(name);
    if (parent == NULL || (parent->type != PANEL_CONF_TYPE_GROUP && parent->type != PANEL_CONF_TYPE_LIST))
        return s;
    s->parent = parent;
    if (parent->first == NULL || index == 0)
    {
        s->next = parent->first;
        parent->first = s;
    }
    else
    {
        for (parent = parent->first; parent->next && index != 1; parent = parent->next)
            index--;
        /* FIXME: check if index is out of range? */
        s->next = parent->next;
        parent->next = s;
    }
    return s;
}

/* frees data, not removes from parent */
static void _config_setting_t_free(config_setting_t *setting)
{
    if (setting == NULL)
        return;
    g_free(setting->name);
    switch (setting->type)
    {
    case PANEL_CONF_TYPE_STRING:
        g_free(setting->str);
        break;
    case PANEL_CONF_TYPE_GROUP:
    case PANEL_CONF_TYPE_LIST:
        while (setting->first)
        {
            config_setting_t *s = setting->first;
            setting->first = s->next;
            _config_setting_t_free(s);
        }
        break;
    case PANEL_CONF_TYPE_INT:
        break;
    }
    g_slice_free(config_setting_t, setting);
}

/* the same as above but removes from parent */
static void _config_setting_t_remove(config_setting_t *setting)
{
    g_return_if_fail(setting->parent);
    g_return_if_fail(setting->parent->type == PANEL_CONF_TYPE_GROUP || setting->parent->type == PANEL_CONF_TYPE_LIST);
    /* remove from parent */
    if (setting->parent->first == setting)
        setting->parent->first = setting->next;
    else
    {
        config_setting_t *s = setting->parent->first;
        while (s->next != NULL && s->next != setting)
            s = s->next;
        g_assert(s->next != NULL);
        s->next = setting->next;
    }
    /* free the data */
    _config_setting_t_free(setting);
}

static config_setting_t * _config_setting_get_member(const config_setting_t * setting, const char * name)
{
    config_setting_t *s;
    for (s = setting->first; s; s = s->next)
        if (g_strcmp0(s->name, name) == 0)
            break;
    return s;
}

/* returns either new or existing setting struct, NULL on error or conflict */
static config_setting_t * _config_setting_try_add(config_setting_t * parent,
                                                  const char * name,
                                                  PanelConfType type)
{
    config_setting_t *s;
    if (parent == NULL || name == NULL)
        return NULL;
    if (name[0] == '\0')
        return NULL;
    if (parent->type == PANEL_CONF_TYPE_GROUP &&
        (s = _config_setting_get_member(parent, name)))
        return (s->type == type) ? s : NULL;
    return _config_setting_t_new(parent, -1, name, type);
}

PanelConf *config_new(void)
{
    PanelConf *c = g_slice_new(PanelConf);
    c->root = _config_setting_t_new(NULL, -1, NULL, PANEL_CONF_TYPE_GROUP);
    return c;
}

void config_destroy(PanelConf * config)
{
    if (config == NULL)
        return;
    _config_setting_t_free(config->root);
    g_slice_free(PanelConf, config);
}

static char *config_read_regular_file(const char *filename, gsize *length_out)
{
    FILE *f = NULL;
    struct stat st;
    char *buff = NULL;
    size_t size = 0;
    int fd = -1;

    g_return_val_if_fail(filename != NULL && length_out != NULL, NULL);
    *length_out = 0;
    /* O_NONBLOCK is important here: opening a FIFO before checking its mode
     * would otherwise block the panel before fstat() can reject it. */
    fd = open(filename, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return NULL;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || (guint64)st.st_size > CONFIG_MAX_SIZE)
        goto fail;
    f = fdopen(fd, "r");
    if (f == NULL)
        goto fail;
    fd = -1; /* f now owns the descriptor */

    /* Allocate a bounded buffer up front.  The extra byte lets us detect a
     * file that grows after fstat without accepting an unbounded stream. */
    buff = g_try_malloc((gsize)CONFIG_MAX_SIZE + 1);
    if (buff == NULL)
        goto fail;

    while (size <= CONFIG_MAX_SIZE)
    {
        size_t want = MIN((size_t)CONFIG_READ_CHUNK,
                          (size_t)CONFIG_MAX_SIZE + 1 - size);
        size_t got = fread(buff + size, 1, want, f);
        size += got;
        if (got < want)
        {
            if (ferror(f))
                goto fail;
            break;
        }
    }
    if (size > CONFIG_MAX_SIZE)
        goto fail;

    fclose(f);
    buff[size] = '\0';
    *length_out = size;
    return buff;

fail:
    if (f != NULL)
        fclose(f);
    if (fd >= 0)
        close(fd);
    if (buff != NULL)
        g_free(buff);
    return NULL;
}

gboolean config_read_file(PanelConf * config, const char * filename)
{
    gsize file_size;
    char *buff, *c, *name, *end, *p;
    char *number_end;
    long number;
    config_setting_t *s, *parent;

    if (config == NULL || config->root == NULL)
        return FALSE;
    buff = config_read_regular_file(filename, &file_size);
    if (buff == NULL)
        return FALSE;
    (void)file_size;
    name = NULL;
    parent = config->root;
    for (c = buff; *c; )
    {
        switch(*c)
        {
        case '#':
_skip_all:
            while (*c && *c != '\n')
                c++;
            if (!*c)
                break;
            /* continue with EOL */
        case '\n':
            name = NULL;
            c++;
            break;
        case ' ':
        case '\t':
            if (name)
                *c = '\0';
            c++;
            break;
        case '=': /* scalar value follows */
            if (parent == NULL || name == NULL)
            {
                g_warning("config: invalid scalar definition");
                goto _skip_all;
            }
            *c++ = '\0';
            while (*c == ' ' || *c == '\t')
                c++; /* skip spaces after '=' */
            if (*c == '\0' || *c == '\n') /* invalid statement */
                break;

            /* Parse an integer only when the complete value is a checked
             * decimal integer.  Otherwise retain it as a string; casting an
             * out-of-range strtol() result to int used to wrap settings. */
            errno = 0;
            number = strtol(c, &number_end, 10);
            if (number_end != c && errno != ERANGE &&
                number >= G_MININT && number <= G_MAXINT)
            {
                end = number_end;
                while (*end == ' ' || *end == '\t')
                    end++; /* skip trailing spaces */
                if (*end == '\0' || *end == '\n')
                {
                    s = _config_setting_try_add(parent, name, PANEL_CONF_TYPE_INT);
                    if (s)
                        s->num = (int)number;
                    else
                        g_warning("config: duplicate setting '%s' conflicts, ignored", name);
                    c = end;
                    break;
                }
            }

            if (c[0] == '"')
            {
                c++;
                for (end = p = c; *end && *end != '\n' && *end != '"'; p++, end++)
                {
                    if (*end == '\\' && end[1] != '\0' && end[1] != '\n')
                    {
                        end++; /* skip quoted char */
                        if (*end == 'n') /* \n */
                            *end = '\n';
                    }
                    if (p != end)
                        *p = *end; /* move char skipping '\\' */
                }
                if (*end == '"')
                {
                    end++;
                    goto _make_string;
                }
                else /* incomplete string */
                    g_warning("config: unfinished string setting '%s', ignored", name);
            }
            else
            {
                for (end = c; *end && *end != '\n'; )
                    end++;
                p = end;
_make_string:
                s = _config_setting_try_add(parent, name, PANEL_CONF_TYPE_STRING);
                if (s)
                {
                    g_free(s->str);
                    s->str = g_strndup(c, p - c);
                    /* g_debug("config loader: got new string %s: %s", name, s->str); */
                }
                else
                    g_warning("config: duplicate setting '%s' conflicts, ignored", name);
            }
            c = end;
            break;
        case '{':
        {
            config_setting_t *list;
            if (parent == NULL)
            {
                g_warning("config: group outside a valid parent ignored");
                name = NULL;
                break;
            }
            list = config_setting_add(parent, "", PANEL_CONF_TYPE_LIST);
            if (list == NULL)
            {
                g_warning("config: unable to create group list");
                name = NULL;
                break;
            }
            if (name)
            {
                *c = '\0';
                s = config_setting_add(list, name, PANEL_CONF_TYPE_GROUP);
            }
            else
                s = NULL;
            c++;
            if (s)
            {
                parent = s;
                /* g_debug("config loader: group '%s' added", name); */
            }
            else
                g_warning("config: invalid group '%s' in config file ignored", name);
            name = NULL;
            break;
        }
        case '}':
            c++;
            if (parent == NULL || parent == config->root)
            {
                g_debug("config: unmatched closing brace ignored");
                name = NULL;
                break;
            }
            if (parent->parent != NULL)
                parent = parent->parent; /* go up, to anonymous list */
            if (parent != NULL && parent->type == PANEL_CONF_TYPE_LIST &&
                parent->parent != NULL)
                parent = parent->parent; /* go to upper group */
            name = NULL;
            break;
        default:
            if (name == NULL)
                name = c;
            c++;
        }
    }
    g_free(buff);
    return TRUE;
}

#define SETTING_INDENT "  "

static gboolean _config_string_needs_quotes(const char *value)
{
    const char *p;
    char *end = NULL;
    errno = 0;

    if (value == NULL || value[0] == '\0')
        return TRUE;
    (void)strtoll(value, &end, 10);
    if (end != value && errno != ERANGE && *end == '\0')
        return TRUE;
    if (g_ascii_isspace(value[0]) || g_ascii_isspace(value[strlen(value) - 1]))
        return TRUE;
    for (p = value; *p != '\0'; p++)
        if (*p == '\n' || *p == '\r' || *p == '\t' ||
            *p == '\\' || *p == '"' || g_ascii_iscntrl(*p))
            return TRUE;
    return FALSE;
}

static void _config_append_string(GString *buf, const char *name,
                                  const char *value)
{
    const char *p;
    gboolean quote = _config_string_needs_quotes(value);
    g_string_append_printf(buf, "%s=", name);
    if (!quote)
    {
        g_string_append(buf, value);
        g_string_append_c(buf, '\n');
        return;
    }
    g_string_append_c(buf, '"');
    for (p = value != NULL ? value : ""; *p != '\0'; p++)
    {
        switch (*p)
        {
        case '\n': g_string_append(buf, "\\n"); break;
        case '\r': g_string_append(buf, "\\r"); break;
        case '\t': g_string_append(buf, "\\t"); break;
        case '\\': g_string_append(buf, "\\\\"); break;
        case '"': g_string_append(buf, "\\\""); break;
        default: g_string_append_c(buf, *p); break;
        }
    }
    g_string_append(buf, "\"\n");
}

static void _config_write_setting(const config_setting_t *setting, GString *buf,
                                  GString *out, FILE *f)
{
    gint indent = buf->len;
    config_setting_t *s;

    switch (setting->type)
    {
    case PANEL_CONF_TYPE_INT:
        g_string_append_printf(buf, "%s=%d\n", setting->name, setting->num);
        break;
    case PANEL_CONF_TYPE_STRING:
        if (!setting->str) /* don't save NULL strings */
            return;
        _config_append_string(buf, setting->name, setting->str);
        break;
    case PANEL_CONF_TYPE_GROUP:
        if (!out && setting->hook) /* plugin does not support settings */
        {
            luxepanel_put_line(f, "%s%s {", buf->str, setting->name);
            setting->hook(setting, f, setting->hook_data);
            luxepanel_put_line(f, "%s}", buf->str);
            /* old settings ways are kinda weird... */
        }
        else
        {
            if (out)
            {
                g_string_append(out, buf->str);
                g_string_append(out, setting->name);
                g_string_append(out, " {\n");
            }
            else
                fprintf(f, "%s%s {\n", buf->str, setting->name);
            g_string_append(buf, SETTING_INDENT);
            for (s = setting->first; s; s = s->next)
                _config_write_setting(s, buf, out, f);
            g_string_truncate(buf, indent);
            if (out)
            {
                g_string_append(out, buf->str);
                g_string_append(out, "}\n");
            }
            else
                fprintf(f, "%s}\n", buf->str);
        }
        return;
    case PANEL_CONF_TYPE_LIST:
        if (setting->name[0] != '\0')
        {
            g_warning("only anonymous lists are supported in panel config, got \"%s\"",
                      setting->name);
            return;
        }
        for (s = setting->first; s; s = s->next)
            _config_write_setting(s, buf, out, f);
        return;
    }
    if (out)
        g_string_append(out, buf->str);
    else
        fputs(buf->str, f);
    g_string_truncate(buf, indent);
}

gboolean config_write_file(PanelConf * config, const char * filename)
{
    GString *str = NULL;
    GString *tmpname = NULL;
    gchar *tmp = NULL;
    gchar *dir = NULL;
    FILE *f = NULL;
    gint fd = -1;
    gint dirfd = -1;
    gboolean ok = FALSE;
    gboolean have_mode = FALSE;
    mode_t mode = 0600;
    struct stat existing;

    if (config == NULL || config->root == NULL || filename == NULL ||
        filename[0] == '\0')
        return FALSE;

    /* Preserve the mode of an existing regular panel file where possible.
     * New files retain g_mkstemp's private 0600 mode. */
    if (stat(filename, &existing) == 0 && S_ISREG(existing.st_mode))
    {
        mode = existing.st_mode & 0777;
        have_mode = TRUE;
    }

    /* Write to a temporary file and rename atomically: the panel saves
     * opportunistically on every config change, and truncating the live
     * file in place loses it on a crash or power failure mid-write. */
    tmpname = g_string_new(filename);
    g_string_append(tmpname, ".tmpXXXXXX");
    tmp = tmpname->str;
    fd = g_mkstemp(tmp);
    if (fd < 0)
        goto done;
    if (have_mode && fchmod(fd, mode) != 0)
        goto done;
    f = fdopen(fd, "w");
    if (f == NULL)
        goto done;
    fd = -1; /* f now owns the descriptor */

    fputs("# luxepanel <profile> config file. Manually editing is not recommended.\n"
          "# Use preference dialog in luxepanel to adjust config when you can.\n\n", f);
    str = g_string_sized_new(128);
    {
        config_setting_t *root_group = config_setting_get_member(config->root, "");
        if (root_group != NULL)
            _config_write_setting(root_group, str, NULL, f);
    }
    ok = !ferror(f);
    /* Make the file durable before publishing its pathname. */
    if (ok && fflush(f) != 0)
        ok = FALSE;
    if (ok && fsync(fileno(f)) != 0)
        ok = FALSE;
    if (fclose(f) != 0)
        ok = FALSE;
    f = NULL;

    if (ok && rename(tmp, filename) != 0)
        ok = FALSE;
    if (ok)
    {
        /* Also publish the directory entry durably.  The panel directory is
         * user-owned, but this also makes the atomic-write guarantee hold
         * across a sudden power loss. */
        dir = g_path_get_dirname(filename);
        dirfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dirfd < 0 || fsync(dirfd) != 0)
            ok = FALSE;
        if (dirfd >= 0)
        {
            close(dirfd);
            dirfd = -1;
        }
    }

done:
    if (f != NULL)
        fclose(f);
    if (fd >= 0)
        close(fd);
    if (!ok && tmp != NULL)
        unlink(tmp);
    if (dirfd >= 0)
        close(dirfd);
    g_free(dir);
    if (str != NULL)
        g_string_free(str, TRUE);
    g_string_free(tmpname, TRUE);
    if (!ok)
        g_warning("Cannot save the panel config to %s", filename);
    return ok;
}

/* it is used for old plugins only */
char * config_setting_to_string(const config_setting_t * setting)
{
    GString *val, *buf;
    g_return_val_if_fail(setting, NULL);
    val = g_string_sized_new(128);
    buf = g_string_sized_new(128);
    _config_write_setting(setting, val, buf, NULL);
    g_string_free(val, TRUE);
    return g_string_free(buf, FALSE);
}

config_setting_t * config_root_setting(const PanelConf * config)
{
    return config != NULL ? config->root : NULL;
}

config_setting_t * config_setting_get_member(const config_setting_t * setting, const char * name)
{
    g_return_val_if_fail(name && setting, NULL);
    g_return_val_if_fail(setting->type == PANEL_CONF_TYPE_GROUP, NULL);
    return _config_setting_get_member(setting, name);
}

config_setting_t * config_setting_get_elem(const config_setting_t * setting, unsigned int index)
{
    config_setting_t *s;
    g_return_val_if_fail(setting, NULL);
    g_return_val_if_fail(setting->type == PANEL_CONF_TYPE_LIST || setting->type == PANEL_CONF_TYPE_GROUP, NULL);
    for (s = setting->first; s && index > 0; s = s->next)
        index--;
    return s;
}

const char * config_setting_get_name(const config_setting_t * setting)
{
    return setting != NULL ? setting->name : NULL;
}

config_setting_t * config_setting_get_parent(const config_setting_t * setting)
{
    return setting != NULL ? setting->parent : NULL;
}

int config_setting_get_int(const config_setting_t * setting)
{
    if (!setting || setting->type != PANEL_CONF_TYPE_INT)
        return 0;
    return setting->num;
}

const char * config_setting_get_string(const config_setting_t * setting)
{
    if (!setting || setting->type != PANEL_CONF_TYPE_STRING)
        return NULL;
    return setting->str;
}

gboolean config_setting_lookup_int(const config_setting_t * setting,
                                   const char * name, int * value)
{
    config_setting_t *sub;

    g_return_val_if_fail(name && setting && value, FALSE);
    g_return_val_if_fail(setting->type == PANEL_CONF_TYPE_GROUP, FALSE);
    sub = _config_setting_get_member(setting, name);
    if (!sub || sub->type != PANEL_CONF_TYPE_INT)
        return FALSE;
    *value = sub->num;
    return TRUE;
}

gboolean config_setting_lookup_string(const config_setting_t * setting,
                                      const char * name, const char ** value)
{
    config_setting_t *sub;

    g_return_val_if_fail(name && setting && value, FALSE);
    g_return_val_if_fail(setting->type == PANEL_CONF_TYPE_GROUP, FALSE);
    sub = _config_setting_get_member(setting, name);
    if (!sub || sub->type != PANEL_CONF_TYPE_STRING)
        return FALSE;
    *value = sub->str;
    return TRUE;
}

/* returns either new or existing setting struct, NULL on args error,
   removes old setting on conflict */
config_setting_t * config_setting_add(config_setting_t * parent, const char * name, PanelConfType type)
{
    config_setting_t *s;
    if (parent == NULL || (parent->type != PANEL_CONF_TYPE_GROUP && parent->type != PANEL_CONF_TYPE_LIST))
        return NULL;
    if (type == PANEL_CONF_TYPE_LIST)
    {
        if (!name || name[0])
            /* only anonymous lists are supported */
            return NULL;
    }
    else if (name == NULL || name[0] == '\0')
        /* other types should be not anonymous */
        return NULL;
    if (parent->type == PANEL_CONF_TYPE_GROUP &&
        (s = _config_setting_get_member(parent, name)))
    {
        if (s->type == type)
            return s;
        _config_setting_t_remove(s);
    }
    return _config_setting_t_new(parent, -1, name, type);
}

static void remove_from_parent(config_setting_t * setting)
{
    config_setting_t *s;

    if (setting->parent->first == setting) {
        setting->parent->first = setting->next;
        goto _isolate_setting;
    }

    for (s = setting->parent->first; s->next; s = s->next)
        if (s->next == setting)
            break;
    g_assert(s->next);
    s->next = setting->next;

_isolate_setting:
    setting->next = NULL;
    setting->parent = NULL;
}

static void append_to_parent(config_setting_t * setting, config_setting_t * parent)
{
    config_setting_t *s;

    setting->parent = parent;
    if (parent->first == NULL) {
        parent->first = setting;
        return;
    }

    s = parent->first;
    while (s->next)
        s = s->next;
    s->next = setting;
}

static void insert_after(config_setting_t * setting, config_setting_t * parent,
        config_setting_t * prev)
{
    setting->parent = parent;
    if (prev == NULL) {
        setting->next = parent->first;
        parent->first = setting;
    } else {
        setting->next = prev->next;
        prev->next = setting;
    }
}

gboolean config_setting_move_member(config_setting_t * setting, config_setting_t * parent, const char * name)
{
    config_setting_t *s;

    g_return_val_if_fail(setting && setting->parent, FALSE);
    if (parent == NULL || name == NULL || parent->type != PANEL_CONF_TYPE_GROUP)
        return FALSE;
    s = _config_setting_get_member(parent, name);
    if (s) /* we cannot rename/move to this name, it exists already */
        return (s == setting);
    if (setting->parent == parent) /* it's just renaming thing */
        goto _rename;
    remove_from_parent(setting); /* remove from old parent */
    append_to_parent(setting, parent); /* add to new parent */
    /* rename if need */
    if (g_strcmp0(setting->name, name) != 0)
    {
_rename:
        g_free(setting->name);
        setting->name = g_strdup(name);
    }
    return TRUE;
}

gboolean config_setting_move_elem(config_setting_t * setting, config_setting_t * parent, int index)
{
    config_setting_t *prev = NULL;

    g_return_val_if_fail(setting && setting->parent, FALSE);
    if (parent == NULL || parent->type != PANEL_CONF_TYPE_LIST)
        return FALSE;
    if (setting->type != PANEL_CONF_TYPE_GROUP) /* we support only list of groups now */
        return FALSE;
    /* let check the place */
    if (index != 0)
    {
        prev = parent->first;
        if (prev)
            for ( ; index != 1 && prev->next; prev = prev->next)
                index--;
        if (index > 1) /* too few elements yet */
        {
_out_of_range:
            g_warning("config_setting_move_elem: index out of range");
            return FALSE;
        }
        if (prev && prev->next == setting) /* it is already there */
            return TRUE;
        if (prev == setting) /* special case: we moving it +1, swapping with next */
        {
            if (prev->next == NULL)
                goto _out_of_range;
            prev = prev->next;
        }
    }
    else if (parent->first == setting) /* it is already there */
        return TRUE;
    remove_from_parent(setting); /* remove from old parent */
    /* add to new parent */
    if (index == 0)
        g_assert(prev == NULL);
    insert_after(setting, parent, prev);
    /* don't rename  */
    return TRUE;
}

gboolean config_setting_set_int(config_setting_t * setting, int value)
{
    if (!setting || setting->type != PANEL_CONF_TYPE_INT)
        return FALSE;
    setting->num = value;
    return TRUE;
}

gboolean config_setting_set_string(config_setting_t * setting, const char * value)
{
    if (!setting || setting->type != PANEL_CONF_TYPE_STRING)
        return FALSE;
    g_free(setting->str);
    setting->str = g_strdup(value);
    return TRUE;
}

gboolean config_setting_remove(config_setting_t * parent, const char * name)
{
    config_setting_t *s = config_setting_get_member(parent, name);
    if (s == NULL)
        return FALSE;
    _config_setting_t_remove(s);
    return TRUE;
}

gboolean config_setting_remove_elem(config_setting_t * parent, unsigned int index)
{
    config_setting_t *s = config_setting_get_elem(parent, index);
    if (s == NULL)
        return FALSE;
    _config_setting_t_remove(s);
    return TRUE;
}

gboolean config_setting_destroy(config_setting_t * setting)
{
    if (setting == NULL || setting->parent == NULL)
        return FALSE;
    _config_setting_t_remove(setting);
    return TRUE;
}

PanelConfType config_setting_type(const config_setting_t * setting)
{
    return setting->type;
}

void config_setting_set_save_hook(config_setting_t * setting, PanelConfSaveHook hook,
                                  gpointer user_data)
{
    if (setting == NULL)
        return;
    setting->hook = hook;
    setting->hook_data = user_data;
}
