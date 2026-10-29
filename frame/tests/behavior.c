/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include "luxe-gtk-ui.h"
#include <glib/gstdio.h>
#include <stdarg.h>
#include <string.h>
#include <webp/encode.h>
#include <webp/mux.h>
static gchar *wallpaper_path;
static GSubprocess *wallpaper_spawn(GSubprocessLauncher *launcher, GError **error, const char *command, ...)
{
    (void)launcher;
    g_assert_cmpstr(command, ==, "frame");
    va_list args;
    va_start(args, command);
    g_assert_cmpstr(va_arg(args, const char *), ==, "--set-wallpaper");
    g_free(wallpaper_path);
    wallpaper_path = g_strdup(va_arg(args, const char *));
    g_assert_null(va_arg(args, const char *));
    va_end(args);
    return g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, error, "/bin/true", NULL);
}
static gboolean simulated_trash_finish(GFile *file, GAsyncResult *result, GError **error)
{
    (void)file;
    return g_task_propagate_boolean(G_TASK(result), error);
}
#define g_file_trash_finish simulated_trash_finish
#define g_subprocess_launcher_spawn wallpaper_spawn
#include "../frame-window.c"
#undef g_file_trash_finish
#undef g_subprocess_launcher_spawn
#include "../frame-app.c"

static void drain(void)
{
    for (guint i = 0; i < 40; i++) {
        while (g_main_context_iteration(NULL, FALSE));
        g_usleep(1000);
    }
}

static void await_image(FrameWindow *win)
{
    gint64 deadline = g_get_monotonic_time() + 5000000;
    while (!win->paintable && !win->load_failed && g_get_monotonic_time() < deadline) drain();
    g_assert_nonnull(win->paintable);
}

static void trash_completed(FrameWindow *win, GFile *file)
{
    TrashToken *token = g_new0(TrashToken, 1);
    token->win = g_object_ref(win);
    token->cancel = g_cancellable_new();
    GTask *result = g_task_new(file, NULL, NULL, NULL);
    g_task_return_boolean(result, TRUE);
    on_trashed(G_OBJECT(file), G_ASYNC_RESULT(result), token);
    g_object_unref(result);
}

/* One application for the whole run: a second registration of the same
 * application id cannot succeed inside one process. */
static FrameApp *test_app;
static FrameApp *shared_app(void);

static void count_idle_frames(GdkFrameClock *clock, gpointer data)
{
    (void) clock;
    (*(guint *) data)++;
}

static void test_idle_resize(void)
{
    FrameWindow *win = frame_window_new(GTK_APPLICATION(shared_app()));
    GdkPixbuf *pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1600, 900);
    gdk_pixbuf_fill(pixels, 0x336699ff);
    GdkPaintable *image = frame_image_new(pixels);
    frame_window_set_paintable(win, image);
    g_object_unref(image);g_object_unref(pixels);
    frame_window_apply_zoom(win); /* Load before map must still refit. */
    gtk_window_set_default_size(GTK_WINDOW(win), 500, 400);
    gtk_window_present(GTK_WINDOW(win));
    for (guint i = 0; i < 20; i++) drain();
    g_assert_cmpint(win->fit_last_width, >, 0);
    int old_width = win->fit_last_width;
    double old_zoom = win->zoom;
    gtk_window_set_default_size(GTK_WINDOW(win), 900, 600);
    for (guint i = 0; i < 20; i++) drain();
    g_assert_cmpint(win->fit_last_width, >, old_width);
    g_assert_cmpfloat(win->zoom, >, old_zoom);
    guint frames = 0;
    gulong handler = g_signal_connect(win->fit_clock, "after-paint",
                                     G_CALLBACK(count_idle_frames), &frames);
    for (guint i = 0; i < 25; i++) drain();
    g_assert_cmpuint(frames, <=, 2);
    g_signal_handler_disconnect(win->fit_clock, handler);
    gtk_window_destroy(GTK_WINDOW(win));
    drain();
}

static FrameApp *
shared_app(void)
{
    if (test_app == NULL) {
        test_app = frame_app_new();
        g_application_set_flags(G_APPLICATION(test_app),
                               G_APPLICATION_HANDLES_OPEN | G_APPLICATION_NON_UNIQUE);
        g_assert_true(g_application_register(G_APPLICATION(test_app), NULL, NULL));
    }
    return test_app;
}

static void test_viewer(void)
{
    gchar *directory = g_dir_make_tmp("frame-audit-XXXXXX", NULL);
    g_assert_nonnull(directory);
    gchar *path1 = g_build_filename(directory, "one é space.png", NULL);
    gchar *path2 = g_build_filename(directory, "two.svg", NULL);
    gchar *missing = g_build_filename(directory, "missing.png", NULL);
    GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 32);
    gdk_pixbuf_fill(image, 0xcc2244ff);
    g_assert_true(gdk_pixbuf_save(image, path1, "png", NULL, NULL));
    g_object_unref(image);
    const char *large_svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='8000' height='4000' "
        "viewBox='0 0 8000 4000'><rect width='8000' height='4000' fill='#22cc44'/></svg>";
    g_assert_true(g_file_set_contents(path2, large_svg, -1, NULL));
    GFile *one = g_file_new_for_path(path1), *two = g_file_new_for_path(path2);
    GFile *absent = g_file_new_for_path(missing);
    FrameApp *app = shared_app();
    frame_app_activate(G_APPLICATION(app));
    FrameWindow *win = FRAME_WINDOW(gtk_application_get_windows(GTK_APPLICATION(app))->data);
    frame_app_activate(G_APPLICATION(app));
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 1);
    GFile *opened[] = { one };
    frame_app_open(G_APPLICATION(app), opened, 1, "");
    await_image(win);
    g_assert_cmpint(frame_image_width(win), ==, 64);
    g_assert_cmpint(frame_image_height(win), ==, 32);
    action_wallpaper(NULL, NULL, win);
    g_assert_cmpstr(wallpaper_path, ==, path1);
    drain();
    g_assert_cmpuint(win->entries->len, ==, 2);
    g_assert_cmpint(win->index, ==, 0);
    GdkPaintable *weak = win->paintable;
    g_object_add_weak_pointer(G_OBJECT(weak), (gpointer *)&weak);
    GVariant *cw = g_variant_ref_sink(g_variant_new_string("cw"));
    action_rotate(NULL, cw, win);
    g_variant_unref(cw);
    drain();
    g_assert_null(weak);
    g_assert_cmpint(frame_image_width(win), ==, 32);
    g_assert_cmpint(frame_image_height(win), ==, 64);
    GListModel *controllers = gtk_widget_observe_controllers(win->picture);
    GtkGesture *pinch = NULL, *drag = NULL;
    for (guint i = 0; i < g_list_model_get_n_items(controllers); i++) {
        GObject *controller = g_list_model_get_item(controllers, i);
        if (GTK_IS_GESTURE_ZOOM(controller)) pinch = GTK_GESTURE(g_object_ref(controller));
        if (GTK_IS_GESTURE_DRAG(controller)) drag = GTK_GESTURE(g_object_ref(controller));
        g_object_unref(controller);
    }
    g_object_unref(controllers);
    g_assert_nonnull(pinch);
    g_assert_nonnull(drag);
    g_signal_emit_by_name(pinch, "begin", NULL);
    double base = win->pinch_base;
    g_signal_emit_by_name(pinch, "scale-changed", 0.5);
    g_assert_cmpfloat(win->zoom, ==, CLAMP(base * 0.5, FRAME_ZOOM_MIN, FRAME_ZOOM_MAX));
    g_object_unref(pinch);
    g_signal_emit_by_name(drag, "drag-begin", 0.0, 0.0);
    g_signal_emit_by_name(drag, "drag-update", 4.0, 6.0);
    g_assert_cmpfloat(win->drag_x, ==, 4.0);
    g_assert_cmpfloat(win->drag_y, ==, 6.0);
    g_signal_emit_by_name(drag, "drag-end", 4.0, 6.0);
    g_assert_cmpfloat(win->drag_x, ==, 0);
    g_object_unref(drag);
    GSimpleAction *slide = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(win), "slideshow"));
    g_action_change_state(G_ACTION(slide), g_variant_new_boolean(TRUE));
    guint old = win->slideshow_id;
    g_action_change_state(G_ACTION(slide), g_variant_new_boolean(TRUE));
    g_assert_null(g_main_context_find_source_by_id(NULL, old));
    g_action_change_state(G_ACTION(slide), g_variant_new_boolean(FALSE));
    action_info(NULL, NULL, win);
    drain();
    GtkWindow *dialog = luxe_window_get_visible_dialog(GTK_WINDOW(win));
    g_assert_nonnull(dialog);
    GtkBox *box = g_object_get_data(G_OBJECT(dialog), "info-box");
    gint64 deadline = g_get_monotonic_time() + 5000000;
    while (!GTK_IS_GRID(gtk_widget_get_first_child(GTK_WIDGET(box))) && g_get_monotonic_time() < deadline) drain();
    g_assert_true(GTK_IS_GRID(gtk_widget_get_first_child(GTK_WIDGET(box))));
    g_assert_false(on_key_pressed(NULL, GDK_KEY_Right, 0, 0, win));
    luxe_dialog_close(dialog);
    drain();
    frame_window_open(win, absent);
    action_info(NULL, NULL, win);
    drain();
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(win));
    box = g_object_get_data(G_OBJECT(dialog), "info-box");
    g_assert_true(GTK_IS_LABEL(gtk_widget_get_first_child(GTK_WIDGET(box))));
    luxe_dialog_close(dialog);
    frame_window_open(win, one);
    frame_window_open(win, two);
    await_image(win);
    g_assert_cmpint(frame_image_width(win), ==, 4096);
    g_assert_cmpint(frame_image_height(win), ==, 2048);
    g_assert_cmpint(win->source_width, ==, 8000);
    g_assert_cmpint(win->source_height, ==, 4000);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(win->status_label)), "8000×4000"));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(win->zoom_label)), "preview"));
    action_rotate(NULL, g_variant_new_string("cw"), win);
    g_assert_cmpint(win->source_width, ==, 8000);
    g_assert_cmpint(win->source_height, ==, 4000);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(win->status_label)), "8000×4000"));
    action_info(NULL, NULL, win);
    drain();
    dialog = luxe_window_get_visible_dialog(GTK_WINDOW(win));
    box = g_object_get_data(G_OBJECT(dialog), "info-box");
    deadline = g_get_monotonic_time() + 5000000;
    while (!GTK_IS_GRID(gtk_widget_get_first_child(GTK_WIDGET(box))) && g_get_monotonic_time() < deadline) drain();
    GtkWidget *grid = gtk_widget_get_first_child(GTK_WIDGET(box));
    g_assert_true(GTK_IS_GRID(grid));
    gboolean found_dimensions = FALSE;
    for (GtkWidget *child = gtk_widget_get_first_child(grid); child; child = gtk_widget_get_next_sibling(child))
        if (GTK_IS_LABEL(child) && strstr(gtk_label_get_text(GTK_LABEL(child)), "8000 × 4000 pixels")) found_dimensions = TRUE;
    g_assert_true(found_dimensions);
    luxe_dialog_close(dialog);
    drain();
    g_assert_true(g_file_equal(win->current_file, two));
    trash_completed(win, one); /* A completed older trash must not navigate away from two. */
    g_assert_true(g_file_equal(win->current_file, two));
    g_assert_cmpuint(win->entries->len, ==, 1);
    frame_window_cancel_list(win);
    if (win->monitor) g_file_monitor_cancel(win->monitor);
    trash_completed(win, two);
    g_assert_null(win->current_file);
    g_assert_null(win->paintable);
    frame_window_open(win, one);
    action_info(NULL, NULL, win);
    FrameWindow *weak_window = win;
    g_object_add_weak_pointer(G_OBJECT(win), (gpointer *)&weak_window);
    gtk_window_destroy(GTK_WINDOW(win));
    deadline = g_get_monotonic_time() + 5000000;
    while (weak_window && g_get_monotonic_time() < deadline) drain();
    g_assert_null(weak_window);
    frame_app_activate(G_APPLICATION(app));
    g_assert_cmpuint(g_list_length(gtk_application_get_windows(GTK_APPLICATION(app))), ==, 1);
    gtk_window_destroy(gtk_application_get_windows(GTK_APPLICATION(app))->data);
    drain();
    g_object_unref(one); g_object_unref(two); g_object_unref(absent);
    g_assert_cmpint(g_unlink(path1), ==, 0);
    g_assert_cmpint(g_unlink(path2), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(directory); g_free(path1); g_free(path2); g_free(missing);
    g_clear_pointer(&wallpaper_path, g_free);
}

/* Encode a lossless WebP, optionally muxing in an EXIF orientation tag, so the
 * decoder is covered without depending on any external image tool. */
static gboolean
write_webp(const char *path, GdkPixbuf *image, int orientation)
{
    gboolean ok = FALSE;
    int width = gdk_pixbuf_get_width(image), height = gdk_pixbuf_get_height(image);
    int channels = gdk_pixbuf_get_n_channels(image), stride = gdk_pixbuf_get_rowstride(image);
    gboolean alpha = gdk_pixbuf_get_has_alpha(image);
    guchar *source = gdk_pixbuf_get_pixels(image);
    guchar *packed = g_malloc((gsize) width * height * 4);
    guchar *encoded = NULL;
    gsize encoded_size;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const guchar *pixel = source + (gsize) y * stride + (gsize) x * channels;
            guchar *out = packed + ((gsize) y * width + x) * 4;
            out[0] = pixel[0]; out[1] = pixel[1]; out[2] = pixel[2];
            out[3] = alpha ? pixel[3] : 0xff;
        }
    }
    encoded_size = WebPEncodeLosslessRGBA(packed, width, height, width * 4, &encoded);
    g_free(packed);
    if (encoded_size == 0) return FALSE;

    if (orientation == 0) {
        ok = g_file_set_contents(path, (const char *) encoded, encoded_size, NULL);
    } else {
        /* Minimal little-endian TIFF: one IFD entry holding the orientation. */
        guchar tiff[26] = {'I','I',0x2a,0x00, 8,0,0,0, 1,0, 0x12,0x01, 3,0, 1,0,0,0,
                           (guchar) orientation, 0, 0,0, 0,0,0,0};
        guchar payload[6 + sizeof tiff];
        WebPMux *mux = WebPMuxNew();
        WebPData image_data = { encoded, encoded_size };
        WebPData exif_data, assembled = { NULL, 0 };
        memcpy(payload, "Exif\0\0", 6);
        memcpy(payload + 6, tiff, sizeof tiff);
        exif_data.bytes = payload;
        exif_data.size = sizeof payload;
        WebPMuxSetImage(mux, &image_data, 1);
        WebPMuxSetChunk(mux, "EXIF", &exif_data, 1);
        if (WebPMuxAssemble(mux, &assembled) == WEBP_MUX_OK)
            ok = g_file_set_contents(path, (const char *) assembled.bytes, assembled.size, NULL);
        WebPDataClear(&assembled);
        WebPMuxDelete(mux);
    }
    WebPFree(encoded);
    return ok;
}

static void
set_pixel(GdkPixbuf *image, int x, int y, guint32 rgba)
{
    int channels = gdk_pixbuf_get_n_channels(image);
    guchar *pixels = gdk_pixbuf_get_pixels(image);
    guchar *pixel = pixels + (gsize) y * gdk_pixbuf_get_rowstride(image) + (gsize) x * channels;
    pixel[0] = (rgba >> 24) & 0xff;
    pixel[1] = (rgba >> 16) & 0xff;
    pixel[2] = (rgba >> 8) & 0xff;
    if (channels == 4) pixel[3] = rgba & 0xff;
}

static cairo_surface_t *render_image(GdkPaintable *image)
{
    int width=gdk_paintable_get_intrinsic_width(image),height=gdk_paintable_get_intrinsic_height(image);
    GtkSnapshot *snapshot=gtk_snapshot_new();
    gdk_paintable_snapshot(image,GDK_SNAPSHOT(snapshot),width,height);
    GskRenderNode *node=gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);
    cairo_t *cr=cairo_create(surface);gsk_render_node_draw(node,cr);
    cairo_destroy(cr);gsk_render_node_unref(node);cairo_surface_flush(surface);
    return surface;
}
static guint32 pixel_at(GdkPaintable *image, int x, int y)
{
    cairo_surface_t *surface=render_image(image);
    const guint32 *row=(const guint32 *)(cairo_image_surface_get_data(surface)+y*cairo_image_surface_get_stride(surface));
    guint32 pixel=row[x];cairo_surface_destroy(surface);
    return ((pixel&0x00ffffff)<<8)|(pixel>>24);
}

static void assert_pixels(GdkPaintable *image, GdkPixbuf *expected)
{
    g_assert_cmpint(gdk_paintable_get_intrinsic_width(image),==,gdk_pixbuf_get_width(expected));
    g_assert_cmpint(gdk_paintable_get_intrinsic_height(image),==,gdk_pixbuf_get_height(expected));
    GdkTexture *texture=gdk_texture_new_for_pixbuf(expected);
    cairo_surface_t *actual=render_image(image),*reference=render_image(GDK_PAINTABLE(texture));
    int stride=cairo_image_surface_get_stride(actual),other=cairo_image_surface_get_stride(reference);
    for(int y=0;y<gdk_pixbuf_get_height(expected);y++)
        g_assert_cmpmem(cairo_image_surface_get_data(actual)+y*stride,gdk_pixbuf_get_width(expected)*4,
                        cairo_image_surface_get_data(reference)+y*other,gdk_pixbuf_get_width(expected)*4);
    cairo_surface_destroy(actual);cairo_surface_destroy(reference);g_object_unref(texture);
}

static void test_display_pixels(void)
{
    GdkPixbuf *pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,2,3);
    const guint32 colors[]={0x112233ff,0xff000080,0xabcdef00,0x00ff0040,0x334455ff,0x0000ffff};
    for(int y=0;y<3;y++)for(int x=0;x<2;x++)set_pixel(pixels,x,y,colors[y*2+x]);
    GdkPaintable *image=frame_image_new(pixels);
    g_assert_nonnull(image);assert_pixels(image,pixels);
    for(int i=0;i<4;i++) {
        for(int horizontal=0;horizontal<2;horizontal++) {
            GdkPaintable *flip=frame_image_flip(image,horizontal);
            GdkPixbuf *expected=gdk_pixbuf_flip(pixels,horizontal);
            assert_pixels(flip,expected);g_object_unref(flip);g_object_unref(expected);
        }
        GdkPaintable *next=frame_image_rotate(image,TRUE);
        GdkPixbuf *expected=gdk_pixbuf_rotate_simple(pixels,GDK_PIXBUF_ROTATE_CLOCKWISE);
        g_object_unref(image);g_object_unref(pixels);image=next;pixels=expected;
        assert_pixels(image,pixels);
    }
    GdkPaintable *ccw=frame_image_rotate(image,FALSE);
    GdkPixbuf *expected=gdk_pixbuf_rotate_simple(pixels,GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
    assert_pixels(ccw,expected);g_object_unref(ccw);g_object_unref(expected);
    g_object_unref(image);g_object_unref(pixels);
    /* The paintable owns converted pixels, not the decoded source object. */
    pixels=gdk_pixbuf_new(GDK_COLORSPACE_RGB,FALSE,8,4,4);gdk_pixbuf_fill(pixels,0x123456ff);
    GdkPixbuf *weak=pixels;g_object_add_weak_pointer(G_OBJECT(pixels),(gpointer *)&weak);
    image=frame_image_new(pixels);g_object_unref(pixels);g_assert_null(weak);
    g_assert_cmphex(pixel_at(image,3,3),==,0x123456ff);g_object_unref(image);
}

static void
await_result(FrameWindow *win)
{
    gint64 deadline = g_get_monotonic_time() + 5000000;
    while (!win->paintable && !win->load_failed && g_get_monotonic_time() < deadline) drain();
}

static void
test_webp(void)
{
    gchar *directory = g_dir_make_tmp("frame-webp-XXXXXX", NULL);
    gchar *plain_path = g_build_filename(directory, "plain.webp", NULL);
    gchar *rotated_path = g_build_filename(directory, "rotated.webp", NULL);
    gchar *huge_path = g_build_filename(directory, "huge.webp", NULL);
    gchar *broken_path = g_build_filename(directory, "broken.webp", NULL);
    GdkPixbuf *image;
    GFile *file;
    FrameWindow *win;

    g_assert_nonnull(directory);
    /* Frame decodes WebP itself, so image/webp must be a listed type even
     * though this system's gdk-pixbuf knows nothing about it. */
    g_assert_true(frame_content_supported("image/webp"));

    image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 4, 2);
    gdk_pixbuf_fill(image, 0xcc2244ff);
    g_assert_true(write_webp(plain_path, image, 0));
    g_object_unref(image);
    image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 2, 3);
    set_pixel(image, 0, 0, 0x110000ff); set_pixel(image, 1, 0, 0x220000ff);
    set_pixel(image, 0, 1, 0x330000ff); set_pixel(image, 1, 1, 0x440000ff);
    set_pixel(image, 0, 2, 0x550000ff); set_pixel(image, 1, 2, 0x660000ff);
    g_assert_true(write_webp(rotated_path, image, 6)); /* 90 degrees clockwise */
    g_object_unref(image);
    image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 8192, 32);
    gdk_pixbuf_fill(image, 0x22cc44ff);
    g_assert_true(write_webp(huge_path, image, 0));
    g_object_unref(image);
    /* Correct RIFF/WEBP header, unusable payload. */
    {
        const guchar broken[] = { 'R','I','F','F', 12,0,0,0, 'W','E','B','P',
                                  'n','o','t','-','w','e','b','p' };
        g_assert_true(g_file_set_contents(broken_path, (const char *) broken, sizeof broken, NULL));
    }

    FrameApp *app = shared_app();
    frame_app_activate(G_APPLICATION(app));
    win = FRAME_WINDOW(gtk_application_get_windows(GTK_APPLICATION(app))->data);

    file = g_file_new_for_path(plain_path);
    frame_window_open(win, file);
    await_result(win);
    g_assert_nonnull(win->paintable);
    g_assert_cmpint(frame_image_width(win), ==, 4);
    g_assert_cmpint(frame_image_height(win), ==, 2);
    g_assert_cmphex(pixel_at(win->paintable, 3, 1), ==, 0xcc2244ff);
    drain();
    /* All four test files are recognized as WebP, including the damaged one. */
    g_assert_cmpuint(win->entries->len, ==, 4);
    for (guint i = 0; i < win->entries->len; i++)
        g_assert_cmpstr(((FrameEntry *) win->entries->pdata[i])->ctype, ==, "image/webp");

    file = g_file_new_for_path(rotated_path);
    frame_window_open(win, file);
    await_result(win);
    g_assert_nonnull(win->paintable);
    g_assert_cmpint(frame_image_width(win), ==, 3);
    g_assert_cmpint(frame_image_height(win), ==, 2);
    g_assert_cmpint(win->source_width, ==, 2);
    g_assert_cmpint(win->source_height, ==, 3);
    /* EXIF orientation 6 is a 90 degree clockwise turn, so the source's left
     * column (A/C/E) becomes the top row and its top row (A/B) the right one:
     *   E C A
     *   F D B */
    g_assert_cmphex(pixel_at(win->paintable, 0, 0), ==, 0x550000ff);
    g_assert_cmphex(pixel_at(win->paintable, 2, 0), ==, 0x110000ff);
    g_assert_cmphex(pixel_at(win->paintable, 2, 1), ==, 0x220000ff);

    file = g_file_new_for_path(huge_path);
    frame_window_open(win, file);
    await_result(win);
    g_assert_nonnull(win->paintable);
    g_assert_cmpint(frame_image_width(win), ==, 4096); /* the shared display cap */
    g_assert_cmpint(frame_image_height(win), ==, 16);
    g_assert_cmpint(win->source_width, ==, 8192);
    g_assert_cmpint(win->source_height, ==, 32);

    /* A damaged file must fail cleanly rather than leave a stale image. */
    file = g_file_new_for_path(broken_path);
    frame_window_open(win, file);
    await_result(win);
    g_assert_null(win->paintable);
    g_assert_true(win->load_failed);

    frame_window_cancel_list(win);
    if (win->monitor) g_file_monitor_cancel(win->monitor);
    gtk_window_destroy(GTK_WINDOW(win));
    drain();
    g_object_unref(file);

    g_assert_cmpint(g_unlink(plain_path), ==, 0);
    g_assert_cmpint(g_unlink(rotated_path), ==, 0);
    g_assert_cmpint(g_unlink(huge_path), ==, 0);
    g_assert_cmpint(g_unlink(broken_path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(directory); g_free(plain_path); g_free(rotated_path);
    g_free(huge_path); g_free(broken_path);
}

int main(int argc, char **argv)
{
    int status;
    g_test_init(&argc, &argv, NULL);
    luxe_ui_init();
    g_test_add_func("/frame/viewer", test_viewer);
    g_test_add_func("/frame/idle-resize", test_idle_resize);
    g_test_add_func("/frame/webp", test_webp);
    g_test_add_func("/frame/display-pixels", test_display_pixels);
    status = g_test_run();
    g_clear_object(&test_app);
    return status;
}
