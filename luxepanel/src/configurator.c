/*
 * Copyright (C) 2006-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 * Copyright (C) 2026 Jerry Howell
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008-2009 Fred Chien <fred@lxde.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2009 Ming-Ting Wei <mwei@lxde.org>
 *               2010 Lajos Kamocsay <lajos@panka.com>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2011-2014 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2012 Rafał Mużyło <galtgendo@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2015 Hanno Zulla <hhz@users.sf.net>
 *
 * This file is a part of LXPanel project.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define __LUXEPANEL_INTERNALS__

#include "private.h"
#include "luxe-spacing.h"
#include "misc.h"
#include "luxe-dialogs-gtk.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib/gi18n.h>
#include <libfm/fm.h>
#include <gio/gdesktopappinfo.h>

#include "dbg.h"

enum{
    COL_NAME,
    COL_EXPAND,
    COL_DATA,
    N_COLS
};

static void save_global_config();

static char* logout_cmd = NULL;

/* CheckButton is not a ToggleButton subclass in GTK4; grouped CheckButtons
 * replaced the (removed) GtkRadioButton in the .ui. */
#define TOGGLE_ACTIVE(w)        gtk_check_button_get_active(GTK_CHECK_BUTTON(w))
#define TOGGLE_SET_ACTIVE(w, n) gtk_check_button_set_active(GTK_CHECK_BUTTON(w), n)
#define SCREEN_WIDTH() \
    WidthOfScreen(DefaultScreenOfDisplay(gdk_x11_display_get_xdisplay(gdk_display_get_default())))
#define SCREEN_HEIGHT() \
    HeightOfScreen(DefaultScreenOfDisplay(gdk_x11_display_get_xdisplay(gdk_display_get_default())))

/* container children listing is gone in GTK4; walk the widget tree */
static GList* panel_box_children_list(GtkWidget* box)
{
    GList* l = NULL;
    GtkWidget* cw;
    if (!box)
        return NULL;
    for (cw = gtk_widget_get_first_child(box); cw; cw = gtk_widget_get_next_sibling(cw))
        l = g_list_append(l, cw);
    return l;
}

static GtkWidget* panel_box_nth_child(GtkWidget* box, int n)
{
    GtkWidget* cw;
    int i;
    for (i = 0, cw = gtk_widget_get_first_child(box); cw && i < n; ++i, cw = gtk_widget_get_next_sibling(cw));
    return cw;
}

/* macros to update config */
#define UPDATE_GLOBAL_INT(panel,name,val) do { \
    config_setting_t *_s = config_setting_add(config_setting_get_elem(config_setting_get_member(config_root_setting(panel->config),""),\
                                                                      0),\
                                              name,PANEL_CONF_TYPE_INT);\
    if (_s) config_setting_set_int(_s,val); } while(0)

#define UPDATE_GLOBAL_STRING(panel,name,val) do { \
    config_setting_t *_s = config_setting_add(config_setting_get_elem(config_setting_get_member(config_root_setting(panel->config),""),\
                                                                      0),\
                                              name,PANEL_CONF_TYPE_STRING);\
    if (_s) config_setting_set_string(_s,val); } while(0)

#define UPDATE_GLOBAL_COLOR(panel,name,val) do { \
    config_setting_t *_s = config_setting_add(config_setting_get_elem(config_setting_get_member(config_root_setting(panel->config),""),\
                                                                      0),\
                                              name,PANEL_CONF_TYPE_STRING);\
    if (_s) { \
        char _c[8];\
        snprintf(_c, sizeof(_c), "#%06x",val);\
        config_setting_set_string(_s,_c); } } while(0)

/* GtkColotButton expects a number between 0 and 65535, but p->alpha has range
 * 0 to 255, and (2^(2n) - 1) / (2^n - 1) = 2^n + 1 = 257, with n = 8. */
static guint16 const alpha_scale_factor = 257;

void panel_config_save(Panel *p);

static void update_opt_menu(GtkWidget *w, int ind);
static void update_toggle_button(GtkWidget *w, gboolean n);
static void modify_plugin( GtkTreeView* view );
static void on_entry_focus_leave( GtkEventControllerFocus* fc, gpointer user_data );
static void on_entry_focus_leave_old( GtkEventControllerFocus* fc, gpointer user_data );
static void attach_entry_focus_out(GtkWidget* entry, GCallback cb, gpointer user_data);
static void on_fm_combo_changed( GtkComboBox* fm, gpointer user_data );
static gboolean _on_entry_focus_out_do_work(GtkWidget* edit, gpointer user_data);

static void fm_combo_apply(GtkWidget* pref_dialog)
{
    GtkComboBox* fm = g_object_get_data(G_OBJECT(pref_dialog), "fm-combo");
    if( fm && g_object_get_data(G_OBJECT(fm), "is-changed") )
    {
        const char* id = gtk_combo_box_get_active_id(fm);
        if( id )
        {
            GDesktopAppInfo* app = g_desktop_app_info_new(id);
            if( app )
            {
                g_app_info_set_as_default_for_type(G_APP_INFO(app), "inode/directory", NULL);
                g_object_unref(app);
            }
        }
    }
}

static void pref_window_close(GtkWindow* win, Panel* panel)
{
    fm_combo_apply(GTK_WIDGET(win));
    panel_config_save( panel );
    gtk_window_destroy( win );
}

static gboolean on_pref_close_request(GtkWidget* w, gpointer panel)
{
    pref_window_close(GTK_WINDOW(w), panel);
    return TRUE;
}

static void on_pref_close_clicked(GtkWidget* btn, gpointer panel)
{
    pref_window_close(GTK_WINDOW(gtk_widget_get_root(btn)), panel);
}

static gboolean edge_selector(Panel* p, int edge)
{
    return (p->edge == edge);
}

/* If there is a panel on this edge and it is not the panel being configured, set the edge unavailable. */
gboolean panel_edge_available(Panel* p, int edge, gint monitor)
{
    GSList* l;
    for (l = all_panels; l != NULL; l = l->next)
    {
        LuxePanel* pl = (LuxePanel*) l->data;
        if ((pl->priv != p) && (pl->priv->edge == edge) &&
            (pl->priv->monitor < 0 || monitor < 0 || pl->priv->monitor == monitor))
            return FALSE;
    }
    return TRUE;
}

static void update_strut_control_button(LuxePanel *panel)
{
    Panel *p = panel->priv;
    gboolean active = _panel_edge_can_strut(panel, p->edge, p->monitor, NULL);
    gboolean old_active = !!gtk_widget_get_sensitive(p->strut_control);

    if (active == old_active)
        return;
    gtk_widget_set_sensitive(p->strut_control, active);
    if (active)
        gtk_widget_set_tooltip_text(p->strut_control, NULL);
    else
        gtk_widget_set_tooltip_text(p->strut_control,
                                    _("Space reservation is not available for"
                                      " this panel because there is another"
                                      " monitor beyond this edge and reservation"
                                      " would cover it if enabled."));
    _panel_set_wm_strut(panel);
}

static void set_edge(LuxePanel* panel, int edge)
{
    Panel *p = panel->priv;

    p->edge = edge;
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    _panel_set_panel_configuration_changed(panel);
    UPDATE_GLOBAL_STRING(p, "edge", num2str(edge_pair, edge, "none"));
    update_strut_control_button(panel);
}

static void edge_bottom_toggle(GtkWidget *widget, LuxePanel *p)
{
    if (TOGGLE_ACTIVE(widget))
        set_edge(p, EDGE_BOTTOM);
}

static void edge_top_toggle(GtkWidget *widget, LuxePanel *p)
{
    if (TOGGLE_ACTIVE(widget))
        set_edge(p, EDGE_TOP);
}

static void edge_left_toggle(GtkWidget *widget, LuxePanel *p)
{
    if (TOGGLE_ACTIVE(widget))
        set_edge(p, EDGE_LEFT);
}

static void edge_right_toggle(GtkWidget *widget, LuxePanel *p)
{
    if (TOGGLE_ACTIVE(widget))
        set_edge(p, EDGE_RIGHT);
}

/* only for old UI file, safe fallback */
static void set_monitor(GtkSpinButton *widget, LuxePanel *panel)
{
    Panel *p = panel->priv;

    p->monitor = gtk_spin_button_get_value_as_int(widget) - 1;
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    _panel_set_panel_configuration_changed(panel);
    UPDATE_GLOBAL_INT(p, "monitor", p->monitor);
}

static void update_mon_sensitivity(GtkCellLayout *layout, GtkCellRenderer *cell,
                                   GtkTreeModel *model, GtkTreeIter *iter,
                                   gpointer user_data)
{
    LuxePanel *panel = user_data;
    Panel *p = panel->priv;
    GtkTreePath *path;
    gint *indices;

    /* set it sensitive if edge is available */
    path = gtk_tree_model_get_path(model, iter);
    indices = gtk_tree_path_get_indices(path);
    g_object_set(cell, "sensitive", (panel_edge_available(p, p->edge,
                                                          indices[0] - 1)), NULL);
    gtk_tree_path_free(path);
}

static void update_edges_buttons(Panel *p)
{
    gtk_widget_set_sensitive(p->edge_bottom_button,
                             panel_edge_available(p, EDGE_BOTTOM, p->monitor));
    gtk_widget_set_sensitive(p->edge_top_button,
                             panel_edge_available(p, EDGE_TOP, p->monitor));
    gtk_widget_set_sensitive(p->edge_left_button,
                             panel_edge_available(p, EDGE_LEFT, p->monitor));
    gtk_widget_set_sensitive(p->edge_right_button,
                             panel_edge_available(p, EDGE_RIGHT, p->monitor));
}

static void set_monitor_cb(GtkComboBox *cb, LuxePanel *panel)
{
    Panel *p = panel->priv;

    /* change monitor */
    p->monitor = gtk_combo_box_get_active(cb) - 1;
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    _panel_set_panel_configuration_changed(panel);
    UPDATE_GLOBAL_INT(p, "monitor", p->monitor);
    /* update edge and strut sensitivities */
    update_edges_buttons(p);
    update_strut_control_button(panel);
}

static void set_alignment(LuxePanel* panel, int align)
{
    Panel *p = panel->priv;

    if (p->margin_control)
        gtk_widget_set_sensitive(p->margin_control, (align != ALIGN_CENTER));
    p->align = align;
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_STRING(p, "align", num2str(allign_pair, align, "none"));
}

static void align_left_toggle(GtkWidget *widget, LuxePanel *p)
{
    if (TOGGLE_ACTIVE(widget))
        set_alignment(p, ALIGN_LEFT);
}

static void align_center_toggle(GtkWidget *widget, LuxePanel *p)
{
    if (TOGGLE_ACTIVE(widget))
        set_alignment(p, ALIGN_CENTER);
}

static void align_right_toggle(GtkWidget *widget, LuxePanel *p)
{
    if (TOGGLE_ACTIVE(widget))
        set_alignment(p, ALIGN_RIGHT);
}

static void
set_margin(GtkSpinButton* spin, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->margin = (int)gtk_spin_button_get_value(spin);
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_INT(p, "margin", p->margin);
}

static void
set_width(GtkSpinButton* spin, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->width = (int)gtk_spin_button_get_value(spin);
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_INT(p, "width", p->width);
}

static void
set_height(GtkSpinButton* spin, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->height = (int)gtk_spin_button_get_value(spin);
    _panel_emit_icon_size_changed(panel);
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_INT(p, "height", p->height);
}

static void set_width_type( GtkWidget *item, LuxePanel* panel )
{
    GtkWidget* spin;
    Panel *p = panel->priv;
    int widthtype;
    gboolean t;

    widthtype = gtk_combo_box_get_active(GTK_COMBO_BOX(item)) + 1;
    if (p->widthtype == widthtype) /* not changed */
        return;

    p->widthtype = widthtype;

    spin = (GtkWidget*)g_object_get_data(G_OBJECT(item), "width_spin" );
    t = (widthtype != WIDTH_REQUEST);
    gtk_widget_set_sensitive( spin, t );
    switch (widthtype)
    {
    case WIDTH_PERCENT:
        gtk_spin_button_set_range( GTK_SPIN_BUTTON(spin), 0, 100 );
        gtk_spin_button_set_value( GTK_SPIN_BUTTON(spin), 100 );
        break;
    case WIDTH_PIXEL:
        if ((p->edge == EDGE_TOP) || (p->edge == EDGE_BOTTOM))
        {
            gtk_spin_button_set_range( GTK_SPIN_BUTTON(spin), 0, SCREEN_WIDTH() );
            gtk_spin_button_set_value( GTK_SPIN_BUTTON(spin), SCREEN_WIDTH() );
        }
        else
        {
            gtk_spin_button_set_range( GTK_SPIN_BUTTON(spin), 0, SCREEN_HEIGHT() );
            gtk_spin_button_set_value( GTK_SPIN_BUTTON(spin), SCREEN_HEIGHT() );
        }
        break;
    case WIDTH_REQUEST:
        break;
    default: ;
    }

    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_STRING(p, "widthtype", num2str(width_pair, widthtype, "none"));
}

/* FIXME: heighttype and spacing and RoundCorners */

static void transparency_toggle( GtkWidget *b, Panel* p)
{
    GtkWidget* tr = (GtkWidget*)g_object_get_data(G_OBJECT(b), "tint_clr");
    gboolean t;

    ENTER;

    t = TOGGLE_ACTIVE(b);
    gtk_widget_set_sensitive(tr, t);
/*
    gtk_widget_set_sensitive(tr_colorl, t);
    gtk_widget_set_sensitive(tr_colorb, t);
*/
    /* Update background immediately. */
    if (t&&!p->transparent) {
        p->transparent = 1;
        p->background = 0;
        panel_update_background( p );
        UPDATE_GLOBAL_INT(p, "transparent", p->transparent);
        UPDATE_GLOBAL_INT(p, "background", p->background);
    }
    RET();
}

/* img_file is a plain button (GtkFileChooserButton is gone); the chosen
 * path is kept in "chosen-file" qdata and shown as the button label. */
static void background_file_helper(Panel * p, GtkWidget * toggle, const char * file)
{
    if (file != NULL && *file != '\0')
    {
        g_free(p->background_file);
        p->background_file = g_strdup(file);
        UPDATE_GLOBAL_STRING(p, "backgroundfile", p->background_file);
    }

    if (TOGGLE_ACTIVE(toggle))
    {
        if ( ! p->background)
        {
            p->transparent = FALSE;
            p->background = TRUE;
            UPDATE_GLOBAL_INT(p, "transparent", p->transparent);
            UPDATE_GLOBAL_INT(p, "background", p->background);
        }
    }
    panel_update_background(p);
}

static void background_toggle( GtkWidget *b, Panel* p)
{
    GtkWidget * fc = (GtkWidget*) g_object_get_data(G_OBJECT(b), "img_file");
    gtk_widget_set_sensitive(fc, TOGGLE_ACTIVE(b));
    background_file_helper(p, b, (const char*)g_object_get_data(G_OBJECT(fc), "chosen-file"));
}

static void background_set_file(GtkWidget* fc, const char* file)
{
    char* base;
    g_object_set_data_full(G_OBJECT(fc), "chosen-file", g_strdup(file), g_free);
    base = file ? g_path_get_basename(file) : NULL;
    gtk_button_set_label(GTK_BUTTON(fc), base ? base : _("Select an image file"));
    g_free(base);
}

static void on_bg_file_response(GtkNativeDialog* nat, int resp, Panel* p)
{
    GtkWidget* fc = GTK_WIDGET(g_object_get_data(G_OBJECT(nat), "fc-btn"));
    if( resp == GTK_RESPONSE_ACCEPT )
    {
        GFile* f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(nat));
        char* file = f ? g_file_get_path(f) : NULL;
        background_set_file(fc, file);
        background_file_helper(p, g_object_get_data(G_OBJECT(fc), "bg-toggle"), file);
        g_free(file);
        g_clear_object(&f);
    }
    gtk_native_dialog_destroy(nat);
}

static void on_img_file_clicked(GtkButton* btn, Panel* p)
{
    GtkWidget* dlg = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "pref-win"));
    GtkFileChooserNative* fc = gtk_file_chooser_native_new(_("Select an image file"),
                                GTK_WINDOW(dlg), GTK_FILE_CHOOSER_ACTION_OPEN,
                                _("OK"), _("Cancel"));
    const char* cur = (const char*)g_object_get_data(G_OBJECT(btn), "chosen-file");
    g_object_set_data(G_OBJECT(fc), "fc-btn", btn);
    if( cur && *cur )
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(fc), g_file_new_for_path(cur), NULL);
    g_signal_connect(fc, "response", G_CALLBACK(on_bg_file_response), p);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(fc));
}

static void
background_disable_toggle( GtkWidget *b, Panel* p )
{
    ENTER;
    if (TOGGLE_ACTIVE(b)) {
        if (p->background!=0||p->transparent!=0) {
            p->background = 0;
            p->transparent = 0;
            /* Update background immediately. */
            panel_update_background( p );
            UPDATE_GLOBAL_INT(p, "transparent", p->transparent);
            UPDATE_GLOBAL_INT(p, "background", p->background);
        }
    }

    RET();
}

static void
on_font_color_set(GtkColorButton* clr, LuxePanel* panel)
{
    Panel *p = panel->priv;

    gtk_color_chooser_get_rgba( GTK_COLOR_CHOOSER(clr), &p->gfontcolor );
    panel_set_panel_configuration_changed(p);
    p->fontcolor = gcolor2rgb24(&p->gfontcolor);
    UPDATE_GLOBAL_COLOR(p, "fontcolor", p->fontcolor);
    _panel_emit_font_changed(panel);
}

static void
on_tint_color_set( GtkColorButton* clr,  Panel* p )
{
    gtk_color_chooser_get_rgba( GTK_COLOR_CHOOSER(clr), &p->gtintcolor );
    p->alpha = (int)((p->gtintcolor.alpha * 255.0) + 0.5);
    panel_update_background( p );
    UPDATE_GLOBAL_COLOR(p, "tintcolor", p->tintcolor);
    UPDATE_GLOBAL_INT(p, "alpha", p->alpha);
}

static void
on_use_font_color_toggled(GtkWidget* btn, LuxePanel* panel)
{
    GtkWidget* clr = (GtkWidget*)g_object_get_data( G_OBJECT(btn), "clr" );
    Panel *p = panel->priv;

    gtk_widget_set_sensitive( clr, TOGGLE_ACTIVE(btn) );
    p->usefontcolor = TOGGLE_ACTIVE(btn);
    panel_set_panel_configuration_changed(p);
    UPDATE_GLOBAL_INT(p, "usefontcolor", p->usefontcolor);
    _panel_emit_font_changed(panel);
}

static void
on_font_size_set(GtkSpinButton* spin, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->fontsize = (int)gtk_spin_button_get_value(spin);
    panel_set_panel_configuration_changed(p);
    UPDATE_GLOBAL_INT(p, "fontsize", p->fontsize);
    _panel_emit_font_changed(panel);
}

static void
on_use_font_size_toggled(GtkWidget* btn, LuxePanel* panel)
{
    GtkWidget* clr = (GtkWidget*)g_object_get_data( G_OBJECT(btn), "clr" );
    Panel *p = panel->priv;

    gtk_widget_set_sensitive( clr, TOGGLE_ACTIVE(btn) );
    p->usefontsize = TOGGLE_ACTIVE(btn);
    panel_set_panel_configuration_changed(p);
    UPDATE_GLOBAL_INT(p, "usefontsize", p->usefontsize);
    _panel_emit_font_changed(panel);
}

static void
set_dock_type(GtkWidget* toggle, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->setdocktype = TOGGLE_ACTIVE(toggle) ? 1 : 0;
    panel_set_dock_type( p );
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_INT(p, "setdocktype", p->setdocktype);
}

static void
set_strut(GtkWidget* toggle, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->setstrut = TOGGLE_ACTIVE(toggle) ? 1 : 0;
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    _panel_set_wm_strut(panel);
    UPDATE_GLOBAL_INT(p, "setpartialstrut", p->setstrut);
}

static void
set_autohide(GtkWidget* toggle, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->autohide = TOGGLE_ACTIVE(toggle) ? 1 : 0;
    gtk_widget_show(GTK_WIDGET(panel));
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_INT(p, "autohide", p->autohide);
    update_strut_control_button(panel);
}

static void
set_height_when_minimized(GtkSpinButton* spin, LuxePanel* panel)
{
    Panel *p = panel->priv;

    p->height_when_hidden = (int)gtk_spin_button_get_value(spin);
    gtk_widget_show(GTK_WIDGET(panel));
    gtk_widget_queue_resize(GTK_WIDGET(panel));
    UPDATE_GLOBAL_INT(p, "heightwhenhidden", p->height_when_hidden);
    update_strut_control_button(panel);
}

static void
set_icon_size(GtkSpinButton *spin, LuxePanel *panel)
{
    Panel *p = panel->priv;

    p->icon_size = (int)gtk_spin_button_get_value(spin);
    panel_set_panel_configuration_changed(p);
    _panel_emit_icon_size_changed(panel);
    UPDATE_GLOBAL_INT(p, "iconsize", p->icon_size);
}

static void
on_sel_plugin_changed( GtkTreeSelection* tree_sel, GtkWidget* label )
{
    GtkTreeIter it;
    GtkTreeModel* model;
    GtkWidget* pl;

    if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
    {
        GtkTreeView* view = gtk_tree_selection_get_tree_view( tree_sel );
        GtkWidget *edit_btn = GTK_WIDGET(g_object_get_data( G_OBJECT(view), "edit_btn" ));
        const LuxePanelPluginInit *init;
        gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
        init = PLUGIN_CLASS(pl);
        gtk_label_set_text(GTK_LABEL(label),
                           g_dgettext(init->gettext_package, init->description));
        gtk_widget_set_sensitive( edit_btn, init->config != NULL );
    }
}

static void
on_plugin_expand_toggled(GtkCellRendererToggle* render, char* path, GtkTreeView* view)
{
    GtkTreeModel* model;
    GtkTreeIter it;
    GtkTreePath* tp = gtk_tree_path_new_from_string( path );
    model = gtk_tree_view_get_model( view );
    if( gtk_tree_model_get_iter( model, &it, tp ) )
    {
        GtkWidget* pl;
        gboolean expand;
        const LuxePanelPluginInit *init;
        LuxePanel *panel;

        gtk_tree_model_get( model, &it, COL_DATA, &pl, COL_EXPAND, &expand, -1 );
        init = PLUGIN_CLASS(pl);
        panel = PLUGIN_PANEL(pl);

        if (init->expand_available)
        {
            config_setting_t *s = g_object_get_qdata(G_OBJECT(pl), luxepanel_plugin_qconf);
            /* Only honor "stretch" if allowed by the plugin. */
            expand = ! expand;
            gtk_list_store_set( GTK_LIST_STORE(model), &it, COL_EXPAND, expand, -1 );

            /* Box child packing is gone; expand maps to h/vexpand. */
            if (panel_get_orientation(panel) == GTK_ORIENTATION_HORIZONTAL)
                gtk_widget_set_hexpand(pl, expand);
            else
                gtk_widget_set_vexpand(pl, expand);
            config_group_set_int(s, "expand", expand ? 1 : 0);
        }
    }
    gtk_tree_path_free( tp );
}

static void on_stretch_render(GtkTreeViewColumn * column, GtkCellRenderer * renderer, GtkTreeModel * model, GtkTreeIter * iter, gpointer data)
{
    /* Set the control visible depending on whether stretch is available for the plugin.
     * The g_object_set method is touchy about its parameter, so we can't pass the boolean directly. */
    GtkWidget * pl;
    gtk_tree_model_get(model, iter, COL_DATA, &pl, -1);
    g_object_set(renderer,
        "visible", ((PLUGIN_CLASS(pl)->expand_available) ? TRUE : FALSE),
        NULL);
}

static void init_plugin_list( LuxePanel* p, GtkTreeView* view, GtkWidget* label )
{
    GtkListStore* list;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeSelection* tree_sel;
    GList *plugins, *l;
    GtkTreeIter it;

    g_object_set_data( G_OBJECT(view), "panel", p );

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
            _("Currently loaded plugins"),
            render, "text", COL_NAME, NULL );
    gtk_tree_view_column_set_expand( col, TRUE );
    gtk_tree_view_append_column( view, col );

    render = gtk_cell_renderer_toggle_new();
    g_object_set( render, "activatable", TRUE, NULL );
    g_signal_connect( render, "toggled", G_CALLBACK( on_plugin_expand_toggled ), view );
    col = gtk_tree_view_column_new_with_attributes(
            _("Stretch"),
            render, "active", COL_EXPAND, NULL );
    gtk_tree_view_column_set_expand( col, FALSE );
    gtk_tree_view_column_set_cell_data_func(col, render, on_stretch_render, NULL, NULL);
    gtk_tree_view_append_column( view, col );

    list = gtk_list_store_new( N_COLS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER );
    plugins = panel_box_children_list(p->priv->box);
    for( l = plugins; l; l = l->next )
    {
        GtkTreeIter it;
        gboolean expand = FALSE;
        gint i;
        GtkWidget *w = (GtkWidget*)l->data;
        config_setting_t *conf = g_object_get_qdata(G_OBJECT(w), luxepanel_plugin_qconf);
        if (conf && config_setting_lookup_int(conf, "expand", &i))
            expand = (i != 0);
        gtk_list_store_append( list, &it );
        gtk_list_store_set( list, &it,
                            COL_NAME,
                            g_dgettext(PLUGIN_CLASS(w)->gettext_package, PLUGIN_CLASS(w)->name),
                            COL_EXPAND, expand,
                            COL_DATA, w,
                            -1);
    }
    g_list_free(plugins);
    gtk_tree_view_set_model( view, GTK_TREE_MODEL( list ) );
    g_object_unref(list);
    g_signal_connect( view, "row-activated",
                      G_CALLBACK(modify_plugin), NULL );
    tree_sel = gtk_tree_view_get_selection( view );
    gtk_tree_selection_set_mode( tree_sel, GTK_SELECTION_BROWSE );
    g_signal_connect( tree_sel, "changed",
                      G_CALLBACK(on_sel_plugin_changed), label);
    if( gtk_tree_model_get_iter_first( GTK_TREE_MODEL(list), &it ) )
        gtk_tree_selection_select_iter( tree_sel, &it );
}

static void on_add_plugin_row_activated( GtkTreeView *tree_view,
                                         GtkTreePath *path,
                                         GtkTreeViewColumn *col,
                                         gpointer user_data)
{
    GtkWidget *dlg;

    dlg = (GtkWidget *) user_data;

    (void) tree_view;
    (void) path;
    (void) col;

    /* Emitting the "response" signal ourselves. */
    gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
}

static void on_add_plugin_response( GtkDialog* dlg,
                                    int response,
                                    GtkTreeView* _view )
{
    LuxePanel* p = (LuxePanel*) g_object_get_data( G_OBJECT(_view), "panel" );
    if( response == GTK_RESPONSE_OK )
    {
        GtkTreeView* view;
        GtkTreeSelection* tree_sel;
        GtkTreeIter it;
        GtkTreeModel* model;

        view = (GtkTreeView*)g_object_get_data( G_OBJECT(dlg), "avail-plugins" );
        tree_sel = gtk_tree_view_get_selection( view );
        if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        {
            char* type = NULL;
            GtkWidget *pl;
            config_setting_t *cfg;

            cfg = config_group_add_subgroup(config_root_setting(p->priv->config),
                                            "Plugin");
            gtk_tree_model_get( model, &it, 1, &type, -1 );
            config_group_set_string(cfg, "type", type);
            if ((pl = luxepanel_add_plugin(p, type, cfg, -1)))
            {
                GtkTreePath* tree_path;
                gboolean expand;

                panel_config_save(p->priv);

                //plugin_widget_set_background(pl, p);
                expand = FALSE;
                {
                    config_setting_t *conf = g_object_get_qdata(G_OBJECT(pl), luxepanel_plugin_qconf);
                    gint iv;
                    if (conf && config_setting_lookup_int(conf, "expand", &iv))
                        expand = (iv != 0);
                }
                model = gtk_tree_view_get_model( _view );
                gtk_list_store_append( GTK_LIST_STORE(model), &it );
                gtk_list_store_set( GTK_LIST_STORE(model), &it,
                                    COL_NAME,
                                    g_dgettext(PLUGIN_CLASS(pl)->gettext_package, PLUGIN_CLASS(pl)->name),
                                    COL_EXPAND, expand,
                                    COL_DATA, pl, -1 );
                tree_sel = gtk_tree_view_get_selection( _view );
                gtk_tree_selection_select_iter( tree_sel, &it );
                if ((tree_path = gtk_tree_model_get_path(model, &it)) != NULL)
                {
                    gtk_tree_view_scroll_to_cell( _view, tree_path, NULL, FALSE, 0, 0 );
                    gtk_tree_path_free( tree_path );
                }
            }
            else /* free unused setting */
                config_setting_destroy(cfg);
            g_free( type );
        }
    }
    gtk_window_destroy( GTK_WINDOW(dlg) );
}

static gint sort_by_name(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
    char *str_a, *str_b;
    gint res;

    gtk_tree_model_get(model, a, 0, &str_a, -1);
    gtk_tree_model_get(model, b, 0, &str_b, -1);
    res = g_utf8_collate(str_a, str_b);
    g_free(str_a);
    g_free(str_b);
    return res;
}

static void on_add_plugin( GtkButton* btn, GtkTreeView* _view )
{
    GtkWidget* dlg, *parent_win, *scroll;
    GHashTable *classes;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeView* view;
    GtkListStore* list;
    GtkTreeSelection* tree_sel;
    GHashTableIter iter;
    gpointer key, val;


    classes = luxepanel_get_all_types();

    parent_win = (GtkWidget*)gtk_widget_get_root( GTK_WIDGET(_view) );
    dlg = gtk_dialog_new( );
    gtk_window_set_title( GTK_WINDOW(dlg), _("Add plugin to panel") );
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(parent_win));
    gtk_dialog_add_button( GTK_DIALOG(dlg), _("Cancel"), GTK_RESPONSE_CANCEL );
    gtk_dialog_add_button( GTK_DIALOG(dlg), _("Add"), GTK_RESPONSE_OK );
    panel_apply_icon(GTK_WINDOW(dlg));

    scroll = gtk_scrolled_window_new( );
    gtk_scrolled_window_set_has_frame( GTK_SCROLLED_WINDOW(scroll), TRUE );
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC );
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_margin_top(scroll, LUXE_SPACING_INNER);
    gtk_widget_set_margin_bottom(scroll, LUXE_SPACING_INNER);
    gtk_widget_set_margin_start(scroll, LUXE_SPACING_INNER);
    gtk_widget_set_margin_end(scroll, LUXE_SPACING_INNER);
    gtk_box_append( GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), scroll );
    view = GTK_TREE_VIEW(gtk_tree_view_new());
    gtk_scrolled_window_set_child( GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(view) );
    tree_sel = gtk_tree_view_get_selection( view );
    gtk_tree_selection_set_mode( tree_sel, GTK_SELECTION_BROWSE );

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
                                            _("Available plugins"),
                                            render, "text", 0, NULL );
    gtk_tree_view_append_column( view, col );

    list = gtk_list_store_new( 2,
                               G_TYPE_STRING,
                               G_TYPE_STRING );

    /* Populate list of available plugins.
     * Omit plugins that can only exist once per system if it is already configured. */
    g_hash_table_iter_init(&iter, classes);
    while(g_hash_table_iter_next(&iter, &key, &val))
    {
        register const LuxePanelPluginInit *init = val;
        if (init->superseded)
            continue;
        if (!init->one_per_system || !_class_is_present(init))
        {
            GtkTreeIter it;
            gtk_list_store_append( list, &it );
            /* it is safe to put classes data here - they will be valid until restart */
            gtk_list_store_set( list, &it,
                                0, g_dgettext(init->gettext_package, init->name),
                                1, key,
                                -1 );
            /* g_debug( "%s (%s)", pc->type, _(pc->name) ); */
        }
    }
    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(list),
                                            sort_by_name, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list),
                                         GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                         GTK_SORT_ASCENDING);

    gtk_tree_view_set_model( view, GTK_TREE_MODEL(list) );
    g_object_unref( list );

    /*
     * The user can add a plugin either by clicking the "Add" button, or by
     * double-clicking the plugin.
     */
    g_signal_connect( dlg, "response",
                      G_CALLBACK(on_add_plugin_response), _view );
    g_signal_connect( view, "row-activated",
                      G_CALLBACK(on_add_plugin_row_activated), (gpointer) dlg);

    g_object_set_data( G_OBJECT(dlg), "avail-plugins", view );

    gtk_window_set_default_size( GTK_WINDOW(dlg), 320, 400 );
    gtk_window_present( GTK_WINDOW(dlg) );
}

static void on_remove_plugin( GtkButton* btn, GtkTreeView* view )
{
    GtkTreeIter it, it2;
    GtkTreePath* tree_path;
    GtkTreeModel* model;
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    GtkWidget *pl, *prev, *next;
    GList *plugins;
    gint i;

    LuxePanel* p = (LuxePanel*) g_object_get_data( G_OBJECT(view), "panel" );

    if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
    {
        tree_path = gtk_tree_model_get_path( model, &it );
        gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
        plugins = panel_box_children_list(p->priv->box);
        i = g_list_index(plugins, pl);
        if (i > 0)
        {
            prev = g_list_nth_data(plugins, i - 1);
            next = g_list_nth_data(plugins, i + 1);
            gtk_tree_path_prev(tree_path);
            gtk_tree_model_get_iter(model, &it2, tree_path);
            gtk_tree_path_next(tree_path);
        }
        else
            prev = next = NULL;
        if( gtk_tree_path_get_indices(tree_path)[0] >= gtk_tree_model_iter_n_children( model, NULL ) )
            gtk_tree_path_prev( tree_path );
        gtk_list_store_remove( GTK_LIST_STORE(model), &it );
        gtk_tree_selection_select_path( tree_sel, tree_path );
        gtk_tree_path_free( tree_path );
        g_list_free(plugins);

        _luxepanel_remove_plugin(p, pl);

        if (next && prev)
        {
            plugins = panel_box_children_list(p->priv->box);
            if (g_list_index(plugins, prev) < 0)
            {
                /* previous was removed */
                gtk_list_store_remove(GTK_LIST_STORE(model), &it2);
            }
            else if (g_list_index(plugins, next) < 0)
            {
                /* next was removed */
                if (gtk_tree_model_iter_next(model, &it2))
                    gtk_list_store_remove(GTK_LIST_STORE(model), &it2);
                //FIXME: else move selection!
            }
            g_list_free(plugins);
        }
    }
}

static void modify_plugin( GtkTreeView* view )
{
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    GtkTreeModel* model;
    GtkTreeIter it;
    GtkWidget* pl;
    const LuxePanelPluginInit *init;

    if( ! gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        return;

    gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
    init = PLUGIN_CLASS(pl);
    if (init->config)
    {
        GtkWidget *dlg;
        LuxePanel *panel = PLUGIN_PANEL(pl);
        dlg = init->config(panel, pl);
        if (dlg)
            _panel_show_config_dialog(panel, pl, dlg);
    }
}

static int get_widget_index(LuxePanel* p, GtkWidget* pl)
{
    GtkWidget* cw;
    int i;
    for (i = 0, cw = gtk_widget_get_first_child(p->priv->box); cw; cw = gtk_widget_get_next_sibling(cw), ++i)
    {
        if (cw == pl)
            return i;
    }
    return -1;
}

static void on_moveup_plugin(  GtkButton* btn, GtkTreeView* view )
{
    GtkTreeIter it, prev;
    GtkTreeModel* model = gtk_tree_view_get_model( view );
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    int i;

    LuxePanel* panel = (LuxePanel*) g_object_get_data( G_OBJECT(view), "panel" );

    if( ! gtk_tree_model_get_iter_first( model, &it ) )
        return;
    if( gtk_tree_selection_iter_is_selected( tree_sel, &it ) )
        return;
    do{
        if( gtk_tree_selection_iter_is_selected(tree_sel, &it) )
        {
            GtkWidget* pl;
            config_setting_t *s;
            gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
            gtk_list_store_move_before( GTK_LIST_STORE( model ),
                                        &it, &prev );

            i = get_widget_index(panel, pl);
            s = g_object_get_qdata(G_OBJECT(pl), luxepanel_plugin_qconf);
            /* reorder in config, 0 is Global */
            if (i == 0)
                i = 1;
            config_setting_move_elem(s, config_setting_get_parent(s), i);
            /* reorder in panel */
            /* place pl before the widget currently at i-1 (swap them) */
            {
                GtkWidget* w = panel_box_nth_child(panel->priv->box, i - 1);
                if (w && w != pl)
                    gtk_box_reorder_child_after(GTK_BOX(panel->priv->box), w, pl);
            }
            panel_config_save(panel->priv);
            return;
        }
        prev = it;
    }while( gtk_tree_model_iter_next( model, &it ) );
}

static void on_movedown_plugin(  GtkButton* btn, GtkTreeView* view )
{
    GtkTreeIter it, next;
    GtkTreeModel* model;
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    GtkWidget* pl;
    config_setting_t *s;
    int i;

    LuxePanel* panel = (LuxePanel*) g_object_get_data( G_OBJECT(view), "panel" );

    if( ! gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        return;
    next = it;

    if( ! gtk_tree_model_iter_next( model, &next) )
        return;

    gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );

    gtk_list_store_move_after( GTK_LIST_STORE( model ), &it, &next );

    i = get_widget_index(panel, pl) + 1;
    s = g_object_get_qdata(G_OBJECT(pl), luxepanel_plugin_qconf);
    /* reorder in config, 0 is Global */
    config_setting_move_elem(s, config_setting_get_parent(s), i + 1);
    /* reorder in panel */
    {
        GtkWidget* w = panel_box_nth_child(panel->priv->box, i);
        if (w && w != pl)
            gtk_box_reorder_child_after(GTK_BOX(panel->priv->box), pl, w);
    }
    panel_config_save(panel->priv);
}

static void
update_opt_menu(GtkWidget *w, int ind)
{
    int i;

    ENTER;
    /* this trick will trigger "changed" signal even if active entry is
     * not actually changing */
    i = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    if (i == ind) {
        i = i ? 0 : 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(w), i);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), ind);
    RET();
}

static void
update_toggle_button(GtkWidget *w, gboolean n)
{
    gboolean c;

    ENTER;
    /* this trick will trigger "changed" signal even if active entry is
     * not actually changing */
    c = TOGGLE_ACTIVE(w);
    if (c == n) {
        TOGGLE_SET_ACTIVE(w, !n);
    }
    TOGGLE_SET_ACTIVE(w, n);
    RET();
}

void panel_configure( LuxePanel* panel, int sel_page )
{
    Panel *p = panel->priv;
    GtkBuilder* builder;
    GtkWidget *w, *w2, *tint_clr;
    GtkComboBox *fm;
    gint monitors;

    if( p->pref_dialog )
    {
        panel_adjust_geometry_terminology(p);
        gtk_window_present(GTK_WINDOW(p->pref_dialog));
        return;
    }

    builder = gtk_builder_new();
    if( !gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/panel-pref.ui", NULL) )
    {
        g_object_unref(builder);
        return;
    }

    p->pref_dialog = (GtkWidget*)gtk_builder_get_object( builder, "panel_pref" );
    g_signal_connect(p->pref_dialog, "close-request", G_CALLBACK(on_pref_close_request), p);
    w = (GtkWidget*)gtk_builder_get_object( builder, "close_btn" );
    g_signal_connect(w, "clicked", G_CALLBACK(on_pref_close_clicked), p);
    g_object_set_data(G_OBJECT(w), "pref-win", p->pref_dialog);
    g_object_add_weak_pointer( G_OBJECT(p->pref_dialog), (gpointer) &p->pref_dialog );
    panel_apply_icon(GTK_WINDOW(p->pref_dialog));

    /* position */
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_bottom" );
    p->edge_bottom_button = w;
    TOGGLE_SET_ACTIVE(w, edge_selector(p, EDGE_BOTTOM));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_bottom_toggle), panel);
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_top" );
    p->edge_top_button = w;
    TOGGLE_SET_ACTIVE(w, edge_selector(p, EDGE_TOP));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_top_toggle), panel);
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_left" );
    p->edge_left_button = w;
    TOGGLE_SET_ACTIVE(w, edge_selector(p, EDGE_LEFT));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_left_toggle), panel);
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_right" );
    p->edge_right_button = w;
    TOGGLE_SET_ACTIVE(w, edge_selector(p, EDGE_RIGHT));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_right_toggle), panel);
    update_edges_buttons(p);

    /* monitor */
    monitors = 1;
    {
        GdkDisplay *cfg_display = gdk_display_get_default();
        GListModel *cfg_monitors = luxepanel_display_usable(cfg_display)
            ? gdk_display_get_monitors(cfg_display) : NULL;
        if (G_IS_LIST_MODEL(cfg_monitors))
            monitors = MAX(1, (int)g_list_model_get_n_items(cfg_monitors));
    }
    g_assert(monitors >= 1);
    w = (GtkWidget*)gtk_builder_get_object( builder, "monitor" );
    if (GTK_IS_SPIN_BUTTON(w))
    {
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(w), 1, monitors);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), p->monitor + 1);
        gtk_widget_set_sensitive(w, monitors > 1);
        g_signal_connect(w, "value-changed", G_CALLBACK(set_monitor), panel);
    }
    else if (GTK_IS_COMBO_BOX(w))
    {
        GtkCellRenderer *cell;
        GtkListStore *model;
        GtkTreeIter it;
        gint i;
        char itext[4];

        /* create a new cell renderer and bind cell data function to it */
        cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(w), cell, TRUE);
        gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(w), cell, "text", 0);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(w), cell,
                                           update_mon_sensitivity, panel, NULL);
        model = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(w)));
        /* add monitors beyond first one to the model */
        for (i = 1; i < monitors; i++)
        {
            snprintf(itext, sizeof(itext), "%d", i + 1);
            gtk_list_store_append(model, &it);
            gtk_list_store_set(model, &it, 0, itext, -1);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(w), p->monitor + 1);
        /* FIXME: set sensitive only if more than 1 monitor available? */
        g_signal_connect(w, "changed", G_CALLBACK(set_monitor_cb), panel);
    }

    /* alignment */
    p->alignment_left_label = w = (GtkWidget*)gtk_builder_get_object( builder, "alignment_left" );
    TOGGLE_SET_ACTIVE(w, (p->align == ALIGN_LEFT));
    g_signal_connect(w, "toggled", G_CALLBACK(align_left_toggle), panel);
    w = (GtkWidget*)gtk_builder_get_object( builder, "alignment_center" );
    TOGGLE_SET_ACTIVE(w, (p->align == ALIGN_CENTER));
    g_signal_connect(w, "toggled", G_CALLBACK(align_center_toggle), panel);
    p->alignment_right_label = w = (GtkWidget*)gtk_builder_get_object( builder, "alignment_right" );
    TOGGLE_SET_ACTIVE(w, (p->align == ALIGN_RIGHT));
    g_signal_connect(w, "toggled", G_CALLBACK(align_right_toggle), panel);

    /* margin */
    p->margin_control = w = (GtkWidget*)gtk_builder_get_object( builder, "margin" );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), p->margin);
    gtk_widget_set_sensitive(p->margin_control, (p->align != ALIGN_CENTER));
    g_signal_connect( w, "value-changed",
                      G_CALLBACK(set_margin), panel);

    /* size */
    p->width_label = (GtkWidget*)gtk_builder_get_object( builder, "width_label");
    p->width_control = w = (GtkWidget*)gtk_builder_get_object( builder, "width" );
    gtk_widget_set_sensitive( w, p->widthtype != WIDTH_REQUEST );
    gint upper = 0;
    if( p->widthtype == WIDTH_PERCENT)
        upper = 100;
    else if( p->widthtype == WIDTH_PIXEL)
        upper = (((p->edge == EDGE_TOP) || (p->edge == EDGE_BOTTOM)) ? SCREEN_WIDTH() : SCREEN_HEIGHT());
    gtk_spin_button_set_range( GTK_SPIN_BUTTON(w), 0, upper );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), p->width );
    g_signal_connect( w, "value-changed", G_CALLBACK(set_width), panel );

    w = (GtkWidget*)gtk_builder_get_object( builder, "width_unit" );
    update_opt_menu( w, p->widthtype - 1 );
    g_object_set_data(G_OBJECT(w), "width_spin", p->width_control );
    g_signal_connect( w, "changed",
                     G_CALLBACK(set_width_type), panel);

    p->height_label = (GtkWidget*)gtk_builder_get_object( builder, "height_label");
    p->height_control = w = (GtkWidget*)gtk_builder_get_object( builder, "height" );
    gtk_spin_button_set_range( GTK_SPIN_BUTTON(w), PANEL_HEIGHT_MIN, PANEL_HEIGHT_MAX );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), p->height );
    g_signal_connect( w, "value-changed", G_CALLBACK(set_height), panel );

    w = (GtkWidget*)gtk_builder_get_object( builder, "height_unit" );
    update_opt_menu( w, HEIGHT_PIXEL - 1);

    w = (GtkWidget*)gtk_builder_get_object( builder, "icon_size" );
    gtk_spin_button_set_range( GTK_SPIN_BUTTON(w), PANEL_HEIGHT_MIN, PANEL_HEIGHT_MAX );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), p->icon_size );
    g_signal_connect( w, "value-changed", G_CALLBACK(set_icon_size), panel );

    /* properties */

    /* Explaination from Ruediger Arp <ruediger@gmx.net>:
        "Set Dock Type", it is referring to the behaviour of
        dockable applications such as those found in WindowMaker (e.g.
        http://www.cs.mun.ca/~gstarkes/wmaker/dockapps ) and other
        lightweight window managers. These dockapps are probably being
        treated in some special way.
    */
    w = (GtkWidget*)gtk_builder_get_object( builder, "as_dock" );
    update_toggle_button( w, p->setdocktype );
    g_signal_connect( w, "toggled",
                      G_CALLBACK(set_dock_type), panel );

    /* Explaination from Ruediger Arp <ruediger@gmx.net>:
        "Set Strut": Reserve panel's space so that it will not be
        covered by maximazied windows.
        This is clearly an option to avoid the panel being
        covered/hidden by other applications so that it always is
        accessible. The panel "steals" some screen estate which cannot
        be accessed by other applications.
        GNOME Panel acts this way, too.
    */
    p->strut_control = w = (GtkWidget*)gtk_builder_get_object( builder, "reserve_space" );
    update_toggle_button( w, p->setstrut );
    update_strut_control_button(panel);
    g_signal_connect( w, "toggled",
                      G_CALLBACK(set_strut), panel );

    w = (GtkWidget*)gtk_builder_get_object( builder, "autohide" );
    update_toggle_button( w, p->autohide );
    g_signal_connect( w, "toggled",
                      G_CALLBACK(set_autohide), panel );

    w = (GtkWidget*)gtk_builder_get_object( builder, "height_when_minimized" );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), p->height_when_hidden);
    g_signal_connect( w, "value-changed",
                      G_CALLBACK(set_height_when_minimized), panel);

    /* transparancy */
    tint_clr = w = (GtkWidget*)gtk_builder_get_object( builder, "tint_clr" );
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(w), &p->gtintcolor);
    if ( ! p->transparent )
        gtk_widget_set_sensitive( w, FALSE );
    g_signal_connect( w, "color-set", G_CALLBACK( on_tint_color_set ), p );

    /* background */
    {
        GtkWidget* none, *trans, *img;
        none = (GtkWidget*)gtk_builder_get_object( builder, "bg_none" );
        trans = (GtkWidget*)gtk_builder_get_object( builder, "bg_transparency" );
        img = (GtkWidget*)gtk_builder_get_object( builder, "bg_image" );

        g_object_set_data(G_OBJECT(trans), "tint_clr", tint_clr);

        if (p->background)
            TOGGLE_SET_ACTIVE( img, TRUE);
        else if (p->transparent)
            TOGGLE_SET_ACTIVE( trans, TRUE);
        else
            TOGGLE_SET_ACTIVE( none, TRUE);

        g_signal_connect(none, "toggled", G_CALLBACK(background_disable_toggle), p);
        g_signal_connect(trans, "toggled", G_CALLBACK(transparency_toggle), p);
        g_signal_connect(img, "toggled", G_CALLBACK(background_toggle), p);

        w = (GtkWidget*)gtk_builder_get_object( builder, "img_file" );
        g_object_set_data(G_OBJECT(img), "img_file", w);
        g_object_set_data(G_OBJECT(w), "bg-toggle", img);
        g_object_set_data(G_OBJECT(w), "pref-win", p->pref_dialog);
        if (p->background_file != NULL)
            background_set_file(w, p->background_file);
        else
        {
            /* fall back to the packaged default background, if any */
            GtkIconPaintable* pt = gtk_icon_theme_lookup_icon(p->icon_theme,
                                        "luxepanel-background", NULL, 16, 1,
                                        gtk_widget_get_direction(w), 0);
            if (pt)
            {
                GFile* f = gtk_icon_paintable_get_file(pt);
                if (f)
                {
                    background_set_file(w, g_file_get_path(f));
                    g_object_unref(f);
                }
                g_object_unref(pt);
            }
        }

        if (!p->background)
            gtk_widget_set_sensitive( w, FALSE);
        g_signal_connect( w, "clicked", G_CALLBACK (on_img_file_clicked), p);
    }

    /* font color */
    w = (GtkWidget*)gtk_builder_get_object( builder, "font_clr" );
    gtk_color_chooser_set_rgba( GTK_COLOR_CHOOSER(w), &p->gfontcolor );
    g_signal_connect(w, "color-set", G_CALLBACK( on_font_color_set ), panel);

    w2 = (GtkWidget*)gtk_builder_get_object( builder, "use_font_clr" );
    TOGGLE_SET_ACTIVE( w2, p->usefontcolor );
    g_object_set_data( G_OBJECT(w2), "clr", w );
    g_signal_connect(w2, "toggled", G_CALLBACK(on_use_font_color_toggled), panel);
    if( ! p->usefontcolor )
        gtk_widget_set_sensitive( w, FALSE );

    /* font size */
    w = (GtkWidget*)gtk_builder_get_object( builder, "font_size" );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), p->fontsize );
    g_signal_connect( w, "value-changed",
                      G_CALLBACK(on_font_size_set), panel);

    w2 = (GtkWidget*)gtk_builder_get_object( builder, "use_font_size" );
    TOGGLE_SET_ACTIVE( w2, p->usefontsize );
    g_object_set_data( G_OBJECT(w2), "clr", w );
    g_signal_connect(w2, "toggled", G_CALLBACK(on_use_font_size_toggled), panel);
    if( ! p->usefontsize )
        gtk_widget_set_sensitive( w, FALSE );

    /* plugin list */
    {
        GtkWidget* plugin_list = (GtkWidget*)gtk_builder_get_object( builder, "plugin_list" );

        /* buttons used to edit plugin list */
        w = (GtkWidget*)gtk_builder_get_object( builder, "add_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_add_plugin), plugin_list );

        w = (GtkWidget*)gtk_builder_get_object( builder, "edit_btn" );
        g_signal_connect_swapped( w, "clicked", G_CALLBACK(modify_plugin), plugin_list );
        g_object_set_data( G_OBJECT(plugin_list), "edit_btn", w );

        w = (GtkWidget*)gtk_builder_get_object( builder, "remove_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_remove_plugin), plugin_list );
        w = (GtkWidget*)gtk_builder_get_object( builder, "moveup_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_moveup_plugin), plugin_list );
        w = (GtkWidget*)gtk_builder_get_object( builder, "movedown_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_movedown_plugin), plugin_list );

        w = (GtkWidget*)gtk_builder_get_object( builder, "plugin_desc" );
        init_plugin_list( panel, GTK_TREE_VIEW(plugin_list), w );
    }
    /* advanced, applications */
    /* fm_app_chooser_combo_box (libfm-gtk) is dropped; minimal own chooser:
     * list installed handlers of inode/directory + "System default". */
    fm = GTK_COMBO_BOX(gtk_builder_get_object(builder, "fm_combobox"));
    {
        GList *apps, *l;
        GAppInfo *def = g_app_info_get_default_for_type("inode/directory", TRUE);
        const char* def_id = def ? g_app_info_get_id(def) : NULL;
        int pos = 0, def_pos = 0;

        apps = g_app_info_get_all_for_type("inode/directory");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fm), _("System default"));
        for (l = apps; l; l = l->next, ++pos)
        {
            const char* id = g_app_info_get_id(G_APP_INFO(l->data));
            if (id && def_id && strcmp(id, def_id) == 0)
                def_pos = pos + 1;
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(fm), id,
                                      g_app_info_get_display_name(G_APP_INFO(l->data)));
        }
        g_list_free_full(apps, g_object_unref);
        gtk_combo_box_set_active(fm, def_pos);
        g_signal_connect(fm, "changed",
                         G_CALLBACK(on_fm_combo_changed), NULL);
        g_object_set_data_full(G_OBJECT(fm), "cur-id", g_strdup(def_id), g_free);
        g_object_set_data(G_OBJECT(p->pref_dialog), "fm-combo", fm);
        if (def)
            g_object_unref(def);
    }

    w = (GtkWidget*)gtk_builder_get_object( builder, "term" );
    if (fm_config->terminal)
        gtk_editable_set_text( GTK_EDITABLE(w), fm_config->terminal );
    attach_entry_focus_out( w, G_CALLBACK(on_entry_focus_leave), &fm_config->terminal);

    /* If we are under LuxeSession, setting logout command is not necessary. */
    w = (GtkWidget*)gtk_builder_get_object( builder, "logout" );
    if( getenv("_LUXESESSION_PID") ) {
        gtk_widget_set_visible( w, FALSE );
        w = (GtkWidget*)gtk_builder_get_object( builder, "logout_label" );
        gtk_widget_set_visible( w, FALSE );
    }
    else {
        if(logout_cmd)
            gtk_editable_set_text( GTK_EDITABLE(w), logout_cmd );
        attach_entry_focus_out( w, G_CALLBACK(on_entry_focus_leave_old), &logout_cmd);
    }

    panel_adjust_geometry_terminology(p);
    gtk_window_present(GTK_WINDOW(p->pref_dialog));
    w = (GtkWidget*)gtk_builder_get_object( builder, "notebook" );
    gtk_notebook_set_current_page( GTK_NOTEBOOK(w), sel_page );

    g_object_unref(builder);
}

void panel_config_save( Panel* p )
{
    gchar *fname;

    if (p == NULL || p->name == NULL || p->config == NULL)
        return;
    fname = _user_config_file_name("panels", p->name);
    /* existance of 'panels' dir ensured in main() */

    if (!config_write_file(p->config, fname)) {
        g_warning("can't open for write %s:", fname);
        g_free( fname );
        return;
    }
    g_free( fname );

    /* save the global config file */
    save_global_config();
    p->config_changed = 0;
}

void luxepanel_config_save(LuxePanel *p)
{
    panel_config_save(p->priv);
}

static void show_error_dialog(const char* msg)
{
    luxe_show_error(NULL, msg, NULL);
}

void logout(void)
{
    const char* l_logout_cmd = logout_cmd;
    /* If LuxeSession is running, _LUXESESSION_PID will be set */
    if( ! l_logout_cmd && getenv("_LUXESESSION_PID") )
        l_logout_cmd = "luxesession-logout";

    if( l_logout_cmd )
    {
        GAppInfo* appinfo;
        GAppLaunchContext* ctx;
        GError* error = NULL;

        appinfo = fm_app_info_create_from_commandline(l_logout_cmd, NULL,
                                                      G_APP_INFO_CREATE_NONE, &error);
        if( ! appinfo )
        {
            show_error_dialog(error ? error->message : _("Command not found"));
            g_clear_error(&error);
            return;
        }
        ctx = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(gdk_display_get_default()));
        fm_app_info_launch(appinfo, NULL, ctx, &error);
        g_object_unref(ctx);
        g_object_unref(appinfo);
        g_clear_error(&error);
    }
    else
        show_error_dialog(_("Logout command is not set"));
}

static void notify_apply_config( GtkWidget* widget )
{
    GSourceFunc apply_func;
    GtkWidget* dlg;

    dlg = (GtkWidget*)gtk_widget_get_root( widget );
    apply_func = g_object_get_data( G_OBJECT(dlg), "apply_func" );
    if( apply_func )
        (*apply_func)( g_object_get_data(G_OBJECT(dlg), "apply_func_data") );
}

static gboolean _on_entry_focus_out_do_work(GtkWidget* edit, gpointer user_data)
{
    char** val = (char**)user_data;
    const char *new_val;
    new_val = gtk_editable_get_text(GTK_EDITABLE(edit));
    if (g_strcmp0(*val, new_val) == 0) /* not changed */
        return FALSE;
    g_free( *val );
    *val = (new_val && *new_val) ? g_strdup( new_val ) : NULL;
    return TRUE;
}

/* the same but affects fm_config instead of panel config */

static void on_spin_changed( GtkSpinButton* spin, gpointer user_data )
{
    int* val = (int*)user_data;
    *val = (int)gtk_spin_button_get_value( spin );
    notify_apply_config( GTK_WIDGET(spin) );
}

static void on_toggle_changed( GtkWidget* btn, gpointer user_data )
{
    gboolean* val = (gboolean*)user_data;
    *val = TOGGLE_ACTIVE(btn);
    notify_apply_config( GTK_WIDGET(btn) );
}

/* "focus-out-event" is gone; entries get an EventControllerFocus */
static void attach_entry_focus_out(GtkWidget* entry, GCallback cb, gpointer user_data)
{
    GtkEventController* fc = gtk_event_controller_focus_new();
    g_signal_connect(fc, "leave", cb, user_data);
    gtk_widget_add_controller(entry, fc);
}

static void on_entry_focus_leave( GtkEventControllerFocus* fc, gpointer user_data )
{
    GtkWidget* edit = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(fc));
    if (_on_entry_focus_out_do_work(edit, user_data))
        fm_config_save(fm_config, NULL);
}

static void on_entry_focus_leave_old( GtkEventControllerFocus* fc, gpointer user_data )
{
    GtkWidget* edit = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(fc));
    if (_on_entry_focus_out_do_work(edit, user_data))
        notify_apply_config( edit );
}

static void on_fm_combo_changed( GtkComboBox* fm, gpointer user_data )
{
    (void)user_data;
    g_object_set_data(G_OBJECT(fm), "is-changed", GINT_TO_POINTER(TRUE));
}

typedef struct
{
    GtkWidget* entry;
    char** val;
} BrowseCtx;

static void on_browse_response(GtkNativeDialog* nat, int resp, gpointer user_data)
{
    BrowseCtx* ctx = user_data;
    if( resp == GTK_RESPONSE_ACCEPT )
    {
        GFile* f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(nat));
        if( f )
        {
            char* file = g_file_get_path(f);
            if( file )
                gtk_editable_set_text(GTK_EDITABLE(ctx->entry), file);
            g_free(file);
            g_object_unref(f);
        }
        if (_on_entry_focus_out_do_work(ctx->entry, ctx->val))
            notify_apply_config(ctx->entry);
    }
    gtk_native_dialog_destroy(nat);
    g_free(ctx);
}

static void on_browse_btn_clicked(GtkButton* btn, GtkEntry* entry)
{
    GtkFileChooserAction action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "chooser-action"));
    GtkWidget* dlg = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "dlg"));
    BrowseCtx* ctx = g_new0(BrowseCtx, 1);
    GtkFileChooserNative* fc = gtk_file_chooser_native_new(
                                        (action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) ? _("Select a directory") : _("Select a file"),
                                        GTK_WINDOW(dlg),
                                        action,
                                        _("OK"), _("Cancel"));
    const char* file = gtk_editable_get_text(GTK_EDITABLE(entry));
    ctx->entry = GTK_WIDGET(entry);
    ctx->val = g_object_get_data(G_OBJECT(btn), "file-val");
    if( file && *file )
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(fc), g_file_new_for_path(file), NULL);
    g_signal_connect(fc, "response", G_CALLBACK(on_browse_response), ctx);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(fc));
}

/* if the plugin was destroyed then destroy the dialog opened for it */
static void on_plugin_destroy(GtkWidget *plugin, GtkDialog *dlg)
{
    gtk_dialog_response(dlg, GTK_RESPONSE_CLOSE);
}

/* Handler for "close-request" from window-based (non-GtkDialog)
 * configuration dialogs built from GTK4 .ui files. */
static gboolean generic_config_dlg_close(GtkWidget * dlg, Panel * panel)
{
    gpointer plugin = g_object_get_data(G_OBJECT(dlg), "generic-config-plugin");
    if (plugin)
        g_signal_handlers_disconnect_by_func(plugin, on_plugin_destroy, dlg);
    g_object_set_data(G_OBJECT(dlg), "generic-config-plugin", NULL);
    panel->plugin_pref_dialog = NULL;
    gtk_window_destroy(GTK_WINDOW(dlg));
    panel_config_save(panel);
    return TRUE;
}

/* Handler for "response" signal from standard configuration dialog. */
static void generic_config_dlg_response(GtkWidget * dlg, int response, Panel * panel)
{
    gpointer plugin = g_object_get_data(G_OBJECT(dlg), "generic-config-plugin");
    if (plugin)
        g_signal_handlers_disconnect_by_func(plugin, on_plugin_destroy, dlg);
    g_object_set_data(G_OBJECT(dlg), "generic-config-plugin", NULL);
    panel->plugin_pref_dialog = NULL;
    gtk_window_destroy(GTK_WINDOW(dlg));
    panel_config_save(panel);
}

void _panel_show_config_dialog(LuxePanel *panel, GtkWidget *p, GtkWidget *dlg)
{

    /* If there is already a plugin configuration dialog open, close it.
     * Then record this one in case the panel or plugin is deleted. */
    if (panel->priv->plugin_pref_dialog != NULL)
        generic_config_dlg_response(panel->priv->plugin_pref_dialog,
                                   GTK_RESPONSE_CLOSE, panel->priv);
    panel->priv->plugin_pref_dialog = dlg;

    /* add some handlers to destroy the dialog on responce or widget destroy */
    if (GTK_IS_DIALOG(dlg))
    {
        g_signal_connect(dlg, "response", G_CALLBACK(generic_config_dlg_response), panel->priv);
        g_signal_connect(p, "destroy", G_CALLBACK(on_plugin_destroy), dlg);
    }
    else
    {
        g_signal_connect(dlg, "close-request", G_CALLBACK(generic_config_dlg_close), panel->priv);
        g_signal_connect(p, "destroy", G_CALLBACK(on_plugin_destroy), dlg);
    }
    g_object_set_data(G_OBJECT(dlg), "generic-config-plugin", p);

    /* adjust config dialog window position to be near plugin */
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(panel));
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Parameters: const char* name, gpointer ret_value, GType type, ....NULL */
static void on_file_btn_response(GtkNativeDialog* nat, int resp, gpointer user_data)
{
    char** val = user_data;
    if( resp == GTK_RESPONSE_ACCEPT )
    {
        g_free(*val);
        *val = NULL;
        {
            GFile* f = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(nat));
            if (f)
            {
                *val = g_file_get_path(f);
                g_object_unref(f);
            }
        }
        notify_apply_config(GTK_WIDGET(g_object_get_data(G_OBJECT(nat), "src-widget")));
    }
    gtk_native_dialog_destroy(nat);
}

static void on_file_btn_clicked(GtkButton* btn, gpointer user_data)
{
    GtkWidget* dlg = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "dlg"));
    char** val = user_data;
    GtkFileChooserNative* fc = gtk_file_chooser_native_new(_("Select a file"),
                                GTK_WINDOW(dlg), GTK_FILE_CHOOSER_ACTION_OPEN,
                                _("OK"), _("Cancel"));
    g_object_set_data(G_OBJECT(fc), "src-widget", btn);
    if( *val )
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(fc), g_file_new_for_path(*val), NULL);
    g_signal_connect(fc, "response", G_CALLBACK(on_file_btn_response), val);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(fc));
}

static GtkWidget *_luxepanel_generic_config_dlg(const char *title, Panel *p,
                                              GSourceFunc apply_func,
                                              gpointer plugin,
                                              const char *name, va_list args)
{
    GtkWidget* dlg;
    GtkBox *dlg_vbox;
    dlg = gtk_dialog_new( );
    gtk_window_set_title( GTK_WINDOW(dlg), title );
    gtk_dialog_add_button( GTK_DIALOG(dlg), _("Close"), GTK_RESPONSE_CLOSE );
    dlg_vbox = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg)));

    panel_apply_icon(GTK_WINDOW(dlg));

    if( apply_func )
        g_object_set_data( G_OBJECT(dlg), "apply_func", apply_func );
    g_object_set_data( G_OBJECT(dlg), "apply_func_data", plugin );

    gtk_box_set_spacing( dlg_vbox, LUXE_SPACING_INNER );

    while( name )
    {
        GtkWidget* entry = NULL;
        gpointer val = va_arg( args, gpointer );
        PluginConfType type = va_arg( args, PluginConfType );
        if (type != CONF_TYPE_TRIM && val == NULL)
            g_critical("NULL pointer for generic config dialog");
        else switch( type )
        {
            case CONF_TYPE_STR:
            case CONF_TYPE_FILE_ENTRY: /* entry with a button to browse for files. */
            case CONF_TYPE_DIRECTORY_ENTRY: /* entry with a button to browse for directories. */
                entry = gtk_entry_new();
                if( *(char**)val )
                    gtk_editable_set_text( GTK_EDITABLE(entry), *(char**)val );
                attach_entry_focus_out( entry, G_CALLBACK(on_entry_focus_leave_old), val );
                gtk_editable_set_width_chars(GTK_EDITABLE(entry), 40);
                break;
            case CONF_TYPE_INT:
                gtk_box_append(dlg_vbox,
                               panel_config_int_button_new(name, val, 0, 1000));
                break;
            case CONF_TYPE_BOOL:
                entry = gtk_check_button_new_with_label(name);
                TOGGLE_SET_ACTIVE( entry, *(gboolean*)val );
                g_signal_connect( entry, "toggled",
                  G_CALLBACK(on_toggle_changed), val );
                break;
            case CONF_TYPE_FILE:
                /* GtkFileChooserButton is gone; plain button + native chooser */
                entry = gtk_button_new_with_label(_("Select a file"));
                if( *(char**)val )
                {
                    char* base = g_path_get_basename(*(char**)val);
                    gtk_button_set_label(GTK_BUTTON(entry), base);
                    g_free(base);
                }
                g_object_set_data(G_OBJECT(entry), "dlg", dlg);
                g_signal_connect( entry, "clicked",
                  G_CALLBACK(on_file_btn_clicked), val );
                break;
            case CONF_TYPE_TRIM:
                {
                entry = gtk_label_new(NULL);
                char *markup = g_markup_printf_escaped ("<span style=\"italic\">%s</span>", name );
                gtk_label_set_markup (GTK_LABEL (entry), markup);
                g_free (markup);
                }
                break;
            case CONF_TYPE_EXTERNAL:
                if (GTK_IS_WIDGET(val))
                    gtk_box_append(dlg_vbox, val);
                else
                    g_critical("value for CONF_TYPE_EXTERNAL is not a GtkWidget");
                break;
        }
        if( entry )
        {
            if(( type == CONF_TYPE_BOOL ) || ( type == CONF_TYPE_TRIM ) || ( type == CONF_TYPE_FILE ))
            {
                gtk_box_append( dlg_vbox, entry );
            }
            else
            {
                GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER );
                gtk_box_append( GTK_BOX(hbox), gtk_label_new(name) );
                gtk_widget_set_hexpand( entry, TRUE );
                gtk_box_append( GTK_BOX(hbox), entry );
                gtk_box_append( dlg_vbox, hbox );
                if ((type == CONF_TYPE_FILE_ENTRY) || (type == CONF_TYPE_DIRECTORY_ENTRY))
                {
                    GtkWidget* browse = gtk_button_new_with_mnemonic(_("_Browse"));
                    gtk_box_append( GTK_BOX(hbox), browse );
                    g_object_set_data(G_OBJECT(browse), "file-val", val);
                    g_object_set_data(G_OBJECT(browse), "dlg", dlg);

                    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
                    if (type == CONF_TYPE_DIRECTORY_ENTRY)
                    {
                      action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
                    }

                    g_object_set_data(G_OBJECT(browse), "chooser-action", GINT_TO_POINTER(action));
                    g_signal_connect( browse, "clicked", G_CALLBACK(on_browse_btn_clicked), entry );
                }
            }
        }
        name = va_arg( args, const char* );
    }

    gtk_widget_set_margin_top( GTK_WIDGET(dlg_vbox), LUXE_SPACING_OUTER );
    gtk_widget_set_margin_bottom( GTK_WIDGET(dlg_vbox), LUXE_SPACING_OUTER );
    gtk_widget_set_margin_start( GTK_WIDGET(dlg_vbox), LUXE_SPACING_OUTER );
    gtk_widget_set_margin_end( GTK_WIDGET(dlg_vbox), LUXE_SPACING_OUTER );

    return dlg;
}

GtkWidget *panel_config_int_button_new(const char *name, gint *val,
                                       gint min, gint max)
{
    GtkWidget *entry = gtk_spin_button_new_with_range(min, max, 1);
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, LUXE_SPACING_INNER);

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry), *val);
    g_signal_connect(entry, "value-changed", G_CALLBACK(on_spin_changed), val);
    gtk_box_append(GTK_BOX(hbox), gtk_label_new(name));
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(hbox), entry);
    return hbox;
}

/* new plugins API -- apply_func() gets GtkWidget* */
GtkWidget *luxepanel_generic_config_dlg(const char *title, LuxePanel *panel,
                                      GSourceFunc apply_func, GtkWidget *plugin,
                                      const char *name, ...)
{
    GtkWidget *dlg;
    va_list args;

    if (plugin == NULL)
        return NULL;
    va_start(args, name);
    dlg = _luxepanel_generic_config_dlg(title, panel->priv, apply_func, plugin, name, args);
    va_end(args);
    return dlg;
}

/* for old plugins compatibility -- apply_func() gets Plugin* */
GtkWidget* create_generic_config_dlg( const char* title, GtkWidget* parent,
                                      GSourceFunc apply_func, Plugin * plugin,
                                      const char* name, ... )
{
    GtkWidget *dlg;
    va_list args;

    if (plugin == NULL)
        return NULL;
    va_start(args, name);
    dlg = _luxepanel_generic_config_dlg(title, plugin->panel, apply_func, plugin, name, args);
    va_end(args);
    _panel_show_config_dialog(plugin->panel->topgwin, plugin->pwid, dlg);
    return dlg;
}

#define COMMAND_GROUP "Command"

void load_global_config()
{
    GKeyFile* kf = g_key_file_new();
    char* file = NULL;
    gboolean loaded = FALSE;
    const gchar * const * dir = g_get_system_config_dirs();

    /* try to load system config file first */
    if (dir) while (dir[0] && !loaded)
    {
        g_free(file);
        file = _system_config_file_name(dir[0], "config");
        if (g_key_file_load_from_file(kf, file, 0, NULL))
            loaded = TRUE;
        dir++;
    }
    if (!loaded) /* fallback to old config place for backward compatibility */
    {
        g_free(file);
        file = _old_system_config_file_name("config");
        if (g_key_file_load_from_file(kf, file, 0, NULL))
            loaded = TRUE;
    }
    /* now try to load user config file */
    g_free(file);
    file = _user_config_file_name("config", NULL);
    if (g_key_file_load_from_file(kf, file, 0, NULL))
        loaded = TRUE;
    g_free(file);

    if( loaded )
    {
        char *fm, *tmp;
        GList *apps, *l;

        logout_cmd = g_key_file_get_string( kf, COMMAND_GROUP, "Logout", NULL );
        /* check for terminal setting on upgrade */
        if (fm_config->terminal == NULL)
        {
            fm_config->terminal = g_key_file_get_string(kf, COMMAND_GROUP,
                                                        "Terminal", NULL);
            if (fm_config->terminal != NULL) /* setting changed, save it */
                fm_config_save(fm_config, NULL);
        }
        /* this is heavy but fortunately it will be ran only once: on upgrade */
        fm = g_key_file_get_string(kf, COMMAND_GROUP, "FileManager", NULL);
        if (fm)
        {
            tmp = strchr(fm, ' '); /* chop params */
            if (tmp)
                *tmp = '\0';
            tmp = strrchr(fm, '/'); /* use only basename */
            if (tmp)
                tmp++;
            else
                tmp = fm;
            tmp = g_strdup_printf("%s.desktop", tmp); /* generate desktop id */
            g_free(fm);
            apps = g_app_info_get_all_for_type("inode/directory");
            for (l = apps; l; l = l->next) /* scan all known applications */
                if (strcmp(tmp, g_app_info_get_id(l->data)) == 0)
                    break;
            if (l != NULL) /* found */
                g_app_info_set_as_default_for_type(l->data, "inode/directory",
                                                   NULL);
            else
                g_warning("the %s is not valid desktop id of file manager", tmp);
            for (l = apps; l; l = l->next) /* free retrieved data */
                g_object_unref(l->data);
            g_list_free(apps);
            g_free(tmp);
            save_global_config();
        }
    }
    g_key_file_free( kf );
}

static void save_global_config()
{
    char* file = _user_config_file_name("config", NULL);
    FILE* f = fopen( file, "w" );
    if( f )
    {
        fprintf( f, "[" COMMAND_GROUP "]\n");
        if( logout_cmd )
            fprintf( f, "Logout=%s\n", logout_cmd );
        fclose( f );
    }
    g_free(file);
}

void free_global_config()
{
    g_free( logout_cmd );
}

/* this is dirty and should be removed later */
const char*
luxepanel_get_file_manager()
{
    GAppInfo *app = g_app_info_get_default_for_type("inode/directory", TRUE);
    static char *exec = NULL;
    const char *c, *x;

    if (!app)
        return "pcmanfm %s";
    c = g_app_info_get_commandline(app);
    x = strchr(c, ' '); /* skip all arguments */
    g_free(exec);
    if (x)
        exec = g_strndup(c, x - c);
    else
        exec = g_strdup(c);
    return exec;
}

/* vim: set sw=4 et sts=4 : */
