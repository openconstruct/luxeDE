/**
 * CPUFreq plugin to lxpanel
 *
 * Copyright (C) 2009 by Daniel Kesler <kesler.daniel@gmail.com>
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

#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <string.h>

#include "plugin.h"
#include "misc.h"

#include "dbg.h"

#define PROC_ICON           "cpufreq-icon"
#define SYSFS_CPU_DIRECTORY "/sys/devices/system/cpu"
#define SCALING_GOV         "scaling_governor"
#define SCALING_AGOV        "scaling_available_governors"
#define SCALING_AFREQ       "scaling_available_frequencies"
#define SCALING_CUR_FREQ    "scaling_cur_freq"
#define SCALING_SETFREQ     "scaling_setspeed"
#define SCALING_MAX         "scaling_max_freq"
#define SCALING_MIN         "scaling_min_freq"

typedef struct {
    GtkWidget *main;
    GtkWidget *plugin;
    config_setting_t *settings;
    GList *governors;
    GList *cpus;
    int has_cpufreq;
    char* cur_governor;
    int   cur_freq;
    unsigned int timer;
    //gboolean remember;
} cpufreq;

typedef struct {
    char *data;
    cpufreq *cf;
} Param;

static void cpufreq_destructor(gpointer user_data);

static void
get_cur_governor(cpufreq *cf){
    FILE *fp;
    char buf[100];
    gchar *path;

    if (cf == NULL || cf->cpus == NULL || cf->cpus->data == NULL)
        return;
    path = g_build_filename(cf->cpus->data, SCALING_GOV, NULL);
    fp = fopen(path, "r");
    g_free(path);
    if (fp != NULL) {
        if (fgets(buf, sizeof(buf), fp)) {
            g_strstrip(buf);
            g_free(cf->cur_governor);
            cf->cur_governor = g_strdup(buf);
        }
        fclose(fp);
    }
}

static void
get_cur_freq(cpufreq *cf){
    FILE *fp;
    char buf[100];
    gchar *path, *end = NULL;
    gint64 value;

    if (cf == NULL || cf->cpus == NULL || cf->cpus->data == NULL)
        return;
    path = g_build_filename(cf->cpus->data, SCALING_CUR_FREQ, NULL);
    fp = fopen(path, "r");
    g_free(path);
    if (fp != NULL) {
        if (fgets(buf, sizeof(buf), fp)) {
            g_strstrip(buf);
            value = g_ascii_strtoll(buf, &end, 10);
            if (end != buf && end != NULL && *end == '\0' &&
                value >= 0 && value <= G_MAXINT)
                cf->cur_freq = (int)value;
        }
        fclose(fp);
    }
}

static void
get_cpus(cpufreq *cf)
{

    const char *cpu;
    gchar *cpu_path;

    GDir * cpuDirectory = g_dir_open(SYSFS_CPU_DIRECTORY, 0, NULL);
    if (cpuDirectory == NULL)
    {
        cf->cpus = NULL;
        printf("cpufreq: no cpu found\n");
        return;
    }

    while ((cpu = g_dir_read_name(cpuDirectory)))
    {
        /* Look for directories of the form "cpu<n>", where "<n>" is a decimal integer. */
        if (g_str_has_prefix(cpu, "cpu") && strlen(cpu) > 3 &&
            g_ascii_isdigit(cpu[3]))
        {
            cpu_path = g_build_filename(SYSFS_CPU_DIRECTORY, cpu, "cpufreq", NULL);

            /* Open the cpu's own cpufreq directory so the probe means
             * something; the stray root re-open leaked one GDir per CPU. */
            GDir * cpufreqDir = g_dir_open(cpu_path, 0, NULL);
            if (cpufreqDir == NULL)
            {
                g_free(cpu_path);
                continue;
            }
            g_dir_close(cpufreqDir);

            cf->has_cpufreq = 1;
            cf->cpus = g_list_append(cf->cpus, cpu_path);
        }
    }
    g_dir_close(cpuDirectory);
}

static gboolean
clicked(GtkWidget *widget, GdkEvent *evt, LuxePanel *panel)
{
    ENTER;
    if (widget == NULL || evt == NULL)
        RET(FALSE);

    if (gdk_button_event_get_button(evt) == 1)
    {
        /* Setting a governor needs root; the old GTK2 governor menu is gone. */
      return TRUE;
    }

    RET(FALSE);
}

static gboolean
_update_tooltip(cpufreq *cf)
{
    char *tooltip;

    if (cf == NULL || cf->main == NULL)
        return G_SOURCE_REMOVE;
    get_cur_freq(cf);
    get_cur_governor(cf);

    ENTER;

    if (cf->cur_governor == NULL)
        tooltip = g_strdup(_("CPU frequency information unavailable"));
    else
        tooltip = g_strdup_printf(_("Frequency: %d MHz\nGovernor: %s"),
                                  cf->cur_freq / 1000, cf->cur_governor);
    gtk_widget_set_tooltip_text(cf->main, tooltip);
    g_free(tooltip);
    RET(TRUE);
}

static gboolean update_tooltip(gpointer user_data)
{
    GtkWidget *plugin = user_data;
    cpufreq *cf = plugin != NULL ? luxepanel_plugin_get_data(plugin) : NULL;
    if (cf == NULL)
        return G_SOURCE_REMOVE;
    return _update_tooltip(cf);
}

static GtkWidget *cpufreq_constructor(LuxePanel *panel, config_setting_t *settings)
{
    cpufreq *cf;
    //GtkWidget *button;

    ENTER;
    cf = g_new0(cpufreq, 1);
    g_return_val_if_fail(cf != NULL, NULL);
    cf->governors = NULL;
    cf->cpus = NULL;
    cf->settings = settings;

    cf->main = luxepanel_button_new_for_icon(panel, PROC_ICON, NULL, NULL);
    cf->plugin = cf->main;
    luxepanel_plugin_set_data(cf->main, cf, cpufreq_destructor);

    cf->has_cpufreq = 0;

    get_cpus(cf);

    //if (config_setting_lookup_int(settings, "Remember", &tmp_int)) cf->remember = tmp_int != 0;
    //if (config_setting_lookup_int(settings, "Governor", &tmp_str)) cf->cur_governor = g_strdup(tmp_str);
    //config_setting_lookup_int(settings, "Frequency", &cf->cur_freq);

    _update_tooltip(cf);
    cf->timer = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 2,
                                            update_tooltip,
                                            g_object_ref(cf->main),
                                            g_object_unref);

    RET(cf->main);
}

static void
cpufreq_destructor(gpointer user_data)
{
    cpufreq *cf = (cpufreq *)user_data;
    g_list_free_full(cf->cpus, g_free);
    g_list_free_full(cf->governors, g_free);
    g_free(cf->cur_governor);
    if (cf->timer != 0)
    {
        g_source_remove(cf->timer);
        cf->timer = 0;
    }
    g_free(cf);
}

FM_DEFINE_MODULE(luxepanel_gtk, cpufreq)

/* Plugin descriptor. */
LuxePanelPluginInit fm_module_init_luxepanel_gtk = {
    .name = N_("CPUFreq frontend"),
    .description = N_("Display CPU frequency and allow to change governors and frequency"),

    .new_instance = cpufreq_constructor,
    //.config      = config,
    .button_press_event = clicked
};
