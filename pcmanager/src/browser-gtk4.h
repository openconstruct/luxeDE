/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PCMGR_BROWSER_GTK4_H
#define PCMGR_BROWSER_GTK4_H
#include <gtk/gtk.h>
void pcmgr_browser_set_application(GtkApplication *app);
void pcmgr_browser_setup(const char *profile, GCallback idle_callback);
void pcmgr_browser_open(const char * const *paths, const char *cwd, gboolean new_window);
guint pcmgr_browser_count(void);
void pcmgr_browser_preferences(void);
#endif
