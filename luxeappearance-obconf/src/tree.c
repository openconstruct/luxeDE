/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   tree.c for ObConf, the configuration tool for Openbox
   Copyright (c) 2003-2007   Dana Jansens

   Copyright (C) 2010        Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
   Copyright (C) 2026        Jerry Howell

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

/* This file is part of ObConf. It's taken by Hong Jen Yee on
 * 2010-08-07 and some modifications were done to make it a loadable
 * module of LuxeAppearance. */

#include "tree.h"
#include "main.h"
#include "../../shared/luxe-rcxml-lock.h"

#include <obt/xml.h>
#include <gdk/x11/gdkx.h>
#include <glib/gstdio.h>
#include <gdk/x11/gdkx11property.h>
#include <glib/gi18n-lib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* Root window of the default X display (X11-only app). */
#define OB_ROOT_WINDOW() \
    (DefaultRootWindow(GDK_DISPLAY_XDISPLAY(gdk_display_get_default())))

/* Nanosecond-resolution mtime so back-to-back writes by another tool are
 * still noticed; size+inode catch replacement-by-rename. */
static gint64 obc_mtime_ns(const GStatBuf *st)
{
#if defined(__APPLE__)
    return (gint64)st->st_mtimespec.tv_sec * (gint64)1000000000
         + (gint64)st->st_mtimespec.tv_nsec;
#elif defined(G_OS_WIN32)
    return (gint64)st->st_mtime * (gint64)1000000000;
#else
    return (gint64)st->st_mtim.tv_sec * (gint64)1000000000
         + (gint64)st->st_mtim.tv_nsec;
#endif
}

static gboolean obc_stat_matches(const GStatBuf *a, const GStatBuf *b)
{
    return obc_mtime_ns(a) == obc_mtime_ns(b) &&
           a->st_size == b->st_size &&
           a->st_ino == b->st_ino;
}

typedef struct { int fd; } RcxmlLock;
static void rcxml_lock_clear(RcxmlLock *lock)
{
    if (lock->fd >= 0) close(lock->fd);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(RcxmlLock, rcxml_lock_clear)

xmlNodePtr tree_find_node(const gchar *path)
{
    xmlNodePtr n, c;
    gchar **nodes;
    if (!xml_i || !path || !*path) return NULL;
    n = obt_xml_root(xml_i);
    if (!n) return NULL;
    nodes = g_strsplit(path, "/", 0);
    for (gchar **it = nodes; *it; ++it) {
        gchar **attrs = g_strsplit(*it, ":", 0);
        c = obt_xml_find_node(n->children, attrs[0]);
        while (c) {
            gboolean matches = TRUE;
            for (gint i = 1; attrs[i] && matches; ++i) {
                gchar **eq = g_strsplit(attrs[i], "=", 2);
                if (eq[1] && !obt_xml_attr_contains(c, eq[0], eq[1])) matches = FALSE;
                g_strfreev(eq);
            }
            if (matches) break;
            c = obt_xml_find_node(c->next, attrs[0]);
        }
        g_strfreev(attrs);
        if (!c) { g_strfreev(nodes); return NULL; }
        n = c;
    }
    g_strfreev(nodes);
    return n;
}

xmlNodePtr tree_ensure_node(const gchar *path, const gchar *def)
{
    xmlNodePtr n, c;
    gchar **nodes;
    gchar **it, **next;

    if (!xml_i || !path || !*path)
        return NULL;
    n = obt_xml_root(xml_i);
    if (!n)
        return NULL;

    nodes = g_strsplit(path, "/", 0);
    for (it = nodes; *it; it = next) {
        gchar **attrs;
        gboolean ok = FALSE;

        attrs = g_strsplit(*it, ":", 0);
        next = it + 1;

        /* match attributes */
        if (!n) {
            g_strfreev(attrs);
            g_strfreev(nodes);
            return NULL;
        }
        c = obt_xml_find_node(n->children, attrs[0]);
        while (c && !ok) {
            gint i;

            ok = TRUE;
            for (i = 1; attrs[i]; ++i) {
                gchar **eq = g_strsplit(attrs[i], "=", 2);
                if (eq[1] && !obt_xml_attr_contains(c, eq[0], eq[1]))
                    ok = FALSE;
                g_strfreev(eq);
            }
            if (!ok)
                c = obt_xml_find_node(c->next, attrs[0]);
        }

        if (!c) {
            gint i;

            c = xmlNewTextChild(n, NULL, BAD_CAST attrs[0], *next ? NULL : BAD_CAST def);

            for (i = 1; attrs[i]; ++i) {
                gchar **eq = g_strsplit(attrs[i], "=", 2);
                if (eq[1])
                    xmlNewProp(c, BAD_CAST eq[0], BAD_CAST eq[1]);
                g_strfreev(eq);
            }
        }
        n = c;

        g_strfreev(attrs);
    }

    g_strfreev(nodes);

    return n;
}

void tree_delete_node(const gchar *path)
{
    xmlNodePtr n;

    n = tree_find_node(path);
    if (!n)
        return;
    xmlUnlinkNode(n);
    xmlFreeNode(n);
}

static gboolean tree_target_is_safe(const gchar *path, GError **error)
{
    GStatBuf st;

    if (lstat(path, &st) == 0) {
        if (S_ISLNK(st.st_mode)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                        "Refusing to modify symlinked Openbox configuration: %s", path);
            return FALSE;
        }
        if (!S_ISREG(st.st_mode)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY,
                        "Openbox configuration is not a regular file: %s", path);
            return FALSE;
        }
        return TRUE;
    }
    if (errno != ENOENT) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot stat Openbox configuration %s: %s", path,
                    g_strerror(errno));
        return FALSE;
    }
    return TRUE;
}

static gchar *tree_target_path(void)
{
    if (obc_config_file)
        return g_strdup(obc_config_file);
    return g_build_filename(obt_paths_config_home(paths), "openbox", "rc.xml", NULL);
}

gboolean tree_apply(void)
{
    g_auto(RcxmlLock) lock = { .fd = -1 };
    g_autofree gchar *p = tree_target_path();
    g_autofree gchar *d = NULL;
    g_autofree gchar *tmp = NULL;
    g_autofree gchar *template = NULL;
    g_autoptr(GError) error = NULL;
    GStatBuf before;
    gboolean existed;
    gint fd = -1;
    XEvent ce;

    if (!xml_i || !paths) {
        obconf_error(_("No Openbox configuration is loaded."), FALSE);
        return FALSE;
    }
    if (!tree_target_is_safe(p, &error)) {
        obconf_error(error->message, FALSE);
        return FALSE;
    }

    d = g_path_get_dirname(p);
    if (!g_file_test(d, G_FILE_TEST_IS_DIR) &&
        !obt_paths_mkdir_path(d, 0700)) {
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Cannot create configuration directory %s", d);
        obconf_error(error->message, FALSE);
        return FALSE;
    }

    lock.fd = luxe_rcxml_lock_acquire(p, &error);
    if (lock.fd < 0) {
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    if (!tree_target_is_safe(p, &error)) {
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    existed = (lstat(p, &before) == 0);
    if (obc_loaded_stat_valid != existed ||
        (existed && !obc_stat_matches(&obc_loaded_stat, &before))) {
        obconf_error(_("rc.xml has changed since it was loaded (another settings tool wrote it). Reload the settings dialog to pick up the changes."), FALSE);
        return FALSE;
    }

    /* Save into the same directory and rename only after a complete write.
     * obt_xml_save_file() writes directly to its target; using it on a temp
     * path avoids truncating a user's rc.xml if XML serialization fails. */
    template = g_build_filename(d, ".obconf-rc-XXXXXX", NULL);
    fd = g_mkstemp(template);
    if (fd < 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot create a temporary Openbox configuration: %s",
                    g_strerror(errno));
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    tmp = g_strdup(template);
    if (close(fd) != 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot close the temporary Openbox configuration: %s",
                    g_strerror(errno));
        g_unlink(tmp);
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    if (!obt_xml_save_file(xml_i, tmp, TRUE)) {
        g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Could not serialize the Openbox configuration");
        g_unlink(tmp);
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    if (g_chmod(tmp, existed ? (before.st_mode & 0777) : 0600) != 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot set permissions on the temporary Openbox configuration: %s",
                    g_strerror(errno));
        g_unlink(tmp);
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    fd = g_open(tmp, O_RDWR | O_CLOEXEC, 0);
    if (fd < 0 || fsync(fd) != 0) {
        gint saved_errno = errno;
        if (fd >= 0)
            close(fd);
        g_set_error(&error, G_IO_ERROR,
                    g_io_error_from_errno(saved_errno),
                    "Cannot flush the temporary Openbox configuration: %s",
                    g_strerror(saved_errno));
        g_unlink(tmp);
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    if (close(fd) != 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot close the temporary Openbox configuration: %s",
                    g_strerror(errno));
        g_unlink(tmp);
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    {
        GStatBuf latest;
        gint latest_result = lstat(p, &latest);
        gboolean changed = existed
            ? (latest_result != 0 || !obc_stat_matches(&obc_loaded_stat, &latest))
            : (latest_result == 0);
        if (changed) {
            g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "rc.xml changed while it was being saved");
            g_unlink(tmp);
            obconf_error(error->message, FALSE);
            return FALSE;
        }
    }
    if (g_rename(tmp, p) != 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Could not replace Openbox configuration %s: %s", p,
                    g_strerror(errno));
        g_unlink(tmp);
        obconf_error(error->message, FALSE);
        return FALSE;
    }
    obc_loaded_stat_valid = (lstat(p, &obc_loaded_stat) == 0);
    {
        int dir_fd = g_open(d, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
        if (dir_fd >= 0) {
            (void)fsync(dir_fd);
            close(dir_fd);
        }
    }

    /* The durable write succeeded. Notify Openbox, but do not turn a
     * best-effort X11 reconfigure request into a false persistence failure. */
    gdk_x11_display_error_trap_push(gdk_display_get_default());
    memset(&ce, 0, sizeof(ce));
    ce.xclient.type = ClientMessage;
    ce.xclient.message_type = gdk_x11_get_xatom_by_name("_OB_CONTROL");
    ce.xclient.display = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    ce.xclient.window = OB_ROOT_WINDOW();
    ce.xclient.format = 32;
    ce.xclient.data.l[0] = 1; /* reconfigure */
    XSendEvent(ce.xclient.display, ce.xclient.window, FALSE,
               SubstructureNotifyMask | SubstructureRedirectMask, &ce);
    XFlush(ce.xclient.display);
    gdk_x11_display_error_trap_pop_ignored(gdk_display_get_default());
    return TRUE;
}

void tree_set_string(const gchar *node, const gchar *value)
{
    xmlNodePtr n;

    n = tree_ensure_node(node, NULL);
    if (!n || !value || !g_utf8_validate(value, -1, NULL))
        return;
    xmlNodeSetContent(n, (const xmlChar*) value);

    obconf_set_dirty();
}

void tree_set_int(const gchar *node, const gint value)
{
    xmlNodePtr n;
    gchar *s;

    n = tree_ensure_node(node, NULL);
    if (!n)
        return;
    s = g_strdup_printf("%d", value);
    xmlNodeSetContent(n, (const xmlChar*) s);
    g_free(s);

    obconf_set_dirty();
}

void tree_set_bool(const gchar *node, const gboolean value)
{
    xmlNodePtr n;

    n = tree_ensure_node(node, NULL);
    if (!n)
        return;
    xmlNodeSetContent(n, (const xmlChar*) (value ? "yes" : "no"));

    obconf_set_dirty();
}

gchar* tree_get_string(const gchar *node, const gchar *def)
{
    xmlNodePtr n;
    gchar *value;

    n = tree_find_node(node);
    if (!n)
        return g_strdup(def);
    value = obt_xml_node_string(n);
    return value ? value : g_strdup(def);
}

gint tree_get_int(const gchar *node, gint def)
{
    xmlNodePtr n;
    n = tree_find_node(node);
    return n ? obt_xml_node_int(n) : def;
}

gboolean tree_get_bool(const gchar *node, gboolean def)
{
    xmlNodePtr n;

    n = tree_find_node(node);
    return n ? obt_xml_node_bool(n) : def;
}
