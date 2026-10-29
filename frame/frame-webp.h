/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FRAME_WEBP_H
#define FRAME_WEBP_H

#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

/* TRUE when the leading bytes are a RIFF/WEBP container. */
gboolean frame_webp_peek (const guchar *data, gsize len);

/* Consume the rest of @stream -- whose first @head_len bytes were already read
 * into @head -- and decode the image.  @max_edge caps the longest edge of the
 * result, matching the display cap applied to every other format. */
GdkPixbuf *frame_webp_decode_stream (GInputStream *stream,
                                     const guchar  *head,
                                     gsize          head_len,
                                     int            max_edge,
                                     int           *source_width,
                                     int           *source_height,
                                     GCancellable  *cancel,
                                     GError       **error);

G_END_DECLS

#endif // FRAME_WEBP_H
