/*
 * Copyright (C) 2006-2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2012 Michael Rawson <michaelrawson76@gmail.com>
 *               2012-2014 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2014-2016 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2018 Mamoru TASAKA <mtasaka@fedoraproject.org>
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

#include "plugin.h"
#include "dclock-schedule.h"
#include "misc.h"

#include <libfm/fm.h>

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#include "dbg.h"

#define DEFAULT_TIP_FORMAT    "%A %x"
#define DEFAULT_CLOCK_FORMAT  "%R"

/* Keep GtkLabel's sizing, markup and accessibility, but rasterize this small
 * readout through Cairo. GTK's GPU text path can clip leading clock glyphs
 * when the label is translated to the far right of a wide panel. */
typedef struct { GtkWidget parent; } ClockFace;
typedef struct { GtkWidgetClass parent; } ClockFaceClass;
G_DEFINE_TYPE(ClockFace, clock_face, GTK_TYPE_WIDGET)
static void clock_face_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    GtkWidget *label = gtk_widget_get_first_child(widget);
    if (!label || !gtk_widget_get_visible(label)) return;
    graphene_point_t zero = GRAPHENE_POINT_INIT(0, 0), point;
    if (!gtk_widget_compute_point(label, widget, &zero, &point)) return;
    int x, y;
    gtk_label_get_layout_offsets(GTK_LABEL(label), &x, &y);
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, gtk_widget_get_width(widget), gtk_widget_get_height(widget));
    cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &bounds);
    GdkRGBA color;
    gtk_style_context_get_color(gtk_widget_get_style_context(label), &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
    cairo_move_to(cr, point.x + x, point.y + y);
    pango_cairo_show_layout(cr, gtk_label_get_layout(GTK_LABEL(label)));
    cairo_destroy(cr);
}
static void clock_face_dispose(GObject *object)
{
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(object));
    if (child) gtk_widget_unparent(child);
    G_OBJECT_CLASS(clock_face_parent_class)->dispose(object);
}
static void clock_face_class_init(ClockFaceClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->snapshot = clock_face_snapshot;
    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
    G_OBJECT_CLASS(klass)->dispose = clock_face_dispose;
}
static void clock_face_init(ClockFace *face) { (void)face; }

/* Private context for digital clock plugin. */
typedef struct {
    GtkWidget * plugin;				/* Back pointer to plugin */
    LuxePanel * panel;
    config_setting_t *settings;
    GtkWidget * clock_label;			/* Label containing clock value */
    GtkWidget * clock_face;
    GtkWidget * clock_icon;			/* Icon when icon_only */
    GtkWidget * calendar_window;		/* Calendar window, if it is being displayed */
    char * clock_format;			/* Format string for clock value */
    char * tooltip_format;			/* Format string for tooltip value */
    char * action;				/* Command to execute on a click */
    gboolean bold;				/* True if bold font */
    gboolean icon_only;				/* True if icon only (no clock value) */
    gboolean center_text;
    guint timer;				/* Timer for periodic update */
    enum {
	AWAITING_FIRST_CHANGE,			/* Experimenting to determine interval, waiting for first change */
	AWAITING_SECOND_CHANGE,			/* Experimenting to determine interval, waiting for second change */
	ONE_SECOND_INTERVAL,			/* Determined that one second interval is necessary */
	ONE_MINUTE_INTERVAL			/* Determined that one minute interval is sufficient */
    } expiration_interval;
    int experiment_count;			/* Count of experiments that have been done to determine interval */
    char * prev_clock_value;			/* Previous value of clock */
    char * prev_tooltip_value;			/* Previous value of tooltip */
} DClockPlugin;

static gboolean dclock_update_display(DClockPlugin * dc);
static gboolean dclock_update_display_widget(GtkWidget *plugin);
static void dclock_destructor(gpointer user_data);
static gboolean dclock_apply_configuration(gpointer user_data);
static void dclock_on_panel_reconfigured(LuxePanel *panel, GtkWidget *p);

/* fm_launch_command_simple lives in libfm-gtk (dropped); minimal local
 * replacement on core libfm + GAppInfo. */
static void dclock_launch_action(const char * cmd)
{
    GAppInfo * appinfo;
    GAppLaunchContext * ctx;
    GError * error = NULL;

    if (cmd == NULL || *cmd == '\0')
        return;
    appinfo = fm_app_info_create_from_commandline(cmd, NULL, G_APP_INFO_CREATE_NONE, &error);
    if (appinfo == NULL)
    {
        g_warning("dclock: failed to create app info for %s: %s", cmd,
                  (error != NULL) ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }
    {
        GdkDisplay *display = gdk_display_get_default();
        if (display == NULL)
        {
            g_warning("dclock: no display for action %s", cmd);
            g_clear_error(&error);
            g_object_unref(appinfo);
            return;
        }
        ctx = G_APP_LAUNCH_CONTEXT(gdk_display_get_app_launch_context(display));
    }
    if (ctx == NULL)
    {
        g_warning("dclock: no launch context for action %s", cmd);
        g_object_unref(appinfo);
        return;
    }
    if (!fm_app_info_launch(appinfo, NULL, ctx, &error))
        g_warning("dclock: failed to launch %s: %s", cmd,
                  (error != NULL) ? error->message : "unknown error");
    g_clear_error(&error);
    g_object_unref(ctx);
    g_object_unref(appinfo);
}

/* Display a window containing the standard calendar widget. */
static GtkWidget * dclock_create_calendar(DClockPlugin * dc)
{
    gint x, y;

    /* Create a new window. */
    GtkWidget * win = gtk_window_new();

    gtk_window_set_default_size(GTK_WINDOW(win), 180, 180);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

    /* Create a vertical box as a child of the window. */
    GtkWidget * box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), box);

    /* Create a standard calendar widget as a child of the vertical box. */
    GtkWidget * calendar = gtk_calendar_new();
    gtk_calendar_set_show_week_numbers(GTK_CALENDAR(calendar), TRUE);
    gtk_calendar_set_show_day_names(GTK_CALENDAR(calendar), TRUE);
    gtk_calendar_set_show_heading(GTK_CALENDAR(calendar), TRUE);
    gtk_widget_set_vexpand(calendar, TRUE);
    gtk_widget_set_hexpand(calendar, TRUE);
    gtk_box_append(GTK_BOX(box), calendar);

    /* Preset the widget position right now to not move it across the screen */
    luxepanel_plugin_popup_set_position_helper(dc->panel, dc->plugin, win, &x, &y);
    {
        GdkSurface * surf = gtk_native_get_surface(GTK_NATIVE(win));
        if (surf && GDK_IS_X11_SURFACE(surf))
            XMoveWindow(gdk_x11_display_get_xdisplay(gdk_surface_get_display(surf)),
                        gdk_x11_surface_get_xid(surf), x, y);
    }

    /* Return the widget. */
    return win;
}

/* Handler for "button-press-event" event from main widget. */
static gboolean dclock_button_press_event(GtkWidget * widget, GdkEvent * evt, LuxePanel * panel)
{
    DClockPlugin * dc;

    (void)panel;
    if (gdk_event_get_event_type(evt) != GDK_BUTTON_PRESS ||
        gdk_button_event_get_button(evt) != 1)
        return FALSE;

    /* If an action is set, execute it. */
    dc = luxepanel_plugin_get_data(widget);
    if (dc == NULL)
        return FALSE;
    if (dc->action != NULL)
        dclock_launch_action(dc->action);

    /* If no action is set, toggle the presentation of the calendar. */
    else
    {
        if (dc->calendar_window == NULL)
        {
            dc->calendar_window = dclock_create_calendar(dc);
            gtk_widget_show(dc->calendar_window);
            gtk_window_present(GTK_WINDOW(dc->calendar_window));
        }
        else
        {
            gtk_window_destroy(GTK_WINDOW(dc->calendar_window));
            dc->calendar_window = NULL;
        }
    }
    return TRUE;
}

/* Set the timer. */
static void dclock_timer_set(DClockPlugin * dc, struct timeval *current_time)
{
    int milliseconds = 1000;

    /* Get current time to millisecond resolution. */
    if (gettimeofday(current_time, NULL) >= 0)
    {
        milliseconds = dclock_boundary_delay(current_time->tv_sec,current_time->tv_usec,
                                              dc->expiration_interval == ONE_MINUTE_INTERVAL);
    }

    /* Be defensive, and set the timer. */
    if (milliseconds <= 0)
        milliseconds = 1000;
    dc->timer = g_timeout_add_full(G_PRIORITY_DEFAULT, milliseconds,
                                    (GSourceFunc)dclock_update_display_widget,
                                    g_object_ref(dc->plugin), g_object_unref);
}

/* Periodic timer callback.
 * Also used during initialization and configuration change to do a redraw. */
static gboolean dclock_update_display(DClockPlugin * dc)
{
    /* Determine the current time. */
    struct timeval now;
    struct tm * current_time;

    if (dc == NULL || dc->plugin == NULL || dc->panel == NULL ||
        dc->clock_label == NULL)
        return G_SOURCE_REMOVE;
    dclock_timer_set(dc, &now);
    current_time = localtime(&now.tv_sec);
    if (current_time == NULL)
        return G_SOURCE_REMOVE;

    /* Determine the content of the clock label and tooltip. */
    char clock_value[64];
    char tooltip_value[64];
    clock_value[0] = '\0';
    if (dc->clock_format != NULL)
        strftime(clock_value, sizeof(clock_value), dc->clock_format, current_time);
    tooltip_value[0] = '\0';
    if (dc->tooltip_format != NULL)
        strftime(tooltip_value, sizeof(tooltip_value), dc->tooltip_format, current_time);

    /* When we write the clock value, it causes the panel to do a full relayout.
     * Since this function may be called too often while the timing experiment is underway,
     * we take the trouble to check if the string actually changed first. */
    if (( ! dc->icon_only)
    && ((dc->prev_clock_value == NULL) || (strcmp(dc->prev_clock_value, clock_value) != 0)))
    {
        /* Convert "\n" escapes in the user's format string to newline characters. */
        char * newlines_converted = NULL;
        if (strstr(clock_value, "\\n") != NULL)
        {
            newlines_converted = g_strdup(clock_value);	/* Just to get enough space for the converted result */
            char * p;
            char * q;
            for (p = clock_value, q = newlines_converted; *p != '\0'; p += 1)
            {
                if ((p[0] == '\\') && (p[1] == 'n'))
                {
                    *q++ = '\n';
                    p += 1;
                }
                else
                    *q++ = *p;
            }
            *q = '\0';
        }

        gchar * utf8 = g_locale_to_utf8(((newlines_converted != NULL) ? newlines_converted : clock_value), -1, NULL, NULL, NULL);
        if (utf8 != NULL)
        {
            luxepanel_draw_label_text(dc->panel, dc->clock_label, utf8, dc->bold, 1, TRUE);
            gtk_widget_queue_draw(dc->clock_face);
            g_free(utf8);
        }
        g_free(newlines_converted);
    }

    /* Determine the content of the tooltip. */
    gchar * utf8 = g_locale_to_utf8(tooltip_value, -1, NULL, NULL, NULL);
    if (utf8 != NULL)
    {
        gtk_widget_set_tooltip_text(dc->plugin, utf8);
        g_free(utf8);
    }

    /* Conduct an experiment to see how often the value changes.
     * Use this to decide whether we update the value every second or every minute.
     * We need to account for the possibility that the experiment is being run when we cross a minute boundary. */
    if (dc->expiration_interval < ONE_SECOND_INTERVAL)
    {
        if (dc->prev_clock_value == NULL)
        {
            /* Initiate the experiment. */
            dc->prev_clock_value = g_strdup(clock_value);
            dc->prev_tooltip_value = g_strdup(tooltip_value);
        }
        else
        {
            if (((dc->icon_only) || (strcmp(dc->prev_clock_value, clock_value) == 0))
            && (strcmp(dc->prev_tooltip_value, tooltip_value) == 0))
            {
                dc->experiment_count += 1;
                if (dc->experiment_count > 3)
                {
                    /* No change within 3 seconds.  Assume change no more often than once per minute. */
                    dc->expiration_interval = ONE_MINUTE_INTERVAL;
                    g_free(dc->prev_clock_value);
                    g_free(dc->prev_tooltip_value);
                    dc->prev_clock_value = NULL;
                    dc->prev_tooltip_value = NULL;
                }
            }
            else if (dc->expiration_interval == AWAITING_FIRST_CHANGE)
            {
                /* We have a change at the beginning of the experiment, but we do not know when the next change might occur.
                 * Continue the experiment for 3 more seconds. */
                dc->expiration_interval = AWAITING_SECOND_CHANGE;
                dc->experiment_count = 0;
                g_free(dc->prev_clock_value);
                g_free(dc->prev_tooltip_value);
                dc->prev_clock_value = g_strdup(clock_value);
                dc->prev_tooltip_value = g_strdup(tooltip_value);
            }
            else
            {
                /* We have a second change.  End the experiment. */
                dc->expiration_interval = ((dc->experiment_count > 3) ? ONE_MINUTE_INTERVAL : ONE_SECOND_INTERVAL);
                g_free(dc->prev_clock_value);
                g_free(dc->prev_tooltip_value);
                dc->prev_clock_value = NULL;
                dc->prev_tooltip_value = NULL;
            }
        }
    }

    /* Reset the timer and return. */
    return G_SOURCE_REMOVE;
}

static gboolean
dclock_update_display_widget(GtkWidget *plugin)
{
    DClockPlugin *dc;

    if (plugin == NULL)
        return G_SOURCE_REMOVE;
    dc = luxepanel_plugin_get_data(plugin);
    if (dc == NULL)
        return G_SOURCE_REMOVE;
    return dclock_update_display(dc);
}

/* Plugin constructor. */
static GtkWidget *dclock_constructor(LuxePanel *panel, config_setting_t *settings)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    DClockPlugin * dc = g_new0(DClockPlugin, 1);
    GtkWidget * p;
    const char *str;
    int tmp_int;

    /* Load parameters from the configuration file. */
    if (config_setting_lookup_string(settings, "ClockFmt", &str))
        dc->clock_format = g_strdup(str);
    if (config_setting_lookup_string(settings, "TooltipFmt", &str))
        dc->tooltip_format = g_strdup(str);
    if (config_setting_lookup_string(settings, "Action", &str))
        dc->action = g_strdup(str);
    if (config_setting_lookup_int(settings, "BoldFont", &tmp_int))
        dc->bold = tmp_int != 0;
    if (config_setting_lookup_int(settings, "IconOnly", &tmp_int))
        dc->icon_only = tmp_int != 0;
    if (config_setting_lookup_int(settings, "CenterText", &tmp_int))
        dc->center_text = tmp_int != 0;

    /* Save construction pointers */
    dc->panel = panel;
    dc->settings = settings;

    /* Allocate top level widget and set into Plugin widget pointer. */
    dc->plugin = p = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    luxepanel_plugin_set_data(p, dc, dclock_destructor);

    /* Allocate a horizontal box as the child of the top level. */
    GtkWidget * hbox = GTK_WIDGET(p);

    /* Create a label and an image as children of the horizontal box.
     * Only one of these is visible at a time, controlled by user preference. */
    dc->clock_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(dc->clock_label), 0.5);
    gtk_label_set_yalign(GTK_LABEL(dc->clock_label), 0.5);
    gtk_widget_set_margin_start(dc->clock_label, 4);
    gtk_widget_set_margin_end(dc->clock_label, 4);
    dc->clock_face = g_object_new(clock_face_get_type(), NULL);
    gtk_widget_set_parent(dc->clock_label, dc->clock_face);
    gtk_box_append(GTK_BOX(hbox), dc->clock_face);
    dc->clock_icon = luxepanel_image_new_for_icon(panel, "clock", -1, NULL);
    gtk_box_append(GTK_BOX(hbox), dc->clock_icon);

    /* Initialize the clock display. */
    if (dc->clock_format == NULL)
        dc->clock_format = g_strdup(_(DEFAULT_CLOCK_FORMAT));
    if (dc->tooltip_format == NULL)
        dc->tooltip_format = g_strdup(_(DEFAULT_TIP_FORMAT));
    dclock_apply_configuration(p);

    /* dclock_apply_configuration() already scheduled the initial update. */
    return p;
}

/* Plugin destructor. */
static void dclock_destructor(gpointer user_data)
{
    DClockPlugin * dc = user_data;

    /* Remove the timer. */
    if (dc->timer != 0)
    {
        g_source_remove(dc->timer);
        dc->timer = 0;
    }

    /* Ensure that the calendar is dismissed. */
    if (dc->calendar_window != NULL)
    {
        gtk_window_destroy(GTK_WINDOW(dc->calendar_window));
        dc->calendar_window = NULL;
    }

    /* Deallocate all memory. */
    g_free(dc->clock_format);
    g_free(dc->tooltip_format);
    g_free(dc->action);
    g_free(dc->prev_clock_value);
    g_free(dc->prev_tooltip_value);
    g_free(dc);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static gboolean dclock_apply_configuration(gpointer user_data)
{
    GtkWidget * p = user_data;
    DClockPlugin * dc = p != NULL ? luxepanel_plugin_get_data(p) : NULL;

    if (dc == NULL || dc->settings == NULL)
        return G_SOURCE_REMOVE;

    /* stop the updater now */
    if (dc->timer)
    {
        g_source_remove(dc->timer);
        dc->timer = 0;
    }

    /* Set up the icon or the label as the displayable widget. */
    gtk_widget_set_visible(dc->clock_face, !dc->icon_only);
    if (dc->icon_only)
    {
        gtk_widget_show(dc->clock_icon);
        gtk_widget_hide(dc->clock_label);
    }
    else
    {
        gtk_widget_show(dc->clock_label);
        gtk_widget_hide(dc->clock_icon);
    }

    if (dc->center_text)
    {
        gtk_label_set_justify(GTK_LABEL(dc->clock_label), GTK_JUSTIFY_CENTER);
    }
    else
    {
        gtk_label_set_justify(GTK_LABEL(dc->clock_label), GTK_JUSTIFY_LEFT);
    }

    /* Rerun the experiment to determine update interval and update the display. */
    g_free(dc->prev_clock_value);
    g_free(dc->prev_tooltip_value);
    dc->expiration_interval = AWAITING_FIRST_CHANGE;
    dc->experiment_count = 0;
    dc->prev_clock_value = NULL;
    dc->prev_tooltip_value = NULL;
    dc->timer = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                (GSourceFunc)dclock_update_display_widget,
                                g_object_ref(p), g_object_unref);

    /* Hide the calendar. */
    if (dc->calendar_window != NULL)
    {
        gtk_window_destroy(GTK_WINDOW(dc->calendar_window));
        dc->calendar_window = NULL;
    }

    /* Save configuration */
    config_group_set_string(dc->settings, "ClockFmt", dc->clock_format);
    config_group_set_string(dc->settings, "TooltipFmt", dc->tooltip_format);
    config_group_set_string(dc->settings, "Action", dc->action);
    config_group_set_int(dc->settings, "BoldFont", dc->bold);
    config_group_set_int(dc->settings, "IconOnly", dc->icon_only);
    config_group_set_int(dc->settings, "CenterText", dc->center_text);
    return FALSE;
}

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *dclock_configure(LuxePanel *panel, GtkWidget *p)
{
    DClockPlugin * dc = luxepanel_plugin_get_data(p);
    return luxepanel_generic_config_dlg(_("Digital Clock"), panel,
        dclock_apply_configuration, p,
        _("Clock Format"), &dc->clock_format, CONF_TYPE_STR,
        _("Tooltip Format"), &dc->tooltip_format, CONF_TYPE_STR,
        _("Format codes: man 3 strftime; %n for line break"), NULL, CONF_TYPE_TRIM,
        _("Action when clicked (default: display calendar)"), &dc->action, CONF_TYPE_STR,
        _("Bold font"), &dc->bold, CONF_TYPE_BOOL,
        _("Tooltip only"), &dc->icon_only, CONF_TYPE_BOOL,
        _("Center text"), &dc->center_text, CONF_TYPE_BOOL,
        NULL);
}

/* Callback when panel configuration changes. */
static void dclock_on_panel_reconfigured(LuxePanel *panel, GtkWidget *p)
{
    dclock_apply_configuration(p);
}

/* Plugin descriptor. */
LuxePanelPluginInit luxepanel_static_plugin_dclock = {
    .name = N_("Digital Clock"),
    .description = N_("Display digital clock and tooltip"),

    .new_instance = dclock_constructor,
    .config = dclock_configure,
    .reconfigure = dclock_on_panel_reconfigured,
    .button_press_event = dclock_button_press_event
};
