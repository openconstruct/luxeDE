/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Diagnostic only: mirrors libfm's startup order to attribute each subsystem. */
#include <libfm/fm.h>
#include <gtk/gtk.h>
#include "renderer-init.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

static void sample(const char *stage)
{
    FILE *file = fopen("/proc/self/smaps_rollup", "r");
    char line[256];
    unsigned long pss=0, anon=0, private=0, value;
    if (file) {
        while (fgets(line, sizeof line, file)) {
            if (sscanf(line, "Pss: %lu", &value) == 1) pss=value;
            if (sscanf(line, "Pss_Anon: %lu", &value) == 1) anon=value;
            if (sscanf(line, "Private_Dirty: %lu", &value) == 1) private+=value;
            if (sscanf(line, "Private_Clean: %lu", &value) == 1) private+=value;
        }
        fclose(file);
    }
    struct mallinfo2 heap = mallinfo2();
    guint driver_maps=0;
    file=fopen("/proc/self/maps", "r");
    if (file) {
        while (fgets(line, sizeof line, file))
            if (strstr(line, "libgallium") || strstr(line, "_dri.so") ||
                strstr(line, "libvulkan_") || strstr(line, "libLLVM")) ++driver_maps;
        fclose(file);
    }
    printf("%s,%lu,%lu,%lu,%zu,%zu,%u\n", stage, pss, anon, private,
           heap.uordblks, heap.hblkhd, driver_maps);
    fflush(stdout);
}
#define STEP(name) do { _fm_##name##_init(); sample(#name); } while (0)
int main(int argc, char **argv)
{
    gboolean with_gtk = argc > 1 && !strcmp(argv[1], "gtk");
    gboolean gtk_stages = argc > 1 && !strcmp(argv[1], "gtk-stages");
    puts("stage,pss_kib,anon_kib,private_kib,heap_live_bytes,heap_mmap_bytes,driver_maps");
    sample("process");
    if (with_gtk) { luxepanel_init_gtk(); sample("gtk-cairo"); }
    if (gtk_stages) {
        g_setenv("GSK_RENDERER", "cairo", TRUE);
        gtk_init(); sample("gtk-init");
        GtkWidget *window = gtk_window_new(); sample("gtk-window");
        gtk_widget_realize(window); sample("gtk-realize");
        g_assert_true(GSK_IS_CAIRO_RENDERER(gtk_native_get_renderer(GTK_NATIVE(window))));
        gtk_window_destroy(GTK_WINDOW(window)); sample("gtk-destroy");
    }
    g_thread_pool_set_max_idle_time(10000);
    fm_config = fm_config_new();
    fm_config_load_from_file(fm_config, NULL);
    sample("fm-config");
    STEP(file); STEP(path); STEP(icon); STEP(monitor); STEP(mime_type);
    STEP(file_info); STEP(folder); STEP(archiver); STEP(thumbnailer);
    STEP(thumbnail_loader); STEP(terminal); STEP(templates); STEP(folder_config);
    fm_qdata_id = g_quark_from_static_string("fm_qdata_id");
    /* Retained work scheduled by initialization is included in a final sample. */
    gint64 deadline = g_get_monotonic_time() + 300000;
    while (g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(10000);
    }
    sample("settled");
    _fm_folder_config_finalize(); _fm_templates_finalize(); _fm_terminal_finalize();
    _fm_thumbnail_loader_finalize(); _fm_thumbnailer_finalize(); _fm_archiver_finalize();
    _fm_folder_finalize(); _fm_file_info_finalize(); _fm_mime_type_finalize();
    _fm_monitor_finalize(); _fm_icon_finalize(); _fm_path_finalize(); _fm_file_finalize();
    g_clear_object(&fm_config);
    sample("finalized");
    return 0;
}
