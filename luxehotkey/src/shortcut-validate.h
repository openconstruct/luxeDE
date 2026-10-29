/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_SHORTCUT_VALIDATE_H
#define LUXE_SHORTCUT_VALIDATE_H

#include <glib.h>
#include <X11/Xlib.h>
#include <string.h>

/* The Openbox serializer supports exactly these modifier names. Validate at
 * every entry point before serialization so unknown tokens cannot vanish. */
typedef struct { guint modifiers; KeySym key; } LuxeShortcutIdentity;

static gboolean luxe_shortcut_parse(const char *key, LuxeShortcutIdentity *identity)
{
    static const char *const modifiers[] = {
        "Primary", "Control", "Ctrl", "Shift", "Alt", "Super", "Meta", "Hyper"
    };
    static const guint bits[] = { 1, 1, 1, 2, 4, 8, 16, 32 };
    guint flags = 0;
    if (!key || !*key || g_str_equal(key, "--")) return FALSE;
    const char *cursor = key;
    while (*cursor == '<') {
        const char *end = strchr(cursor + 1, '>');
        if (!end || end == cursor + 1) return FALSE;
        gboolean known = FALSE;
        for (guint i = 0; i < G_N_ELEMENTS(modifiers); i++)
            if ((gsize)(end - cursor - 1) == strlen(modifiers[i]) &&
                strncmp(cursor + 1, modifiers[i], end - cursor - 1) == 0)
                { known = TRUE; flags |= bits[i]; }
        if (!known) return FALSE;
        cursor = end + 1;
    }
    if (!*cursor || strpbrk(cursor, "<> \t\r\n") != NULL) return FALSE;
    KeySym symbol = XStringToKeysym(cursor);
    if (symbol == NoSymbol) return FALSE;
    if (identity) { identity->modifiers = flags; identity->key = symbol; }
    return TRUE;
}

static inline gboolean luxe_shortcut_valid(const char *key)
{ return luxe_shortcut_parse(key, NULL); }

static inline gboolean luxe_shortcut_equal(const char *a, const char *b)
{
    LuxeShortcutIdentity first, second;
    return luxe_shortcut_parse(a, &first) && luxe_shortcut_parse(b, &second) &&
           first.modifiers == second.modifiers && first.key == second.key;
}
#endif
