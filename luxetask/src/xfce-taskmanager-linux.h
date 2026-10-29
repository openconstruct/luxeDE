/* $Id: xfce-taskmanager-linux.h 3940 2008-02-10 22:48:45Z nebulon $
 *
 * Copyright (c) 2006 Johannes Zellner, <webmaster@nebulon.de>
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

#ifndef XFCE4_TASKMANAGER_LINUX_H
#define XFCE4_TASKMANAGER_LINUX_H

#include <glib.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "types.h"
#include "utils.h"

#define SIGNAL_NO   0
#define SIGNAL_KILL SIGKILL
#define SIGNAL_TERM SIGTERM
#define SIGNAL_CONT SIGCONT
#define SIGNAL_STOP SIGSTOP

void get_task_details(gint pid,struct task *task);
void get_task_details_with_options(gint pid, struct task *task,
                                  gboolean full_path, gboolean resolve_username);
GArray *get_task_list(void);
GArray *get_task_list_with_options(gboolean full_path);
gboolean get_system_status(system_status *sys_stat);
gboolean get_cpu_usage_from_proc(system_status *sys_stat);
int task_open_handle(gint task_id, guint64 start_time);
void send_signal_to_task(int handle, gint signal);
void set_priority_to_task(gint task_id, guint64 start_time, gint prio);
gboolean task_pid_is_protected(gint pid);

#endif
