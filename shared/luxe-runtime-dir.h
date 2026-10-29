/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_RUNTIME_DIR_H
#define LUXE_RUNTIME_DIR_H

#include <glib.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Recover a missing logind runtime directory only when it is private and
 * owned by this user. Never create it or replace an existing setting. */
static inline gboolean luxe_restore_runtime_dir_from(const char *base)
{
    const char *existing = g_getenv("XDG_RUNTIME_DIR");
    if (existing && *existing)
        return TRUE;
    if (getuid() != geteuid())
        return FALSE;
    g_autofree char *path = g_strdup_printf("%s/%lu", base, (unsigned long)getuid());
    GStatBuf st;
    if (g_lstat(path, &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != getuid() || (st.st_mode & 0777) != 0700)
        return FALSE;
    return g_setenv("XDG_RUNTIME_DIR", path, TRUE);
}

static inline gboolean luxe_restore_runtime_dir(void)
{
    return luxe_restore_runtime_dir_from("/run/user");
}
#endif
