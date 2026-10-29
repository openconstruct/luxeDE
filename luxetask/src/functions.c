/* $Id: functions.c 3940 2008-02-10 22:48:45Z nebulon $
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

/* modified by Hong Jen Yee <pcman.tw@gmail.com> and the LXDE team
 * on 2008-04-26 to be used in LXDE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#include "functions.h"

extern gint refresh_interval;

static system_status *sys_stat =NULL;
static gboolean refresh_in_flight;
static gint64 last_sample_at;

typedef struct {
    GArray *tasks;
    system_status status;
    gboolean memory_valid;
    gboolean cpu_valid;
    gdouble cpu_usage;
    gint64 captured_at;
    gint64 previous_sample_at;
} TaskSnapshot;

typedef struct {
    system_status previous_status;
    gint64 previous_sample_at;
    gboolean full_path;
} TaskWork;

static gboolean task_window_is_minimized(void)
{
    GtkNative *native = main_window ? gtk_widget_get_native(main_window) : NULL;
    GdkSurface *surface = native ? gtk_native_get_surface(native) : NULL;

    return surface != NULL && GDK_IS_TOPLEVEL(surface) &&
           (gdk_toplevel_get_state(GDK_TOPLEVEL(surface)) & GDK_TOPLEVEL_STATE_MINIMIZED) != 0;
}

gboolean task_is_visible(const struct task *task)
{
    return (show_user_tasks && task->uid == own_uid) ||
           (show_root_tasks && task->uid == 0) ||
           (show_other_tasks && task->uid != own_uid && task->uid != 0);
}

static void apply_task_snapshot(TaskSnapshot *snapshot)
{
    guint i, j;
    gdouble cpu_usage;
    guint num_cpus;
    guint64 memory_used;
    char tooltip[256];

    GArray *new_task_list = snapshot->tasks;
    gdouble elapsed = snapshot->previous_sample_at > 0 ?
        (snapshot->captured_at - snapshot->previous_sample_at) / 1000000.0 : 0;
    if (!sys_stat) sys_stat = g_new0(system_status, 1);
    *sys_stat = snapshot->status;
    num_cpus = MAX((guint64)1, sys_stat->cpu_count);
    long ticks = MAX(1, sysconf(_SC_CLK_TCK));

    /* gets the new task list */
    if (!new_task_list) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cpu_usage_progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cpu_usage_progress_bar),
                                  _("CPU usage: unknown (process list unavailable)"));
        if (!snapshot->memory_valid) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(mem_usage_progress_bar), 0.0);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(mem_usage_progress_bar), _("Memory: unknown"));
        }
        return; /* Keep the refresh timer alive and retain the prior rows. */
    }

    /* O(old+new) matching via pid -> new-list index (stored +1 so 0 means miss). */
    {
        GHashTable *new_by_pid = g_hash_table_new(g_direct_hash, g_direct_equal);
        for (j = 0; j < new_task_list->len; j++)
        {
            struct task *new_tmp = &g_array_index(new_task_list, struct task, j);
            new_tmp->checked = FALSE;
            g_hash_table_replace(new_by_pid, GINT_TO_POINTER((gint)new_tmp->pid),
                                 GINT_TO_POINTER(j + 1));
        }

        /* check if task is new and marks the task that its checked*/
        for(i = 0; i < task_array->len; i++)
        {
            struct task *tmp = &g_array_index(task_array, struct task, i);
            gpointer p = g_hash_table_lookup(new_by_pid, GINT_TO_POINTER((gint)tmp->pid));
            tmp->checked = FALSE;

            if(p != NULL)
            {
                struct task *new_tmp = &g_array_index(new_task_list, struct task, GPOINTER_TO_INT(p) - 1);

                if (tmp->start_time != new_tmp->start_time)
                    continue;
                new_tmp->time_percentage = elapsed > 0 && new_tmp->time >= tmp->time ?
                    CLAMP((new_tmp->time - tmp->time) * 100.0 / (ticks * elapsed * num_cpus), 0, 100) : 0;
                gboolean was_visible = task_is_visible(tmp);
                gboolean visible = task_is_visible(new_tmp);
                if(
                    strcmp(tmp->name, new_tmp->name) ||
                    strcmp(tmp->uname, new_tmp->uname) ||
                    tmp->uid != new_tmp->uid ||
                    tmp->ppid != new_tmp->ppid ||
                    tmp->state[0]!=new_tmp->state[0] ||
                    tmp->size != new_tmp->size ||
                    tmp->rss != new_tmp->rss ||
                    tmp->time_percentage != new_tmp->time_percentage ||
                    tmp->prio != new_tmp->prio
                 )
                {
                    *tmp = *new_tmp;
                    if (was_visible && !visible) remove_list_item(tmp->pid);
                    else if (!was_visible && visible) add_new_list_item(i);
                    else if (visible) refresh_list_item(i);
                }
                else
                {
                    tmp->time_percentage=new_tmp->time_percentage;
                }
                tmp->time = new_tmp->time;
                tmp->checked = TRUE;
                new_tmp->checked = TRUE;
            }
        }
        g_hash_table_destroy(new_by_pid);
    }

    /* check for unchecked old-tasks for deleting */
    i = 0;
    while( i < task_array->len)
    {

        struct task *tmp = &g_array_index(task_array, struct task, i);

        if(!tmp->checked)
        {
            remove_list_item(tmp->pid);
            g_array_remove_index_fast(task_array, i);
            tasks--;
        }
        else
            i++;

    }

    /* check for unchecked new tasks for inserting */
    for(i = 0; i < new_task_list->len; i++)
    {
        struct task *new_tmp = &g_array_index(new_task_list, struct task, i);

        if(!new_tmp->checked)
        {
            struct task *new_task = new_tmp;
            g_array_append_val(task_array, *new_task);
            if(task_is_visible(new_task))
                add_new_list_item(tasks);
            tasks++;
        }
    }

    /* update the CPU and memory progress bars */
    memory_used = sys_stat->mem_total - MIN(sys_stat->mem_total, sys_stat->mem_free);
    if ( show_cached_as_free )
    {
        if (sys_stat->mem_available_valid)
            memory_used = sys_stat->mem_total - MIN(sys_stat->mem_total, sys_stat->mem_available);
        else {
            memory_used -= MIN(memory_used, sys_stat->mem_cached);
            memory_used -= MIN(memory_used, sys_stat->mem_buffered);
        }
    }
    if (!snapshot->memory_valid || sys_stat->mem_total == 0)
    {
        /* /proc/meminfo unreadable: avoid 0/0 NaN fraction. */
        g_snprintf(tooltip, sizeof tooltip, "%s", _("Memory: unknown"));
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(mem_usage_progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(mem_usage_progress_bar), tooltip);
    }
    else
    {
        gdouble mem_frac = (gdouble)memory_used / (gdouble)sys_stat->mem_total;
        if (!(mem_frac >= 0.0 && mem_frac <= 1.0))
            mem_frac = 0.0;
        g_snprintf(tooltip, sizeof tooltip, _("Memory: %" G_GUINT64_FORMAT " MB of %" G_GUINT64_FORMAT " MB used"),
                   memory_used / 1024, sys_stat->mem_total / 1024);
        if (gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(mem_usage_progress_bar)) != mem_frac)
            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (mem_usage_progress_bar), mem_frac);
        if(strcmp(tooltip,gtk_progress_bar_get_text(GTK_PROGRESS_BAR(mem_usage_progress_bar))))
            gtk_progress_bar_set_text (GTK_PROGRESS_BAR (mem_usage_progress_bar), tooltip);
    }

    if (snapshot->cpu_valid) {
        cpu_usage = snapshot->cpu_usage;
        g_snprintf(tooltip, sizeof tooltip, _("CPU usage: %0.0f %%"), cpu_usage * 100.0);
    } else {
        cpu_usage = 0.0;
        g_snprintf(tooltip, sizeof tooltip, "%s", _("CPU usage: unknown"));
    }
    if (gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(cpu_usage_progress_bar)) != cpu_usage)
        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (cpu_usage_progress_bar), cpu_usage);
    if(strcmp(tooltip,gtk_progress_bar_get_text(GTK_PROGRESS_BAR(cpu_usage_progress_bar))))
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (cpu_usage_progress_bar), tooltip);
}

static void task_snapshot_free(TaskSnapshot *snapshot)
{
    if (!snapshot) return;
    if (snapshot->tasks) g_array_unref(snapshot->tasks);
    g_free(snapshot);
}

static void collect_task_snapshot(GTask *task, gpointer source_object,
                                  gpointer task_data, GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;
    TaskWork *work = task_data;
    TaskSnapshot *snapshot = g_new0(TaskSnapshot, 1);
    snapshot->status = work->previous_status;
    snapshot->previous_sample_at = work->previous_sample_at;
    snapshot->tasks = get_task_list_with_options(work->full_path);
    snapshot->memory_valid = get_system_status(&snapshot->status);
    snapshot->cpu_valid = get_cpu_usage(&snapshot->status, &snapshot->cpu_usage);
    snapshot->captured_at = g_get_monotonic_time();
    g_task_return_pointer(task, snapshot, NULL);
}

static void task_snapshot_ready(GObject *source_object, GAsyncResult *result,
                                gpointer user_data)
{
    (void)source_object;
    (void)user_data;
    GTask *task = G_TASK(result);
    GError *error = NULL;
    TaskSnapshot *snapshot = g_task_propagate_pointer(task, &error);
    refresh_in_flight = FALSE;
    if (error) {
        g_warning("Could not collect process snapshot: %s", error->message);
        g_clear_error(&error);
        return;
    }
    if (main_window && task_array && !task_window_is_minimized()) {
        if (snapshot->tasks)
            last_sample_at = snapshot->captured_at;
        apply_task_snapshot(snapshot);
    }
    task_snapshot_free(snapshot);
}

gboolean refresh_task_list(void)
{
    if (!main_window || task_window_is_minimized() || refresh_in_flight)
        return TRUE;
    if (!sys_stat) sys_stat = g_new0(system_status, 1);
    TaskWork *work = g_new0(TaskWork, 1);
    work->previous_status = *sys_stat;
    work->previous_sample_at = last_sample_at;
    work->full_path = show_full_path;
    GTask *task = g_task_new(NULL, NULL, task_snapshot_ready, NULL);
    g_task_set_task_data(task, work, g_free);
    refresh_in_flight = TRUE;
    g_task_run_in_thread(task, collect_task_snapshot);
    g_object_unref(task);
    return TRUE;
}

gboolean get_cpu_usage(system_status *sys_stat, gdouble *usage)
{
    guint64 current_jiffies;
    guint64 current_used;
    guint64 delta_jiffies;

    if (get_cpu_usage_from_proc(sys_stat) && sys_stat->cpu_old_jiffies > 0) {
            current_used =
                sys_stat->cpu_user +
                sys_stat->cpu_nice +
                sys_stat->cpu_system;
            current_jiffies =
                current_used +
                sys_stat->cpu_idle;
            if (current_jiffies <= sys_stat->cpu_old_jiffies || current_used < sys_stat->cpu_old_used)
                return FALSE;
            delta_jiffies =
                current_jiffies - sys_stat->cpu_old_jiffies;

            *usage = (gdouble)( current_used - sys_stat->cpu_old_used ) /
                     (gdouble)delta_jiffies ;
            return *usage >= 0.0 && *usage <= 1.0;
    }
    return FALSE;
}

gboolean refresh_task_list_cb(gpointer data)
{
    (void) data;
    return refresh_task_list();
}

static const struct {
    const char *key;
    gint *value;
    gint fallback, minimum, maximum;
} config_keys[] = {
    { "show_user_tasks", &show_user_tasks, TRUE, 0, 1 },
    { "show_root_tasks", &show_root_tasks, FALSE, 0, 1 },
    { "show_other_tasks", &show_other_tasks, FALSE, 0, 1 },
    { "show_full_path", &show_full_path, FALSE, 0, 1 },
    { "show_cached_as_free", &show_cached_as_free, TRUE, 0, 1 },
    { "full_view", &full_view, TRUE, 0, 1 },
    { "win_width", &win_width, 500, 300, 16384 },
    { "win_height", &win_height, 400, 200, 16384 },
    { "refresh_interval", &refresh_interval, 2, 1, 60 }
};

static GKeyFile *read_config(void)
{
    GKeyFile *file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(file, config_file,
        G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &error) &&
        !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        g_printerr("luxetask: preserving unreadable configuration: %s\n", error->message);
        g_key_file_unref(file);
        file = NULL;
    }
    g_clear_error(&error);
    return file;
}

static gint config_value(GKeyFile *file, guint index)
{
    if (!file) return config_keys[index].fallback;
    GError *error = NULL;
    gint value = g_key_file_get_integer(file, "General", config_keys[index].key, &error);
    if (error) value = config_keys[index].fallback;
    g_clear_error(&error);
    return CLAMP(value, config_keys[index].minimum, config_keys[index].maximum);
}

void load_config(void)
{
    GKeyFile *file = read_config();
    for (guint i = 0; i < G_N_ELEMENTS(config_keys); i++)
        *config_keys[i].value = config_value(file, i);
    if (file) g_key_file_unref(file);
}

void save_config(void)
{
    if (!config_file) return;
    GKeyFile *file = read_config();
    if (!file) return; /* Never replace malformed/unreadable user input. */
    if (main_window) {
        gint width = gtk_widget_get_width(main_window), height = gtk_widget_get_height(main_window);
        if (width > 0) win_width = width;
        if (height > 0) win_height = height;
    }
    gboolean changed = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(config_keys); i++) {
        gint value = CLAMP(*config_keys[i].value, config_keys[i].minimum, config_keys[i].maximum);
        if (value != config_value(file, i)) {
            g_key_file_set_integer(file, "General", config_keys[i].key, value);
            changed = TRUE;
        }
    }
    if (changed) {
        struct stat info;
        if (lstat(config_file, &info) == 0 && S_ISLNK(info.st_mode)) {
            g_printerr("luxetask: preserving symlinked configuration\n");
        } else {
            gchar *directory = g_path_get_dirname(config_file);
            g_mkdir_with_parents(directory, 0700);
            g_free(directory);
            gsize length;
            gchar *text = g_key_file_to_data(file, &length, NULL);
            GError *error = NULL;
            if (!g_file_set_contents_full(config_file, text, length,
                G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, &error)) {
                g_printerr("luxetask: configuration was not saved: %s\n", error->message);
                g_clear_error(&error);
            }
            g_free(text);
        }
    }
    g_key_file_unref(file);
}
