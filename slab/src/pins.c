/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "pins.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

gboolean slab_id_valid(const char *id)
{
    if (!id || strlen(id) > 255 || !g_str_has_suffix(id, ".desktop") ||
        strlen(id) <= 8 || id[0] == '.' || strchr(id, '/') || strchr(id, '\\')) return FALSE;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
        if (*p <= 32 || *p >= 127) return FALSE;
    return TRUE;
}

int slab_pin_index(GPtrArray *pins, const char *id)
{
    for (guint i = 0; i < pins->len; ++i)
        if (g_str_equal(g_ptr_array_index(pins, i), id)) return (int)i;
    return -1;
}

static void invalid(GError **error)
{
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Pins must be a regular file, at most 64 KiB, with up to 32 desktop-file IDs.");
}

GPtrArray *slab_pins_load(const char *path, GError **error)
{
    GPtrArray *pins = g_ptr_array_new_with_free_func(g_free);
    int fd = g_open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0);
    if (fd < 0) {
        if (errno == ENOENT) {
            const char *defaults[] = {"luxeterm.desktop", "pcmanager.desktop", "org.luxe.Settings.desktop"};
            for (guint i = 0; i < G_N_ELEMENTS(defaults); ++i) g_ptr_array_add(pins, g_strdup(defaults[i]));
            return pins;
        }
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot read pins: %s", g_strerror(errno));
        g_ptr_array_unref(pins); return NULL;
    }
    struct stat st;
    g_auto(GStrv) lines = NULL;
    char buffer[65537]; size_t total = 0;
    gboolean okay = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= 65536;
    while (okay && total < sizeof buffer) {
        ssize_t n = read(fd, buffer + total, sizeof buffer - total);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) okay = FALSE;
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(fd);
    if (!okay || total > 65536 || memchr(buffer, 0, total)) goto fail;
    buffer[total] = 0;
    lines = g_strsplit(buffer, "\n", -1);
    for (guint i = 0; lines[i]; ++i) {
        char *id = g_strstrip(lines[i]);
        if (!*id || *id == '#') continue;
        if (!slab_id_valid(id)) goto fail;
        if (slab_pin_index(pins, id) >= 0) continue;
        if (pins->len >= SLAB_MAX_PINS) goto fail;
        g_ptr_array_add(pins, g_strdup(id));
    }
    return pins;
fail:
    invalid(error); g_ptr_array_unref(pins); return NULL;
}

gboolean slab_pins_save(const char *path, GPtrArray *pins, GError **error)
{
    if (pins->len > SLAB_MAX_PINS) { invalid(error); return FALSE; }
    GStatBuf st;
    if (g_lstat(path, &st) == 0 && !S_ISREG(st.st_mode)) { invalid(error); return FALSE; }
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot create pins directory: %s", g_strerror(errno));
        return FALSE;
    }
    GString *text = g_string_new(NULL);
    for (guint i = 0; i < pins->len; ++i) {
        const char *id = g_ptr_array_index(pins, i);
        if (!slab_id_valid(id)) { g_string_free(text, TRUE); invalid(error); return FALSE; }
        g_string_append_printf(text, "%s\n", id);
    }
    gboolean ok = g_file_set_contents_full(path, text->str, text->len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
    g_string_free(text, TRUE);
    return ok;
}

static gboolean pins_equal(GPtrArray *a, GPtrArray *b)
{
    if (a->len != b->len) return FALSE;
    for (guint i = 0; i < a->len; ++i)
        if (!g_str_equal(g_ptr_array_index(a, i), g_ptr_array_index(b, i))) return FALSE;
    return TRUE;
}

gboolean slab_pins_save_if_unchanged(const char *path, GPtrArray *expected,
                                     GPtrArray *pins, GError **error)
{
    g_autofree char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create pins directory: %s", g_strerror(errno));
        return FALSE;
    }
    g_autofree char *lock_path = g_strconcat(path, ".lock", NULL);
    int fd = g_open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot open pins lock: %s", g_strerror(errno));
        return FALSE;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid()) {
        close(fd); invalid(error); return FALSE;
    }
    if (flock(fd, LOCK_EX) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot lock pins: %s", g_strerror(errno));
        close(fd); return FALSE;
    }
    g_autoptr(GPtrArray) current = slab_pins_load(path, error);
    gboolean ok = current && pins_equal(current, expected);
    if (current && !ok)
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY,
                            "Pins changed in another Slab window. Reload and retry your edit.");
    if (ok) ok = slab_pins_save(path, pins, error);
    flock(fd, LOCK_UN); close(fd);
    return ok;
}

/* Return a new ordering; callers commit it only after persistence succeeds. */
GPtrArray *slab_pins_move(GPtrArray *pins, const char *id, guint index)
{
    GPtrArray *copy = g_ptr_array_new_with_free_func(g_free);
    index = MIN(index, pins->len);
    for (guint i = 0; i <= pins->len; ++i) {
        if (i == index) g_ptr_array_add(copy, g_strdup(id));
        if (i < pins->len && !g_str_equal(id, g_ptr_array_index(pins, i)))
            g_ptr_array_add(copy, g_strdup(g_ptr_array_index(pins, i)));
    }
    return copy;
}
