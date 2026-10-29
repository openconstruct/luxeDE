/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#include "frame-image.h"

typedef struct {
    GObject parent;
    cairo_surface_t *surface;
    cairo_matrix_t matrix;
    int width, height;
} FrameImage;
typedef GObjectClass FrameImageClass;
static void paintable_init(GdkPaintableInterface *iface);
G_DEFINE_TYPE_WITH_CODE(FrameImage, frame_image, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, paintable_init))

static void snapshot(GdkPaintable *paintable, GdkSnapshot *snapshot, double width, double height)
{
    FrameImage *image = (FrameImage *)paintable;
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    cairo_t *cr = gtk_snapshot_append_cairo(GTK_SNAPSHOT(snapshot), &bounds);
    cairo_scale(cr, width/image->width, height/image->height);
    cairo_transform(cr, &image->matrix);
    cairo_set_source_surface(cr, image->surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_paint(cr);
    cairo_destroy(cr);
}
static GdkPaintableFlags flags(GdkPaintable *image)
{ (void)image; return GDK_PAINTABLE_STATIC_SIZE | GDK_PAINTABLE_STATIC_CONTENTS; }
static int width(GdkPaintable *image) { return ((FrameImage *)image)->width; }
static int height(GdkPaintable *image) { return ((FrameImage *)image)->height; }
static double ratio(GdkPaintable *image) { return (double)width(image)/height(image); }
static void paintable_init(GdkPaintableInterface *iface)
{
    iface->snapshot = snapshot;
    iface->get_flags = flags;
    iface->get_intrinsic_width = width;
    iface->get_intrinsic_height = height;
    iface->get_intrinsic_aspect_ratio = ratio;
}
static void finalize(GObject *object)
{
    cairo_surface_destroy(((FrameImage *)object)->surface);
    G_OBJECT_CLASS(frame_image_parent_class)->finalize(object);
}
static void frame_image_class_init(FrameImageClass *klass)
{ klass->finalize = finalize; }
static void frame_image_init(FrameImage *image)
{ cairo_matrix_init_identity(&image->matrix); }

GdkPaintable *frame_image_new(GdkPixbuf *pixels)
{
    /* GDK's public conversion preserves alpha/premultiplication exactly as the
     * toolkit does. Keep its surface, not the pixbuf plus a texture download.
     * The worker releases source pixels before presenting the result. */
    cairo_surface_t *dummy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(dummy);
    cairo_surface_t *surface = NULL;
    gdk_cairo_set_source_pixbuf(cr, pixels, 0, 0);
    if (cairo_status(cr)==CAIRO_STATUS_SUCCESS &&
        cairo_pattern_get_surface(cairo_get_source(cr), &surface)==CAIRO_STATUS_SUCCESS &&
        cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS)
        cairo_surface_reference(surface);
    else surface = NULL;
    cairo_destroy(cr);
    cairo_surface_destroy(dummy);
    if (!surface) return NULL;
    FrameImage *image = g_object_new(frame_image_get_type(), NULL);
    image->surface = surface;
    image->width = gdk_pixbuf_get_width(pixels);
    image->height = gdk_pixbuf_get_height(pixels);
    return GDK_PAINTABLE(image);
}

static GdkPaintable *transformed(GdkPaintable *paintable, cairo_matrix_t *delta, gboolean swap)
{
    FrameImage *old = (FrameImage *)paintable;
    FrameImage *image = g_object_new(frame_image_get_type(), NULL);
    image->surface = cairo_surface_reference(old->surface);
    image->width = swap ? old->height : old->width;
    image->height = swap ? old->width : old->height;
    cairo_matrix_multiply(&image->matrix, &old->matrix, delta);
    return GDK_PAINTABLE(image);
}
GdkPaintable *frame_image_rotate(GdkPaintable *paintable, gboolean clockwise)
{
    cairo_matrix_t delta;
    if (clockwise) cairo_matrix_init(&delta, 0, 1, -1, 0, height(paintable), 0);
    else cairo_matrix_init(&delta, 0, -1, 1, 0, 0, width(paintable));
    return transformed(paintable, &delta, TRUE);
}
GdkPaintable *frame_image_flip(GdkPaintable *paintable, gboolean horizontal)
{
    cairo_matrix_t delta;
    cairo_matrix_init(&delta, horizontal ? -1 : 1, 0, 0, horizontal ? 1 : -1,
                      horizontal ? width(paintable) : 0, horizontal ? 0 : height(paintable));
    return transformed(paintable, &delta, FALSE);
}
