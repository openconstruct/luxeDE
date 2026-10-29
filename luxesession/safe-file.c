/* Copyright (C) 2026 Jerry Howell
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Small user-configuration writer shared by the generated session settings
 * code.  It never follows a final symlink and reports a failed durable write
 * instead of truncating the user's existing file.
 */
#include "safe-file.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void set_errno_error(GError **error, const char *operation, const char *path)
{
    if (error != NULL && *error == NULL)
    {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "%s %s: %s", operation, path, g_strerror(errno));
    }
}

gchar **
lxsession_parse_command(const gchar *command_line,
                        const gchar *delimiter, int max_tokens)
{
    gchar **argv = NULL;
    GError *error = NULL;
    (void)delimiter;
    (void)max_tokens;

    if (command_line != NULL && command_line[0] != '\0' &&
        g_shell_parse_argv(command_line, NULL, &argv, &error) &&
        argv != NULL && argv[0] != NULL && argv[0][0] != '\0')
    {
        g_clear_error(&error);
        return argv;
    }
    g_clear_error(&error);
    g_strfreev(argv);
    argv = g_new0(gchar *, 2);
    argv[0] = g_strdup("");
    return argv;
}

gboolean
lxsession_read_bounded_file(const gchar *path, gsize max_bytes,
                             gchar **contents, gsize *length, GError **error)
{
    int fd = -1;
    struct stat st;
    gchar *buffer = NULL;
    gsize total = 0;

    if (error != NULL)
        *error = NULL;
    if (contents != NULL)
        *contents = NULL;
    if (length != NULL)
        *length = 0;
    if (path == NULL || contents == NULL || length == NULL || max_bytes == 0 ||
        max_bytes > G_MAXSIZE - 1)
    {
        if (error != NULL)
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                                "invalid bounded file input");
        return FALSE;
    }

    fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
    {
        set_errno_error(error, "open", path);
        return FALSE;
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        st.st_size < 0 || (guint64)st.st_size > max_bytes)
    {
        if (error != NULL)
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                        "refusing non-regular or oversized file: %s", path);
        goto fail;
    }
    buffer = g_malloc0((gsize)st.st_size + 1);
    while (total < (gsize)st.st_size)
    {
        ssize_t n = read(fd, buffer + total, (gsize)st.st_size - total);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            if (n == 0)
                errno = EIO;
            set_errno_error(error, "read", path);
            goto fail;
        }
        total += (gsize)n;
    }
    if (memchr(buffer, '\0', total) != NULL)
    {
        if (error != NULL)
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                        "NUL byte in text file: %s", path);
        goto fail;
    }
    close(fd);
    *contents = buffer;
    *length = total;
    return TRUE;

fail:
    if (fd >= 0)
        close(fd);
    g_free(buffer);
    return FALSE;
}

gboolean
lxsession_key_file_load_bounded(GKeyFile *key_file, const gchar *path,
                                 GError **error)
{
    gchar *contents = NULL;
    gsize length = 0;
    GError *local_error = NULL;
    gboolean ok;

    if (key_file == NULL)
    {
        if (error != NULL)
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                                "invalid key file");
        return FALSE;
    }
    ok = lxsession_read_bounded_file(path, 1024U * 1024U,
                                     &contents, &length, &local_error) &&
         g_key_file_load_from_data(key_file, contents, length,
                                   G_KEY_FILE_NONE, &local_error);
    if (!ok && local_error != NULL)
    {
        if (error != NULL)
            g_propagate_error(error, local_error);
        else
            g_error_free(local_error);
    }
    else
        g_clear_error(&local_error);
    g_free(contents);
    return ok;
}

gboolean
lxsession_atomic_write_file(const gchar *path, const gchar *contents,
                            gssize length, GError **error)
{
    gchar *directory = NULL;
    gchar *template = NULL;
    struct stat existing;
    mode_t mode = 0600;
    int fd = -1;
    int dir_fd = -1;
    gsize remaining;
    const gchar *cursor;

    if (error != NULL)
        *error = NULL;
    if (path == NULL || contents == NULL || length < 0 ||
        (length > 0 && strlen(contents) < (gsize)length))
    {
        if (error != NULL)
            g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                                "invalid atomic file input");
        return FALSE;
    }

    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0 && errno != EEXIST)
    {
        set_errno_error(error, "create directory for", path);
        goto fail;
    }
    if (lstat(path, &existing) == 0)
    {
        if (!S_ISREG(existing.st_mode) || existing.st_uid != getuid())
        {
            if (error != NULL)
                g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_PERM,
                            "refusing non-user regular target: %s", path);
            goto fail;
        }
        mode = existing.st_mode & 0777;
    }
    else if (errno != ENOENT)
    {
        set_errno_error(error, "stat", path);
        goto fail;
    }

    template = g_strdup_printf("%s/.luxesession-write-XXXXXX", directory);
    fd = g_mkstemp(template);
    if (fd < 0)
    {
        set_errno_error(error, "create temporary file for", path);
        goto fail;
    }
    if (fchmod(fd, mode) != 0)
    {
        set_errno_error(error, "set mode on", path);
        goto fail;
    }

    cursor = contents;
    remaining = (gsize)length;
    while (remaining > 0)
    {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
        {
            if (written == 0)
                errno = EIO;
            set_errno_error(error, "write", path);
            goto fail;
        }
        cursor += written;
        remaining -= (gsize)written;
    }
    if (fsync(fd) != 0)
    {
        set_errno_error(error, "flush", path);
        goto fail;
    }
    if (close(fd) != 0)
    {
        fd = -1;
        set_errno_error(error, "close", path);
        goto fail;
    }
    fd = -1;

    if (g_rename(template, path) != 0)
    {
        set_errno_error(error, "replace", path);
        goto fail;
    }
    g_clear_pointer(&template, g_free);

    dir_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0)
    {
        (void)fsync(dir_fd);
        close(dir_fd);
        dir_fd = -1;
    }
    g_free(directory);
    return TRUE;

fail:
    if (fd >= 0)
        close(fd);
    if (dir_fd >= 0)
        close(dir_fd);
    if (template != NULL)
    {
        if (g_file_test(template, G_FILE_TEST_EXISTS))
            g_unlink(template);
        g_free(template);
    }
    g_free(directory);
    return FALSE;
}
