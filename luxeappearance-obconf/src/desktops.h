/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   desktops.h for ObConf, the configuration tool for Openbox
   Copyright (c) 2026        Jerry Howell <jerr@luxe.desktop>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#ifndef obconf__desktops_h
#define obconf__desktops_h

#include <glib.h>

void desktops_setup_tab(void);
gboolean desktops_validate(void);
gboolean desktops_apply(void);
void desktops_cleanup(void);

#endif
