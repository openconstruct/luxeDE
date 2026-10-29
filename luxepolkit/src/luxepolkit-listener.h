/*
 *      lx-polkit-listener.h
 *
 *      Copyright 2010 PCMan <pcman.tw@gmail.com>
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


#ifndef __LX_POLKIT_LISTENER_H__
#define __LX_POLKIT_LISTENER_H__

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>

G_BEGIN_DECLS

#define LUXEPOLKIT_LISTENER_TYPE				(luxepolkit_listener_get_type())
#define LUXEPOLKIT_LISTENER(obj)				(G_TYPE_CHECK_INSTANCE_CAST((obj),\
			LUXEPOLKIT_LISTENER_TYPE, LuxePolkitListener))
#define LUXEPOLKIT_LISTENER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass),\
			LUXEPOLKIT_LISTENER_TYPE, LuxePolkitListenerClass))
#define IS_LUXEPOLKIT_LISTENER(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj),\
			LUXEPOLKIT_LISTENER_TYPE))
#define IS_LUXEPOLKIT_LISTENER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),\
			LUXEPOLKIT_LISTENER_TYPE))

typedef struct _LuxePolkitListener			LuxePolkitListener;
typedef struct _LuxePolkitListenerClass		LuxePolkitListenerClass;

struct _LuxePolkitListener
{
	PolkitAgentListener parent;
};

struct _LuxePolkitListenerClass
{
	PolkitAgentListenerClass parent_class;
};

GType luxepolkit_listener_get_type(void);
PolkitAgentListener* luxepolkit_listener_new(void);
void luxepolkit_listener_set_ready_fd(int fd);

G_END_DECLS

#endif /* __LX_POLKIT_LISTENER_H__ */
