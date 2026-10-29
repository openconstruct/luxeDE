/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include "../frame-decode.h"
#include <string.h>
#include <webp/encode.h>

static GdkPixbuf *decode(const void *bytes, gsize length, GCancellable *cancel, GError **error)
{
    g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_data(bytes, length, NULL);
    return frame_decode_stream(stream, 32, NULL, NULL, cancel, error);
}

int main(void)
{
    g_autoptr(GdkPixbuf) original = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 32);
    gdk_pixbuf_fill(original, 0x3180a0ff);
    g_autofree char *png = NULL;
    gsize png_len = 0;
    g_assert_true(gdk_pixbuf_save_to_buffer(original, &png, &png_len, "png", NULL, NULL));
    g_autoptr(GError) error = NULL;
    g_autoptr(GdkPixbuf) image = decode(png, png_len, NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(image);
    g_assert_cmpint(gdk_pixbuf_get_width(image), ==, 32);
    g_assert_cmpint(gdk_pixbuf_get_height(image), ==, 16);


    /* A valid image followed by excessive bytes is still an oversized stream. */
    g_autofree char *large = g_malloc0(FRAME_MAX_INPUT + 1);
    memcpy(large, png, png_len);
    g_assert_null(decode(large, FRAME_MAX_INPUT + 1, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);

    const char *svg = "<svg xmlns='http://www.w3.org/2000/svg' width='10000' height='10000'></svg>";
    g_assert_null(decode(svg, strlen(svg), NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);

    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_assert_null(decode(png, png_len, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);

    guint8 rgba[4 * 4 * 4] = {0};
    guint8 *webp = NULL;
    size_t webp_len = WebPEncodeLosslessRGBA(rgba, 4, 4, 16, &webp);
    g_assert_cmpuint(webp_len, >, 0);
    g_clear_object(&image);
    image = decode(webp, webp_len, NULL, &error);
    g_assert_no_error(error); g_assert_nonnull(image);
    memset(large, 0, FRAME_MAX_INPUT + 1);
    memcpy(large, webp, webp_len);
    g_assert_null(decode(large, FRAME_MAX_INPUT + 1, NULL, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    WebPFree(webp);
    return 0;
}
