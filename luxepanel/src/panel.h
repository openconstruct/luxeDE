/*
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Lajos Kamocsay <lajos@panka.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2012 Michael Rawson <michaelrawson76@gmail.com>
 *               2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2014 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2018 Mamoru TASAKA <mtasaka@fedoraproject.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __PANEL_H__
#define __PANEL_H__ 1

#include <X11/Xlib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * LUXEPANEL_CHECK_VERSION
 *
 * A simple macro for plugins to check features.
 *
 * Since: 0.8.0
 */
#define LUXEPANEL_CHECK_VERSION(_a,_b,_c) \
    (0 > _a || \
    (0 == _a && 10 > _b) || \
    (0 == _a && 10 == _b && 0 >= _c))

#define LX_TYPE_PANEL                  (luxepanel_get_type())
#define LUXEPANEL(obj)                   (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                                        LX_TYPE_PANEL, LuxePanel))
#define LUXEPANEL_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST((klass), \
                                        LX_TYPE_PANEL, LuxePanelClass))
#define LX_IS_PANEL(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                        LX_TYPE_PANEL))

extern GType luxepanel_get_type          (void) G_GNUC_CONST;

/* A little trick to be compatible with some themes which rely on the
   PanelToplevel class, so we use LuxePanel as alias for PanelToplevel */
typedef struct _LuxePanel LuxePanel;
typedef struct _LuxePanel PanelToplevel;
typedef struct _LuxePanelClass PanelToplevelClass;

typedef struct _Panel Panel;

struct _LuxePanel
{
    GtkWindow window;
    Panel *priv;
};

/**
 * LuxePanelClass:
 * @parent_class: The parent class.
 * @icon_size_changed: callback for "icon-size-changed" signal, emitted when
 *              icons size for panel is changed in the configuration dialog.
 * @panel_font_changed: callback for "panel-font-changed" signal, emitted when
 *              custom font enabled, disabled or its metrics or color changed
 *              in the panel configuration dialog.
 * @layout_changed: callback for "layout-changed" signal, emitted after the
 *              panel has allocated its contents.  Widgets that keep foreign
 *              X windows (the tray) must re-read their position because GTK4
 *              does not re-run size_allocate on a position-only move.
 */
struct _LuxePanelClass
{
    GtkWindowClass parent_class;
    void (*icon_size_changed)(LuxePanel *panel);
    void (*panel_font_changed)(LuxePanel *panel);
    void (*layout_changed)(LuxePanel *panel);
};

/**
 * panel_apply_icon
 * @w: a window to apply
 *
 * Sets appropriate icon as the window icon for @w.
 */
extern void panel_apply_icon(GtkWindow *w);

/**
 * luxepanel_draw_label_text
 * @p: a panel instance
 * @label: a label widget
 * @text: (allow-none): text for the label
 * @bold: %TRUE if text should be bold
 * @custom_size_factor: scale factor for font size
 * @custom_color: %TRUE to use font color from panel settings
 *
 * Changes @label to contain @text with appropriate attributes using the
 * panel @p settings.
 */
extern void luxepanel_draw_label_text(LuxePanel * p, GtkWidget * label, const char * text,
                                    gboolean bold, float custom_size_factor,
                                    gboolean custom_color);

/**
 * luxepanel_draw_label_text_with_color
 * @p: a panel instance
 * @label: a label widget
 * @text: (allow-none): text for the label
 * @bold: %TRUE if text should be bold
 * @custom_size_factor: scale factor for font size
 * @color: GdkColor for drawing text
 *
 * Changes @label to contain @text with appropriate attributes using the
 * panel @p settings.
 */
extern void luxepanel_draw_label_text_with_color(LuxePanel * p, GtkWidget * label, const char * text,
                                    gboolean bold, float custom_size_factor,
                                    GdkRGBA *color);

/**
 * luxepanel_config_save
 * @p: a panel instance
 *
 * Immediately saves current configuration for panel @p.
 */
void luxepanel_config_save(LuxePanel *p); /* defined in configurator.c */

/* Accessors APIs for Panel* */
extern GtkOrientation panel_get_orientation(LuxePanel *panel);
extern gint panel_get_icon_size(LuxePanel *panel);
extern gint panel_get_height(LuxePanel *panel);
extern Window panel_get_xwindow(LuxePanel *panel);
extern gint panel_get_monitor(LuxePanel *panel);
extern GtkStyleContext *panel_get_defstyle(LuxePanel *panel);
extern GtkIconTheme *panel_get_icon_theme(LuxePanel *panel);
extern gboolean panel_is_at_bottom(LuxePanel *panel);
extern gboolean panel_is_dynamic(LuxePanel *panel);
extern GtkWidget *panel_box_new(LuxePanel *panel, gboolean homogeneous, gint spacing);
extern GtkWidget *panel_separator_new(LuxePanel *panel);

G_END_DECLS

#endif
