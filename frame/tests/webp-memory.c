/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
/* Diagnostic control for the old full-decode/resize path versus native scaling. */
#include "../frame-webp.h"
#include <webp/decode.h>
#include <webp/encode.h>
#include <sys/resource.h>
#include <string.h>
#include <stdio.h>
static void free_old(guchar *pixels, gpointer unused) { (void) unused; WebPFree(pixels); }
int main(int argc, char **argv)
{
    g_assert_cmpint(argc, ==, 3);
    if (g_str_equal(argv[1], "fixture")) {
        gsize size = (gsize) 8192 * 4096 * 4;
        guint8 *pixels = g_malloc(size), *encoded = NULL;
        for (gsize i = 0; i < size; i += 4) {
            pixels[i] = (i / 4) % 256;
            pixels[i+1] = ((i / 4) / 8192) % 256;
            pixels[i+2] = 100; pixels[i+3] = 255;
        }
        size_t length = WebPEncodeRGBA(pixels, 8192, 4096, 8192 * 4, 80, &encoded);
        g_assert_cmpuint(length, >, 0);
        g_assert_true(g_file_set_contents(argv[2], (char *) encoded, length, NULL));
        g_free(pixels); WebPFree(encoded);
        return 0;
    }
    gint64 start = g_get_monotonic_time();
    GdkPixbuf *pixbuf;
    if (g_str_equal(argv[1], "old")) {
        gchar *data;
        gsize size;
        g_assert_true(g_file_get_contents(argv[2], &data, &size, NULL));
        int width, height;
        guint8 *pixels = WebPDecodeRGBA((guint8 *) data, size, &width, &height);
        g_assert_nonnull(pixels);
        GdkPixbuf *original = gdk_pixbuf_new_from_data(pixels, GDK_COLORSPACE_RGB,
            TRUE, 8, width, height, width * 4, free_old, NULL);
        pixbuf = gdk_pixbuf_scale_simple(original, 4096, 2048, GDK_INTERP_BILINEAR);
        g_object_unref(original); g_free(data);
    } else {
        GFile *file = g_file_new_for_path(argv[2]);
        GFileInputStream *stream = g_file_read(file, NULL, NULL);
        guint8 head[12];
        gsize length;
        g_assert_true(g_input_stream_read_all(G_INPUT_STREAM(stream), head, sizeof head, &length, NULL, NULL));
        pixbuf = frame_webp_decode_stream(G_INPUT_STREAM(stream), head, length, 4096, NULL, NULL, NULL, NULL);
        g_object_unref(stream); g_object_unref(file);
    }
    g_assert_nonnull(pixbuf);
    g_assert_cmpint(gdk_pixbuf_get_width(pixbuf), ==, 4096);
    g_assert_cmpint(gdk_pixbuf_get_height(pixbuf), ==, 2048);
    /* VmHWM belongs to this ELF's address space; inherited child rusage can
     * retain the launcher's much higher pre-exec maximum on this host. */
    gchar *status_text;
    g_assert_true(g_file_get_contents("/proc/self/status", &status_text, NULL, NULL));
    gchar *peak = strstr(status_text, "VmHWM:");
    g_assert_nonnull(peak);
    long peak_kib = strtol(peak + strlen("VmHWM:"), NULL, 10);
    printf("%s peak_rss_kib=%ld elapsed_ms=%.1f\n", argv[1], peak_kib,
        (g_get_monotonic_time() - start) / 1000.0);
    g_free(status_text);
    g_object_unref(pixbuf);
    return 0;
}
