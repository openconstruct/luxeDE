/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <gtk/gtk.h>

/* Immutable display pixels; transforms share storage and never modify files. */
GdkPaintable *frame_image_new(GdkPixbuf *pixels);
GdkPaintable *frame_image_rotate(GdkPaintable *image, gboolean clockwise);
GdkPaintable *frame_image_flip(GdkPaintable *image, gboolean horizontal);
