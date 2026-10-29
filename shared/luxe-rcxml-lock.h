/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LUXE_RCXML_LOCK_H
#define LUXE_RCXML_LOCK_H

#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

/* Suite writers keep this lock file in the same private directory as rc.xml.
 * Never unlink it: unlinking a held lock would let a second writer lock a new
 * inode and enter the critical section concurrently. */
static inline int luxe_rcxml_lock_acquire(const char *config_path, GError **error)
{
    g_autofree char *directory = g_path_get_dirname(config_path);
    g_autofree char *basename = g_path_get_basename(config_path);
    g_autofree char *lock_name = g_strdup_printf(".%s.luxe.lock", basename);
    g_autofree char *lock_path = g_build_filename(directory, lock_name, NULL);
    struct stat path_stat, opened_stat;
    int fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Cannot open the Luxe rc.xml writer lock: %s", g_strerror(errno));
        return -1;
    }
    if (lstat(lock_path, &path_stat) != 0 || fstat(fd, &opened_stat) != 0) {
        int saved_errno = errno;
        close(fd);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                    "The Luxe rc.xml writer lock is unsafe: %s", g_strerror(saved_errno));
        return -1;
    }
    if (!S_ISREG(opened_stat.st_mode) || opened_stat.st_nlink != 1 ||
        opened_stat.st_uid != geteuid() || (opened_stat.st_mode & 0077) != 0 ||
        path_stat.st_dev != opened_stat.st_dev || path_stat.st_ino != opened_stat.st_ino) {
        close(fd);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                            "The Luxe rc.xml writer lock is unsafe");
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;
        close(fd);
        g_set_error(error, G_IO_ERROR,
                    saved_errno == EWOULDBLOCK ? G_IO_ERROR_BUSY : g_io_error_from_errno(saved_errno),
                    saved_errno == EWOULDBLOCK ? "Another Luxe settings tool is writing rc.xml" :
                    "Cannot acquire the Luxe rc.xml writer lock: %s",
                    g_strerror(saved_errno));
        return -1;
    }
    return fd;
}

#endif
