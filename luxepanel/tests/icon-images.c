/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the real image scaler, discarding unrelated misc.c functions. */
#include "../src/misc.c"
#include <glib/gstdio.h>

gint panel_get_icon_size(LuxePanel *panel) { return panel->priv->icon_size; }
gint panel_get_height(LuxePanel *panel) { return panel->priv->height; }
/* Exported misc entry points reference this label renderer, but the scaler
 * must not call it. Keep the probe independent of a running Panel window. */
void luxepanel_draw_label_text(LuxePanel *panel, GtkWidget *label, const char *text,
                              gboolean bold, float factor, gboolean custom)
{
    (void)panel; (void)label; (void)text; (void)bold; (void)factor; (void)custom;
    g_assert_not_reached();
}

int main(void)
{
    gtk_init();
    Panel settings = {0};
    LuxePanel panel = {.priv = &settings};
    ImgData data = {.size = -1, .panel = &panel,
                    .icon = (FmIcon *)g_themed_icon_new("audio-volume-muted")};
    GtkWidget *image = g_object_ref_sink(gtk_image_new());
    const int heights[] = {16, 28, 32, 64};
    const int sizes[] = {8, 28, 48};
    for (guint h = 0; h < G_N_ELEMENTS(heights); h++)
        for (guint s = 0; s < G_N_ELEMENTS(sizes); s++) {
            settings.height = heights[h]; settings.icon_size = sizes[s];
            _gtk_image_set_from_file_scaled(image, &data);
            int tile = MIN(settings.height, settings.icon_size), width, height;
            gtk_widget_get_size_request(image, &width, &height);
            g_assert_cmpint(width, ==, tile - 2);
            g_assert_cmpint(height, ==, tile - 2);
            int natural;
            gtk_widget_measure(image, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &natural, NULL, NULL);
            g_assert_cmpint(natural, ==, tile);
            g_assert_cmpint(gtk_image_get_pixel_size(GTK_IMAGE(image)), ==, tile - 2);
            g_assert_nonnull(data.pixbuf);
            g_assert_cmpint(gdk_pixbuf_get_width(data.pixbuf), ==, tile - 2);
            g_assert_cmpint(gdk_pixbuf_get_height(data.pixbuf), ==, tile - 2);
        }
    /* Inspect a full-bleed image through the real GtkImage snapshot too. */
    g_autofree char *root = g_dir_make_tmp("luxe-panel-image-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(root, "tile.png", NULL);
    GdkPixbuf *tile = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 8, 8);
    gdk_pixbuf_fill(tile, 0x525252ff);
    g_assert_true(gdk_pixbuf_save(tile, path, "png", NULL, NULL));
    g_object_unref(tile);
    GFile *file = g_file_new_for_path(path);
    g_object_unref(data.icon); data.icon = (FmIcon *)g_file_icon_new(file);
    g_object_unref(file);
    settings.height = settings.icon_size = 28;
    _gtk_image_set_from_file_scaled(image, &data);
    GtkWidget *window = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_child(GTK_WINDOW(window), image);
    gtk_window_present(GTK_WINDOW(window));
    for (int i = 0; i < 20; i++) {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(10000);
    }
    g_assert_cmpint(gtk_widget_get_width(image), ==, 26);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gtk_widget_snapshot_child(window, image, snapshot);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 28, 28);
    cairo_t *cr = cairo_create(surface);
    gsk_render_node_draw(node, cr); cairo_surface_flush(surface);
    unsigned char *pixels = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int n = 0; n < 28; n++) {
        g_assert_cmpuint(((guint32 *)pixels)[n] >> 24, ==, 0);
        g_assert_cmpuint(((guint32 *)(pixels+27*stride))[n] >> 24, ==, 0);
        g_assert_cmpuint(((guint32 *)(pixels+n*stride))[0] >> 24, ==, 0);
        g_assert_cmpuint(((guint32 *)(pixels+n*stride))[27] >> 24, ==, 0);
    }
    g_assert_cmpuint(((guint32 *)(pixels+stride))[1] >> 24, >, 0);
    cairo_destroy(cr); cairo_surface_destroy(surface); gsk_render_node_unref(node);
    gtk_window_destroy(GTK_WINDOW(window));
    g_remove(path); g_rmdir(root);
    g_object_unref(data.icon);
    data.icon = (FmIcon *)g_themed_icon_new("audio-volume-muted");
    data.size = 20;
    _gtk_image_set_from_file_scaled(image, &data);
    g_assert_cmpint(gdk_pixbuf_get_width(data.pixbuf), ==, 20);
    int width, height;
    gtk_widget_get_size_request(image, &width, &height);
    g_assert_cmpint(width, ==, -1); g_assert_cmpint(height, ==, -1);
    int natural;
    gtk_widget_measure(image, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &natural, NULL, NULL);
    /* Explicit-size callers retain the old GtkImage canvas, not auto margins. */
    GtkWidget *baseline = g_object_ref_sink(gtk_image_new());
    gtk_image_set_from_pixbuf(GTK_IMAGE(baseline), data.pixbuf);
    int baseline_natural;
    gtk_widget_measure(baseline, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &baseline_natural, NULL, NULL);
    g_assert_cmpint(natural, ==, baseline_natural);
    g_object_unref(baseline);
    g_assert_cmpint(gtk_widget_get_margin_start(image), ==, 0);
    g_object_unref(data.pixbuf); g_object_unref(data.icon); g_object_unref(image);
    g_print("PASS: 12 automatic image sizes inset artwork, preserve tiles and explicit sizing\n");
    return 0;
}
