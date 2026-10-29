/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* Debian builds gdk-pixbuf without a WebP loader, so GdkPixbufLoader cannot
 * decode a .webp at all and gdk_pixbuf_get_formats() never reports image/webp.
 * Frame's own desktop entry already advertises image/webp, so decode those
 * files through libwebp instead of refusing them, and apply the same display
 * cap the pixbuf loader applies to every other format. */

#include "frame-webp.h"
#include "frame-decode.h"

#include <string.h>
#include <webp/decode.h>
#include <libexif/exif-data.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-tag.h>
#include <libexif/exif-utils.h>

/* Bounds so an absurd or hostile file fails closed instead of exhausting the
 * machine.  WebP itself tops out at 16383x16383. */
#define FRAME_WEBP_MAX_INPUT  FRAME_MAX_INPUT
#define FRAME_WEBP_MAX_PIXELS FRAME_MAX_PIXELS

/* RIFF/WEBP header: "RIFF", file size, "WEBP". */
#define FRAME_WEBP_HEADER_SIZE 12

/* EXIF orientation values; the enum is not published by libexif's headers. */
enum {
  FRAME_ORIENTATION_TOP_LEFT = 1,
  FRAME_ORIENTATION_TOP_RIGHT = 2,
  FRAME_ORIENTATION_BOTTOM_RIGHT = 3,
  FRAME_ORIENTATION_BOTTOM_LEFT = 4,
  FRAME_ORIENTATION_LEFT_TOP = 5,
  FRAME_ORIENTATION_RIGHT_TOP = 6,
  FRAME_ORIENTATION_RIGHT_BOTTOM = 7,
  FRAME_ORIENTATION_LEFT_BOTTOM = 8
};

static void
set_failure (GError **error, const char *message)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", message);
}

gboolean
frame_webp_peek (const guchar *data, gsize len)
{
  return data != NULL && len >= FRAME_WEBP_HEADER_SIZE
      && memcmp (data, "RIFF", 4) == 0
      && memcmp (data + 8, "WEBP", 4) == 0;
}

/* The advanced decoder writes directly into our capped RGBA allocation. */
static void
free_rgba (guchar *pixels, gpointer data)
{
  (void) data;
  g_free (pixels);
}

/* Read what is left of the stream after the already-consumed head. */
static GByteArray *
read_remainder (GInputStream *stream, const guchar *head, gsize head_len,
                GCancellable *cancel, GError **error)
{
  GByteArray *bytes = g_byte_array_new ();
  guchar buffer[65536];

  g_byte_array_append (bytes, head, head_len);
  while (TRUE)
    {
      if (bytes->len > FRAME_WEBP_MAX_INPUT)
        {
          g_byte_array_unref (bytes);
          set_failure (error, "The WebP file is too large to open");
          return NULL;
        }
      gssize length = g_input_stream_read (stream, buffer, sizeof buffer, cancel, error);
      if (length < 0)
        {
          g_byte_array_unref (bytes);
          return NULL;
        }
      if (length == 0)
        break;
      if (bytes->len + (gsize) length > FRAME_WEBP_MAX_INPUT)
        {
          g_byte_array_unref (bytes);
          set_failure (error, "The WebP file is too large to open");
          return NULL;
        }
      g_byte_array_append (bytes, buffer, length);
    }
  return bytes;
}

/* Read the EXIF orientation out of the container's EXIF chunk, if any.  WebP
 * stores the pixels unrotated, exactly as gdk-pixbuf would hand them over. */
static guint
read_orientation (const guchar *data, gsize len)
{
  guint orientation = FRAME_ORIENTATION_TOP_LEFT;
  gsize offset = FRAME_WEBP_HEADER_SIZE;

  while (offset + 8 <= len)
    {
      /* Chunk sizes are little-endian regardless of the host byte order. */
      guint32 size = (guint32) data[offset + 4]
                   | ((guint32) data[offset + 5] << 8)
                   | ((guint32) data[offset + 6] << 16)
                   | ((guint32) data[offset + 7] << 24);
      const guchar *id = data + offset;

      offset += 8;
      if (offset + size > len)
        break;
      if (memcmp (id, "EXIF", 4) == 0)
        {
          const guchar *payload = data + offset;
          ExifData *exif;
          ExifContent *content;
          ExifEntry *entry;

          /* libexif expects the APP1 "Exif\0\0" header that the WebP container
           * stores, and it only exposes the rendered description, so take the
           * raw SHORT out of the parsed entry instead. */
          exif = exif_data_new_from_data (payload, size);
          if (exif != NULL)
            {
              content = exif->ifd[EXIF_IFD_0];
              entry = content != NULL
                ? exif_content_get_entry (content, EXIF_TAG_ORIENTATION) : NULL;
              if (entry != NULL && entry->format == EXIF_FORMAT_SHORT && entry->size >= 2)
                orientation = exif_get_short (entry->data, exif_data_get_byte_order (exif));
              exif_data_unref (exif);
            }
          break;
        }
      offset += size + (size & 1);
    }
  return orientation;
}

/* Same mapping gdk-pixbuf applies for a pixbuf's embedded orientation. */
static GdkPixbuf *
apply_orientation (GdkPixbuf *pixbuf, guint orientation)
{
  GdkPixbuf *result = pixbuf;
  GdkPixbuf *step;

  switch (orientation)
    {
    case FRAME_ORIENTATION_TOP_RIGHT:
      result = gdk_pixbuf_flip (result, TRUE);
      break;
    case FRAME_ORIENTATION_BOTTOM_RIGHT:
      result = gdk_pixbuf_rotate_simple (result, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
      break;
    case FRAME_ORIENTATION_BOTTOM_LEFT:
      result = gdk_pixbuf_flip (result, FALSE);
      break;
    case FRAME_ORIENTATION_LEFT_TOP:
      step = gdk_pixbuf_flip (result, TRUE);
      result = gdk_pixbuf_rotate_simple (step, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
      g_object_unref (step);
      break;
    case FRAME_ORIENTATION_RIGHT_TOP:
      result = gdk_pixbuf_rotate_simple (result, GDK_PIXBUF_ROTATE_CLOCKWISE);
      break;
    case FRAME_ORIENTATION_RIGHT_BOTTOM:
      step = gdk_pixbuf_flip (result, TRUE);
      result = gdk_pixbuf_rotate_simple (step, GDK_PIXBUF_ROTATE_CLOCKWISE);
      g_object_unref (step);
      break;
    case FRAME_ORIENTATION_LEFT_BOTTOM:
      result = gdk_pixbuf_rotate_simple (result, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
      break;
    default:
      break;
    }
  return result;
}

static GdkPixbuf *
decode (const guchar *data, gsize len, int max_edge, int *source_width, int *source_height, GError **error)
{
  WebPBitstreamFeatures features;
  GdkPixbuf *decoded, *oriented;
  WebPDecoderConfig config;
  guchar *rgba;
  int width, height;

  /* WebPGetFeatures wants the whole RIFF container, and returns a VP8StatusCode
   * whose success value is zero. */
  if (WebPGetFeatures (data, len, &features) != VP8_STATUS_OK)
    {
      set_failure (error, "Not a valid WebP image");
      return NULL;
    }
  if (features.width <= 0 || features.height <= 0)
    {
      set_failure (error, "The WebP image has no dimensions");
      return NULL;
    }
  if ((guint64) features.width * (guint64) features.height > FRAME_WEBP_MAX_PIXELS)
    {
      set_failure (error, "The WebP image is too large to display safely");
      return NULL;
    }
  if (source_width) *source_width = features.width;
  if (source_height) *source_height = features.height;

  width = features.width;
  height = features.height;
  if (max_edge > 0 && (width > max_edge || height > max_edge))
    {
      double scale = MIN ((double) max_edge / width, (double) max_edge / height);
      width = MAX (1, (int) (width * scale));
      height = MAX (1, (int) (height * scale));
    }
  if (!WebPInitDecoderConfig (&config))
    {
      set_failure (error, "Could not initialize the WebP decoder");
      return NULL;
    }
  rgba = g_try_malloc ((gsize) width * height * 4);
  if (rgba == NULL)
    {
      set_failure (error, "Not enough memory to decode the WebP image");
      return NULL;
    }
  config.output.colorspace = MODE_RGBA;
  config.output.is_external_memory = 1;
  config.output.u.RGBA.rgba = rgba;
  config.output.u.RGBA.stride = width * 4;
  config.output.u.RGBA.size = (gsize) width * height * 4;
  config.options.use_scaling = width != features.width || height != features.height;
  config.options.scaled_width = width;
  config.options.scaled_height = height;
  VP8StatusCode status = WebPDecode (data, len, &config);
  WebPFreeDecBuffer (&config.output);
  if (status != VP8_STATUS_OK)
    {
      g_free (rgba);
      set_failure (error, "Could not decode the WebP image");
      return NULL;
    }

  decoded = gdk_pixbuf_new_from_data (rgba, GDK_COLORSPACE_RGB, TRUE, 8,
                                      width, height, width * 4,
                                      free_rgba, NULL);
  if (decoded == NULL)
    {
      g_free (rgba);
      set_failure (error, "Could not decode the WebP image");
      return NULL;
    }

  oriented = apply_orientation (decoded, read_orientation (data, len));
  if (oriented != decoded)
    g_object_unref (decoded);

  return oriented;
}

GdkPixbuf *
frame_webp_decode_stream (GInputStream *stream, const guchar *head, gsize head_len,
                          int max_edge, int *source_width, int *source_height,
                          GCancellable *cancel, GError **error)
{
  GByteArray *bytes;
  GdkPixbuf *pixbuf;

  g_return_val_if_fail (G_IS_INPUT_STREAM (stream), NULL);

  bytes = read_remainder (stream, head, head_len, cancel, error);
  if (bytes == NULL)
    return NULL;
  if (g_cancellable_set_error_if_cancelled (cancel, error))
    {
      g_byte_array_unref (bytes);
      return NULL;
    }
  pixbuf = decode ((const guchar *) bytes->data, bytes->len, max_edge, source_width, source_height, error);
  g_byte_array_unref (bytes);
  if (pixbuf && g_cancellable_set_error_if_cancelled (cancel, error))
    g_clear_object (&pixbuf);
  return pixbuf;
}
