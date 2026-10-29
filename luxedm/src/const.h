/* SLiM - Simple Login Manager
 *  Copyright (C) 1997, 1998 Per Liden
 *  Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
 *  Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>
 *  Copyright (C) 2022-23 Rob Pearce <slim@flitspace.org.uk>
 *  Copyright (C) 2026 Jerry Howell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef _CONST_H_
#define _CONST_H_

#include <csignal>

#define DISPLAY	":0.0"

#define CONSOLE_STR	 "console"
#define HALT_STR		"halt"
#define REBOOT_STR	  "reboot"
#define EXIT_STR		"exit"
#define SUSPEND_STR	 "suspend"

#define HIDE		0
#define SHOW		1

#define OK_EXIT	 0
#define ERR_EXIT	1

/* variables replaced in login_cmd */
#define SESSION_VAR	 "%session"
#define THEME_VAR	   "%theme"

/* variables replaced in pre-session_cmd and post-session_cmd */
#define USER_VAR	   "%user"

/* max height/width for images: 4096 caps a 4K-side decode while still covering
 * packaged themes and 4K greeter backgrounds */
#define MAX_DIMENSION 4096

/* total byte budget for a single decoded image plane set (64 MiB keeps a
 * 4096x4096 RGB decode inside budget while rejecting ~400 MB-class inputs) */
#define MAX_IMAGE_BYTES (64 * 1024 * 1024)

/* Set by the greeter's async-signal-safe termination handler. */
extern volatile sig_atomic_t luxedm_signal_pending;

#endif /* _CONST_H_ */
