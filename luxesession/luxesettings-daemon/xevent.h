/*
 *      xevent.h
 *      
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
 *      
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#ifndef __XEVENT_H__
#define __XEVENT_H__

#include <glib.h>
#include <X11/Xlib.h>

G_BEGIN_DECLS

extern Display* dpy;

typedef enum{
	LXS_RELOAD,
	LXS_EXIT,
	LXS_LAST_CMD
}LXS_CMD;

gboolean xevent_init();
gboolean single_instance_check();
/* Claim the _LUXESESSION selection when it is unowned (unlike
 * single_instance_check this never exits/takes over; it just makes a
 * running settings daemon detectable by luxeappearance et al). */
void claim_luxesession_selection();
void xevent_finalize();

void send_internal_command( int cmd );

G_END_DECLS

#endif
