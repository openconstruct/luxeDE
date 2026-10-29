/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
#include "state.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#define STATE_MAX_BYTES (8 * 1024 * 1024)
gboolean lucid_state_write(GKeyFile *state, const char *path, GError **error)
{
    struct stat info;
    if(lstat(path,&info)==0&&!S_ISREG(info.st_mode)){
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Refusing a non-regular session file");return FALSE;
    }
    g_autofree char *parent = g_path_get_dirname(path);
    if (g_mkdir_with_parents(parent, 0700) < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot create session folder: %s", g_strerror(errno));
        return FALSE;
    }
    gsize size;
    g_autofree char *data = g_key_file_to_data(state, &size, error);
    if (!data) return FALSE;
    if(size>STATE_MAX_BYTES){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Session exceeds the recovery size limit");return FALSE;}
    return g_file_set_contents_full(path, data, size,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
}
GKeyFile *lucid_state_read(const char *path, GError **error)
{
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(fd<0){g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot open session: %s",g_strerror(errno));return NULL;}
    struct stat info;
    if(fstat(fd,&info)!=0||!S_ISREG(info.st_mode)||info.st_size<0||info.st_size>STATE_MAX_BYTES){
        close(fd);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid or oversized session file");return NULL;
    }
    g_autoptr(GString) text=g_string_new(NULL);
    char buffer[8192];ssize_t got;
    while((got=read(fd,buffer,sizeof buffer))!=0){
        if(got<0&&errno==EINTR)continue;
        if(got<0||text->len+got>STATE_MAX_BYTES||memchr(buffer,0,got)){
            close(fd);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Cannot read valid session data");return NULL;
        }
        g_string_append_len(text,buffer,got);
    }
    close(fd);
    GKeyFile *state = g_key_file_new();
    if (!g_key_file_load_from_data(state, text->str, text->len, G_KEY_FILE_NONE, error)) {
        g_key_file_unref(state); return NULL;
    }
    GError *field_error=NULL;
    int count = g_key_file_get_integer(state, "Session", "count", &field_error);
    gboolean valid=field_error==NULL;
    g_clear_error(&field_error);
    int version=g_key_file_get_integer(state,"Session","version",&field_error);
    valid=valid&&field_error==NULL;
    g_clear_error(&field_error);
    if (!valid || count < 0 || count > 1000 || version != 1) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Unsupported or invalid session file");
        g_key_file_unref(state); return NULL;
    }
    return state;
}
char *lucid_session_path(const char *directory, const char *name)
{
    g_autofree char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, name, -1);
    g_autofree char *file = g_strconcat(hash, ".session", NULL);
    return g_build_filename(directory, file, NULL);
}

static gboolean state_is_complete(GKeyFile *state)
{
    GError *error = NULL;
    gboolean complete = g_key_file_get_boolean(state, "Session", "complete", &error);
    if (g_error_matches(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
        g_error_matches(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND))
        complete = TRUE; /* Legacy sessions predate the completeness flag. */
    g_clear_error(&error);
    return complete;
}

char *lucid_state_preserve(GKeyFile *state, const char *directory, GError **error)
{
    gsize size = 0;
    g_autofree char *data = g_key_file_to_data(state, &size, error);
    if (!data) return NULL;
    if (size > STATE_MAX_BYTES) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Previous recovery exceeds the preservation limit");
        return NULL;
    }
    g_autofree char *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)data, size);
    g_autofree char *name = g_strconcat("recovered-", hash, ".session", NULL);
    g_autofree char *path = g_build_filename(directory, name, NULL);
    if (g_mkdir_with_parents(directory, 0700) != 0) goto io_error;
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        if (errno != EEXIST) goto io_error;
        g_autoptr(GKeyFile) existing = lucid_state_read(path, error);
        if (!existing) { g_prefix_error(error, "Cannot verify preserved recovery %s: ", path); return NULL; }
        gsize existing_size = 0;
        g_autofree char *existing_data = g_key_file_to_data(existing, &existing_size, error);
        if (!existing_data) return NULL;
        if (existing_size != size || memcmp(existing_data, data, size)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "A different file occupies the recovery preservation path: %s", path);
            return NULL;
        }
        fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) goto io_error;
    } else {
        gsize offset = 0;
        while (offset < size) {
            ssize_t written = write(fd, data + offset, size - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { int saved = written == 0 ? EIO : errno; close(fd); errno = saved; goto io_error; }
            offset += written;
        }
    }
    if (fsync(fd) != 0) { int saved = errno; close(fd); errno = saved; goto io_error; }
    if (close(fd) != 0) goto io_error;
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) goto io_error;
    if (fsync(fd) != 0) { int saved = errno; close(fd); errno = saved; goto io_error; }
    if (close(fd) != 0) goto io_error;
    return g_steal_pointer(&path);
io_error:
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot preserve previous recovery at %s: %s", path, g_strerror(errno));
    return NULL;
}

gboolean lucid_state_preserve_existing(const char *path, const char *directory,
                                       GError **error)
{
    g_autoptr(GKeyFile) state = lucid_state_read(path, error);
    if (!state) {
        if (error && g_error_matches(*error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_clear_error(error);
            return TRUE;
        }
        return FALSE;
    }
    /* Keep only one pre-limit snapshot. Repeated edits to an oversized
     * session must not create an unbounded content-addressed archive. A
     * complete backup is never replaced by a partial snapshot. */
    g_autofree char *backup_path = g_build_filename(directory,
        "recovery-before-limited.session", NULL);
    gboolean candidate_complete = state_is_complete(state);
    g_autoptr(GKeyFile) existing = lucid_state_read(backup_path, NULL);
    if (!candidate_complete && existing) {
        gboolean backup_complete = state_is_complete(existing);
        if (backup_complete) return TRUE;
    }
    return lucid_state_write(state, backup_path, error);
}
