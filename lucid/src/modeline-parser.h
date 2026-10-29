/*
 * modelie-parser.h
 * Emacs, Kate and Vim-style modelines support for gedit.
 *
 * Copyright (C) 2005-2007 - Steve Frécinaux <code@istique.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MODELINE_PARSER_H
#define MODELINE_PARSER_H

#include <glib.h>
#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

void	modeline_parser_init		(const gchar *data_dir);
void	modeline_parser_shutdown	(void);
void	modeline_parser_apply_modeline	(GtkSourceView *view);

G_END_DECLS

#endif /* MODELINE_PARSER_H */
/* ex:set ts=8 noet: */
