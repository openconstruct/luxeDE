/*
 * Copyright (C) 2009-2010 Marty Jack <martyj19@comcast.net>
 * Copyright (C) 2026 Jerry Howell
 *               2009-2010 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
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

#include <gtk/gtk.h>
#include <string.h>

#include "icon-grid.h"
#include "panel.h" /* for panel_get_height() */
#include "gtk-compat.h"

/* Properties */
enum {
  PROP_0,
  PROP_ORIENTATION,
  PROP_SPACING,
  PROP_CONSTRAIN_WIDTH,
  PROP_ASPECT_WIDTH
  //PROP_FILL_WIDTH
};

/* Representative of an icon grid.  This is a manager that packs widgets into a rectangular grid whose size adapts to conditions.
 * GTK4 port: GtkWidget subclass (GtkContainer is gone); child list is
 * maintained manually, layout happens in measure/size_allocate.  No
 * subwindows -- gestures/controllers attached to the widget itself
 * receive events across its whole allocation, including empty space. */
struct _PanelIconGrid
{
    GtkWidget widget;				/* Parent widget */
    GList * children;				/* List of icon grid elements */
    GtkOrientation orientation;			/* Desired orientation */
    gint child_width;				/* Desired child width */
    gint child_height;				/* Desired child height */
    guint spacing;				/* Desired spacing between grid elements */
    gint border;				/* Border width (GtkWidget has no border-width in GTK4) */
    gint target_dimension;			/* Desired dimension perpendicular to orientation */
    gboolean constrain_width : 1;		/* True if width should be constrained by allocated space */
    gboolean aspect_width : 1;			/* True if children should maintain aspect */
    gboolean fill_width : 1;			/* True if children should fill unused width */
    int rows;					/* Computed layout rows */
    int columns;				/* Computed layout columns */
    GtkWidget *dest_item;			/* Drag destination to draw focus */
    PanelIconGridDropPosition dest_pos;		/* Position to draw focus */
};

struct _PanelIconGridClass
{
    GtkWidgetClass parent_class;
};

static void icon_grid_element_check_requisition(PanelIconGrid *ig,
                                                GtkRequisition *requisition)
{
    if (ig->aspect_width && !ig->constrain_width &&
        requisition->width > 1 && requisition->height > 1)
    {
        /* calculate width from aspect */
        gdouble ratio = (gdouble)requisition->width / requisition->height;
        requisition->width = MAX(ig->child_height * ratio, ig->child_width);
    }
    else
    {
        requisition->width = ig->child_width;
    }
    requisition->height = ig->child_height;
}

/* Fetch natural size of a child and apply the grid element rules. */
static void panel_icon_grid_child_requisition(PanelIconGrid *ig,
                                              GtkWidget *child,
                                              GtkRequisition *requisition)
{
    gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1,
                       NULL, &requisition->width, NULL, NULL);
    gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, -1,
                       NULL, &requisition->height, NULL, NULL);
    icon_grid_element_check_requisition(ig, requisition);
}

static void check_for_recalc(PanelIconGrid *ig)
{
    GtkRoot *root = gtk_widget_get_root((GtkWidget *)ig);
    GtkWidget *toplevel = root != NULL ? GTK_WIDGET(root) : NULL;

    if (toplevel != NULL && LX_IS_PANEL(toplevel))
    {
        gint height = panel_get_height((LuxePanel *)toplevel);

        if (height != ig->target_dimension)
        {
            ig->target_dimension = height;
            gtk_widget_queue_resize((GtkWidget *)ig);
        }
    }
}

/* Establish the geometry of an icon grid. */
static void panel_icon_grid_calculate_size(PanelIconGrid *ig,
                                           GtkRequisition *requisition)
{
    GList *ige;
    int target_dimension = MAX(ig->target_dimension, 0);
    guint border = ig->border;
    guint target_borders = MAX(2 * border, ig->spacing);
    gint row = 0, w = 0;
    GtkRequisition child_requisition;

    requisition->width = 0;
    requisition->height = 0;
    ig->rows = 0;
    ig->columns = 0;
    if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        /* In horizontal orientation, fit as many rows into the available height as possible.
         * Then allocate as many columns as necessary.  Guard against zerodivides. */
        if ((ig->child_height + ig->spacing) != 0)
            ig->rows = (target_dimension + ig->spacing + border * 2) / (ig->child_height + ig->spacing);
        if (ig->rows == 0)
            ig->rows = 1;
        /* Count visible children and columns. */
        for (ige = ig->children; ige != NULL; ige = ige->next)
            if (gtk_widget_get_visible(ige->data))
            {
                panel_icon_grid_child_requisition(ig, ige->data, &child_requisition);
                if (row == 0)
                    ig->columns++;
                w = MAX(w, child_requisition.width);
                row++;
                if (row == ig->rows)
                {
                    if (requisition->width > 0)
                         requisition->width += ig->spacing;
                    requisition->width += w;
                    row = w = 0;
                }
            }
        if (w > 0)
        {
            if (requisition->width > 0)
                 requisition->width += ig->spacing;
            requisition->width += w;
        }
        if (requisition->width > 0)
            requisition->width += target_borders;
        if (ig->columns > 0)
            requisition->height = (ig->child_height + ig->spacing) * ig->rows - ig->spacing + 2 * border;
    }
    else
    {
        /* In vertical orientation, fit as many columns into the available width as possible.
         * Then allocate as many rows as necessary.  Guard against zerodivides. */
        if ((ig->child_width + ig->spacing) != 0)
            ig->columns = (target_dimension + ig->spacing + border * 2) / (ig->child_width + ig->spacing);
        if (ig->columns == 0)
            ig->columns = 1;
        /* Count visible children and rows. */
        for (ige = ig->children; ige != NULL; ige = ige->next)
            if (gtk_widget_get_visible(ige->data))
            {
                panel_icon_grid_child_requisition(ig, ige->data, &child_requisition);
                if (w > 0)
                {
                    w += ig->spacing;
                    if (w + child_requisition.width + (int)border > target_dimension)
                    {
                        w = 0;
                        ig->rows++;
                    }
                }
                w += child_requisition.width;
                requisition->width = MAX(requisition->width, w);
            }
        if (w > 0)
            ig->rows++;
        if (requisition->width > 0)
            requisition->width += 2 * border;
        if (ig->rows > 0)
            requisition->height = (ig->child_height + ig->spacing) * ig->rows - ig->spacing + target_borders;
    }
}

/* Establish the widget placement of an icon grid.  GTK4: allocations are
 * widget-relative, no subwindows. */
static void panel_icon_grid_size_allocate(GtkWidget *widget,
                                          int width, int height, int baseline)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);
    GtkRequisition req;
    GtkAllocation child_allocation;
    int child_width;
    int child_height;
    GtkTextDirection direction;
    guint border;
    guint x_border, y_border;
    int x_delta;
    guint next_coord;
    guint x, y;
    GList *ige;
    GtkWidget *child;

    border = ig->border;
    x_border = y_border = border;
    if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
        x_border = MAX(border, ig->spacing / 2);
    else
        y_border = MAX(border, ig->spacing / 2);
    child_allocation.x = 0;
    child_allocation.y = 0;
    child_allocation.width = MAX(width - 2 * border, 0);
    child_allocation.height = MAX(height - 2 * border, 0);

    /* Get and save the desired container geometry. */
    child_width = ig->child_width;
    child_height = ig->child_height;
    if (ig->orientation == GTK_ORIENTATION_HORIZONTAL && height > 1)
    {
        /* Don't allow children go out of the grid */
        if ((child_height + (int)border * 2) > height)
            child_height = MAX(1, height - 2 * border);
    }
    else if (ig->orientation == GTK_ORIENTATION_VERTICAL && width > 1)
    {
        /* Don't allow children go out of the grid */
        if ((child_width + (int)border * 2) > width)
            child_width = MAX(1, width - 2 * border);
    }

    /* Refresh rows/columns if the panel height changed. */
    check_for_recalc(ig);

    /* Get the constrained child geometry if the allocated geometry is insufficient.
     * All children are still the same size and share equally in the deficit. */
    if ((ig->columns != 0) && (ig->rows != 0) && (child_allocation.width > 0))
    {
        if (ig->constrain_width &&
            (x_delta = (child_allocation.width + ig->spacing) / ig->columns - ig->spacing) < child_width)
            /* Keep GtkButton's contents measurable even when every task
             * cannot fit. The grid clips the excess at its own edge. */
            child_width = MAX(48, x_delta);
        /* fill vertical space evenly in horisontal orientation */
        if (ig->orientation == GTK_ORIENTATION_HORIZONTAL &&
            (x_delta = (child_allocation.height + ig->spacing) / ig->rows - ig->spacing) > child_height)
            child_height = MAX(2, x_delta);
    }

    /* Initialize parameters to control repositioning each visible child. */
    direction = gtk_widget_get_direction(widget);
    x = (direction == GTK_TEXT_DIR_RTL) ? width - x_border : x_border;
    y = y_border;
    x_delta = 0;
    next_coord = border;

    /* Reposition each visible child. */
    for (ige = ig->children; ige != NULL; ige = ige->next)
    {
        child = ige->data;
        if (gtk_widget_get_visible(child))
        {
            /* Do necessary operations on the child. */
            panel_icon_grid_child_requisition(ig, child, &req);
            child_allocation.width = MIN(req.width, child_width);
            child_allocation.height = MIN(req.height, child_height);

            /* Check this grid position */
            if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
                y = next_coord;
                if (y + child_height > height - y_border && y > y_border)
                {
                    y = y_border;
                    if (direction == GTK_TEXT_DIR_RTL)
                        x -= (x_delta + ig->spacing);
                    else
                        x += (x_delta + ig->spacing);
                    x_delta = 0;
                }
                next_coord = y + child_height + ig->spacing;
                x_delta = MAX(x_delta, child_allocation.width);
            }
            else
            {
                x = next_coord;
                if (direction == GTK_TEXT_DIR_RTL)
                {
                    if (x < width - x_border && x - child_allocation.width < x_border)
                    {
                        x = width - x_border;
                        y += child_height + ig->spacing;
                    }
                    next_coord = x - child_allocation.width - ig->spacing;
                }
                else
                {
                    if (x + child_allocation.width > width - x_border && x > x_border)
                    {
                        x = x_border;
                        y += child_height + ig->spacing;
                    }
                    next_coord = x + child_allocation.width + ig->spacing;
                }
            }
            if (direction == GTK_TEXT_DIR_RTL)
                child_allocation.x = x - child_allocation.width;
            else
                child_allocation.x = x;
            if (req.height < child_height - 1)
                y += (child_height - req.height) / 2;
            child_allocation.y = y;

            gtk_widget_size_allocate(child, &child_allocation, -1);
        }
    }
}

/* Add an icon grid element. */
void panel_icon_grid_add(PanelIconGrid *ig, GtkWidget *widget)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));
    g_return_if_fail(GTK_IS_WIDGET(widget));
    g_return_if_fail(gtk_widget_get_parent(widget) == NULL);
    g_return_if_fail(widget != GTK_WIDGET(ig));

    /* Insert at the tail of the child list.  This keeps the graphics in the order they were added. */
    ig->children = g_list_append(ig->children, widget);

    /* Add the widget to the layout container. */
    gtk_widget_set_parent(widget, GTK_WIDGET(ig));
}

void panel_icon_grid_set_constrain_width(PanelIconGrid * ig, gboolean constrain_width)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    if ((!ig->constrain_width && !constrain_width) ||
        (ig->constrain_width && constrain_width))
        return;

    ig->constrain_width = !!constrain_width;
    gtk_widget_set_overflow(GTK_WIDGET(ig), constrain_width ? GTK_OVERFLOW_HIDDEN : GTK_OVERFLOW_VISIBLE);
    gtk_widget_queue_resize(GTK_WIDGET(ig));
}

void panel_icon_grid_set_aspect_width(PanelIconGrid * ig, gboolean aspect_width)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    if ((!ig->aspect_width && !aspect_width) || (ig->aspect_width && aspect_width))
        return;

    ig->aspect_width = !!aspect_width;
    gtk_widget_queue_resize(GTK_WIDGET(ig));
}

/* Remove an icon grid element. */
void panel_icon_grid_remove(PanelIconGrid *ig, GtkWidget *widget)
{
    GList *children;
    GtkWidget *child;

    g_return_if_fail(PANEL_IS_ICON_GRID(ig));
    g_return_if_fail(GTK_IS_WIDGET(widget));

    children = ig->children;
    while (children)
    {
        child = children->data;
        if (widget == child)
        {
            gboolean was_visible = gtk_widget_get_visible(widget);

            /* The child is found.  Remove from child list and layout container. */
            gtk_widget_unparent (widget);
            if (ig->dest_item == widget)
                ig->dest_item = NULL;
            ig->children = g_list_remove_link(ig->children, children);
            g_list_free(children);

            /* Do a relayout if needed. */
            if (was_visible)
                gtk_widget_queue_resize(GTK_WIDGET(ig));
            break;
        }
        children = children->next;
    }
}

/* Get the index of an icon grid element. */
gint panel_icon_grid_get_child_position(PanelIconGrid * ig, GtkWidget * child)
{
    g_return_val_if_fail(PANEL_IS_ICON_GRID(ig), -1);

    return g_list_index(ig->children, child);
}

/* Reorder an icon grid element. */
void panel_icon_grid_reorder_child(PanelIconGrid * ig, GtkWidget * child, gint position)
{
    GList *old_link;
    GList *new_link;
    gint old_position;

    g_return_if_fail(PANEL_IS_ICON_GRID(ig));
    g_return_if_fail(GTK_IS_WIDGET(child));

    old_link = ig->children;
    old_position = 0;
    while (old_link)
    {
        if (old_link->data == child)
            break;
        old_link = old_link->next;
        old_position++;
    }

    g_return_if_fail(old_link != NULL);

    if (position == old_position)
        return;

    /* Remove the child from its current position. */
    ig->children = g_list_delete_link(ig->children, old_link);
    if (position < 0)
        new_link = NULL;
    else
        new_link = g_list_nth(ig->children, position);

    /* If the child was found, insert it at the new position. */
    ig->children = g_list_insert_before(ig->children, new_link, child);

    /* Do a relayout. */
    if (gtk_widget_get_visible(child) && gtk_widget_get_visible(GTK_WIDGET(ig)))
        gtk_widget_queue_resize(child);
}

guint panel_icon_grid_get_n_children(PanelIconGrid * ig)
{
    g_return_val_if_fail(PANEL_IS_ICON_GRID(ig), 0);

    return g_list_length(ig->children);
}

/* Change the geometry of an icon grid. */
void panel_icon_grid_set_geometry(PanelIconGrid * ig,
    GtkOrientation orientation, gint child_width, gint child_height, gint spacing, gint border, gint target_dimension)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    ig->border = MAX(border, 0);
    child_width = CLAMP(child_width, 1, 1000000);
    child_height = CLAMP(child_height, 1, 1000000);
    target_dimension = CLAMP(target_dimension, 0, 1000000);

    if (ig->orientation == orientation && ig->child_width == child_width &&
            ig->child_height == child_height && (gint)ig->spacing == spacing &&
            ig->target_dimension == target_dimension)
        return;

    ig->orientation = orientation;
    ig->child_width = child_width;
    ig->child_height = child_height;
    ig->spacing = MAX(spacing, 1);
    ig->target_dimension = MAX(target_dimension, 0);
    gtk_widget_queue_resize(GTK_WIDGET(ig));
}

/* get position for coordinates, return FALSE if it's outside of icon grid */
gboolean panel_icon_grid_get_dest_at_pos(PanelIconGrid * ig, gint x, gint y,
                                         GtkWidget ** child, PanelIconGridDropPosition * pos)
{
    GtkAllocation allocation;
    PanelIconGridDropPosition drop_pos = PANEL_ICON_GRID_DROP_INTO;
    GtkWidget *widget;
    GList *ige;
    gboolean rtl, upper = TRUE;

    g_return_val_if_fail(PANEL_IS_ICON_GRID(ig), FALSE);

    widget = GTK_WIDGET(ig);
    if (!gtk_widget_get_realized(widget))
        return FALSE;

    rtl = (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL);
    if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        for (ige = ig->children; ige != NULL; ige = ige->next)
        {
            gtk_widget_get_allocation(ige->data, &allocation);
            if (x < allocation.x)
            {
                if (!rtl)
                {
                    /* reached next column */
                    drop_pos = PANEL_ICON_GRID_DROP_LEFT_BEFORE;
                    break;
                }
            }
            else if (x < (allocation.x + allocation.width))
            {
                /* within this column */
                if (y < allocation.y)
                {
                    /* reached next row */
                    if (upper)
                        drop_pos = rtl ? PANEL_ICON_GRID_DROP_RIGHT_BEFORE : PANEL_ICON_GRID_DROP_LEFT_BEFORE;
                    else
                        drop_pos = PANEL_ICON_GRID_DROP_ABOVE;
                    break;
                }
                else if (y < (allocation.y + allocation.height))
                {
                    /* within this row */
                    drop_pos = PANEL_ICON_GRID_DROP_INTO;
                    break;
                }
                upper = FALSE;
            }
            else if (rtl)
            {
                /* reached next column */
                drop_pos = PANEL_ICON_GRID_DROP_RIGHT_BEFORE;
                break;
            }
        }
    }
    else
    {
        for (ige = ig->children; ige != NULL; ige = ige->next)
        {
            gtk_widget_get_allocation(ige->data, &allocation);
            if (y < allocation.y)
            {
                    /* reached next row */
                    drop_pos = PANEL_ICON_GRID_DROP_ABOVE;
                    break;
            }
            else if (y < (allocation.y + allocation.height))
            {
                /* within this row */
                if (x < allocation.x)
                {
                    if (!rtl)
                    {
                        /* reached next column */
                        if (upper)
                            drop_pos = PANEL_ICON_GRID_DROP_ABOVE;
                        else
                            drop_pos = PANEL_ICON_GRID_DROP_LEFT_BEFORE;
                        break;
                    }
                }
                else if (x < (allocation.x + allocation.width))
                {
                    /* within this column */
                    drop_pos = PANEL_ICON_GRID_DROP_INTO;
                    break;
                }
                else if (rtl)
                {
                    /* reached next column */
                    if (upper)
                        drop_pos = PANEL_ICON_GRID_DROP_ABOVE;
                    else
                        drop_pos = PANEL_ICON_GRID_DROP_RIGHT_BEFORE;
                    break;
                }
                upper = FALSE;
            }
        }
    }
    if (ige == NULL)
    {
        /* not within allocated space */
        ige = g_list_last(ig->children);
        if (ig->orientation != GTK_ORIENTATION_HORIZONTAL)
            drop_pos = PANEL_ICON_GRID_DROP_BELOW;
        else if (rtl)
            drop_pos = PANEL_ICON_GRID_DROP_LEFT_AFTER;
        else
            drop_pos = PANEL_ICON_GRID_DROP_RIGHT_AFTER;
    }
    if (child)
        *child = (ige == NULL) ? NULL : ige->data;
    if (pos)
        *pos = drop_pos;
    return TRUE;
}

static void panel_icon_grid_queue_draw_child(PanelIconGrid * ig, GtkWidget * child)
{
    /* GTK4 has no partial-invalidate API; a full queue_draw is cheap
     * enough for the rare drag-indicator updates. */
    gtk_widget_queue_draw(GTK_WIDGET(ig));
}

/* sets data and renders widget appropriately, need be drawable and realized */
void panel_icon_grid_set_drag_dest(PanelIconGrid * ig, GtkWidget * child,
                                   PanelIconGridDropPosition pos)
{
    GtkWidget *current_dest;

    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    // reset previous state
    current_dest = ig->dest_item;
    if (current_dest)
    {
        ig->dest_item = NULL;
        panel_icon_grid_queue_draw_child(ig, current_dest);
    }

    // need a special support for empty grid?
    ig->dest_pos = pos;

    // remember new state
    if (child && g_list_find(ig->children, child))
    {
        ig->dest_item = child;
        panel_icon_grid_queue_draw_child(ig, child);
    }
}

PanelIconGridDropPosition panel_icon_grid_get_drag_dest(PanelIconGrid * ig,
                                                        GtkWidget ** child)
{
    g_return_val_if_fail(PANEL_IS_ICON_GRID(ig), 0);

    if (child)
        *child = ig->dest_item;
    return ig->dest_pos;
}

G_DEFINE_TYPE_WITH_CODE(PanelIconGrid, panel_icon_grid, GTK_TYPE_WIDGET,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_ORIENTABLE, NULL));

static void panel_icon_grid_set_property(GObject *object, guint prop_id,
                                         const GValue *value, GParamSpec *pspec)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(object);
    guint spacing;
    GtkOrientation orientation;

    switch (prop_id)
    {
    case PROP_ORIENTATION:
        orientation = g_value_get_enum(value);
        if (orientation != ig->orientation)
        {
            ig->orientation = orientation;
            gtk_widget_queue_resize(GTK_WIDGET(ig));
        }
        break;
    case PROP_SPACING:
        spacing = g_value_get_uint(value);
        if (spacing != ig->spacing)
        {
            ig->spacing = spacing;
            g_object_notify(object, "spacing");
            gtk_widget_queue_resize(GTK_WIDGET(ig));
        }
        break;
    case PROP_CONSTRAIN_WIDTH:
        panel_icon_grid_set_constrain_width(ig, g_value_get_boolean(value));
        break;
    case PROP_ASPECT_WIDTH:
        panel_icon_grid_set_aspect_width(ig, g_value_get_boolean(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void panel_icon_grid_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(object);

    switch (prop_id)
    {
    case PROP_ORIENTATION:
        g_value_set_enum(value, ig->orientation);
        break;
    case PROP_SPACING:
        g_value_set_uint(value, ig->spacing);
        break;
    case PROP_CONSTRAIN_WIDTH:
        g_value_set_boolean(value, ig->constrain_width);
        break;
    case PROP_ASPECT_WIDTH:
        g_value_set_boolean(value, ig->aspect_width);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void panel_icon_grid_measure(GtkWidget *widget,
                                    GtkOrientation orientation, int for_size,
                                    int *minimum, int *natural,
                                    int *minimum_baseline, int *natural_baseline)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);
    GtkRequisition requisition;

    if (minimum_baseline)
        *minimum_baseline = -1;
    if (natural_baseline)
        *natural_baseline = -1;

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        if (ig->orientation == GTK_ORIENTATION_VERTICAL)
        {
            if (minimum)
                *minimum = MIN(ig->target_dimension, ig->child_width);
            if (natural)
                *natural = ig->target_dimension;
            return;
        }
        panel_icon_grid_calculate_size(ig, &requisition);
        /* The taskbar is the flexible part of the panel.  Its natural width
         * follows the window count, but its minimum must leave room for the
         * clock, tray and other plugins when many windows are open. */
        if (minimum)
            *minimum = ig->constrain_width ? 0 : requisition.width;
        if (natural)
            *natural = requisition.width;
    }
    else
    {
        if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
        {
            if (minimum)
                *minimum = ig->target_dimension;
            if (natural)
                *natural = ig->target_dimension;
        }
        else
        {
            panel_icon_grid_calculate_size(ig, &requisition);
            if (minimum)
                *minimum = requisition.height;
            if (natural)
                *natural = requisition.height;
        }
    }
}

/* Drag destination focus indicator; widget background is CSS territory
 * in GTK4 (panel transparency handled by bg.c snapshot). */
static void panel_icon_grid_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);

    if (ig->dest_item)
    {
        GtkAllocation allocation;
        int x, y, width, height;
        graphene_rect_t bounds;
        cairo_t *cr;
        GtkStyleContext *context;

        gtk_widget_get_allocation(ig->dest_item, &allocation);
        switch(ig->dest_pos)
        {
        case PANEL_ICON_GRID_DROP_LEFT_AFTER:
        case PANEL_ICON_GRID_DROP_LEFT_BEFORE:
            x = allocation.x - 2;
            width = 2;
            y = allocation.y;
            height = allocation.height;
            break;
        case PANEL_ICON_GRID_DROP_RIGHT_AFTER:
        case PANEL_ICON_GRID_DROP_RIGHT_BEFORE:
            x = allocation.x + allocation.width;
            width = 2;
            y = allocation.y;
            height = allocation.height;
            break;
        case PANEL_ICON_GRID_DROP_BELOW:
            x = allocation.x;
            width = allocation.width;
            y = allocation.y + allocation.height;
            height = 2;
            break;
        case PANEL_ICON_GRID_DROP_ABOVE:
            x = allocation.x;
            width = allocation.width;
            y = allocation.y - 2;
            height = 2;
            break;
        case PANEL_ICON_GRID_DROP_INTO:
        default:
            x = allocation.x - 1;
            width = allocation.width + 2;
            y = allocation.y - 1;
            height = allocation.height + 2;
        }

        graphene_rect_init(&bounds, 0, 0,
                           gtk_widget_get_allocated_width(widget),
                           gtk_widget_get_allocated_height(widget));
        cr = gtk_snapshot_append_cairo(snapshot, &bounds);
        context = gtk_widget_get_style_context(widget);
        gtk_style_context_set_state(context, gtk_widget_get_state_flags(widget));
        gtk_render_focus(context, cr, x, y, width, height);
        cairo_destroy(cr);
    }

    GTK_WIDGET_CLASS(panel_icon_grid_parent_class)->snapshot(widget, snapshot);
}

static void panel_icon_grid_dispose(GObject *object)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(object);

    if (ig->children != NULL)
    {
        GList *ige;
        for (ige = ig->children; ige != NULL; ige = ige->next)
            gtk_widget_unparent(GTK_WIDGET(ige->data));
        g_list_free(ig->children);
        ig->children = NULL;
    }

    G_OBJECT_CLASS(panel_icon_grid_parent_class)->dispose(object);
}

static void panel_icon_grid_class_init(PanelIconGridClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->set_property = panel_icon_grid_set_property;
    object_class->get_property = panel_icon_grid_get_property;
    object_class->dispose = panel_icon_grid_dispose;

    widget_class->measure = panel_icon_grid_measure;
    widget_class->size_allocate = panel_icon_grid_size_allocate;
    widget_class->snapshot = panel_icon_grid_snapshot;

    g_object_class_override_property(object_class,
                                     PROP_ORIENTATION,
                                     "orientation");
    g_object_class_install_property(object_class,
                                    PROP_SPACING,
                                    g_param_spec_uint("spacing",
                                                      "Spacing",
                                                      "The amount of space between children",
                                                      1,
                                                      G_MAXINT,
                                                      1,
                                                      G_PARAM_READWRITE));
    g_object_class_install_property(object_class,
                                    PROP_CONSTRAIN_WIDTH,
                                    g_param_spec_boolean("constrain-width",
                                                         "Constrain width",
                                                         "Whether to constrain width by allocated space",
                                                         FALSE, G_PARAM_READWRITE));
    g_object_class_install_property(object_class,
                                    PROP_ASPECT_WIDTH,
                                    g_param_spec_boolean("aspect-width",
                                                         "Maintain children aspect",
                                                         "Whether to set children width to maintain their aspect",
                                                         FALSE, G_PARAM_READWRITE));
}

static void panel_icon_grid_init(PanelIconGrid *ig)
{
    ig->orientation = GTK_ORIENTATION_HORIZONTAL;
}

/* Establish an icon grid in a specified container widget.
 * The icon grid manages the contents of the container.
 * The orientation, geometry of the elements, and spacing can be varied.  All elements are the same size. */
GtkWidget * panel_icon_grid_new(
    GtkOrientation orientation, gint child_width, gint child_height, gint spacing, gint border, gint target_dimension)
{
    /* Create a structure representing the icon grid and collect the parameters. */
    PanelIconGrid * ig = g_object_new(PANEL_TYPE_ICON_GRID,
                                      "orientation", orientation,
                                      "spacing", MAX(spacing, 1),
                                      NULL);

    ig->child_width = CLAMP(child_width, 1, 1000000);
    ig->child_height = CLAMP(child_height, 1, 1000000);
    ig->target_dimension = CLAMP(target_dimension, 0, 1000000);
    ig->border = CLAMP(border, 0, 1000000);

    return (GtkWidget *)ig;
}

