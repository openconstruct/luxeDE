/*
 * Copyright (C) 2016 Andriy Grytsenko <andrej@rep.kiev.ua>
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
#include <config.h>
#endif

#include "private.h"
#include "space.h"

#include <X11/cursorfont.h>

/* helper: root-window origin of a surface (gdk_window_get_origin is gone) */
static void _move_surface_origin(GdkSurface *surface, int *ox, int *oy)
{
    Display *dpy;
    Window child;

    if (ox != NULL) *ox = 0;
    if (oy != NULL) *oy = 0;
    if (surface == NULL || !GDK_IS_X11_SURFACE(surface) || ox == NULL || oy == NULL)
        return;
    dpy = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surface));
    XTranslateCoordinates(dpy, gdk_x11_surface_get_xid(surface),
                          DefaultRootWindow(dpy), 0, 0, ox, oy, &child);
}

/* helper: children of the panel box, GTK4-style sibling walk */
static GList *_move_box_children(GtkWidget *box)
{
    GList *l = NULL;
    GtkWidget *cw;

    if (box == NULL)
        return NULL;
    for (cw = gtk_widget_get_first_child(box); cw; cw = gtk_widget_get_next_sibling(cw))
        l = g_list_append(l, cw);
    return l;
}

/* helper: current index of w in the box */
static int _move_box_index(GtkWidget *box, GtkWidget *w)
{
    GtkWidget *cw;
    int i;

    if (box == NULL || w == NULL)
        return -1;
    for (i = 0, cw = gtk_widget_get_first_child(box); cw; cw = gtk_widget_get_next_sibling(cw), ++i)
        if (cw == w)
            return i;
    return -1;
}

/* helper: emulate legacy "position" child property (gtk_box_reorder_child) */
static void _move_box_set_position(GtkWidget *box, GtkWidget *w, int pos)
{
    GList *list, *l;
    int n = 0;

    if (box == NULL || w == NULL)
        return;
    list = _move_box_children(box);
    l = g_list_find(list, w);
    if (l == NULL)
    {
        g_list_free(list);
        return;
    }
    n = g_list_length(list) - 1;
    list = g_list_remove_link(list, l);
    if (pos < 0)
        pos = 0;
    if (pos > n)
        pos = n;
    if (list == NULL)
    {
        g_list_free(l);
        return;
    }
    if (pos == 0)
    {
        GtkWidget *first = (GtkWidget *)list->data;
        if (first != w)
            gtk_box_reorder_child_after(GTK_BOX(box), first, w);
    }
    else
    {
        GtkWidget *anchor = (GtkWidget *)g_list_nth_data(list, pos - 1);
        if (anchor != w)
            gtk_box_reorder_child_after(GTK_BOX(box), w, anchor);
    }
    g_list_free(list);
    g_list_free(l);
}

/* helper: the "expand" flag lives in the plugin's own config now */
static gboolean _move_space_get_expand(GtkWidget *w)
{
    config_setting_t *conf = g_object_get_qdata(G_OBJECT(w), luxepanel_plugin_qconf);
    gint i = 0;

    if (conf)
        config_setting_lookup_int(conf, "expand", &i);
    return (i != 0);
}

static void _move_space_set_expand(Panel *p, GtkWidget *w, gboolean expand)
{
    config_setting_t *cfg = g_object_get_qdata(G_OBJECT(w), luxepanel_plugin_qconf);

    if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_set_hexpand(w, expand);
    else
        gtk_widget_set_vexpand(w, expand);
    if (cfg)
        config_group_set_int(cfg, "expand", expand ? 1 : 0);
}

gboolean _luxepanel_button_release(GtkWidget *widget, GdkEvent *event)
{
    LuxePanel *panel = widget != NULL ? PLUGIN_PANEL(widget) : NULL;
    Panel *p = panel != NULL ? panel->priv : NULL;

    if (p == NULL || event == NULL)
        return FALSE;

    if (gdk_event_get_device(event) == p->move_device &&
        gdk_button_event_get_button(event) == 2 &&
        p->move_state != PANEL_MOVE_STOP)
    {
        if (p->move_state == PANEL_MOVE_MOVING)
        {
            /* ungrab pointer and free the "move" cursor */
            GdkSurface *surface = GTK_IS_NATIVE(widget) ?
                gtk_native_get_surface(GTK_NATIVE(widget)) : NULL;
            if (surface != NULL && GDK_IS_X11_SURFACE(surface))
            {
                Display *dpy = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surface));
                Cursor xc = (Cursor)(guintptr)g_object_get_data(G_OBJECT(widget), "luxepanel-move-cursor");

                XUngrabPointer(dpy, CurrentTime);
                if (xc != None)
                    XFreeCursor(dpy, xc);
                g_object_set_data(G_OBJECT(widget), "luxepanel-move-cursor", NULL);
            }
        }
        p->move_state = PANEL_MOVE_STOP;
        p->move_device = NULL;
        return TRUE;
    }
    return FALSE;
}

gboolean _luxepanel_motion_notify(GtkWidget *widget, GdkEvent *event)
{
    LuxePanel *panel = widget != NULL ? PLUGIN_PANEL(widget) : NULL;
    Panel *p = panel != NULL ? panel->priv : NULL;

    if (p == NULL || event == NULL || p->box == NULL)
        return FALSE;
    GList *plugins, *l;
    GtkAllocation alloc;
    int s;
    int x, y, old_pos, new_pos;
    gboolean expand, rtl;
    config_setting_t *cfg;
    PanelPluginMoveData *in, *out;

    if (gdk_event_get_device(event) != p->move_device)
        return FALSE;

    if (p->move_state == PANEL_MOVE_DETECT)
    {
        {
            gdouble ex, ey;
            GdkSurface *surface = gdk_event_get_surface(event);
            int ox = 0, oy = 0;

            if (surface)
                _move_surface_origin(surface, &ox, &oy);
            gdk_event_get_position(event, &ex, &ey);
            x = ox + (int)ex - p->ax;
            y = oy + (int)ey - p->ay;
        }
        /* check threshold, start moving */
        if (gtk_drag_check_threshold(widget, p->move_x, p->move_y, x, y))
        {
            plugins = _move_box_children(p->box);
            for (l = plugins; l; l = l->next)
            {
                gtk_widget_get_allocation(l->data, &alloc);
                if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                {
                    if (p->move_x >= alloc.x && p->move_x < alloc.x + alloc.width)
                        break;
                }
                else
                {
                    if (p->move_y >= alloc.y && p->move_y < alloc.y + alloc.height)
                        break;
                }
            }
            if (l == NULL || PANEL_IS_SPACE(l->data))
            {
                p->move_state = PANEL_MOVE_STOP;
                p->move_device = NULL;
                g_list_free(plugins);
                return TRUE;
            }
            /* grab pointer, use cursor "move" (raw X: GDK4 has no grabs) */
            {
                GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));
                Display *dpy = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surface));
                Cursor xc = XCreateFontCursor(dpy, XC_fleur);

                s = (XGrabPointer(dpy, gdk_x11_surface_get_xid(surface), FALSE,
                                  ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                  GrabModeAsync, GrabModeAsync, None, xc, CurrentTime)
                     == GrabSuccess);
                if (s)
                    g_object_set_data(G_OBJECT(widget), "luxepanel-move-cursor",
                                      GUINT_TO_POINTER((guint)xc));
                else
                    XFreeCursor(dpy, xc);
            }
            if (s)
            {
                p->move_state = PANEL_MOVE_MOVING;
                /* load all drag data into panel data */
                p->move_plugin = l->data;
                p->move_before.space = NULL;
                p->move_before.plugin = NULL;
                p->move_after.space = NULL;
                p->move_after.plugin = NULL;
                if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                    p->move_diff = alloc.x + alloc.width / 2 - p->move_x;
                else
                    p->move_diff = alloc.y + alloc.height / 2 - p->move_y;
                /* g_debug("move_diff is %d",p->move_diff); */
                if (l->prev)
                {
                    GList *save = l;

                    l = l->prev;
                    if (PANEL_IS_SPACE(l->data))
                    {
                        p->move_before.space = l->data;
                        expand = _move_space_get_expand(l->data);
                        if (expand)
                        {
                            gtk_widget_get_allocation(l->data, &alloc);
                            if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                                p->move_before.space_size = alloc.width;
                            else
                                p->move_before.space_size = alloc.height;
                        }
                        else
                            p->move_before.space_size = -1;
                        l = l->prev;
                    }
                    if (l)
                    {
                        p->move_before.plugin = l->data;
                        gtk_widget_get_allocation(l->data, &alloc);
                        if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                            p->move_before.plugin_center = alloc.x + alloc.width / 2;
                        else
                            p->move_before.plugin_center = alloc.y + alloc.height / 2;
                    }
                    l = save;
                }
                if (l->next)
                {
                    l = l->next;
                    if (PANEL_IS_SPACE(l->data))
                    {
                        p->move_after.space = l->data;
                        expand = _move_space_get_expand(l->data);
                        if (expand)
                        {
                            gtk_widget_get_allocation(l->data, &alloc);
                            if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                                p->move_after.space_size = alloc.width;
                            else
                                p->move_after.space_size = alloc.height;
                        }
                        else
                            p->move_after.space_size = -1;
                        l = l->next;
                    }
                    if (l)
                    {
                        p->move_after.plugin = l->data;
                        gtk_widget_get_allocation(l->data, &alloc);
                        if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                            p->move_after.plugin_center = alloc.x + alloc.width / 2;
                        else
                            p->move_after.plugin_center = alloc.y + alloc.height / 2;
                    }
                }
                g_list_free(plugins);
                return TRUE;
            }
            g_list_free(plugins);
        }
    }
    else if (p->move_state == PANEL_MOVE_MOVING)
    {
        /* calculate current and new positions of moving widget center */
        {
            gdouble ex, ey;
            GdkSurface *surface = gdk_event_get_surface(event);
            int ox = 0, oy = 0;

            if (surface)
                _move_surface_origin(surface, &ox, &oy);
            gdk_event_get_position(event, &ex, &ey);
            x = ox;
            y = oy;
            gtk_widget_get_allocation(p->move_plugin, &alloc);
            if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
                old_pos = alloc.x + alloc.width / 2;
                new_pos = x + (int)ex - p->ax + p->move_diff;
                rtl = (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL);
            }
            else
            {
                old_pos = alloc.y + alloc.height / 2;
                new_pos = y + (int)ey - p->ay + p->move_diff;
                rtl = FALSE;
            }
        }
        /* actually move widget, changing spaces or swapping with others */
        if (new_pos > old_pos)
        {
            x = new_pos - old_pos;
            /* cursor is right/down */
            if (rtl)
                goto _split_or_swap_before;
            else
                goto _split_or_swap_after;
        }
        else if (new_pos < old_pos)
        {
            x = old_pos - new_pos;
            /* cursor is left/up */
            if (rtl)
            {
                rtl = FALSE; /* it now means before => after */
_split_or_swap_after:
                in = &p->move_before;
                out = &p->move_after;
            }
            else
            {
                rtl = TRUE; /* it now means after => before */
_split_or_swap_before:
                in = &p->move_after;
                out = &p->move_before;
            }
            /* g_debug("moving plugin by %d", new_pos - old_pos); */
            if (out->space)
            {
                /* split space and move plugin */
                if (in->space)
                {
                    /* between spaces */
                    if (out->space_size < 0)
                    {
                        /* fixed size, decrease out */
                        y = _panel_space_get_size(out->space) - x;
                        /* g_debug("next is fixed space of %d -> %d", x+y, y); */
                        if (y >= 2)
                        {
                            _panel_space_resize(out->space, y);
                            if (in->space_size > 0 &&
                                y > in->space_size - x)
                            {
                                /* fixed became bigger than expanded, let swap */
                                /* g_debug("swap 'expand' on spaces"); */
                                _move_space_set_expand(p, out->space, TRUE);
                                out->space_size = y;
                                _move_space_set_expand(p, in->space, FALSE);
                                _panel_space_resize(in->space, in->space_size + x);
                                in->space_size = -1;
                            }
                            /* in->space_size < 0 will be handled below */
                        }
                        else
                        {
                            /* remove empty space plugin */
                            /* g_debug("removing next space %p", out->space); */
                            luxepanel_remove_plugin(p->topgwin, out->space);
                            out->space = NULL;
                            y += x + gtk_box_get_spacing(GTK_BOX(p->box));
                            if (in->space_size >= 0)
                                /* correct size of expanded */
                                in->space_size += y;
                            else
                            {
                                /* both were fixed size - correct size of which left */
                                y += _panel_space_get_size(in->space);
                                _panel_space_resize(in->space, y);
                                /* g_debug("change prev size to %d", y); */
                            }
                            /* both spaces were handled so may return */
                            return TRUE;
                        }
                    }
                    else
                    {
                        if (out->space_size < x)
                            /* expandable size exhausted */
                            x = out->space_size;
                            //FIXME: if in->space_size >= 0 then remove out->space
                        out->space_size -= x;
                    }
                    if (in->space_size < 0)
                    {
                        /* fixed size, increase before */
                        y = _panel_space_get_size(in->space) + x;
                        _panel_space_resize(in->space, y);
                        /* g_debug("prev is fixed space of %d -> %d", y-x, y); */
                        if (out->space_size >= 0 &&
                            y > out->space_size)
                        {
                            /* g_debug("swap 'expand' on spaces"); */
                            /* fixed became bigger than expanded, let swap */
                            _move_space_set_expand(p, in->space, TRUE);
                            in->space_size = y;
                            if (out->space_size >= 2)
                            {
                                _move_space_set_expand(p, out->space, FALSE);
                                _panel_space_resize(out->space, out->space_size);
                                out->space_size = -1;
                            }
                            else
                            {
                                /* g_debug("removing next space"); */
                                /* remove empty space plugin */
                                luxepanel_remove_plugin(p->topgwin, out->space);
                                out->space = NULL;
                                in->space_size += out->space_size;
                                in->space_size += gtk_box_get_spacing(GTK_BOX(p->box));
                            }
                        }
                    }
                    else
                        in->space_size += x;
                    /* end of between spaces case */
                }
                else if (x >= 2)
                {
                    /* there is only space after */
                    /* ensure out->space size is at least x+2 */
                    if (out->space_size < 0)
                    {
                        y = _panel_space_get_size(out->space);
                        if (y - x < 2)
                            goto _swap_next_space;
                        /* decrement in->space size by x */
                        _panel_space_resize(out->space, y - x);
                        goto _add_prev_space;
                    }
                    else if (out->space_size - x < 2)
                    {
_swap_next_space:
                        /* too big change, just swap */
                        y = _move_box_index(p->box, p->move_plugin);
                        /* reposition space from next to prev */
                        in->space = out->space;
                        in->space_size = out->space_size;
                        out->space = NULL;
                        _move_box_set_position(p->box, in->space, y);
                        cfg = g_object_get_qdata(G_OBJECT(in->space),
                                                 luxepanel_plugin_qconf);
                        config_setting_move_elem(cfg, config_setting_get_parent(cfg),
                                                 y + 1);
                        /* g_debug("swapped with next space %p", in->space); */
                    }
                    else
                    {
                        out->space_size -= x;
_add_prev_space:
                        /* create PanelSpace of size x before plugin on move */
                        y = _move_box_index(p->box, p->move_plugin);
                        cfg = config_group_add_subgroup(config_root_setting(p->config),
                                                        "Plugin");
                        config_group_set_string(cfg, "type", "space");
                        in->space_size = -1;
                        if (rtl)
                            /* create space after moving one */
                            y++;
                            /* else create space before moving one, i.e. in place */
                        in->space = luxepanel_add_plugin(p->topgwin, "space", cfg, y);
                        config_setting_move_elem(cfg, config_setting_get_parent(cfg),
                                                 y + 1);
                        if (in->space == NULL)
                            //FIXME: is it ever possible?
                            config_setting_destroy(cfg);
                        else
                            _panel_space_resize(in->space, x);
                        /* g_debug("added space %d before plugin", x); */
                    }
                }
            }
            else if (out->plugin)
            {
                /* no space after, check if need swap */
                if (new_pos < old_pos)
                {
                    /* going down */
                    if (new_pos < out->plugin_center)
                    {
                        /* need swap */
                        gtk_widget_get_allocation(p->move_plugin, &alloc);
                        if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                            x = alloc.width;
                        else
                            x = alloc.height;
                        x += gtk_box_get_spacing(GTK_BOX(p->box));
                        goto _swap_next;
                    }
                }
                else if (new_pos > out->plugin_center)
                {
                    /* going up, need swap */
                    gtk_widget_get_allocation(p->move_plugin, &alloc);
                    if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                        x = -alloc.width;
                    else
                        x = -alloc.height;
                    x -= gtk_box_get_spacing(GTK_BOX(p->box));
_swap_next:
                    /* swap with next plugin and reload all data */
                    plugins = _move_box_children(p->box);
                    l = g_list_find(plugins, p->move_plugin);
                    y = g_list_position(plugins, l);
                    if (rtl)
                    {
                        /* going down in list */
                        g_assert(l && l->prev);
                        l = l->prev->prev;
                    }
                    else
                    {
                        /* going up in list */
                        g_assert(l && l->next);
                        l = l->next->next;
                    }
                    /* g_debug("swapping with next plugin %p", out->plugin); */
                    in->space = NULL;
                    in->plugin = out->plugin;
                    in->plugin_center = out->plugin_center + x;
                    /* swap next plugin with one being moved */
                    _move_box_set_position(p->box, in->plugin, y);
                    cfg = g_object_get_qdata(G_OBJECT(in->plugin),
                                             luxepanel_plugin_qconf);
                    config_setting_move_elem(cfg, config_setting_get_parent(cfg),
                                             y + 1);
                    /* check and set out->plugin and out->space */
                    if (l && PANEL_IS_SPACE(l->data))
                    {
                        out->space = l->data;
                        expand = _move_space_get_expand(l->data);
                        if (expand)
                        {
                            gtk_widget_get_allocation(l->data, &alloc);
                            if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                                out->space_size = alloc.width;
                            else
                                out->space_size = alloc.height;
                        }
                        else
                            out->space_size = -1;
                        if (rtl)
                            l = l->prev;
                        else
                            l = l->next;
                    }
                    else
                        out->space = NULL;
                    if (l)
                    {
                        out->plugin = l->data;
                        gtk_widget_get_allocation(l->data, &alloc);
                        if (p->orientation == GTK_ORIENTATION_HORIZONTAL)
                            out->plugin_center = alloc.x + alloc.width / 2;
                        else
                            out->plugin_center = alloc.y + alloc.height / 2;
                    }
                    else
                        out->plugin = NULL;
                    g_list_free(plugins);
                }
            }
        }
        return TRUE;
    }
    return FALSE;
}
