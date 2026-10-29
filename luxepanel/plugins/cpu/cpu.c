/*
 * CPU usage plugin to lxpanel
 *
 * Copyright (C) 2006-2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2009 Marty Jack <martyj19@comcast.net>
 *               2009 Jürgen Hötzel <juergen@archlinux.org>
 *               2012 Rafał Mużyło <galtgendo@gmail.com>
 *               2012-2013 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2013 Marko Rauhamaa <marko@pacujo.net>
 *               2014 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2015 Rafał Mużyło <galtgendo@gmail.com>
 *
 * This file is a part of LXPanel project.
 *
 * Copyright (C) 2004 by Alexandre Pereira da Silva <alexandre.pereira@poli.usp.br>
 * Copyright (C) 2026 Jerry Howell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
/*A little bug fixed by Mykola <mykola@2ka.mipt.ru>:) */

#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include "plugin.h"
#include "gtk-compat.h"

#define BORDER_SIZE 2
#define PANEL_HEIGHT_DEFAULT 26 /* from panel defaults */

/* #include "../../dbg.h" */

typedef unsigned long long CPUTick;		/* Value from /proc/stat */
typedef float CPUSample;			/* Saved CPU utilization value as 0.0..1.0 */

struct cpu_stat {
    CPUTick u, n, s, i;				/* User, nice, system, idle */
};

/* Private context for CPU plugin. */
typedef struct {
    GdkRGBA foreground_color;			/* Foreground color for drawing area */
    GtkWidget * da;				/* Drawing area */
    GtkWidget * plugin;
    cairo_surface_t * pixmap;				/* Pixmap to be drawn on drawing area */

    guint timer;				/* Timer for periodic update */
    CPUSample * stats_cpu;			/* Ring buffer of CPU utilization values */
    unsigned int ring_cursor;			/* Cursor for ring buffer */
    guint pixmap_width;				/* Width of drawing area pixmap; also size of ring buffer; does not include border size */
    guint pixmap_height;			/* Height of drawing area pixmap; does not include border size */
    struct cpu_stat previous_cpu_stat;		/* Previous value of cpu_stat */
    gboolean has_previous_stat;
} CPUPlugin;

static void redraw_pixmap(CPUPlugin * c);
static gboolean cpu_update(CPUPlugin * c);
static void resize_and_redraw(CPUPlugin * c, guint new_pixmap_width, guint new_pixmap_height);
static void draw_func(GtkDrawingArea * da, cairo_t * cr, int width, int height, CPUPlugin * c);

static void cpu_destructor(gpointer user_data);

/* Redraw after timer callback or resize. */
static void redraw_pixmap(CPUPlugin * c)
{
    cairo_t *cr;

    if (c == NULL || c->pixmap == NULL || c->stats_cpu == NULL ||
        c->pixmap_width == 0 || c->pixmap_height == 0)
        return;
    cr = cairo_create(c->pixmap);
    cairo_set_line_width (cr, 1.0);
    /* Erase pixmap. */
    cairo_rectangle(cr, 0, 0, c->pixmap_width, c->pixmap_height);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_fill(cr);

    /* Recompute pixmap. */
    unsigned int i;
    unsigned int drawing_cursor = c->ring_cursor;
    gdk_cairo_set_source_rgba(cr, &c->foreground_color);
    for (i = 0; i < c->pixmap_width; i++)
    {
        /* Draw one bar of the CPU usage graph. */
        if (c->stats_cpu[drawing_cursor] != 0.0)
        {
            cairo_move_to(cr, i + 0.5, c->pixmap_height);
            cairo_line_to(cr, i + 0.5, c->pixmap_height - c->stats_cpu[drawing_cursor] * c->pixmap_height);
            cairo_stroke(cr);
        }

        /* Increment and wrap drawing cursor. */
        drawing_cursor += 1;
        if (drawing_cursor >= c->pixmap_width)
            drawing_cursor = 0;
    }

    /* check_cairo_status(cr); */
    cairo_destroy(cr);

    /* Redraw pixmap. */
    gtk_widget_queue_draw(c->da);
}

/* Periodic timer callback. */
static gboolean cpu_update(CPUPlugin * c)
{
    if (c == NULL)
        return G_SOURCE_REMOVE;
    if ((c->stats_cpu != NULL) && (c->pixmap != NULL))
    {
        /* Open statistics file and scan out CPU usage. */
        struct cpu_stat cpu;
        FILE * stat = fopen("/proc/stat", "r");
        if (stat == NULL)
            return TRUE;
        int fscanf_result = fscanf(stat, "cpu %llu %llu %llu %llu", &cpu.u, &cpu.n, &cpu.s, &cpu.i);
        fclose(stat);

        /* Ensure that fscanf succeeded. */
        if (fscanf_result == 4)
        {
            if (!c->has_previous_stat)
            {
                c->previous_cpu_stat = cpu;
                c->has_previous_stat = TRUE;
            }
            else if (cpu.u >= c->previous_cpu_stat.u &&
                     cpu.n >= c->previous_cpu_stat.n &&
                     cpu.s >= c->previous_cpu_stat.s &&
                     cpu.i >= c->previous_cpu_stat.i)
            {
                struct cpu_stat cpu_delta;
                cpu_delta.u = cpu.u - c->previous_cpu_stat.u;
                cpu_delta.n = cpu.n - c->previous_cpu_stat.n;
                cpu_delta.s = cpu.s - c->previous_cpu_stat.s;
                cpu_delta.i = cpu.i - c->previous_cpu_stat.i;
                CPUTick total = cpu_delta.u + cpu_delta.n + cpu_delta.s + cpu_delta.i;

                if (total != 0)
                {
                    CPUTick busy = cpu_delta.u + cpu_delta.n + cpu_delta.s;
                    c->stats_cpu[c->ring_cursor] = (CPUSample)busy / (CPUSample)total;
                    c->ring_cursor += 1;
                    if (c->ring_cursor >= c->pixmap_width)
                        c->ring_cursor = 0;
                    redraw_pixmap(c);
                }
            }
            c->previous_cpu_stat = cpu;
        }
    }
    return TRUE;
}

static gboolean cpu_update_widget(GtkWidget *plugin)
{
    CPUPlugin *c = plugin != NULL ? luxepanel_plugin_get_data(plugin) : NULL;
    return c != NULL ? cpu_update(c) : G_SOURCE_REMOVE;
}

/* reallocate the pixmap (and stats buffer) at the new size */
static void resize_and_redraw(CPUPlugin * c, guint new_pixmap_width, guint new_pixmap_height)
{
    if (c == NULL || new_pixmap_width == 0 || new_pixmap_height == 0)
        return;
    new_pixmap_width = CLAMP(new_pixmap_width, 1, 4096);
    new_pixmap_height = CLAMP(new_pixmap_height, 1, 4096);
    if ((new_pixmap_width > 0) && (new_pixmap_height > 0))
    {
        /* If statistics buffer does not exist or it changed size, reallocate and preserve existing data. */
        if ((c->stats_cpu == NULL) || (new_pixmap_width != c->pixmap_width))
        {
            CPUSample * new_stats_cpu = g_new0(typeof(*c->stats_cpu), new_pixmap_width);
            if (c->stats_cpu != NULL)
            {
                if (new_pixmap_width > c->pixmap_width)
                {
                    /* New allocation is larger.
                     * Introduce new "oldest" samples of zero following the cursor. */
                    memcpy(&new_stats_cpu[0],
                        &c->stats_cpu[0], c->ring_cursor * sizeof(CPUSample));
                    memcpy(&new_stats_cpu[new_pixmap_width - c->pixmap_width + c->ring_cursor],
                        &c->stats_cpu[c->ring_cursor], (c->pixmap_width - c->ring_cursor) * sizeof(CPUSample));
                }
                else if (c->ring_cursor <= new_pixmap_width)
                {
                    /* New allocation is smaller, but still larger than the ring buffer cursor.
                     * Discard the oldest samples following the cursor. */
                    memcpy(&new_stats_cpu[0],
                        &c->stats_cpu[0], c->ring_cursor * sizeof(CPUSample));
                    memcpy(&new_stats_cpu[c->ring_cursor],
                        &c->stats_cpu[c->pixmap_width - new_pixmap_width + c->ring_cursor], (new_pixmap_width - c->ring_cursor) * sizeof(CPUSample));
                }
                else
                {
                    /* New allocation is smaller, and also smaller than the ring buffer cursor.
                     * Discard all oldest samples following the ring buffer cursor and additional samples at the beginning of the buffer. */
                    memcpy(&new_stats_cpu[0],
                        &c->stats_cpu[c->ring_cursor - new_pixmap_width], new_pixmap_width * sizeof(CPUSample));
                    c->ring_cursor = 0;
                }
                g_free(c->stats_cpu);
            }
            c->stats_cpu = new_stats_cpu;
        }

        /* Allocate or reallocate pixmap. */
        c->pixmap_width = new_pixmap_width;
        c->pixmap_height = new_pixmap_height;
        if (c->pixmap)
            cairo_surface_destroy(c->pixmap);
        c->pixmap = cairo_image_surface_create(CAIRO_FORMAT_RGB24, c->pixmap_width, c->pixmap_height);
        /* check_cairo_surface_status(&c->pixmap); */

        /* Redraw pixmap at the new size. */
        redraw_pixmap(c);
    }
}

/* GTK4 draw func: resize to current size, then paint the pixmap */
static void draw_func(GtkDrawingArea * da, cairo_t * cr, int width, int height, CPUPlugin * c)
{
    guint new_pixmap_width = MAX(width - BORDER_SIZE * 2, 0);
    guint new_pixmap_height = MAX(height - BORDER_SIZE * 2, 0);

    if ((c->pixmap == NULL) ||
        (new_pixmap_width != c->pixmap_width) || (new_pixmap_height != c->pixmap_height))
    {
        if ((new_pixmap_width > 0) && (new_pixmap_height > 0))
            resize_and_redraw(c, new_pixmap_width, new_pixmap_height);
        else if (c->pixmap != NULL)
        {
            cairo_surface_destroy(c->pixmap);
            c->pixmap = NULL;
            c->pixmap_width = 0;
            c->pixmap_height = 0;
        }
    }
    if (c->pixmap != NULL)
    {
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_source_surface(cr, c->pixmap, BORDER_SIZE, BORDER_SIZE);
        cairo_paint(cr);
    }
}

/* Plugin constructor. */
static GtkWidget *cpu_constructor(LuxePanel *panel, config_setting_t *settings)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    CPUPlugin * c = g_new0(CPUPlugin, 1);
    GtkWidget * p;

    /* Allocate top level widget and set into Plugin widget pointer. */
    p = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    luxepanel_plugin_set_data(p, c, cpu_destructor);
    c->plugin = p;

    /* Allocate drawing area as a child of top level widget. */
    c->da = gtk_drawing_area_new();
    gtk_widget_set_size_request(c->da, 40, PANEL_HEIGHT_DEFAULT);
    gtk_box_append(GTK_BOX(p), c->da);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->da),
                                   (GtkDrawingAreaDrawFunc) draw_func, c, NULL);

    /* Clone a graphics context and set "green" as its foreground color.
     * We will use this to draw the graph. */
    gdk_rgba_parse(&c->foreground_color, "green");

    /* Connect a timer to refresh the statistics.  2 s keeps /proc/stat
     * sampling cheap on a 24/7 panel; the sparkline drawing below mirrors
     * monitors.c but is intentionally not shared (separate loadable
     * modules with no common library -- see monitors.c). */
    c->timer = g_timeout_add_full(G_PRIORITY_DEFAULT, 2000,
                                  (GSourceFunc)cpu_update_widget,
                                  g_object_ref(p), g_object_unref);
    return p;
}

/* Plugin destructor. */
static void cpu_destructor(gpointer user_data)
{
    CPUPlugin * c = (CPUPlugin *)user_data;

    /* Disconnect the timer. */
    if (c->timer != 0)
    {
        g_source_remove(c->timer);
        c->timer = 0;
    }

    /* Deallocate memory. */
    if (c->pixmap != NULL)
        cairo_surface_destroy(c->pixmap);
    g_free(c->stats_cpu);
    g_free(c);
}

FM_DEFINE_MODULE(luxepanel_gtk, cpu)

/* Plugin descriptor. */
LuxePanelPluginInit fm_module_init_luxepanel_gtk = {
    .name = N_("CPU Usage Monitor"),
    .description = N_("Display CPU usage"),
    .new_instance = cpu_constructor,
};
