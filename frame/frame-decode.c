/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include "frame-decode.h"
#include "frame-webp.h"

typedef struct { int max_edge, width, height; gboolean rejected; } DecodeSize;

static void prepared(GdkPixbufLoader *loader, int width, int height, gpointer data)
{
    DecodeSize *size = data;
    size->width = width;
    size->height = height;
    if (width <= 0 || height <= 0 || (guint64)width * height > FRAME_MAX_PIXELS) {
        size->rejected = TRUE;
        /* Some loaders allocate during the same write that reports size. */
        gdk_pixbuf_loader_set_size(loader, 1, 1);
        return;
    }
    if (width > size->max_edge || height > size->max_edge) {
        double scale = MIN((double)size->max_edge / width, (double)size->max_edge / height);
        gdk_pixbuf_loader_set_size(loader, MAX(1, (int)(width * scale)), MAX(1, (int)(height * scale)));
    }
}

GdkPixbuf *frame_decode_stream(GInputStream *stream, int max_edge,
                               int *source_width, int *source_height,
                               GCancellable *cancel, GError **error)
{
    g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
    g_return_val_if_fail(max_edge > 0, NULL);
    guchar block[65536];
    gsize head_len = 0;
    if (!g_input_stream_read_all(stream, block, 12, &head_len, cancel, error)) return NULL;
    gssize count = (gssize)head_len;
    if (frame_webp_peek(block, count))
        return frame_webp_decode_stream(stream, block, count, max_edge, source_width, source_height, cancel, error);

    DecodeSize size = {.max_edge = max_edge};
    g_autoptr(GdkPixbufLoader) loader = gdk_pixbuf_loader_new();
    g_signal_connect(loader, "size-prepared", G_CALLBACK(prepared), &size);
    gsize consumed = 0;
    gboolean ok = TRUE;
    while (count > 0) {
        if ((gsize)count > FRAME_MAX_INPUT - consumed) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Image input exceeds the size limit");
            ok = FALSE;
            break;
        }
        consumed += count;
        if (!gdk_pixbuf_loader_write(loader, block, count, error)) { ok = FALSE; break; }
        if (size.rejected) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Image dimensions exceed the size limit");
            ok = FALSE;
            break;
        }
        count = g_input_stream_read(stream, block, sizeof block, cancel, error);
        if (count < 0) { ok = FALSE; break; }
    }
    if (!gdk_pixbuf_loader_close(loader, ok ? error : NULL)) ok = FALSE;
    if (!ok) return NULL;
    /* Some loaders (notably SVG) announce dimensions only on close. */
    if (size.rejected) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Image dimensions exceed the size limit");
        return NULL;
    }
    GdkPixbuf *decoded = gdk_pixbuf_loader_get_pixbuf(loader);
    if (!decoded) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Could not decode the image");
        return NULL;
    }
    if (g_cancellable_set_error_if_cancelled(cancel, error)) return NULL;
    if (source_width) *source_width = size.width;
    if (source_height) *source_height = size.height;
    return gdk_pixbuf_apply_embedded_orientation(decoded);
}
