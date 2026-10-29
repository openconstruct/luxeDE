/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libexif/exif-data.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-tag.h>

#include "frame-exif.h"

/* Build a display summary of interesting EXIF tags.  Returns a NULL-terminated
 * array of "Label" / "Value" pairs (two strings per row) or NULL when the file
 * carries no EXIF data. */
char **
frame_exif_summary (const char *path)
{
	ExifData *data;
	GPtrArray *rows;

	data = exif_data_new_from_file (path);
	if (data == NULL)
		return NULL;

	rows = g_ptr_array_new_with_free_func (g_free);

	for (int ifd = 0; ifd < EXIF_IFD_COUNT; ifd++)
	{
		ExifContent *content = data->ifd[ifd];
		if (content == NULL)
			continue;
		for (unsigned i = 0; i < content->count; i++)
		{
			ExifEntry *entry = content->entries[i];
			const char *label = NULL;
			char value[1024] = {0};

			switch (entry->tag)
			{
				case EXIF_TAG_MAKE: label = _("Camera make"); break;
				case EXIF_TAG_MODEL: label = _("Camera model"); break;
				case EXIF_TAG_DATE_TIME: label = _("Date and time"); break;
				case EXIF_TAG_DATE_TIME_ORIGINAL: label = _("Date taken"); break;
				case EXIF_TAG_EXPOSURE_TIME: label = _("Exposure"); break;
				case EXIF_TAG_FNUMBER: label = _("Aperture"); break;
				case EXIF_TAG_ISO_SPEED_RATINGS: label = _("ISO speed"); break;
				case EXIF_TAG_FOCAL_LENGTH: label = _("Focal length"); break;
				case EXIF_TAG_FLASH: label = _("Flash"); break;
				case EXIF_TAG_ORIENTATION: label = _("Orientation (as recorded)"); break;
				default: continue;
			}

			exif_entry_get_value (entry, value, sizeof (value));
			if (value[0] == '\0')
				continue;
			g_ptr_array_add (rows, g_strdup (_(label)));
			g_ptr_array_add (rows, g_strdup (value));
		}
	}

	exif_data_unref (data);

	if (rows->len == 0)
	{
		g_ptr_array_unref (rows);
		return NULL;
	}
	g_ptr_array_add (rows, NULL);
	return (char **) g_ptr_array_free (rows, FALSE);
}
