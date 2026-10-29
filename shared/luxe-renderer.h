/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_RENDERER_H
#define LUXE_RENDERER_H
#include <gtk/gtk.h>

/* GDK's `all` means all flags except the other tokens. Keep unrelated flags
 * and caller spelling, but leave the two unused graphics APIs disabled. */
static inline char *luxe_without_graphics(const char *original,
                                        const char *gl, const char *vulkan)
{
    g_auto(GStrv) tokens = g_strsplit_set(original ? original : "", ":;, \t", -1);
    gboolean inverted = FALSE;
    for (guint i = 0; tokens[i]; ++i)
        inverted |= g_ascii_strcasecmp(tokens[i], "all") == 0;
    GString *flags = g_string_new(NULL);
    for (guint i = 0; tokens[i]; ++i) {
        if (!*tokens[i] || (inverted &&
            (!g_ascii_strcasecmp(tokens[i], gl) || !g_ascii_strcasecmp(tokens[i], vulkan))))
            continue;
        if (flags->len) g_string_append_c(flags, ',');
        g_string_append(flags, tokens[i]);
    }
    if (!inverted) {
        if (flags->len) g_string_append_c(flags, ',');
        g_string_append_printf(flags, "%s,%s", gl, vulkan);
    }
    return g_string_free(flags, FALSE);
}

/* GTK caches its renderer choice at the first surface. Prime that choice
 * locally, then restore the caller's environment before launching children.
 * Call only when a process needs its first GUI, not in headless agents.
 * These native/Cairo frontends do not use GtkGLArea; avoid initializing the
 * unused Mesa/GL/Vulkan stacks as well as selecting the Cairo renderer. */
static inline void luxe_gtk_init(void)
{
    static gboolean primed;
    if (primed)
        return;
    g_autofree char *original = g_strdup(g_getenv("GSK_RENDERER"));
    gboolean modern = gtk_check_version(4, 16, 0) == NULL;
    const char *key = modern ? "GDK_DISABLE" : "GDK_DEBUG";
    g_autofree char *graphics_original = g_strdup(g_getenv(key));
    g_autofree char *flags = luxe_without_graphics(graphics_original,
        modern ? "gl" : "gl-disable", modern ? "vulkan" : "vulkan-disable");
    g_setenv(key, flags, TRUE);
    g_setenv("GSK_RENDERER", "cairo", TRUE);
    gtk_init();
    GtkWidget *window = gtk_window_new();
    gtk_widget_realize(window);
    GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(window));
    if (!renderer || !GSK_IS_CAIRO_RENDERER(renderer))
        g_error("luxe: Cairo renderer initialization failed");
    gtk_window_destroy(GTK_WINDOW(window));
    if (original)
        g_setenv("GSK_RENDERER", original, TRUE);
    else
        g_unsetenv("GSK_RENDERER");
    if (graphics_original)
        g_setenv(key, graphics_original, TRUE);
    else
        g_unsetenv(key);
    primed = TRUE;
}
#endif
