/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later
 *
 * LuxUp update detection.  Every channel is probed through its normal
 * command-line client; nothing here requires root.  The optional APT list
 * refresh reuses LuxuryStore's authenticated helper. */
#include "check.h"
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib/gstdio.h>

#ifndef LUXUP_HELPER
#define LUXUP_HELPER "/usr/libexec/luxurystore-apt"
#endif

#ifndef LUXUP_AGENT_RETRIES
#define LUXUP_AGENT_RETRIES 30
#endif
#ifndef LUXUP_AGENT_RETRY_MS
#define LUXUP_AGENT_RETRY_MS 500
#endif
#ifndef LUXUP_COMMAND_TIMEOUT
#define LUXUP_COMMAND_TIMEOUT "60s"
#endif

static void cancel_command(GCancellable *cancel, gpointer data)
{
    (void)cancel;
    /* timeout forwards TERM to its owned process group, with a KILL fallback. */
    g_subprocess_send_signal(G_SUBPROCESS(data), SIGTERM);
}

static const char *source_names[] = {"APT", "Flathub", "Snap Store"};

const char *luxup_source_name(int source)
{
    return (source >= 0 && source < 3) ? source_names[source] : "Unknown";
}

static LuxUpPackage *package_new(int source, const char *name, const char *installed, const char *available)
{
    LuxUpPackage *p = g_new0(LuxUpPackage, 1);
    p->source = source;
    p->name = g_strdup(name);
    p->installed = g_strdup(installed ? installed : "");
    p->available = g_strdup(available ? available : "");
    return p;
}

static void package_free(LuxUpPackage *p)
{
    if (!p)
        return;
    g_free(p->name);
    g_free(p->installed);
    g_free(p->available);
    g_free(p->scope);
    g_free(p->ref);
    g_free(p);
}

void luxup_result_free(LuxUpResult *result)
{
    if (!result)
        return;
    if (result->packages)
        g_ptr_array_unref(result->packages);
    g_free(result->apt_refresh_error);
    g_free(result->apt_check_error);
    g_free(result->flatpak_user_error);
    g_free(result->flatpak_system_error);
    g_free(result->snap_check_error);
    g_free(result);
}

guint luxup_count(const LuxUpResult *result, int source)
{
    guint n = 0;
    if (!result || !result->packages)
        return 0;
    for (guint i = 0; i < result->packages->len; ++i) {
        LuxUpPackage *p = g_ptr_array_index(result->packages, i);
        if (p->source == source)
            ++n;
    }
    return n;
}

/* Run a command with a hard timeout.  Returns stdout on success, NULL on
 * failure with *error set (caller frees).  The cancellable aborts the
 * in-flight child so cancellation is effective, not decorative. */
static char *run_command(const char *const *args, const char *working_dir,
                         GCancellable *cancellable, GError **error)
{
    if (g_cancellable_set_error_if_cancelled(cancellable, error)) return NULL;
    GPtrArray *argv = g_ptr_array_new();
    g_ptr_array_add(argv, (gpointer) "/usr/bin/timeout");
    g_ptr_array_add(argv, (gpointer) "--kill-after=5s");
    g_ptr_array_add(argv, (gpointer) LUXUP_COMMAND_TIMEOUT);
    for (const char *const *a = args; *a; ++a)
        g_ptr_array_add(argv, (gpointer) *a);
    g_ptr_array_add(argv, NULL);

    GSubprocessLauncher *launcher = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_setenv(launcher, "LC_ALL", "C.UTF-8", TRUE);
    if (working_dir)
        g_subprocess_launcher_set_cwd(launcher, working_dir);
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, (const char *const *) argv->pdata, error);
    g_ptr_array_unref(argv);
    g_object_unref(launcher);
    if (!process)
        return NULL;

    gulong cancelled = cancellable ? g_cancellable_connect(cancellable,
        G_CALLBACK(cancel_command), g_object_ref(process), g_object_unref) : 0;
    GInputStream *streams[] = {g_subprocess_get_stdout_pipe(process), g_subprocess_get_stderr_pipe(process)};
    const gsize limits[] = {8u * 1024u * 1024u, 256u * 1024u};
    GByteArray *buffers[] = {g_byte_array_new(), g_byte_array_new()};
    GPollFD fds[3] = {{.fd = -1}, {.fd = -1}, {.fd = -1}};
    gboolean ok = TRUE, cancelled_poll = FALSE;
    for (guint i = 0; i < 2; i++) {
        if (!G_IS_UNIX_INPUT_STREAM(streams[i])) { ok = FALSE; break; }
        fds[i].fd = g_unix_input_stream_get_fd(G_UNIX_INPUT_STREAM(streams[i]));
        fds[i].events = G_IO_IN | G_IO_HUP | G_IO_ERR;
        int flags = fcntl(fds[i].fd, F_GETFL);
        if (flags < 0 || fcntl(fds[i].fd, F_SETFL, flags | O_NONBLOCK) < 0) { ok = FALSE; break; }
    }
    if (!ok) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot read command output safely");
    if (cancellable) cancelled_poll = g_cancellable_make_pollfd(cancellable, &fds[2]);
    while (ok && (fds[0].fd >= 0 || fds[1].fd >= 0)) {
        if (g_cancellable_set_error_if_cancelled(cancellable, error)) { ok = FALSE; break; }
        int ready = g_poll(fds, cancelled_poll ? 3 : 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot read command output: %s", g_strerror(errno));
            ok = FALSE; break;
        }
        for (guint i = 0; i < 2 && ok; i++) {
            if (fds[i].fd < 0 || !fds[i].revents) continue;
            guint8 block[16384];
            ssize_t count = read(fds[i].fd, block, sizeof block);
            if (count > 0) {
                if ((gsize)count > limits[i] - buffers[i]->len) {
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                                "Command %s exceeded its output limit", i ? "error output" : "output");
                    ok = FALSE;
                } else g_byte_array_append(buffers[i], block, count);
            } else if (count == 0) fds[i].fd = -1;
            else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot read command output: %s", g_strerror(errno));
                ok = FALSE;
            }
        }
    }
    if (cancelled_poll) g_cancellable_release_fd(cancellable);
    char *out = NULL, *err = NULL;
    if (ok && ((buffers[0]->len && !g_utf8_validate((char *)buffers[0]->data, buffers[0]->len, NULL)) ||
               (buffers[1]->len && !g_utf8_validate((char *)buffers[1]->data, buffers[1]->len, NULL)))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Command output is not valid UTF-8 text");
        ok = FALSE;
    }
    if (ok) {
        g_byte_array_append(buffers[0], (const guint8 *)"", 1);
        g_byte_array_append(buffers[1], (const guint8 *)"", 1);
        out = (char *)g_byte_array_free(buffers[0], FALSE);
        err = (char *)g_byte_array_free(buffers[1], FALSE);
    } else {
        g_byte_array_unref(buffers[0]); g_byte_array_unref(buffers[1]);
    }
    if (!ok) {
        g_subprocess_send_signal(process, SIGTERM);
        for (guint i = 0; i < 2; i++) g_input_stream_close(streams[i], NULL, NULL);
        g_subprocess_wait(process, NULL, NULL);
    } else if (!g_subprocess_wait(process, cancellable, error)) {
        ok = FALSE;
        g_subprocess_send_signal(process, SIGTERM);
        g_subprocess_wait(process, NULL, NULL);
    }
    if (cancelled) g_cancellable_disconnect(cancellable, cancelled);
    gboolean success = ok && g_subprocess_get_successful(process);
    if (!success && error && !*error) {
        if (g_subprocess_get_if_exited(process) && g_subprocess_get_exit_status(process) == 124)
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "The command timed out.");
        else {
            gchar *detail = g_strstrip(g_strdup(err ? err : ""));
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                        detail[0] ? detail : "The command failed.");
            g_free(detail);
        }
    }
    g_free(err);
    g_object_unref(process);
    if (!success) {
        g_free(out);
        return NULL;
    }
    return out ? out : g_strdup("");
}

static gboolean executable_exists(const char *path)
{
    return g_file_test(path, G_FILE_TEST_IS_EXECUTABLE);
}

static void probe_failed(LuxUpResult *result, char **slot, const char *source, GError *error)
{
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) return;
    result->check_failed = TRUE;
    if (!*slot) {
        const char *message = error ? error->message : "Unknown error";
        g_autofree char *bounded = g_utf8_substring(message, 0, MIN(g_utf8_strlen(message, -1), 1024));
        *slot = g_strdup_printf("%s: %s", source, bounded);
    }
}

/* Refresh the APT lists through LuxuryStore's pkexec helper.  A cancelled or
 * failed authentication leaves the existing lists in place. */
static char *refresh_apt(GCancellable *cancellable)
{
    if (cancellable && g_cancellable_is_cancelled(cancellable))
        return NULL;
    if (!executable_exists(LUXUP_HELPER) || !executable_exists("/usr/bin/pkexec"))
        return g_strdup("The LuxuryStore helper is not installed; using the existing package lists.");
    const char *args[] = {"/usr/bin/pkexec", "--disable-internal-agent", LUXUP_HELPER, "--refresh", "apt", NULL};
    for (guint attempt = 0; ; attempt++) {
        g_autoptr(GError) error = NULL;
        g_autofree char *out = run_command(args, NULL, cancellable, &error);
        if (out || (cancellable && g_cancellable_is_cancelled(cancellable)))
            return NULL;
        /* Both autostart entries can run before the agent has registered.
         * pkexec's C-locale error identifies this pre-prompt failure. Never
         * retry a dismissed/denied prompt or a failed privileged operation. */
        const char *message = error ? error->message : "Package list refresh failed.";
        if (attempt >= LUXUP_AGENT_RETRIES ||
            g_strcmp0(message, "Error executing command as another user: No authentication agent found.") != 0)
            return g_strdup(message);

        /* This runs in the check worker. Cancellation wakes the wait at once;
         * no sleeps or synchronous authorization requests run on GTK's thread. */
        GPollFD pollfd;
        if (cancellable && g_cancellable_make_pollfd(cancellable, &pollfd)) {
            g_poll(&pollfd, 1, LUXUP_AGENT_RETRY_MS);
            g_cancellable_release_fd(cancellable);
        } else {
            g_usleep(LUXUP_AGENT_RETRY_MS * 1000);
        }
        if (cancellable && g_cancellable_is_cancelled(cancellable))
            return NULL;
    }
}

/* Persist only successful refreshes. This state belongs to the current user,
 * is bounded on read, and is replaced atomically so a crash cannot leave a
 * partially written timestamp that looks valid. */
static char *apt_refresh_state_path(void)
{
    return g_build_filename(g_get_user_state_dir(), "luxup", "apt-refreshed-at", NULL);
}

static gint64 read_apt_refresh_time(void)
{
    g_autofree char *path = apt_refresh_state_path();
    char buffer[33];
    struct stat st;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() ||
        st.st_size < 2 || st.st_size > 32) { close(fd); return 0; }
    ssize_t n;
    do { n = read(fd, buffer, sizeof buffer - 1); } while (n < 0 && errno == EINTR);
    close(fd);
    if (n < 2 || n >= (ssize_t)sizeof buffer) return 0;
    buffer[n] = 0;
    char *end = NULL;
    errno = 0;
    gint64 value = g_ascii_strtoll(buffer, &end, 10);
    if (errno || value <= 0 || end == buffer || (*end != '\n' && *end != 0) ||
        (end[0] == '\n' && end[1] != 0)) return 0;
    return value;
}

static gboolean write_apt_refresh_time(gint64 value)
{
    g_autofree char *path = apt_refresh_state_path();
    g_autofree char *directory = g_path_get_dirname(path);
    g_autofree char *temporary = g_strdup_printf("%s/.apt-refreshed-at-XXXXXX", directory);
    g_autofree char *contents = g_strdup_printf("%" G_GINT64_FORMAT "\n", value);
    struct stat st;
    if (g_mkdir_with_parents(directory, 0700) != 0 || g_lstat(directory, &st) != 0 ||
        !S_ISDIR(st.st_mode) || st.st_uid != getuid() || S_ISLNK(st.st_mode)) return FALSE;
    if ((st.st_mode & 077) && g_chmod(directory, 0700) != 0) return FALSE;
    int fd = g_mkstemp_full(temporary, O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return FALSE;
    gsize length = strlen(contents), written = 0;
    gboolean ok = TRUE;
    while (written < length) {
        ssize_t n = write(fd, contents + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = FALSE; break; }
        written += (gsize)n;
    }
    if (ok && fsync(fd) != 0) ok = FALSE;
    if (close(fd) != 0) ok = FALSE;
    if (ok && g_rename(temporary, path) != 0) ok = FALSE;
    if (ok) {
        int dirfd = open(directory, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (dirfd < 0 || fsync(dirfd) != 0) ok = FALSE;
        if (dirfd >= 0) close(dirfd);
    }
    if (!ok) g_unlink(temporary);
    return ok;
}

static void check_apt(LuxUpResult *result, GCancellable *cancellable)
{
    const char *args[] = {"/usr/bin/apt", "list", "--upgradable", NULL};
    GError *error = NULL;
    char *out = run_command(args, NULL, cancellable, &error);
    if (!out) {
        probe_failed(result, &result->apt_check_error, "APT", error);
        g_clear_error(&error);
        return;
    }
    GRegex *pattern = g_regex_new("^([^/]+)/(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+\\[upgradable from: (.+)\\]$",
                                  G_REGEX_MULTILINE, 0, NULL);
    GMatchInfo *match = NULL;
    gchar **lines = g_strsplit(out, "\n", -1);
    gboolean saw_header = FALSE, saw_package = FALSE;
    for (guint i = 0; lines[i]; ++i) {
        g_strstrip(lines[i]);
        if (!*lines[i]) continue;
        if (g_str_has_prefix(lines[i], "Listing...")) { saw_header = TRUE; continue; }
        if (!g_regex_match(pattern, lines[i], 0, &match)) {
            result->check_failed = TRUE;
            if (!result->apt_check_error)
                result->apt_check_error = g_strdup("APT returned an unrecognized update listing; results may be incomplete.");
            g_match_info_free(match);
            match = NULL;
            continue;
        }
        gchar *name = g_match_info_fetch(match, 1);
        saw_package = TRUE;
        gchar *available = g_match_info_fetch(match, 3);
        gchar *installed = g_match_info_fetch(match, 5);
        g_ptr_array_add(result->packages, package_new(LUXUP_SOURCE_APT, name, installed, available));
        g_free(name);
        g_free(available);
        g_free(installed);
        g_match_info_free(match);
        match = NULL;
    }
    if (!saw_header && !saw_package) {
        result->check_failed = TRUE;
        if (!result->apt_check_error)
            result->apt_check_error = g_strdup("APT returned no recognizable update listing; results may be incomplete.");
    }
    g_regex_unref(pattern);
    g_strfreev(lines);
    g_free(out);
}

static gboolean flathub_official(const char *url)
{
    return g_strcmp0(url, "https://dl.flathub.org/repo/") == 0 ||
           g_strcmp0(url, "https://dl.flathub.org/repo") == 0;
}

static gboolean flatpak_scope_verified(const char *scope, LuxUpResult *result,
                                         GCancellable *cancellable)
{
    const char *args[] = {"/usr/bin/flatpak", "remotes", scope, "--columns=name,url", NULL};
    GError *error = NULL;
    char *out = run_command(args, NULL, cancellable, &error);
    if (!out) {
        gboolean user = g_str_equal(scope, "--user");
        probe_failed(result, user ? &result->flatpak_user_error : &result->flatpak_system_error,
                     user ? "Flatpak user" : "Flatpak system", error);
        g_clear_error(&error);
        return FALSE;
    }
    gboolean verified = FALSE;
    gchar **lines = g_strsplit(out, "\n", -1);
    for (guint i = 0; lines[i] && !verified; ++i) {
        gchar **fields = g_strsplit(lines[i], "\t", -1);
        if (fields[0] && fields[1] && g_strcmp0(fields[0], "flathub") == 0 && flathub_official(fields[1]))
            verified = TRUE;
        g_strfreev(fields);
    }
    g_strfreev(lines);
    g_free(out);
    return verified;
}

static void check_flatpak(LuxUpResult *result, GHashTable *seen, GCancellable *cancellable)
{
    gboolean flatpak_available = executable_exists("/usr/bin/flatpak");
    if (!flatpak_available)
        return;
    for (int i = 0; i < 2; ++i) {
        if (cancellable && g_cancellable_is_cancelled(cancellable))
            return;
        const char *scope = i == 0 ? "--user" : "--system";
        if (!flatpak_scope_verified(scope, result, cancellable))
            continue;
        const char *args[] = {"/usr/bin/flatpak", "remote-ls", scope, "--app", "--updates",
                              "--columns=ref,name,version", "flathub", NULL};
        GError *error = NULL;
        char *out = run_command(args, NULL, cancellable, &error);
        if (!out) {
            probe_failed(result, i == 0 ? &result->flatpak_user_error : &result->flatpak_system_error,
                         i == 0 ? "Flatpak user" : "Flatpak system", error);
            g_clear_error(&error);
            continue;
        }
        gchar **lines = g_strsplit(out, "\n", -1);
        for (guint k = 0; lines[k]; ++k) {
            if (!lines[k][0])
                continue;
            gchar **fields = g_strsplit(lines[k], "\t", -1);
            guint n_fields = g_strv_length(fields);
            const char *ref = fields[0];
            g_autofree char *identity = ref && *ref ? g_strdup_printf("%s:%s", scope, ref) : NULL;
            if (identity && !g_hash_table_contains(seen, identity)) {
                const char *name = (fields[1] && fields[1][0]) ? fields[1] : ref;
                const char *available = n_fields > 2 ? fields[2] : "";
                g_hash_table_add(seen, g_strdup(identity));
                LuxUpPackage *package = package_new(LUXUP_SOURCE_FLATPAK, name, "", available);
                package->scope = g_strdup(i == 0 ? "user" : "system");
                package->ref = g_strdup(ref);
                g_ptr_array_add(result->packages, package);
            }
            g_strfreev(fields);
        }
        g_strfreev(lines);
        g_free(out);
    }
}

static void check_snap(LuxUpResult *result, GCancellable *cancellable)
{
    result->snap_available = executable_exists("/usr/bin/snap");
    if (!result->snap_available)
        return;

    const char *time_args[] = {"/usr/bin/snap", "refresh", "--time", NULL};
    GError *time_error = NULL;
    char *time_out = run_command(time_args, NULL, cancellable, &time_error);
    if (time_out) {
        gchar **time_lines = g_strsplit(time_out, "\n", -1);
        gboolean saw_timer = FALSE, saw_last = FALSE, saw_next = FALSE;
        gboolean saw_hold = FALSE, invalid_hold = FALSE;
        for (guint i = 0; time_lines[i]; ++i) {
            gchar *line = g_strstrip(time_lines[i]);
            if (g_str_has_prefix(line, "timer:") && *(g_strstrip(line + strlen("timer:"))))
                saw_timer = TRUE;
            else if (g_str_has_prefix(line, "last:") && *(g_strstrip(line + strlen("last:"))))
                saw_last = TRUE;
            else if (g_str_has_prefix(line, "next:") && *(g_strstrip(line + strlen("next:"))))
                saw_next = TRUE;
            else if (g_str_has_prefix(line, "hold:")) {
                saw_hold = TRUE;
                const char *value = g_strstrip(line + strlen("hold:"));
                if (!*value || g_str_equal(value, "forever")) {
                    result->snap_hold_known = TRUE;
                    result->snap_auto_refresh_held = g_str_equal(value, "forever");
                } else {
                    g_autoptr(GDateTime) hold_time = g_date_time_new_from_iso8601(value, NULL);
                    if (hold_time)
                        result->snap_hold_known = TRUE;
                    else
                        invalid_hold = TRUE;
                }
            }
        }
        /* Snap omits `hold:` when no hold is configured. Its complete
         * timer/last/next schedule is then the positive evidence for no hold. */
        if (!saw_hold && saw_timer && saw_last && saw_next)
            result->snap_hold_known = TRUE;
        if (invalid_hold)
            result->snap_hold_known = FALSE;
        g_strfreev(time_lines);
        g_free(time_out);
    }
    g_clear_error(&time_error);

    const char *args[] = {"/usr/bin/snap", "refresh", "--list", NULL};
    GError *error = NULL;
    char *out = run_command(args, NULL, cancellable, &error);
    if (!out) {
        probe_failed(result, &result->snap_check_error, "Snap", error);
        g_clear_error(&error);
        return;
    }
    gchar **lines = g_strsplit(out, "\n", -1);
    for (guint i = 0; lines[i]; ++i) {
        gchar **raw = g_strsplit_set(lines[i], " \t", -1);
        GPtrArray *fields = g_ptr_array_new();
        for (guint k = 0; raw[k]; ++k)
            if (raw[k][0])
                g_ptr_array_add(fields, raw[k]);
        if (fields->len >= 3 &&
            g_strcmp0(g_ptr_array_index(fields, 0), "Name") != 0 &&
            !g_str_has_prefix(lines[i], "All snaps")) {
            g_ptr_array_add(result->packages,
                package_new(LUXUP_SOURCE_SNAP,
                            g_ptr_array_index(fields, 0), "",
                            g_ptr_array_index(fields, 1)));
        }
        g_ptr_array_unref(fields);
        g_strfreev(raw);
    }
    g_strfreev(lines);
    g_free(out);
}

LuxUpResult *luxup_check(gboolean refresh_apt_lists, GCancellable *cancellable)
{
    LuxUpResult *result = g_new0(LuxUpResult, 1);
    result->packages = g_ptr_array_new_with_free_func((GDestroyNotify) package_free);
    result->apt_available = executable_exists("/usr/bin/apt");
    if (result->apt_available) result->apt_refreshed_at = read_apt_refresh_time();
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        return result;
    }
    if (refresh_apt_lists && result->apt_available) {
        result->apt_refresh_error = refresh_apt(cancellable);
        if (result->apt_refresh_error) result->check_failed = TRUE;
        else if (!cancellable || !g_cancellable_is_cancelled(cancellable)) {
            result->apt_refreshed_at = g_get_real_time() / G_USEC_PER_SEC;
            if (!write_apt_refresh_time(result->apt_refreshed_at))
                g_warning("Could not persist the last successful APT refresh time");
        }
    }
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        return result;
    }
    if (result->apt_available) check_apt(result, cancellable);
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        return result;
    }
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    check_flatpak(result, seen, cancellable);
    g_hash_table_unref(seen);
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        return result;
    }
    check_snap(result, cancellable);
    return result;
}
