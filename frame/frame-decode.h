/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#ifndef FRAME_MAX_INPUT
#define FRAME_MAX_INPUT (256u * 1024u * 1024u)
#endif
#define FRAME_MAX_PIXELS 64000000u

/* Reads one image, bounds encoded bytes and source dimensions, scales the
 * preview and applies embedded orientation. The caller owns the result. */
GdkPixbuf *frame_decode_stream(GInputStream *stream, int max_edge,
                               int *source_width, int *source_height,
                               GCancellable *cancel, GError **error);
