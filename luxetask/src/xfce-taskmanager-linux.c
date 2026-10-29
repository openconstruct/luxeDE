/* $Id: xfce-taskmanager-linux.c 3940 2008-02-10 22:48:45Z nebulon $
 *
 * Copyright (c) 2006 Johannes Zellner, <webmaster@nebulon.de>
 * Copyright (C) 2026 Jerry Howell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <poll.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include "xfce-taskmanager-linux.h"
/* #include <sys/sysinfo.h> */

/* NSS may block; callers resolve names only in the single collection worker.
 * Keep the cache bounded and refresh it periodically to reflect account edits. */
static GHashTable *uid_name_cache = NULL;
#define UID_NAME_CACHE_LIMIT 256
static gint64 uid_name_cache_updated_at;

static const gchar *cached_user_name(uid_t uid)
{
    gpointer key = GINT_TO_POINTER((gint)uid);
    const gchar *cached;
    struct passwd *passwdp;

    if (uid_name_cache == NULL)
        uid_name_cache = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                               NULL, g_free);
    gint64 now = g_get_monotonic_time();
    if (uid_name_cache_updated_at == 0 ||
        now - uid_name_cache_updated_at >= 60 * G_TIME_SPAN_SECOND) {
        g_hash_table_remove_all(uid_name_cache);
        uid_name_cache_updated_at = now;
    }
    cached = g_hash_table_lookup(uid_name_cache, key);
    if (cached != NULL)
        return cached;

    passwdp = getpwuid(uid);
    if (passwdp != NULL && passwdp->pw_name != NULL)
    {
        if (g_hash_table_size(uid_name_cache) >= UID_NAME_CACHE_LIMIT)
            g_hash_table_remove_all(uid_name_cache);
        g_hash_table_insert(uid_name_cache, key, g_strdup(passwdp->pw_name));
    }
    else {
        if (g_hash_table_size(uid_name_cache) >= UID_NAME_CACHE_LIMIT)
            g_hash_table_remove_all(uid_name_cache);
        g_hash_table_insert(uid_name_cache, key, g_strdup_printf("%u", (guint)uid));
    }
    return g_hash_table_lookup(uid_name_cache, key);
}

static gboolean parse_task_stat(const gchar *text, struct task *task)
{
    const gchar *begin = strchr(text, '('), *end = strrchr(text, ')');
    if (!begin || !end || end <= begin || end[1] != ' ') return FALSE;
    gchar **fields = g_strsplit(end + 2, " ", -1);
    guint64 user, system, bytes;
    gint64 parent, priority, resident;
    gboolean valid = g_strv_length(fields) >= 22 && strlen(fields[0]) == 1 &&
        g_ascii_string_to_signed(fields[1], 10, 0, G_MAXINT, &parent, NULL) &&
        g_ascii_string_to_unsigned(fields[11], 10, 0, G_MAXUINT64, &user, NULL) &&
        g_ascii_string_to_unsigned(fields[12], 10, 0, G_MAXUINT64 - user, &system, NULL) &&
        g_ascii_string_to_signed(fields[16], 10, -20, 19, &priority, NULL) &&
        g_ascii_string_to_unsigned(fields[19], 10, 0, G_MAXUINT64, &task->start_time, NULL) &&
        g_ascii_string_to_unsigned(fields[20], 10, 0, G_MAXUINT64, &bytes, NULL) &&
        g_ascii_string_to_signed(fields[21], 10, G_MININT64, G_MAXINT64, &resident, NULL) &&
        (guint64)MAX((gint64)0, resident) <= G_MAXUINT64 / MAX(1, page_size);
    if (valid) {
        gsize length = MIN((gsize)(end - begin - 1), sizeof(task->name) - 1);
        memcpy(task->name, begin + 1, length);
        task->name[length] = 0;
        task->state[0] = fields[0][0];
        task->state[1] = 0;
        task->ppid = parent;
        task->prio = priority;
        task->time = user + system;
        task->size = bytes / 1024;
        task->rss = (guint64)MAX((gint64)0, resident) * MAX(1, page_size);
    }
    g_strfreev(fields);
    return valid;
}

void get_task_details_with_options(pid_t pid, struct task *task,
                                  gboolean full_path, gboolean resolve_username)
{
    gchar path[64], stat_text[4096];
    struct stat info;
    memset(task, 0, sizeof(*task));
    task->pid = -1;
    g_snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    ssize_t length = read(fd, stat_text, sizeof(stat_text) - 1);
    gboolean owned = fstat(fd, &info) == 0;
    close(fd);
    if (length <= 0 || !owned) return;
    stat_text[length] = 0;
    if (!parse_task_stat(stat_text, task)) return;
    g_snprintf(path, sizeof(path), "/proc/%d/task", pid);
    struct stat effective;
    if (stat(path, &effective) == 0) info = effective;
    task->uid = info.st_uid;
    if (resolve_username)
        g_strlcpy(task->uname, cached_user_name(task->uid), sizeof(task->uname));
    else
        g_snprintf(task->uname, sizeof(task->uname), "%u", (guint)task->uid);
    if (full_path || strlen(task->name) >= 15) {
        g_snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            gchar command[sizeof(task->name)];
            length = read(fd, command, sizeof(command) - 1);
            close(fd);
            if (length > 0) {
                command[length] = 0;
                if (full_path) {
                    for (ssize_t i = 0; i < length; i++)
                        if (!command[i]) command[i] = ' ';
                    g_strlcpy(task->name, g_strchomp(command), sizeof(task->name));
                } else {
                    const gchar *base = strrchr(command, '/');
                    g_strlcpy(task->name, base ? base + 1 : command, sizeof(task->name));
                }
            }
        }
    }
    gchar *valid = g_utf8_make_valid(task->name, -1);
    g_strlcpy(task->name, valid, sizeof(task->name));
    g_free(valid);
    const gchar *invalid;
    if (!g_utf8_validate(task->name, -1, &invalid))
        task->name[invalid - task->name] = 0;
    task->pid = pid;
}

void get_task_details(pid_t pid, struct task *task)
{
    /* UI-side identity checks do not need NSS and must not block GTK. */
    get_task_details_with_options(pid, task, show_full_path, FALSE);
}

static int proc_filter(const struct dirent *d)
{
    int c=d->d_name[0];
    return c>='1' && c<='9';
}

GArray *get_task_list_with_options(gboolean full_path)
{
    GArray *task_list;
    struct dirent **namelist;
    int n;
    int count=0;


    n=scandir("/proc",&namelist,proc_filter,0);
    if(n<0) return NULL;
    task_list = g_array_new(FALSE, FALSE, sizeof (struct task));

    g_array_set_size(task_list,n);
    while(n--)
    {
        pid_t pid = atol(namelist[n]->d_name);
        struct task *task=&g_array_index(task_list, struct task, count);
	free(namelist[n]);
        get_task_details_with_options(pid, task, full_path, TRUE);
        if(task->pid > 0 && task->size > 0)	// don't show error or kenerl threads
        {
            count++;
        }
    }
    task_list->len=count;
    free(namelist);
    return task_list;
}

GArray *get_task_list(void)
{
    return get_task_list_with_options(show_full_path);
}

gboolean get_cpu_usage_from_proc(system_status *sys_stat)
{
    const gchar *file_name = "/proc/stat";
    FILE *file;

    if ( sys_stat->valid_proc_reading == TRUE ) {
        sys_stat->cpu_old_jiffies =
            sys_stat->cpu_user +
            sys_stat->cpu_nice +
            sys_stat->cpu_system+
            sys_stat->cpu_idle;
        sys_stat->cpu_old_used =
            sys_stat->cpu_user +
            sys_stat->cpu_nice +
            sys_stat->cpu_system;
    } else {
        sys_stat->cpu_old_jiffies = 0;
    }

    sys_stat->valid_proc_reading = FALSE;

    file = fopen (file_name, "rb");
    if(!file) return FALSE;
    guint64 wait = 0, irq = 0, softirq = 0, steal = 0;
    if ( fscanf (file, "cpu\t%"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT
                 " %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT,
                 &sys_stat->cpu_user,
                 &sys_stat->cpu_nice,
                 &sys_stat->cpu_system,
                 &sys_stat->cpu_idle, &wait, &irq, &softirq, &steal
                ) >= 4 )
    {
        sys_stat->cpu_idle += wait;
        sys_stat->cpu_system += irq + softirq + steal;
        sys_stat->valid_proc_reading = TRUE;
    }
    fclose( file );
    return sys_stat->valid_proc_reading;
}

static gboolean parse_mem_value(const char *text, guint64 *value)
{
    while (g_ascii_isspace(*text)) text++;
    if (!g_ascii_isdigit(*text)) return FALSE;
    errno = 0;
    char *end;
    guint64 parsed = g_ascii_strtoull(text, &end, 10);
    if (errno == ERANGE) return FALSE;
    while (g_ascii_isspace(*end)) end++;
    if (strncmp(end, "kB", 2)) return FALSE;
    end += 2;
    while (g_ascii_isspace(*end)) end++;
    if (*end) return FALSE;
    *value = parsed;
    return TRUE;
}

static gboolean parse_meminfo(FILE *file, system_status *sys_stat)
{
    gchar buffer[100];
    gboolean total_found = FALSE, free_found = FALSE;
    gboolean seen[6] = {FALSE};

    sys_stat->mem_valid = FALSE;

    /* Same approach as 'free' from procps-3.3.12: mem_cached
     * is Cached+SReclaimable (see proc/sysinfo.c line 707 */
    sys_stat->mem_cached = 0;
    sys_stat->mem_total = sys_stat->mem_free = sys_stat->mem_buffered = 0;
    sys_stat->mem_available = 0;
    sys_stat->mem_available_valid = FALSE;

    while (fgets (buffer, 100, file) != NULL)
    {
        if (!strchr(buffer, '\n') && !feof(file)) return FALSE;
        const char *value = NULL;
        enum { TOTAL, FREE, AVAILABLE, CACHE, RECLAIMABLE, BUFFER, OTHER } field = OTHER;
        if (g_str_has_prefix(buffer,"MemTotal:")) field=TOTAL, value=buffer+9;
        else if (g_str_has_prefix(buffer,"MemFree:")) field=FREE, value=buffer+8;
        else if (g_str_has_prefix(buffer,"MemAvailable:")) field=AVAILABLE, value=buffer+13;
        else if (g_str_has_prefix(buffer,"Cached:")) field=CACHE, value=buffer+7;
        else if (g_str_has_prefix(buffer,"SReclaimable:")) field=RECLAIMABLE, value=buffer+13;
        else if (g_str_has_prefix(buffer,"Buffers:")) field=BUFFER, value=buffer+8;
        if (field == OTHER) continue;
        if (seen[field]) return FALSE;
        seen[field] = TRUE;
        guint64 parsed;
        if (!parse_mem_value(value, &parsed)) return FALSE;
        if (field == TOTAL) sys_stat->mem_total=parsed, total_found=TRUE;
        else if (field == FREE) sys_stat->mem_free=parsed, free_found=TRUE;
        else if (field == AVAILABLE) sys_stat->mem_available=parsed, sys_stat->mem_available_valid=TRUE;
        else if (field == BUFFER) sys_stat->mem_buffered=parsed;
        else {
            if (parsed > G_MAXUINT64 - sys_stat->mem_cached) return FALSE;
            sys_stat->mem_cached += parsed;
        }
    }
    sys_stat->mem_valid = !ferror(file) && total_found && free_found &&
                          sys_stat->mem_total > 0;
    return sys_stat->mem_valid;
}

gboolean get_system_status (system_status *sys_stat)
{
    FILE *file = fopen ("/proc/meminfo", "r");
    system_status memory = {0};
    gboolean valid = file != NULL && parse_meminfo(file, &memory);
    if (file) fclose (file);
    sys_stat->mem_valid = valid;
    if (valid) {
        sys_stat->mem_total = memory.mem_total;
        sys_stat->mem_free = memory.mem_free;
        sys_stat->mem_cached = memory.mem_cached;
        sys_stat->mem_buffered = memory.mem_buffered;
        sys_stat->mem_available = memory.mem_available;
        sys_stat->mem_available_valid = memory.mem_available_valid;
    } else {
        sys_stat->mem_total = 0;
        sys_stat->mem_free = sys_stat->mem_cached = sys_stat->mem_buffered = 0;
        sys_stat->mem_available = 0;
        sys_stat->mem_available_valid = FALSE;
    }

    sys_stat->cpu_count=MAX(1, sysconf(_SC_NPROCESSORS_ONLN));
    return valid;
}

int task_open_handle(gint task_id, guint64 start_time)
{
    if (task_pid_is_protected(task_id)) {
        show_error(_("This task is this program, its ancestor, or a protected system process."));
        return -1;
    }
    int fd = syscall(SYS_pidfd_open, task_id, 0);
    if (fd < 0) {
        show_error(_("Could not open the selected task: %s"), g_strerror(errno));
        return -1;
    }
    struct task current;
    get_task_details(task_id, &current);
    struct pollfd check = { .fd = fd, .events = POLLIN };
    if (current.pid != task_id || current.start_time != start_time || poll(&check, 1, 0) != 0) {
        close(fd);
        show_error(_("The selected task has exited or changed. Refresh and select it again."));
        return -1;
    }
    return fd;
}

void send_signal_to_task(int handle, gint signal)
{
    if (handle >= 0 && signal != 0 && syscall(SYS_pidfd_send_signal, handle, signal, NULL, 0) != 0)
        show_error(_("Could not signal the selected task: %s"), g_strerror(errno));
}

/* True when `pid` is pid 1, this process, or one of this process's
 * ancestors (the session chain the panel and WM live in). The walk
 * starts from getpid() itself: a target is protected only if it IS us
 * or appears in OUR OWN ancestor chain. Walking from the target
 * instead would mark every userspace process protected, since every
 * process descends from PID 1. */
gboolean task_pid_is_protected(gint pid)
{
    if(pid <= 1)
        return TRUE;
    pid_t self = getpid();
    if(pid == self)
        return TRUE;
    pid_t cursor = self;
    for(gint round = 0; round < 64; round++)
    {
        gchar *stat_path = g_strdup_printf("/proc/%d/stat", (int)cursor);
        gchar *content = NULL;
        gchar *close_paren;
        gint ppid = 0;
        if(!g_file_get_contents(stat_path, &content, NULL, NULL))
        {
            g_free(content); g_free(stat_path);
            return FALSE;
        }
        g_free(stat_path);
        /* The comm field may contain spaces/parens; parse after the last ')'. */
        close_paren = strrchr(content, ')');
        if(close_paren && sscanf(close_paren + 2, "%*c %d", &ppid) != 1)
            ppid = 0;
        g_free(content);
        if(ppid <= 0)
            return FALSE;
        if(ppid == pid)
            return TRUE;
        if(ppid <= 1)
            return FALSE;
        cursor = ppid;
    }
    return FALSE;
}

void set_priority_to_task(gint task_id, guint64 start_time, gint prio)
{
    /* Linux has no pidfd-based setpriority(). Revalidate immediately before
     * the numeric-PID syscall; unlike signalling, this cannot be atomic. */
    if(task_id > 0 && prio >= -20 && prio <= 19)
    {
        if (task_pid_is_protected(task_id)) {
            show_error(_("This task is this program, its ancestor, or a protected system process."));
            return;
        }
        struct task current;
        get_task_details(task_id, &current);
        if (current.pid != task_id || current.start_time != start_time) {
            show_error(_("The selected task has exited or changed. Refresh and select it again."));
            return;
        }
        errno = 0;
        if(setpriority(PRIO_PROCESS, task_id, prio) != 0) {
            int saved_errno = errno;
            show_error(_("Couldn't set priority %d to the task with ID %d: %s"),
                       prio, task_id, g_strerror(saved_errno));
        }
    }
}
