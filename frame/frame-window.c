/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

#include <gtk/gtk.h>
#include "luxe-gtk-ui.h"
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "frame-window.h"
#include "frame-app.h"
#include "frame-exif.h"
#include "frame-decode.h"
#include "frame-image.h"

/* Display textures are capped so huge photographs stay light on 4 GB
 * machines; zooming past the cap interpolates from the texture. */
#define FRAME_TEXTURE_MAX_EDGE 4096
#define FRAME_ZOOM_MIN 0.1
#define FRAME_ZOOM_MAX 8.0

typedef struct
{
	GFile *file;
	char *name;					/* display (decoded) file name */
	char *collate;				/* natural-order sort key */
	char *ctype;				/* content type */
	goffset size;
} FrameEntry;

static void
frame_entry_free (FrameEntry * entry)
{
	g_clear_object (&entry->file);
	g_free (entry->name);
	g_free (entry->collate);
	g_free (entry->ctype);
	g_free (entry);
}

struct _FrameWindow
{
	GtkApplicationWindow parent;

	GSettings *settings;
	gboolean closed;
	gboolean load_failed;
	guint rebuild_id;
	gboolean image_cleanup_pending;
	double drag_x, drag_y, pinch_base;

	// Template widgets
	GtkWidget *picture;
	GtkWidget *scroller;
	GtkWidget *headerbar;
	GtkWidget *title_label;
	GtkWidget *subtitle_label;
	GtkWidget *status_label;
	GtkWidget *zoom_label;
	GtkWidget *error_revealer;
	GtkWidget *error_label;
	GtkWidget *statusbar;

	// Current image
	GdkPaintable *paintable;	/* NULL when no image is open */
	GFile *current_file;
	int source_width, source_height; /* File pixels, before EXIF or view transforms. */

	// Loading (sequence numbers discard results from superseded requests)
	GCancellable *load_cancel;
	GFile *load_pending; /* Keep only the newest request while a decode is busy. */
	gboolean load_running;
	guint load_seq;
	GCancellable *trash_cancel;
	guint error_timeout_id;

	// Folder image list
	GFile *folder;
	GPtrArray *entries;			/* FrameEntry*, sorted */
	int index;					/* position in entries, -1 when unset */
	GFileMonitor *monitor;
	guint list_seq;
	GCancellable *list_cancel;

	// Zoom
	double zoom;				/* current scale factor */
	int zoom_mode;				/* 0 = fit, 1 = 100%, 2 = custom */
	int fit_last_width;
	int fit_last_height;
	GdkFrameClock *fit_clock;

	// Slideshow
	guint slideshow_id;
};

G_DEFINE_TYPE (FrameWindow, frame_window, GTK_TYPE_APPLICATION_WINDOW)

static void frame_window_set_paintable (FrameWindow * win, GdkPaintable * paintable);
static void frame_window_update_status (FrameWindow * win);
static void frame_window_apply_zoom (FrameWindow * win);
static void frame_window_maybe_refit (FrameWindow * win);
static void frame_window_show_error (FrameWindow * win, const char *message);
static void frame_window_load (FrameWindow * win, GFile * file);
static void frame_window_navigate (FrameWindow * win, int direction);
static void frame_window_stop_slideshow (FrameWindow * win);
static void frame_window_rebuild_list (FrameWindow * win);
static void frame_window_set_folder (FrameWindow * win, GFile * folder);
static gboolean frame_window_find_current(FrameWindow *win);

/* Pixbuf formats supported by this installation, by MIME type. */
static GHashTable *
frame_supported_types (void)
{
	static GHashTable *table = NULL;
	GSList *formats, *iter;

	if (G_UNLIKELY (table == NULL))
	{
		table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		formats = gdk_pixbuf_get_formats ();
		for (iter = formats; iter != NULL; iter = iter->next)
		{
			gchar **mimes = gdk_pixbuf_format_get_mime_types (iter->data);
			for (int i = 0; mimes[i] != NULL; i++)
				g_hash_table_add (table, g_strdup (mimes[i]));
			g_strfreev (mimes);
		}
		g_slist_free (formats);
		/* Debian's gdk-pixbuf ships no WebP loader, so the loop above
		 * never sees image/webp even though Frame decodes it itself. */
		g_hash_table_add (table, g_strdup ("image/webp"));
	}
	return table;
}

static gboolean
frame_content_supported (const char *ctype)
{
	return ctype != NULL && g_hash_table_contains (frame_supported_types (), ctype);
}

/*** Error banner ***/

static gboolean
frame_error_hide (FrameWindow * win)
{
	win->error_timeout_id = 0;
	gtk_revealer_set_reveal_child (GTK_REVEALER (win->error_revealer), FALSE);
	return G_SOURCE_REMOVE;
}

static void
frame_window_show_error (FrameWindow * win, const char *message)
{
	if (win->closed) return;
	if (win->error_timeout_id != 0)
	{
		g_source_remove (win->error_timeout_id);
		win->error_timeout_id = 0;
	}
	gtk_label_set_text (GTK_LABEL (win->error_label), message);
	gtk_revealer_set_reveal_child (GTK_REVEALER (win->error_revealer), TRUE);
	win->error_timeout_id = g_timeout_add_seconds (4, (GSourceFunc) frame_error_hide, win);
}

/*** Image loading ***/

/* Tokens discard results from superseded loads: rapid navigation must
 * never apply an older image after a newer request. */
typedef struct
{
	FrameWindow *win;			/* strong reference */
	GCancellable *cancel;
	GFile *file;
	guint seq;
} LoadToken;

typedef struct {
    GdkPaintable *image;
    int width, height;
} DecodedImage;

static void decoded_image_free(DecodedImage *decoded)
{
    g_clear_object(&decoded->image);
    g_free(decoded);
}

static void
load_token_free (LoadToken * token)
{
	g_clear_object (&token->cancel);
	g_clear_object (&token->file);
	g_clear_object (&token->win);
	g_free (token);
}

static void
frame_window_cancel_load (FrameWindow * win)
{
	win->load_seq++;
	if (win->load_cancel != NULL)
	{
		g_cancellable_cancel (win->load_cancel);
		g_clear_object (&win->load_cancel);
	}
}

static void
on_scaled (GObject * source, GAsyncResult * result, gpointer user_data)
{
	LoadToken *token = user_data;
	FrameWindow *win = token->win;
	DecodedImage *decoded;
	GError *error = NULL;
	(void) source;

	decoded = g_task_propagate_pointer (G_TASK (result), &error);
	win->load_running = FALSE;
	if (win->load_pending != NULL)
	{
		GFile *pending = g_steal_pointer (&win->load_pending);
		g_clear_pointer (&decoded, decoded_image_free);
		g_clear_error (&error);
		load_token_free (token);
		if (!win->closed)
		{
			frame_window_load (win, pending);
		}
		g_clear_object (&pending);
		return;
	}
	if (decoded == NULL)
	{
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
		    && token->seq == win->load_seq)
		{
			win->load_failed = TRUE;
			frame_window_show_error (win, _("Could not read the image"));
			frame_window_update_status(win);
			if (error != NULL)
				g_message ("frame: image load failed: %s", error->message);
		}
		g_clear_error (&error);
		load_token_free (token);
		return;
	}

	if (token->seq != win->load_seq)
	{
		decoded_image_free (decoded);
		load_token_free (token);
		return;
	}

	win->source_width = decoded->width;
	win->source_height = decoded->height;
	frame_window_set_paintable (win, decoded->image);
	decoded_image_free (decoded);

	// Keyboard navigation lives on the window; keep focus on the image
	gtk_window_set_focus (GTK_WINDOW (win), win->picture);

	// Apply the preferred zoom mode for a freshly opened image
	win->zoom_mode = g_settings_get_int (win->settings, "zoom-mode");
	frame_window_apply_zoom (win);
	frame_window_maybe_refit (win);
	frame_window_update_status (win);
	load_token_free (token);
}

static void scale_task_func(GTask *task, gpointer source, gpointer task_data, GCancellable *cancel)
{
    (void)source;
    GFile *file = task_data;
    GError *error = NULL;
    GFileInputStream *stream = g_file_read(file, cancel, &error);
    if (!stream) {
        g_task_return_error(task, error);
        return;
    }
    int width = 0, height = 0;
    GdkPixbuf *oriented = frame_decode_stream(G_INPUT_STREAM(stream), FRAME_TEXTURE_MAX_EDGE, &width, &height, cancel, &error);
    g_object_unref(stream);
    if (error) g_task_return_error(task, error);
    else if (oriented) {
        GdkPaintable *image = frame_image_new(oriented);
        g_object_unref(oriented);
        if (image) {
            DecodedImage *decoded = g_new0(DecodedImage, 1);
            decoded->image = image; decoded->width = width; decoded->height = height;
            g_task_return_pointer(task, decoded, (GDestroyNotify)decoded_image_free);
        }
        else g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot prepare image pixels");
    }
    else g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not decode the image");
}

static void frame_window_load(FrameWindow *win, GFile *file)
{
    if (win->closed) return;
    frame_window_cancel_load(win);
    win->load_failed = FALSE;
    g_set_object(&win->current_file, file);
    win->source_width = win->source_height = 0;
    win->index = -1;
    if (win->entries) frame_window_find_current(win);
    frame_window_set_paintable(win, NULL);
    frame_window_apply_zoom(win);
    frame_window_update_status(win);
    if (win->load_running) {
        /* A decoder may be in a non-interruptible codec call. Do not start
         * another large decode beside it; retain only the latest navigation
         * target and start it when the cancelled worker returns. */
        g_set_object(&win->load_pending, file);
        return;
    }
    win->load_cancel = g_cancellable_new();
    LoadToken *token = g_new0(LoadToken, 1);
    token->win = g_object_ref(win);
    token->file = g_object_ref(file);
    token->cancel = g_object_ref(win->load_cancel);
    token->seq = win->load_seq;
    win->load_running = TRUE;
    GTask *task = g_task_new(NULL, token->cancel, on_scaled, token);
    g_task_set_task_data(task, g_object_ref(file), g_object_unref);
    g_task_run_in_thread(task, scale_task_func);
    g_object_unref(task);
}

/*** Folder listing ***/

typedef struct
{
	FrameWindow *win;			/* strong reference */
	guint seq;
	GFileEnumerator *enumerator;
	GPtrArray *entries;			/* FrameEntry*, sorted on completion */
	GCancellable *cancel;
} ListToken;

static void
list_token_free (ListToken * token)
{
	g_clear_object (&token->win);
	g_clear_object (&token->enumerator);
	if (token->entries != NULL)
		g_ptr_array_unref (token->entries);
	g_clear_object (&token->cancel);
	g_free (token);
}

static void frame_window_cancel_list (FrameWindow * win);

static gint
frame_entry_compare (gconstpointer a, gconstpointer b)
{
	const FrameEntry *ea = *(FrameEntry * const *) a;
	const FrameEntry *eb = *(FrameEntry * const *) b;
	return strcmp (ea->collate, eb->collate);
}

static gboolean
frame_window_find_current (FrameWindow * win)
{
	if (win->current_file == NULL)
		return FALSE;
	for (guint i = 0; i < win->entries->len; i++)
	{
		FrameEntry *entry = g_ptr_array_index (win->entries, i);
		if (g_file_equal (entry->file, win->current_file))
		{
			win->index = i;
			return TRUE;
		}
	}
	return FALSE;
}

static void
on_list_closed (GObject * source, GAsyncResult * result, gpointer user_data)
{
	ListToken *token = user_data;
	FrameWindow *win = token->win;
	GError *error = NULL;
	(void) source;

	if (!g_file_enumerator_close_finish (token->enumerator, result, &error))
	{
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
		    && token->seq == win->list_seq)
			g_warning ("frame: enumerate close: %s", error->message);
		g_clear_error (&error);
	}

	// The most recent enumeration wins; older ones are discarded
	if (token->seq == win->list_seq)
	{
		g_ptr_array_sort (token->entries, frame_entry_compare);
		if (win->entries != NULL)
			g_ptr_array_unref (win->entries);
		win->entries = token->entries;
		token->entries = NULL;
		win->index = -1;
		frame_window_find_current(win);
		frame_window_update_status(win);
	}
	list_token_free (token);
}

static void list_close_and_finish (ListToken * token);

/* Batch size keeps the UI responsive on slow (GVfs/network) filesystems. */
#define FRAME_LIST_BATCH 100

static void on_next_files (GObject * source, GAsyncResult * result, gpointer user_data);
static void list_request_batch (ListToken * token);

static void
list_close_and_finish (ListToken * token)
{
	g_file_enumerator_close_async (token->enumerator,
	                               G_PRIORITY_DEFAULT,
	                               token->cancel,
	                               on_list_closed,
	                               token);
}

static void
list_request_batch (ListToken * token)
{
	g_file_enumerator_next_files_async (token->enumerator,
	                                    FRAME_LIST_BATCH,
	                                    G_PRIORITY_DEFAULT,
	                                    token->cancel,
	                                    on_next_files,
	                                    token);
}

static void
on_next_files (GObject * source, GAsyncResult * result, gpointer user_data)
{
	ListToken *token = user_data;
	FrameWindow *win = token->win;
	GList *infos, *iter;
	GError *error = NULL;
	(void) source;

	infos = g_file_enumerator_next_files_finish (token->enumerator,
	                                             result, &error);
	if (infos == NULL)
	{
		if (error != NULL)
		{
			if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
			    && token->seq == win->list_seq)
				g_warning ("frame: enumerate: %s", error->message);
			g_clear_error (&error);
		}
		list_close_and_finish (token);
		return;
	}

	for (iter = infos; iter != NULL; iter = iter->next)
	{
		GFileInfo *info = G_FILE_INFO (iter->data);
		GFile *child;
		FrameEntry *entry;
		const char *name, *ctype;

		ctype = g_file_info_get_content_type (info);
		if (g_file_info_get_is_hidden (info)
		    || g_file_info_get_file_type (info) != G_FILE_TYPE_REGULAR
		    || !frame_content_supported (ctype))
			continue;

		name = g_file_info_get_display_name (info);
		child = g_file_enumerator_get_child (token->enumerator, info);
		entry = g_new0 (FrameEntry, 1);
		entry->file = child;
		entry->name = g_strdup (name);
		entry->ctype = g_strdup (ctype);
		entry->size = g_file_info_get_size (info);
		entry->collate = g_utf8_collate_key_for_filename (name, -1);
		g_ptr_array_add (token->entries, entry);
	}
	g_list_free_full (infos, g_object_unref);

	// The most recent enumeration wins; older ones are discarded
	if (token->seq != win->list_seq)
	{
		list_close_and_finish (token);
		return;
	}

	list_request_batch (token);
}

static void
on_enumerated (GObject * source, GAsyncResult * result, gpointer user_data)
{
	ListToken *token = user_data;
	FrameWindow *win = token->win;
	GError *error = NULL;

	token->enumerator = g_file_enumerate_children_finish (G_FILE (source),
	                                                     result, &error);
	if (token->enumerator == NULL)
	{
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
		    && token->seq == win->list_seq)
			frame_window_show_error (win, _("Could not open the folder"));
		g_clear_error (&error);
		list_token_free (token);
		return;
	}

	token->entries = g_ptr_array_new_with_free_func ((GDestroyNotify) frame_entry_free);
	list_request_batch (token);
}

static void
frame_window_cancel_list (FrameWindow * win)
{
	win->list_seq++;
	if (win->list_cancel != NULL)
	{
		g_cancellable_cancel (win->list_cancel);
		g_clear_object (&win->list_cancel);
	}
}

static void
frame_window_rebuild_list (FrameWindow * win)
{
	ListToken *token;

	if (win->folder == NULL || win->closed)
		return;
	frame_window_cancel_list (win);
	win->list_cancel = g_cancellable_new ();
	token = g_new0 (ListToken, 1);
	token->win = g_object_ref (win);
	token->seq = win->list_seq;
	token->cancel = g_object_ref (win->list_cancel);
	g_file_enumerate_children_async (win->folder,
	                                 G_FILE_ATTRIBUTE_STANDARD_NAME ","
	                                 G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
	                                 G_FILE_ATTRIBUTE_STANDARD_TYPE ","
	                                 G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
	                                 G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
	                                 G_FILE_ATTRIBUTE_STANDARD_SIZE,
	                                 G_FILE_QUERY_INFO_NONE,
	                                 G_PRIORITY_DEFAULT,
	                                 token->cancel,
	                                 on_enumerated,
	                                 token);
}

static gboolean rebuild_timeout(gpointer data)
{
	FrameWindow *win = data;
	win->rebuild_id = 0;
	frame_window_rebuild_list(win);
	return G_SOURCE_REMOVE;
}

static void
on_folder_changed (GFileMonitor * monitor G_GNUC_UNUSED, GFile * file G_GNUC_UNUSED, GFile * other G_GNUC_UNUSED,
                   GFileMonitorEvent event G_GNUC_UNUSED, FrameWindow * win)
{
	// Re-list the folder asynchronously; the current image stays selected
	if (!win->closed && !win->rebuild_id)
		win->rebuild_id = g_timeout_add(100, rebuild_timeout, win);
}

static void
frame_window_set_folder (FrameWindow * win, GFile * folder)
{
	if (win->folder != NULL && g_file_equal (win->folder, folder))
		return;

	g_set_object (&win->folder, folder);
	g_clear_pointer(&win->entries, g_ptr_array_unref);
	win->index = -1;

	if (win->monitor != NULL)
	{
		g_file_monitor_cancel (win->monitor);
		g_clear_object (&win->monitor);
	}
	win->monitor = g_file_monitor_directory (folder, G_FILE_MONITOR_NONE, NULL, NULL);
	if (win->monitor != NULL)
		g_signal_connect (win->monitor, "changed", G_CALLBACK (on_folder_changed), win);

	frame_window_rebuild_list (win);
}

/*** Zoom and display ***/

static void
frame_window_set_paintable (FrameWindow * win, GdkPaintable * paintable)
{
	g_set_object (&win->paintable, paintable);
	gtk_picture_set_paintable (GTK_PICTURE (win->picture), paintable);
	win->image_cleanup_pending = TRUE;
}

static int
frame_image_width (FrameWindow * win)
{
	return win->paintable != NULL ? gdk_paintable_get_intrinsic_width(win->paintable) : 0;
}

static int
frame_image_height (FrameWindow * win)
{
	return win->paintable != NULL ? gdk_paintable_get_intrinsic_height(win->paintable) : 0;
}

static void
frame_window_apply_zoom (FrameWindow * win)
{
	int iw = frame_image_width (win);
	int ih = frame_image_height (win);
	int w, h;
	gchar *zoomtext;

	if (iw < 1 || ih < 1)
	{
		gtk_widget_set_size_request (win->picture, -1, -1);
		gtk_label_set_text (GTK_LABEL (win->zoom_label), "");
		return;
	}

	if (win->zoom_mode == 0)
	{
		// Fit the visible area
		GtkWidget *viewport = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (win->scroller));
		int vw = gtk_widget_get_width (viewport);
		int vh = gtk_widget_get_height (viewport);
		if (vw > 0 && vh > 0)
		{
			win->fit_last_width = vw;
			win->fit_last_height = vh;
			win->zoom = MIN ((double) vw / iw, (double) vh / ih);
		}
	}
	else if (win->zoom_mode == 1)
	{
		win->zoom = 1.0;
	}
	else
	{
		win->zoom = CLAMP (win->zoom, FRAME_ZOOM_MIN, FRAME_ZOOM_MAX);
	}

	w = MAX (1, (int) (iw * win->zoom));
	h = MAX (1, (int) (ih * win->zoom));
	gtk_widget_set_size_request (win->picture, w, h);

	zoomtext = g_strdup_printf (MAX(win->source_width, win->source_height) > FRAME_TEXTURE_MAX_EDGE
	    ? _("%.0f%% (preview)") : _("%.0f%%"), win->zoom * 100.0);
	gtk_label_set_text (GTK_LABEL (win->zoom_label), zoomtext);
	g_free (zoomtext);
}

/* Loads refit directly. Observe completed layouts too, without requesting
 * continuous animation frames merely to detect a changed viewport. */
static void
frame_window_maybe_refit (FrameWindow * win)
{
	GtkWidget *viewport;
	int vw, vh;

	if (win->zoom_mode != 0 || win->paintable == NULL)
		return;

	viewport = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (win->scroller));
	vw = gtk_widget_get_width (viewport);
	vh = gtk_widget_get_height (viewport);
	if (vw > 0 && vh > 0 && (vw != win->fit_last_width || vh != win->fit_last_height))
		frame_window_apply_zoom (win);
}

static void
on_fit_after_paint (GdkFrameClock *clock, gpointer user_data)
{
	(void) clock;
	frame_window_maybe_refit (FRAME_WINDOW (user_data));
	FrameWindow *win = FRAME_WINDOW(user_data);
	if (win->image_cleanup_pending) {
		win->image_cleanup_pending = FALSE;
#ifdef __GLIBC__
		/* Old snapshots release their image surface at repaint. Return freed
		 * decode/transform pages once at that boundary, never on a timer. */
		malloc_trim(0);
#endif
	}
}

static void
on_fit_realize (GtkWidget *widget, gpointer data)
{
	(void) data;
	FrameWindow *win = FRAME_WINDOW (widget);
	win->fit_clock = g_object_ref (gtk_widget_get_frame_clock (widget));
	g_signal_connect_object (win->fit_clock, "after-paint",
	                         G_CALLBACK (on_fit_after_paint), win, 0);
}

static void
frame_window_update_status (FrameWindow * win)
{
	gchar *text = NULL;
	gchar *title = NULL;
	gchar *subtitle = NULL;
	int iw, ih;

	if (win->current_file == NULL)
	{
		gtk_label_set_text (GTK_LABEL (win->title_label), "Frame");
		gtk_label_set_text (GTK_LABEL (win->subtitle_label), _("Open an image to begin (Ctrl+O)"));
		gtk_label_set_text (GTK_LABEL (win->status_label), "");
		gtk_label_set_text (GTK_LABEL (win->zoom_label), "");
		return;
	}

	if (win->paintable == NULL)
	{
		gchar *basename = g_file_get_basename(win->current_file);
		title = g_filename_display_name(basename);
		g_free(basename);
		gtk_label_set_text (GTK_LABEL (win->title_label), title);
		const char *state = win->load_failed ? _("Could not read the image") : _("Loading…");
		gtk_label_set_text (GTK_LABEL (win->subtitle_label), state);
		gtk_label_set_text (GTK_LABEL (win->status_label), state);
		g_free (title);
		return;
	}

	iw = win->source_width;
	ih = win->source_height;

	if (win->index >= 0 && win->entries && (guint)win->index < win->entries->len)
	{
		FrameEntry *entry = g_ptr_array_index (win->entries, win->index);
		gchar *size = g_format_size_full (entry->size, G_FORMAT_SIZE_IEC_UNITS);
		text = g_strdup_printf ("%s · %d×%d · %s", entry->name, iw, ih, size);
		title = g_strdup (entry->name);
		subtitle = g_strdup_printf (_("%d of %d"), win->index + 1, win->entries->len);
		g_free (size);
	}
	else
	{
		gchar *basename = g_file_get_basename(win->current_file);
		title = g_filename_display_name(basename);
		g_free(basename);
		text = g_strdup_printf ("%s · %d×%d", title, iw, ih);
		subtitle = g_strdup ("");
	}

	gtk_label_set_text (GTK_LABEL (win->title_label), title);
	gtk_label_set_text (GTK_LABEL (win->subtitle_label), subtitle);
	gtk_label_set_text (GTK_LABEL (win->status_label), text);

	g_free (text);
	g_free (title);
	g_free (subtitle);
}

/*** Navigation ***/

static void
frame_window_navigate (FrameWindow * win, int direction)
{
	int count;


	if (win->entries == NULL || win->entries->len == 0)
		return;
	count = win->entries->len;
	if (count == 1)
		return;

	if (win->index < 0)
		win->index = 0;
	else
		win->index = (win->index + direction + count) % count;

	{
		FrameEntry *entry = g_ptr_array_index (win->entries, win->index);
		frame_window_load (win, entry->file);
	}
}

/*** Navigation and zoom actions ***/

static void
action_nav (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter, gpointer user_data)
{
	frame_window_navigate (FRAME_WINDOW (user_data),
	                       g_strcmp0 (g_variant_get_string (parameter, NULL), "prev") == 0 ? -1 : 1);
}

static void
action_zoom (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	const char *what = g_variant_get_string (parameter, NULL);

	if (frame_image_width (win) < 1)
		return;

	if (g_strcmp0 (what, "in") == 0)
	{
		win->zoom_mode = 2;		/* custom */
		win->zoom = CLAMP (win->zoom * 1.25, FRAME_ZOOM_MIN, FRAME_ZOOM_MAX);
	}
	else if (g_strcmp0 (what, "out") == 0)
	{
		win->zoom_mode = 2;
		win->zoom = CLAMP (win->zoom / 1.25, FRAME_ZOOM_MIN, FRAME_ZOOM_MAX);
	}
	else if (g_strcmp0 (what, "fit") == 0)
	{
		win->zoom_mode = 0;
		frame_window_apply_zoom (win);
		return;
	}
	else if (g_strcmp0 (what, "one") == 0)
	{
		win->zoom_mode = 1;
	}

	frame_window_apply_zoom (win);
}

/*** Info dialog: all file work uses an immutable snapshot off the UI thread. ***/

typedef struct {
    GFile *file;
    GWeakRef dialog;
    GFileInfo *info;
    char **exif_rows;
    int width, height;
} InfoData;

static void info_data_free(gpointer pointer)
{
    InfoData *data = pointer;
    g_clear_object(&data->file);
    g_clear_object(&data->info);
    g_strfreev(data->exif_rows);
    g_weak_ref_clear(&data->dialog);
    g_free(data);
}

static void info_task(GTask *task, gpointer source, gpointer pointer, GCancellable *cancel)
{
    (void)source;
    InfoData *data = pointer;
    GError *error = NULL;
    data->info = g_file_query_info(data->file,
        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE, cancel, &error);
    if (!data->info) {
        g_task_return_error(task, error);
        return;
    }
    gchar *path = g_file_get_path(data->file);
    if (path) data->exif_rows = frame_exif_summary(path);
    g_free(path);
    g_task_return_boolean(task, TRUE);
}

static void info_row(GtkGrid *grid, guint row, const char *title, const char *text)
{
    GtkWidget *label = gtk_label_new(title);
    gchar *valid = g_utf8_make_valid(text ? text : "?", -1);
    GtkWidget *value = gtk_label_new(valid);
    g_free(valid);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(value), 0);
    gtk_label_set_selectable(GTK_LABEL(value), TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(value, TRUE);
    gtk_grid_attach(grid, label, 0, row, 1, 1);
    gtk_grid_attach(grid, value, 1, row, 1, 1);
}

static void info_ready(GObject *source, GAsyncResult *result, gpointer unused)
{
    (void)source; (void)unused;
    InfoData *data = g_task_get_task_data(G_TASK(result));
    GError *error = NULL;
    gboolean success = g_task_propagate_boolean(G_TASK(result), &error);
    GtkWindow *dialog = g_weak_ref_get(&data->dialog);
    if (dialog && !g_object_get_data(G_OBJECT(dialog), "closed")) {
        GtkBox *box = g_object_get_data(G_OBJECT(dialog), "info-box");
        gtk_box_remove(box, gtk_widget_get_first_child(GTK_WIDGET(box)));
        if (!success) {
            gtk_box_append(box, gtk_label_new(_("Could not read image information")));
        } else {
            GtkWidget *grid = gtk_grid_new();
            gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
            gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
            gtk_box_append(box, grid);
            const char *ctype = g_file_info_get_content_type(data->info);
            gchar *desc = ctype ? g_content_type_get_description(ctype) : g_strdup("?");
            gchar *size = g_format_size_full(g_file_info_get_size(data->info), G_FORMAT_SIZE_IEC_UNITS);
            guint row = 0;
            info_row(GTK_GRID(grid), row++, _("File"), g_file_info_get_display_name(data->info));
            info_row(GTK_GRID(grid), row++, _("Type"), desc);
            info_row(GTK_GRID(grid), row++, _("Size"), size);
            g_free(desc); g_free(size);
            GDateTime *mtime = g_file_info_get_modification_date_time(data->info);
            if (mtime) {
                gchar *text = g_date_time_format(mtime, "%Y-%m-%d %H:%M:%S");
                info_row(GTK_GRID(grid), row++, _("Modified"), text);
                g_free(text); g_date_time_unref(mtime);
            }
            if (data->width > 0) {
                gchar *text = g_strdup_printf(_("%d × %d pixels"), data->width, data->height);
                info_row(GTK_GRID(grid), row++, _("File dimensions"), text);
                g_free(text);
            }
            for (guint i = 0; data->exif_rows && data->exif_rows[i]; i += 2)
                info_row(GTK_GRID(grid), row++, data->exif_rows[i], data->exif_rows[i + 1]);
        }
    }
    g_clear_object(&dialog);
    g_clear_error(&error);
}

static void info_closed(GtkWindow *dialog, gpointer unused)
{
    (void)unused;
    g_object_set_data(G_OBJECT(dialog), "closed", GINT_TO_POINTER(1));
}

static void action_info(GSimpleAction *action, GVariant *parameter, gpointer pointer)
{
    (void)action; (void)parameter;
    FrameWindow *win = FRAME_WINDOW(pointer);
    if (!win->current_file || win->closed) return;
    GtkWindow *dialog = luxe_dialog_new();
    gtk_window_set_title(dialog, _("Image Information"));
    luxe_dialog_set_width(dialog, 440);
    luxe_dialog_set_height(dialog, 440);
    GtkWidget *toolbar = luxe_toolbar_new();
    luxe_toolbar_add_top_bar(GTK_BOX(toolbar), gtk_header_bar_new());
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_box_append(GTK_BOX(box), gtk_label_new(_("Loading…")));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), box);
    luxe_toolbar_set_content(GTK_BOX(toolbar), scroller);
    luxe_window_set_content(dialog, toolbar);
    g_object_set_data(G_OBJECT(dialog), "info-box", box);
    g_signal_connect(dialog, "destroy", G_CALLBACK(info_closed), NULL);
    InfoData *data = g_new0(InfoData, 1);
    data->file = g_object_ref(win->current_file);
    data->width = win->source_width;
    data->height = win->source_height;
    g_weak_ref_init(&data->dialog, dialog);
    GTask *task = g_task_new(NULL, NULL, info_ready, NULL);
    g_task_set_task_data(task, data, info_data_free);
    g_task_run_in_thread(task, info_task);
    g_object_unref(task);
    luxe_dialog_present(dialog, GTK_WIDGET(win));
}

/*** Open ***/

static void
on_open_dialog_response (GObject * source, GAsyncResult * result, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	GFile *file;
	GError *error = NULL;

	file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result, &error);
	if (file == NULL)
	{
		if (!g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
			g_warning ("frame: open dialog failed: %s", error->message);
		g_clear_error (&error);
		g_object_unref(win);
		return;
	}
	if (!win->closed) frame_window_open (win, file);
	g_object_unref (file);
	g_object_unref(win);
}

static void
action_open (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	GtkFileDialog *dialog;
	GtkFileFilter *filter;

	dialog = gtk_file_dialog_new ();
	gtk_file_dialog_set_title (dialog, _("Open Image"));

	/* Pin the dialog to an explicit folder. Adw startup activates
	 * xdg-desktop-portal (Settings portal), which flips GtkFileDialog
	 * to the out-of-process backend; without an explicit folder that
	 * backend opens in its own cwd instead of ours. Prefer the current
	 * image's folder, falling back to the process working directory. */
	{
		GFile *initial_folder = NULL;
		if (win->current_file != NULL)
			initial_folder = g_file_get_parent (win->current_file);
		if (initial_folder == NULL)
			initial_folder = g_file_new_for_path (".");
		gtk_file_dialog_set_initial_folder (dialog, initial_folder);
		g_object_unref (initial_folder);
	}

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Images"));
	gtk_file_filter_add_mime_type (filter, "image/*");
	gtk_file_dialog_set_default_filter (dialog, filter);
	g_object_unref (filter);

	gtk_file_dialog_open (dialog, GTK_WINDOW (win), NULL, on_open_dialog_response, g_object_ref(win));
	g_object_unref (dialog);
}

/*** Open with ***/

static void
on_appchooser_response (GtkDialog * dialog, int response, gpointer user_data)
{
	(void)user_data;
	GAppInfo *appinfo;
	GError *error = NULL;
	GFile *file;

	if (response != GTK_RESPONSE_OK)
	{
		gtk_window_destroy (GTK_WINDOW (dialog));
		return;
	}

	// GtkAppChooser is deprecated in GTK 4.18 without a replacement;
	// a custom picker is future work.
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	appinfo = gtk_app_chooser_get_app_info (GTK_APP_CHOOSER (dialog));
	G_GNUC_END_IGNORE_DEPRECATIONS

	file = g_object_ref(g_object_get_data(G_OBJECT(dialog), "selected-file"));
	gtk_window_destroy (GTK_WINDOW (dialog));

	if (appinfo != NULL)
	{
		GList *files = g_list_prepend (NULL, file);
		if (!g_app_info_launch (appinfo, files, NULL, &error))
		{
			g_warning ("frame: launching application failed: %s", error->message);
			g_clear_error (&error);
		}
		g_list_free (files);
		g_object_unref (appinfo);
	}
	g_object_unref (file);
}

static void
action_openwith (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	GtkWidget *dialog;

	if (win->current_file == NULL)
		return;

	// GtkAppChooser is deprecated in GTK 4.18 without a replacement;
	// a custom picker is future work.
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	dialog = gtk_app_chooser_dialog_new (GTK_WINDOW (win), GTK_DIALOG_MODAL, win->current_file);
	G_GNUC_END_IGNORE_DEPRECATIONS
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (win));
	gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
	g_object_set_data_full(G_OBJECT(dialog), "selected-file", g_object_ref(win->current_file), g_object_unref);
	g_signal_connect (dialog, "response", G_CALLBACK (on_appchooser_response), NULL);
	gtk_window_present (GTK_WINDOW (dialog));
}

/*** Rotate and flip (in-memory view transforms; files are never modified) ***/

static void
transform_done (FrameWindow * win, GdkPaintable * transformed)
{
	frame_window_set_paintable (win, transformed);
	g_object_unref(transformed);
	frame_window_apply_zoom (win);
	frame_window_update_status (win);
}

static void
action_rotate (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	if (win->paintable == NULL)
		return;

	transform_done(win, frame_image_rotate(win->paintable,
	    g_strcmp0(g_variant_get_string(parameter, NULL), "cw") == 0));
}

static void
action_flip (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	if (win->paintable == NULL)
		return;

	transform_done(win, frame_image_flip(win->paintable,
	    g_strcmp0(g_variant_get_string(parameter, NULL), "h") == 0));
}

/*** Set as wallpaper through Frame's wallpaper service ***/

static void
on_wallpaper_exit (GObject * source, GAsyncResult * result, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	GError *error = NULL;

	if (!g_subprocess_wait_check_finish (G_SUBPROCESS (source), result, &error))
	{
		g_warning ("frame: wallpaper service exited abnormally: %s", error->message);
		frame_window_show_error (win, _("Could not set the wallpaper"));
		g_clear_error (&error);
	}
	g_object_unref (win);
}

static void
action_wallpaper (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	gchar *path;
	GSubprocessLauncher *launcher;
	GSubprocess *proc;
	GError *error = NULL;

	if (win->current_file == NULL)
		return;

	path = g_file_get_path (win->current_file);
	if (path == NULL)
	{
		frame_window_show_error (win, _("Only local images can be set as wallpaper"));
		return;
	}

	// Delegate to Frame's lightweight wallpaper path; argument
	// arrays only, the path never passes through a shell.
	launcher = g_subprocess_launcher_new ((GSubprocessFlags)
	                                      (G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
	                                       G_SUBPROCESS_FLAGS_STDERR_SILENCE));
	proc = g_subprocess_launcher_spawn (launcher, &error,
	                                    "frame", "--set-wallpaper", path, NULL);
	if (proc == NULL)
	{
		g_warning ("frame: cannot launch the wallpaper service: %s", error->message);
		frame_window_show_error (win, _("Could not launch the wallpaper service"));
		g_clear_error (&error);
	}
	else
	{
		g_subprocess_wait_check_async (proc, NULL, on_wallpaper_exit, g_object_ref (win));
		g_object_unref (proc);
	}
	g_object_unref (launcher);
	g_free (path);
}

/*** Move to trash ***/

typedef struct
{
	FrameWindow *win;
	GCancellable *cancel;
} TrashToken;

static void
trash_token_free (TrashToken * token)
{
	g_clear_object (&token->cancel);
	g_clear_object (&token->win);
	g_free (token);
}

static void
frame_window_cancel_trash (FrameWindow * win)
{
	if (win->trash_cancel != NULL)
	{
		g_cancellable_cancel (win->trash_cancel);
		g_clear_object (&win->trash_cancel);
	}
}

static void
on_trashed(GObject *source, GAsyncResult *result, gpointer user_data)
{
    TrashToken *token = user_data;
    FrameWindow *win = token->win;
    GError *error = NULL;
    if (win->trash_cancel == token->cancel) g_clear_object(&win->trash_cancel);
    gboolean trashed = g_file_trash_finish(G_FILE(source), result, &error);
    if (!win->closed && trashed) {
        GFile *file = G_FILE(source);
        gboolean was_current = win->current_file && g_file_equal(file, win->current_file);
        gint removed = -1;
        for (guint i = 0; win->entries && i < win->entries->len; i++) {
            FrameEntry *entry = g_ptr_array_index(win->entries, i);
            if (g_file_equal(file, entry->file)) {
                removed = i;
                g_ptr_array_remove_index(win->entries, i);
                break;
            }
        }
        if (was_current) {
            frame_window_cancel_load(win);
            g_clear_object(&win->current_file);
            frame_window_set_paintable(win, NULL);
            win->index = -1;
            if (win->entries && win->entries->len) {
                guint next = removed < 0 ? 0 : MIN((guint)removed, win->entries->len - 1);
                FrameEntry *entry = g_ptr_array_index(win->entries, next);
                frame_window_load(win, entry->file);
            } else frame_window_apply_zoom(win);
        } else {
            win->index = -1;
            if (win->entries) frame_window_find_current(win);
        }
        frame_window_update_status(win);
        frame_window_rebuild_list(win);
    } else if (!trashed && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        frame_window_show_error(win, _("Moving the image to the trash failed"));
    }
    g_clear_error(&error);
    trash_token_free(token);
}

static void
action_trash (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	TrashToken *token;

	if (win->current_file == NULL || win->trash_cancel != NULL)
		return;

	// Trash only, never a fallback to permanent deletion
	win->trash_cancel = g_cancellable_new ();
	token = g_new0 (TrashToken, 1);
	token->win = g_object_ref (win);
	token->cancel = g_object_ref (win->trash_cancel);
	g_file_trash_async (win->current_file, 0, token->cancel, on_trashed, token);
}

/*** Slideshow and fullscreen ***/

static gboolean
slideshow_tick (FrameWindow * win)
{
	frame_window_navigate (win, 1);
	return G_SOURCE_CONTINUE;
}

static void
frame_window_stop_slideshow (FrameWindow * win)
{
	if (win->slideshow_id != 0)
	{
		g_source_remove (win->slideshow_id);
		win->slideshow_id = 0;
	}
}

static void
on_slideshow_change_state (GSimpleAction * action, GVariant * state, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);

	frame_window_stop_slideshow(win);
	if (g_variant_get_boolean (state))
	{
		int interval = g_settings_get_int (win->settings, "slideshow-interval");
		win->slideshow_id = g_timeout_add_seconds (MAX (1, interval),
		                                           (GSourceFunc) slideshow_tick, win);
	}
	else
	{
		frame_window_stop_slideshow (win);
	}
	g_simple_action_set_state (action, state);
}

static void
on_fullscreen_change_state (GSimpleAction * action, GVariant * state, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);

	if (g_variant_get_boolean (state))
		gtk_window_fullscreen (GTK_WINDOW (win));
	else
		gtk_window_unfullscreen (GTK_WINDOW (win));
	g_simple_action_set_state (action, state);
}

static void
on_fullscreen_changed (GObject * object G_GNUC_UNUSED, GParamSpec * pspec G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	gboolean fullscreen = gtk_window_is_fullscreen (GTK_WINDOW (win));
	GSimpleAction *fs;

	/* GtkApplicationWindow hosts the header bar inside GtkBox,
	 * not as a GtkWindow titlebar, so toggle the bound widget directly. */
	gtk_widget_set_visible (win->headerbar, !fullscreen);
	gtk_widget_set_visible (win->statusbar, !fullscreen);

	fs = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (win), "fullscreen"));
	if (fs != NULL)
		g_simple_action_set_state (fs, g_variant_new_boolean (fullscreen));
}

/*** Escape ***/

static void
action_escape (GSimpleAction * action G_GNUC_UNUSED, GVariant * parameter G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);

	if (gtk_window_is_fullscreen (GTK_WINDOW (win)))
		gtk_window_unfullscreen (GTK_WINDOW (win));
}

/*** Gestures: ctrl+scroll zoom, pinch zoom, middle-drag pan ***/

static gboolean
on_scroll_zoom (GtkEventControllerScroll * controller,
                double dx, double dy, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (controller));

	if (!(state & GDK_CONTROL_MASK))
		return FALSE;			/* normal scrolling through to the scrolled window */

	if (frame_image_width (win) < 1)
		return FALSE;

	win->zoom_mode = 2;
	if (dy < 0 || dx > 0)
		win->zoom = CLAMP (win->zoom * 1.1, FRAME_ZOOM_MIN, FRAME_ZOOM_MAX);
	else
		win->zoom = CLAMP (win->zoom / 1.1, FRAME_ZOOM_MIN, FRAME_ZOOM_MAX);
	frame_window_apply_zoom (win);
	return TRUE;
}

static void
on_pinch_begin (GtkGestureZoom * gesture, GdkEventSequence *sequence, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	(void)sequence;
	win->pinch_base = win->zoom;
}

static void
on_pinch_scale (GtkGestureZoom * gesture, double scale, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	(void)gesture;

	if (frame_image_width (win) < 1)
		return;
	win->zoom_mode = 2;
	win->zoom = CLAMP (win->pinch_base * scale, FRAME_ZOOM_MIN, FRAME_ZOOM_MAX);
	frame_window_apply_zoom (win);
}

static void
on_middle_drag_begin (GtkGestureDrag * gesture, double x G_GNUC_UNUSED, double y G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW(user_data);
	win->drag_x = win->drag_y = 0;
	gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
on_middle_drag_update (GtkGestureDrag * gesture G_GNUC_UNUSED, double offset_x, double offset_y,
                       gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	GtkAdjustment *h, *v;

	h = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (win->scroller));
	v = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (win->scroller));
	if (h != NULL)
		gtk_adjustment_set_value (h, CLAMP (gtk_adjustment_get_value (h) - (offset_x - win->drag_x),
		                                    gtk_adjustment_get_lower (h), gtk_adjustment_get_upper (h)));
	if (v != NULL)
		gtk_adjustment_set_value (v, CLAMP (gtk_adjustment_get_value (v) - (offset_y - win->drag_y),
		                                    gtk_adjustment_get_lower (v), gtk_adjustment_get_upper (v)));
	win->drag_x = offset_x;
	win->drag_y = offset_y;
}

static void
on_middle_drag_end (GtkGestureDrag * gesture G_GNUC_UNUSED, double offset_x G_GNUC_UNUSED, double offset_y G_GNUC_UNUSED,
                    gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW(user_data);
	win->drag_x = win->drag_y = 0;
}

/*** Drag and drop: opening image files dropped onto the window ***/

static gboolean
on_drop (GtkDropTarget * target G_GNUC_UNUSED, const GValue * value, double x G_GNUC_UNUSED, double y G_GNUC_UNUSED, gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);
	const GSList *files;

	if (!G_VALUE_HOLDS (value, GDK_TYPE_FILE_LIST))
		return FALSE;
	files = gdk_file_list_get_files (g_value_get_boxed (value));
	if (files == NULL)
		return FALSE;
	frame_window_open (win, G_FILE (files->data));
	return TRUE;
}

/*** Actions setup ***/

static void
frame_window_install_actions (FrameWindow * win)
{
	static GActionEntry entries[] = {
		{ .name = "open", .activate = action_open },
		{ .name = "info", .activate = action_info },
		{ .name = "openwith", .activate = action_openwith },
		{ .name = "rotate", .activate = action_rotate, .parameter_type = "s" },
		{ .name = "flip", .activate = action_flip, .parameter_type = "s" },
		{ .name = "wallpaper", .activate = action_wallpaper },
		{ .name = "trash", .activate = action_trash },
		{ .name = "nav", .activate = action_nav, .parameter_type = "s" },
		{ .name = "zoom", .activate = action_zoom, .parameter_type = "s" },
		{ .name = "escape", .activate = action_escape },
	};
	GSimpleAction *slideshow, *fullscreen;

	g_action_map_add_action_entries (G_ACTION_MAP (win), entries,
	                                 G_N_ELEMENTS (entries), win);

	slideshow = g_simple_action_new_stateful ("slideshow", NULL, g_variant_new_boolean (FALSE));
	g_signal_connect (slideshow, "change-state", G_CALLBACK (on_slideshow_change_state), win);
	g_action_map_add_action (G_ACTION_MAP (win), G_ACTION (slideshow));
	g_object_unref (slideshow);

	fullscreen = g_simple_action_new_stateful ("fullscreen", NULL, g_variant_new_boolean (FALSE));
	g_signal_connect (fullscreen, "change-state", G_CALLBACK (on_fullscreen_change_state), win);
	g_action_map_add_action (G_ACTION_MAP (win), G_ACTION (fullscreen));
	g_object_unref (fullscreen);

	g_signal_connect (win, "notify::fullscreened", G_CALLBACK (on_fullscreen_changed), win);
}

/*** Keyboard: the app-level accelerators in frame-app.c handle the
 * viewer keys (nav, zoom, rotate, trash, fullscreen, slideshow, info,
 * wallpaper, Escape). Only the plain arrow keys are also handled here,
 * in the capture phase, because the scrolled window consumes arrows
 * for scrolling before the accelerators can fire. ***/

static gboolean
on_key_pressed (GtkEventControllerKey * controller G_GNUC_UNUSED,
                guint keyval, guint keycode G_GNUC_UNUSED, GdkModifierType state,
                gpointer user_data)
{
	FrameWindow *win = FRAME_WINDOW (user_data);

	if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_SHIFT_MASK)) ||
	    luxe_window_get_visible_dialog(GTK_WINDOW(win))) return FALSE;
	switch (keyval)
	{
		case GDK_KEY_Right:
			frame_window_navigate (win, 1);
			return TRUE;
		case GDK_KEY_Left:
			frame_window_navigate (win, -1);
			return TRUE;
		default:
			return FALSE;
	}
}

/*** Public: open a file (and its folder) ***/

void
frame_window_open (FrameWindow * win, GFile * file)
{
	GFile *parent;

	g_return_if_fail (FRAME_IS_WINDOW (win));
	g_return_if_fail (G_IS_FILE (file));

	parent = g_file_get_parent (file);
	if (parent != NULL)
	{
		frame_window_set_folder (win, parent);
		g_object_unref (parent);
	}

	// Show the image straight away; the folder list catches up asynchronously
	frame_window_load (win, file);
}

/*** Template and lifecycle ***/

static void frame_window_closed(GtkWidget *widget, gpointer data)
{
	(void)data;
	FrameWindow *win = FRAME_WINDOW(widget);
	if (win->fit_clock != NULL)
		g_signal_handlers_disconnect_by_func (win->fit_clock, on_fit_after_paint, win);
	g_clear_object (&win->fit_clock);
	win->closed = TRUE;
	frame_window_stop_slideshow(win);
	frame_window_cancel_load(win);
	g_clear_object (&win->load_pending);
	frame_window_cancel_trash(win);
	frame_window_cancel_list(win);
	if (win->monitor) g_file_monitor_cancel(win->monitor);
	if (win->rebuild_id) { g_source_remove(win->rebuild_id); win->rebuild_id = 0; }
	if (win->error_timeout_id) { g_source_remove(win->error_timeout_id); win->error_timeout_id = 0; }
}

static void
frame_window_dispose (GObject * object)
{
	FrameWindow *win = FRAME_WINDOW (object);

	frame_window_closed(GTK_WIDGET(win), NULL);
	g_clear_object (&win->paintable);
	g_clear_object (&win->current_file);
	g_clear_object (&win->load_pending);
	g_clear_object (&win->folder);
	g_clear_object (&win->monitor);
	if (win->entries != NULL)
	{
		g_ptr_array_unref (win->entries);
		win->entries = NULL;
	}
	g_clear_object (&win->settings);

	G_OBJECT_CLASS (frame_window_parent_class)->dispose (object);
}

static void
frame_window_init (FrameWindow * win)
{
	GtkEventController *scroll;
	GtkEventController *keys;
	GtkGesture *pinch, *drag;
	GtkDropTarget *drop;

	gtk_widget_init_template (GTK_WIDGET (win));
	g_signal_connect(win, "realize", G_CALLBACK(on_fit_realize), NULL);
	g_signal_connect(win, "unrealize", G_CALLBACK(frame_window_closed), NULL);

	win->entries = NULL;
	win->index = -1;
	win->zoom = 1.0;
	win->zoom_mode = 0;

	win->settings = g_settings_new ("org.luxe.Frame");

	frame_window_install_actions (win);

	// Zoom gestures and scrolling
	scroll = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
	g_signal_connect (scroll, "scroll", G_CALLBACK (on_scroll_zoom), win);
	gtk_widget_add_controller (win->picture, scroll);

	pinch = gtk_gesture_zoom_new ();
	g_signal_connect (pinch, "begin", G_CALLBACK (on_pinch_begin), win);
	g_signal_connect (pinch, "scale-changed", G_CALLBACK (on_pinch_scale), win);
	gtk_widget_add_controller (win->picture, GTK_EVENT_CONTROLLER (pinch));

	drag = gtk_gesture_drag_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_MIDDLE);
	g_signal_connect (drag, "drag-begin", G_CALLBACK (on_middle_drag_begin), win);
	g_signal_connect (drag, "drag-update", G_CALLBACK (on_middle_drag_update), win);
	g_signal_connect (drag, "drag-end", G_CALLBACK (on_middle_drag_end), win);
	gtk_widget_add_controller (win->picture, GTK_EVENT_CONTROLLER (drag));

	// Viewer keys at the toplevel, capture phase: this runs before the
	// scrolled window's own arrow-key bindings and before focus-widget
	// routing, so navigation keys work wherever focus rests.
	keys = gtk_event_controller_key_new ();
	gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
	g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key_pressed), win);
	gtk_widget_add_controller (GTK_WIDGET (win), keys);

	// Drag and drop
	drop = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
	g_signal_connect (drop, "drop", G_CALLBACK (on_drop), win);
	gtk_widget_add_controller (GTK_WIDGET (win), GTK_EVENT_CONTROLLER (drop));

	frame_window_update_status (win);
}

static void
frame_window_class_init (FrameWindowClass * class)
{
	G_OBJECT_CLASS (class)->dispose = frame_window_dispose;

	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (class),
	                                             "/org/luxe/frame/frame-window.ui");

	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, picture);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, scroller);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, headerbar);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, title_label);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, subtitle_label);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, status_label);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, zoom_label);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, error_revealer);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, error_label);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (class), FrameWindow, statusbar);
}

FrameWindow *
frame_window_new (GtkApplication * app)
{
	return g_object_new (frame_window_get_type (), "application", app, NULL);
}
