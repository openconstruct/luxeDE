/**
 * Plugin for the lxpanel.
 *
 * Displays several monitors in the panel.
 *
 * A lot of code in this plugin comes from the CPU plugin (that only displays a
 * CPU monitor), that is distributed under the following terms :
 *
 * Copyright (C) 2010 Cyril Roelandt <steap@users.sourceforge.net>
 *               2012-2014 Henry Gebhardt <hsggebhardt@googlemail.com>
 *               2012 Rafał Mużyło <galtgendo@gmail.com>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2015 Rafał Mużyło <galtgendo@gmail.com>
 *               2018 Mamoru TASAKA <mtasaka@fedoraproject.org>
 *               2019 Carles Pina i Estany <carles@pina.cat>
 *
 * <terms>
 * Copyright (c) 2008-2014 LxDE Developers, see the file AUTHORS for details.
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
 * </terms>
 *
 */

/*
 * HOWTO : Add your own monitor for the resource "foo".
 *
 * 1) Write the foo_update() function, that fills in the stats.
 * 2) Write the foo_tooltip_update() function, that updates your tooltip. This
 *    is optional, but recommended.
 * 3) Add a #define FOO_POSITION, and increment N_MONITORS.
 * 4) Add :
 *     - the default color of your plugin ("default_colors" table)
 *     - the update function ("update_functions" table)
 *     - the tooltip update function ("tooltip_update" table)
 * 5) Configuration :
 *     - edit the monitors_config() function so that a "Display FOO usage"
 *     checkbox appears in the prefs dialog.
 *     - edit the monitors_save() function so that a "DisplayFOO" string appears
 *     in the config file ("~/.config/luxepanel/<profile>/config")
 *     - edit the monitors_config() function so that a "FOO color" entry appears
 *       in the prefs dialog.
 *     - edit the monitors_save() function so that a "FOOColor" string appears
 *       in the config file.
 *     - edit the monitors_constructor() function so that options are correctly
 *     aplied. Adding something like :
 *
 *     else if (g_ascii_strcasecmp(s.t[0], "DisplayFOO") == 0)
 *         mp->displayed_monitors[FOO_POSITION] = atoi(s.t[1])
 *     else if (g_ascii_strcasecmp(s.t[0], "FOOColor") == 0)
 *         colors[FOO_POSITION] = g_strndup(s.t[1], COLOR_SIZE-1);
 *
 *     should be enough.
 * 6) Enjoy.
 */

/*
 * FIXME : known BUGS :
 *     - when removing a monitor and re-adding it, it is drawn with a white
 *     border of BORDER_SIZE pixels around it.
 */

#include <stdlib.h>
#include <glib/gi18n.h>
#include <errno.h>
#include <libfm/fm.h>

#include "plugin.h"

#include "dbg.h"

#define PLUGIN_NAME      "MonitorsPlugin"
#define BORDER_SIZE      2                  /* Pixels               */
#define DEFAULT_WIDTH    40                 /* Pixels               */
/* 2 s: /proc polling has no change signal, so sample slowly on a 24/7
 * panel.  The sparkline drawing below mirrors cpu.c but is intentionally
 * not shared (separate loadable modules with no common library). */
#define UPDATE_PERIOD    2                  /* Seconds              */
#define COLOR_SIZE       8                  /* In chars : #xxxxxx\0 */

#ifndef ENTER
#define ENTER fprintf(stderr, "Entering %s\n", __func__);
#endif

/*
 * Stats are stored in a circular buffer.
 * Newest values are on the left of the ring cursor.
 * Oldest values are on the right of the ring cursor.
 */
typedef float stats_set;

typedef unsigned long long CPUTick;
struct cpu_stat {
    CPUTick u, n, s, i;
};

struct Monitor {
    GdkRGBA      foreground_color;  /* Foreground color for drawing area      */
    GtkWidget    *da;               /* Drawing area                           */
    cairo_surface_t    *pixmap;     /* Pixmap to be drawn on drawing area     */
    gint         pixmap_width;      /* Width and size of the buffer           */
    gint         pixmap_height;     /* Does not include border size           */
    stats_set    *stats;            /* Circular buffer of values              */
    stats_set    total;             /* Maximum possible value, as in mem_total*/
    gint         ring_cursor;       /* Cursor for ring/circular buffer        */
    gchar        *color;            /* Color of the graph                     */
    gboolean     (*update) (struct Monitor *); /* Update function             */
    void         (*update_tooltip) (struct Monitor *);
    gboolean     has_previous_cpu;
    struct cpu_stat previous_cpu_stat;
};

typedef struct Monitor Monitor;
typedef gboolean (*update_func) (Monitor *);
typedef void (*tooltip_update_func) (Monitor *);

/*
 * Position of our monitors : monitor 0 will always be on the left of the
 * plugin, monitor 1 on the right of monitor 0 (or on the left of the plugin if
 * monitor 0 is not displayed), etc.
 */
#define CPU_POSITION    0
#define MEM_POSITION    1
#define N_MONITORS      2

/* Our plugin */
typedef struct {
    LuxePanel *panel;
    GtkWidget *plugin;
    config_setting_t *settings;
    Monitor  *monitors[N_MONITORS];          /* Monitors                      */
    int      displayed_monitors[N_MONITORS]; /* Booleans                      */
    char     *action;                        /* What to do on click           */
    guint    timer;                          /* Timer for regular updates     */
} MonitorsPlugin;

/*
 * Prototypes
 */
static void monitor_set_foreground_color(MonitorsPlugin *, Monitor *, const gchar *);
static void monitor_resize(Monitor * m, int new_pixmap_width, int new_pixmap_height);

/* CPU Monitor */
static gboolean cpu_update(Monitor *);
static void     cpu_tooltip_update (Monitor *m);

/* RAM Monitor */
static gboolean mem_update(Monitor *);
static void     mem_tooltip_update (Monitor *m);

static void draw_func(GtkDrawingArea * da, cairo_t * cr, int width, int height, Monitor * m);
static void redraw_pixmap (Monitor *m);

/* Monitors functions */
static void monitors_destructor(gpointer);
static gboolean monitors_apply_config(gpointer);

/******************************************************************************
 *                              Monitor functions                             *
 ******************************************************************************/
static Monitor*
monitor_init(MonitorsPlugin *mp, Monitor *m, gchar *color)
{
    ENTER;

    m->da = gtk_drawing_area_new();
    gtk_widget_set_size_request(m->da, DEFAULT_WIDTH, panel_get_height(mp->panel));

    monitor_set_foreground_color(mp, m, color);

    /* GTK4: draw func replaces the configure-event/expose-event signals. */
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(m->da),
                                   (GtkDrawingAreaDrawFunc) draw_func, m, NULL);

    return m;
}

static void
monitor_free(Monitor *m)
{
    if (!m)
        return;

    g_free(m->color);
    if (m->pixmap)
        cairo_surface_destroy(m->pixmap);
    if (m->stats)
        g_free(m->stats);
    g_free(m);

    return;
}

static void
monitor_set_foreground_color(MonitorsPlugin *mp, Monitor *m, const gchar *color)
{
    const gchar *safe_color = color != NULL ? color : "#0000FF";
    (void)mp;
    g_free(m->color);
    m->color = g_strndup(safe_color, COLOR_SIZE - 1);
    if (!gdk_rgba_parse(&m->foreground_color, safe_color))
        gdk_rgba_parse(&m->foreground_color, "#0000FF");
}
/******************************************************************************
 *                          End of monitor functions                          *
 ******************************************************************************/

/******************************************************************************
 *                                 CPU monitor                                *
 ******************************************************************************/
typedef float CPUSample;	   /* Saved CPU utilization value as 0.0..1.0 */

static gboolean
cpu_update(Monitor * c)
{

    if ((c->stats != NULL) && (c->pixmap != NULL))
    {
        /* Open statistics file and scan out CPU usage. */
        struct cpu_stat cpu;
        FILE * stat = fopen("/proc/stat", "r");
        if (stat == NULL)
            return TRUE;
        int fscanf_result = fscanf(stat, "cpu %llu %llu %llu %llu",
                                    &cpu.u, &cpu.n, &cpu.s, &cpu.i);
        fclose(stat);

        /* Ensure that fscanf succeeded. */
        if (fscanf_result == 4)
        {
            if (!c->has_previous_cpu)
            {
                c->has_previous_cpu = TRUE;
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
                    c->stats[c->ring_cursor] = (stats_set)busy / (stats_set)total;
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

static void
cpu_tooltip_update (Monitor *m)
{
    if (m && m->stats) {
        gchar *tooltip_text;
        gint ring_pos = (m->ring_cursor == 0)
            ? m->pixmap_width - 1 : m->ring_cursor - 1;
        tooltip_text = g_strdup_printf(_("CPU usage: %.2f%%"),
                m->stats[ring_pos] * 100);
        gtk_widget_set_tooltip_text(m->da, tooltip_text);
        g_free(tooltip_text);
    }
}

/******************************************************************************
 *                            End of CPU Monitor                              *
 ******************************************************************************/

/******************************************************************************
 *                               RAM Monitor                                  *
 ******************************************************************************/
static gboolean
mem_update(Monitor * m)
{
    ENTER;

    FILE *meminfo;
    char buf[80];
    long int mem_total = 0;
    long int mem_free  = 0;
    long int mem_buffers = 0;
    long int mem_cached = 0;
    long int mem_sreclaimable = 0;
    unsigned int readmask = 0x10 | 0x8 | 0x4 | 0x2 | 0x1;

    if (!m->stats || !m->pixmap)
        RET(TRUE);

    meminfo = fopen("/proc/meminfo", "r");
    if (!meminfo) {
        g_warning("monitors: Could not open /proc/meminfo: %d, %s",
                  errno, strerror(errno));
        RET(FALSE);
    }

    while (readmask && fgets(buf, sizeof(buf), meminfo)) {
        if (sscanf(buf, "MemTotal: %ld kB\n", &mem_total) == 1) {
            readmask ^= 0x1;
            continue;
        }
        if (sscanf(buf, "MemFree: %ld kB\n", &mem_free) == 1) {
            readmask ^= 0x2;
            continue;
        }
        if (sscanf(buf, "Buffers: %ld kB\n", &mem_buffers) == 1) {
            readmask ^= 0x4;
            continue;
        }
        if (sscanf(buf, "Cached: %ld kB\n", &mem_cached) == 1) {
            readmask ^= 0x8;
            continue;
        }
        if (sscanf(buf, "SReclaimable: %ld kB\n", &mem_sreclaimable) == 1) {
            readmask ^= 0x10;
            continue;
        }
    }

    fclose(meminfo);

    if (readmask) {
        g_warning("monitors: Couldn't read all values from /proc/meminfo: "
                  "readmask %x", readmask);
        RET(FALSE);
    }

    m->total = mem_total;
    if (mem_total <= 0)
        RET(FALSE);

    /* Keep arithmetic non-negative even when a vendor kernel reports
     * inconsistent meminfo values. */
    {
        long used = mem_total - mem_buffers - mem_free - mem_cached - mem_sreclaimable;
        if (used < 0)
            used = 0;
        if (used > mem_total)
            used = mem_total;
        m->stats[m->ring_cursor] = (stats_set)used / (stats_set)mem_total;
    }

    m->ring_cursor++;
    if (m->ring_cursor >= m->pixmap_width)
        m->ring_cursor = 0;

    /* Redraw the pixmap, with the new sample */
    redraw_pixmap (m);

    RET(TRUE);
}

static void
mem_tooltip_update (Monitor *m)
{
    if (m && m->stats) {
        gchar *tooltip_text;
        gint ring_pos = (m->ring_cursor == 0)
            ? m->pixmap_width - 1 : m->ring_cursor - 1;
        tooltip_text = g_strdup_printf(_("RAM usage: %.1fMB (%.2f%%)"),
                m->stats[ring_pos] * m->total / 1024,
                m->stats[ring_pos] * 100);
        gtk_widget_set_tooltip_text(m->da, tooltip_text);
        g_free(tooltip_text);
    }
}
/******************************************************************************
 *                             End of RAM Monitor                             *
 ******************************************************************************/

/******************************************************************************
 *                            Basic events handlers                           *
 ******************************************************************************/
/* GTK4 draw func: resize to current size, then paint the pixmap */
static void
draw_func(GtkDrawingArea * da, cairo_t * cr, int width, int height, Monitor * m)
{
    (void)da;
    int new_pixmap_width = MAX(width - BORDER_SIZE * 2, 0);
    int new_pixmap_height = MAX(height - BORDER_SIZE * 2, 0);

    if (m->pixmap == NULL ||
        new_pixmap_width != m->pixmap_width || new_pixmap_height != m->pixmap_height)
    {
        if ((new_pixmap_width > 0) && (new_pixmap_height > 0))
            monitor_resize(m, new_pixmap_width, new_pixmap_height);
        else if (m->pixmap != NULL)
        {
            cairo_surface_destroy(m->pixmap);
            m->pixmap = NULL;
            m->pixmap_width = 0;
            m->pixmap_height = 0;
        }
    }
    if (m->pixmap != NULL)
    {
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_set_source_surface(cr, m->pixmap, BORDER_SIZE, BORDER_SIZE);
        cairo_paint(cr);
        check_cairo_status(cr);
    }
}

/* Resize the pixmap and stats buffer. */
static void
monitor_resize(Monitor * m, int new_pixmap_width, int new_pixmap_height)
{
    if (m == NULL || new_pixmap_width <= 0 || new_pixmap_height <= 0)
        return;
    new_pixmap_width = CLAMP(new_pixmap_width, 1, 4096);
    new_pixmap_height = CLAMP(new_pixmap_height, 1, 4096);
    if (new_pixmap_width > 0 && new_pixmap_height > 0)
    {
        /*
         * If the stats buffer does not exist (first time we get inside this
         * function) or its size changed, reallocate the buffer and preserve
         * existing data.
         */
        if (!m->stats || (new_pixmap_width != m->pixmap_width))
        {
            stats_set *new_stats = g_new0(stats_set, new_pixmap_width);

            if (!new_stats)
                return;

            if (m->stats)
            {
                /* New allocation is larger.
                 * Add new "oldest" samples of zero following the cursor*/
                if (new_pixmap_width > m->pixmap_width)
                {
                    /* Number of values between the ring cursor and the end of
                     * the buffer */
                    int nvalues = m->pixmap_width - m->ring_cursor;

                    memcpy(new_stats,
                           m->stats,
                           m->ring_cursor * sizeof (stats_set));
                    memcpy(new_stats + new_pixmap_width - nvalues,
                           m->stats + m->ring_cursor,
                           nvalues * sizeof(stats_set));
                }
                /* New allocation is smaller, but still larger than the ring
                 * buffer cursor */
                else if (m->ring_cursor <= new_pixmap_width)
                {
                    /* Numver of values that can be stored between the end of
                     * the new buffer and the ring cursor */
                    int nvalues = new_pixmap_width - m->ring_cursor;
                    memcpy(new_stats,
                           m->stats,
                           m->ring_cursor * sizeof(stats_set));
                    memcpy(new_stats + m->ring_cursor,
                           m->stats + m->pixmap_width - nvalues,
                           nvalues * sizeof(stats_set));
                }
                /* New allocation is smaller, and also smaller than the ring
                 * buffer cursor.  Discard all oldest samples following the ring
                 * buffer cursor and additional samples at the beginning of the
                 * buffer. */
                else
                {
                    memcpy(new_stats,
                           m->stats + m->ring_cursor - new_pixmap_width,
                           new_pixmap_width * sizeof(stats_set));
                }
                g_free(m->stats);
            }
            m->stats = new_stats;
        }

        m->pixmap_width = new_pixmap_width;
        m->pixmap_height = new_pixmap_height;
        if (m->pixmap)
            cairo_surface_destroy(m->pixmap);
        m->pixmap = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                   m->pixmap_width,
                                   m->pixmap_height);
        check_cairo_surface_status(&m->pixmap);
        redraw_pixmap(m);
    }
}

static void monitors_launch_action(const char * cmd)
{
    GAppInfo * appinfo;
    GAppLaunchContext * ctx;
    GError * error = NULL;

    if (cmd == NULL || *cmd == '\0')
        return;
    appinfo = fm_app_info_create_from_commandline(cmd, NULL, G_APP_INFO_CREATE_NONE, &error);
    if (appinfo == NULL)
    {
        g_warning("monitors: failed to create app info for %s", cmd);
        g_clear_error(&error);
        return;
    }
    {
        GdkDisplay *display = gdk_display_get_default();
        if (display == NULL)
        {
            g_object_unref(appinfo);
            return;
        }
        ctx = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(display));
    }
    if (ctx == NULL)
    {
        g_object_unref(appinfo);
        return;
    }
    if (!fm_app_info_launch(appinfo, NULL, ctx, &error))
        g_warning("monitors: failed to launch %s", cmd);
    g_clear_error(&error);
    g_object_unref(ctx);
    g_object_unref(appinfo);
}

static gboolean monitors_button_press_event(GtkWidget* widget, GdkEvent* evt, LuxePanel *panel)
{
    MonitorsPlugin* mp;

    if (widget == NULL || evt == NULL)
        return FALSE;
    (void)panel;
    if (gdk_event_get_event_type(evt) != GDK_BUTTON_PRESS ||
        gdk_button_event_get_button(evt) != 1)
        return FALSE;

    mp = luxepanel_plugin_get_data(widget);
    if (mp == NULL)
        return FALSE;
    if (mp->action != NULL)
        monitors_launch_action(mp->action);
    else
        monitors_launch_action("luxetask");

    return TRUE;
}
/******************************************************************************
 *                       End of basic events handlers                         *
 ******************************************************************************/

static void
redraw_pixmap (Monitor *m)
{
    int i;
    cairo_t *cr;

    if (m == NULL || m->pixmap == NULL || m->stats == NULL ||
        m->pixmap_width <= 0 || m->pixmap_height <= 0)
        return;
    cr = cairo_create(m->pixmap);

    cairo_set_line_width (cr, 1.0);

    /* Erase pixmap */
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);

    gdk_cairo_set_source_rgba(cr, &m->foreground_color);
    for (i = 0; i < m->pixmap_width; i++)
    {
        unsigned int drawing_cursor = (m->ring_cursor + i) % m->pixmap_width;

        /* Draw one bar of the graph */
        cairo_move_to(cr, i + 0.5, m->pixmap_height);
        cairo_line_to(cr, i + 0.5, (1.0 - m->stats[drawing_cursor]) * m->pixmap_height);
        cairo_stroke(cr);
    }

    check_cairo_status(cr);
    cairo_destroy(cr);
    /* Redraw pixmap */
    gtk_widget_queue_draw(m->da);
}

static update_func update_functions [N_MONITORS] = {
    [CPU_POSITION] = cpu_update,
    [MEM_POSITION] = mem_update
};

static char *default_colors[N_MONITORS] = {
    [CPU_POSITION] = "#0000FF",
    [MEM_POSITION] = "#FF0000"
};

static tooltip_update_func tooltip_update[N_MONITORS] = {
    [CPU_POSITION] = cpu_tooltip_update,
    [MEM_POSITION] = mem_tooltip_update
};

/* Colors currently used. We cannot store them in the "struct Monitor"s where
 * they belong, because we free these when the user removes them. And since we
 * want the colors to stay the same even after removing/adding a widget... */
static char *colors[N_MONITORS] = {
    NULL,
    NULL
};

/*
 * This function is called every UPDATE_PERIOD seconds. It updates all
 * monitors.
 */
static gboolean
monitors_update(gpointer data)
{
    MonitorsPlugin *mp;
    int i;

    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;
    mp = (MonitorsPlugin *) data;
    if (!mp)
        RET(FALSE);

    for (i = 0; i < N_MONITORS; i++)
    {
        if (mp->monitors[i])
        {
            if (mp->monitors[i]->update)
                mp->monitors[i]->update(mp->monitors[i]);
            if (mp->monitors[i]->update_tooltip)
                mp->monitors[i]->update_tooltip(mp->monitors[i]);
        }
    }

    return TRUE;
}

static gboolean
monitors_update_widget(GtkWidget *plugin)
{
    MonitorsPlugin *mp = plugin != NULL ?
        luxepanel_plugin_get_data(plugin) : NULL;
    return mp != NULL ? monitors_update(mp) : G_SOURCE_REMOVE;
}

static Monitor*
monitors_add_monitor (GtkWidget *p, MonitorsPlugin *mp, update_func update,
             tooltip_update_func update_tooltip, gchar *color)
{
    ENTER;

    Monitor *m;

    m = g_new0(Monitor, 1);
    m = monitor_init(mp, m, color);
    m->update = update;
    m->update_tooltip = update_tooltip;
    gtk_box_append(GTK_BOX(p), m->da);
    gtk_widget_show(m->da);

    RET(m);
}

static GtkWidget *
monitors_constructor(LuxePanel *panel, config_setting_t *settings)
{
    ENTER;
    int i;
    MonitorsPlugin *mp;
    GtkWidget *p;
    const char *tmp;

    mp = g_new0(MonitorsPlugin, 1);
    mp->panel = panel;
    mp->settings = settings;

    p = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_box_set_homogeneous(GTK_BOX(p), TRUE);
    luxepanel_plugin_set_data(p, mp, monitors_destructor);
    mp->plugin = p;

    /* First time we use this plugin : only display CPU usage */
    mp->displayed_monitors[CPU_POSITION] = 1;

    /* Apply options */
    config_setting_lookup_int(settings, "DisplayCPU",
                              &mp->displayed_monitors[CPU_POSITION]);
    config_setting_lookup_int(settings, "DisplayRAM",
                              &mp->displayed_monitors[MEM_POSITION]);
    if (config_setting_lookup_string(settings, "Action", &tmp))
        mp->action = g_strdup(tmp);
    if (config_setting_lookup_string(settings, "CPUColor", &tmp))
        colors[CPU_POSITION] = g_strndup(tmp, COLOR_SIZE-1);
    if (config_setting_lookup_string(settings, "RAMColor", &tmp))
        colors[MEM_POSITION] = g_strndup(tmp, COLOR_SIZE-1);

    /* Initializing monitors */
    for (i = 0; i < N_MONITORS; i++)
    {
        if (!colors[i])
            colors[i] = g_strndup(default_colors[i], COLOR_SIZE-1);

        if (mp->displayed_monitors[i])
        {
            mp->monitors[i] = monitors_add_monitor(p, mp,
                                                   update_functions[i],
                                                   tooltip_update[i],
                                                   colors[i]);
        }
    }

    /* Adding a timer : monitors will be updated every UPDATE_PERIOD
     * seconds */
    mp->timer = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, UPDATE_PERIOD,
                                            (GSourceFunc)monitors_update_widget,
                                            g_object_ref(p), g_object_unref);
    RET(p);
}

static void
monitors_destructor(gpointer user_data)
{
    ENTER;
    int            i;
    MonitorsPlugin *mp;

    mp = (MonitorsPlugin *) user_data;

    /* Removing timer */
    if (mp->timer != 0)
    {
        g_source_remove(mp->timer);
        mp->timer = 0;
    }

    /* Freeing all monitors */
    for (i = 0; i < N_MONITORS; i++)
    {
        if (mp->monitors[i])
            monitor_free(mp->monitors[i]);
    }

    g_free(mp->action);
    g_free(mp);

    RET();
}

static GtkWidget *
monitors_config (LuxePanel *panel, GtkWidget *p)
{
    ENTER;

    GtkWidget *dialog;
    MonitorsPlugin *mp;

    mp = luxepanel_plugin_get_data(p);

    dialog = luxepanel_generic_config_dlg(_("Resource monitors"),
        panel, monitors_apply_config, p,
        _("Display CPU usage"), &mp->displayed_monitors[0], CONF_TYPE_BOOL,
        _("CPU color"), &colors[CPU_POSITION], CONF_TYPE_STR,
        _("Display RAM usage"), &mp->displayed_monitors[1], CONF_TYPE_BOOL,
        _("RAM color"), &colors[MEM_POSITION], CONF_TYPE_STR,
        _("Action when clicked (default: luxetask)"), &mp->action, CONF_TYPE_STR,
        NULL);

    RET(dialog);
}

static gboolean
monitors_apply_config (gpointer user_data)
{
    ENTER;
    GtkWidget *p = user_data;
    MonitorsPlugin *mp;
    mp = p != NULL ? luxepanel_plugin_get_data(p) : NULL;
    if (mp == NULL)
        return G_SOURCE_REMOVE;

    int i;
    int current_n_monitors = 0;

start:
    for (i = 0; i < N_MONITORS; i++)
    {
        /* User may remove color value. In such case, reset to the default */
        if (!colors[i])
            colors[i] = g_strndup(default_colors[i], COLOR_SIZE-1);

        if (mp->displayed_monitors[i])
            current_n_monitors++;

        if (mp->displayed_monitors[i] && !mp->monitors[i])
        {
            /* We've just activated monitor<i> */
            mp->monitors[i] = monitors_add_monitor(p, mp,
                                                   update_functions[i],
                                                   tooltip_update[i],
                                                   colors[i]);
            /*
             * It is probably best for users if their monitors are always
             * displayed in the same order : the CPU monitor always on the left,
             * the RAM monitor always on the right of the CPU monitor (if the
             * CPU monitor is displayed), etc. That's why we do not just use
             * gtk_box_pack_start/gtk_box_pack_end, and use
             * gtk_box_reorder_child.
             */
            {
                /* move the new drawing area after the previously added one */
                GtkWidget * prev = gtk_widget_get_first_child(p);
                int j;
                for (j = 0; j < current_n_monitors - 1 && prev != NULL; j++)
                    prev = gtk_widget_get_next_sibling(prev);
                gtk_box_reorder_child_after(GTK_BOX(p), mp->monitors[i]->da, prev);
            }
        }
        else if (!mp->displayed_monitors[i] && mp->monitors[i])
        {
            /* We've just removed monitor<i> */
            gtk_box_remove(GTK_BOX(p), mp->monitors[i]->da);
            monitor_free(mp->monitors[i]);
            mp->monitors[i] = NULL;
        }
        if (mp->monitors[i] &&
            strncmp(mp->monitors[i]->color, colors[i], COLOR_SIZE) != 0)
        {
            /* We've changed the color */
            monitor_set_foreground_color(mp, mp->monitors[i], colors[i]);
        }
    }

    /* Workaround meant to prevent users to display no monitor at all.
     * FIXME : write something clean. When there is only one monitor displayed,
     * its toggle button should not be clickable in the prefs. */
    if (current_n_monitors == 0)
    {
        mp->displayed_monitors[0] = 1;
        goto start;
    }
    config_group_set_int(mp->settings, "DisplayCPU", mp->displayed_monitors[CPU_POSITION]);
    config_group_set_int(mp->settings, "DisplayRAM", mp->displayed_monitors[MEM_POSITION]);
    config_group_set_string(mp->settings, "Action", mp->action);
    config_group_set_string(mp->settings, "CPUColor",
                            mp->monitors[CPU_POSITION] ? colors[CPU_POSITION] : NULL);
    config_group_set_string(mp->settings, "RAMColor",
                            mp->monitors[MEM_POSITION] ? colors[MEM_POSITION] : NULL);

    RET(FALSE);
}

FM_DEFINE_MODULE(luxepanel_gtk, monitors)

LuxePanelPluginInit fm_module_init_luxepanel_gtk = {
    .name = N_("Resource monitors"),
    .description = N_("Display monitors (CPU, RAM)"),
    .new_instance = monitors_constructor,
    .config = monitors_config,
    .button_press_event = monitors_button_press_event
};

/* vim: set sw=4 sts=4 et : */
