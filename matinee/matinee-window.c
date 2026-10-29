/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* The Matinee window.  One surface (GtkPicture) is the only place playback is
 * shown: video frames when the file has a video stream, the visualiser when it
 * does not.  Playback state comes from the engine, which owns FFmpeg and the
 * ALSA clock; this file owns GTK and the playlist. */

#include "matinee-window.h"

#include <math.h>
#include <string.h>

#define MATINEE_SEEK_UPDATE_MS 250
#define MATINEE_MAX_LIBRARY_DEPTH 12
#define MATINEE_MAX_LIBRARY_FILES 20000
#define MATINEE_MAX_LIBRARY_ENTRIES 100000
#define MATINEE_POSITION_UPPER 1000.0

struct _MatineeWindow
{
	GtkApplicationWindow parent_instance;

	/* Template widgets. */
	GtkWidget *header;
	GtkWidget *open_button;
	GtkWidget *menu_button;
	GtkWidget *paned;
	GtkWidget *surface;
	GtkWidget *empty_page;
	GtkWidget *playlist_scroll;
	GtkWidget *playlist_view;
	GtkWidget *seek;
	GtkWidget *play_button;
	GtkWidget *stop_button;
	GtkWidget *position_label;
	GtkWidget *library_status;
	GtkWidget *now_playing;
	GtkWidget *volume;

	MatineeEngine *engine;
	MatineeEngine *opening_engine;
	guint open_generation;
	MatineePlaylist *playlist;
	MatineeVisual *visual;
	GSettings *settings;
	GCancellable *chooser_cancel;
	GCancellable *library_cancel;
	GHashTable *library_entries;
	guint library_generation;
	gboolean library_rescan_pending;

	guint position_timer;
	gboolean updating_seek;
	gboolean show_visual;
	gint64 visual_last_time_us;
	guint visual_tick_id;
};

G_DEFINE_TYPE (MatineeWindow, matinee_window, GTK_TYPE_APPLICATION_WINDOW)

static void select_entry (MatineeWindow *window, guint position, gboolean play);
static void action_rescan_library (GSimpleAction *action, GVariant *parameter, gpointer data);
static void update_play_button (MatineeWindow *window);
static void refresh_playlist_visibility (MatineeWindow *window);
static void render_visualizer (MatineeWindow *window);
static gboolean position_tick (gpointer data);
static void update_position (MatineeWindow *window);
typedef struct _OpenRequest
{
	GWeakRef window;
	MatineeEngine *engine;
	gchar *path;
	guint generation;
	gboolean play;
} OpenRequest;
static void open_request_free (OpenRequest *request);
static void open_worker (GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable);
static void open_done (GObject *source, GAsyncResult *result, gpointer data);

typedef struct {
	GWeakRef window;
	GCancellable *cancel;
	gboolean replace;
} ChooserRequest;

static ChooserRequest *
chooser_request_new (MatineeWindow *window, gboolean replace)
{
	ChooserRequest *request = g_new0 (ChooserRequest, 1);
	g_weak_ref_init (&request->window, window);
	request->cancel = g_object_ref (window->chooser_cancel);
	request->replace = replace;
	return request;
}

static void
chooser_request_free (ChooserRequest *request)
{
	g_weak_ref_clear (&request->window);
	g_object_unref (request->cancel);
	g_free (request);
}

/*** Playlist model for the list view ***/

enum
{
	COLUMN_TITLE,
	COLUMN_DETAIL,
	COLUMN_N
};

static void
entry_setup (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
	(void) factory;
	(void) data;
	GtkWidget *container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget **labels = g_new0 (GtkWidget *, COLUMN_N);
	gtk_widget_set_margin_start (container, 6);
	gtk_widget_set_margin_end (container, 6);
	gtk_widget_set_margin_top (container, 4);
	gtk_widget_set_margin_bottom (container, 4);
	gtk_list_item_set_child (item, container);

	for (int i = 0; i < COLUMN_N; i++)
	{
		GtkWidget *label = gtk_label_new (NULL);
		gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
		gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
		if (i == COLUMN_DETAIL)
		{
			gtk_widget_add_css_class (label, "dim-label");
			gtk_widget_add_css_class (label, "caption");
		}
		labels[i] = label;
		gtk_box_append (GTK_BOX (container), label);
	}
	/* The label array is handed to bind() through the item's child. */
	g_object_set_data_full (G_OBJECT (container), "matinee-labels", labels, (GDestroyNotify) g_free);
}

static void
entry_bind (GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
	(void) factory;
	(void) data;
	GtkWidget *container = gtk_list_item_get_child (item);
	GtkWidget **labels = container != NULL
		? g_object_get_data (G_OBJECT (container), "matinee-labels") : NULL;
	MatineeEntry *entry = gtk_list_item_get_item (item);

	if (entry == NULL || labels == NULL)
		return;
	gtk_label_set_text (GTK_LABEL (labels[COLUMN_TITLE]), matinee_entry_display (entry));
	{
		double length = matinee_entry_length (entry);
		if (length > 0.0)
		{
			char *seconds = g_strdup_printf ("%d:%02d", (int) length / 60, (int) length % 60);
			gtk_label_set_text (GTK_LABEL (labels[COLUMN_DETAIL]), seconds);
			g_free (seconds);
		}
		else
		{
			gtk_label_set_text (GTK_LABEL (labels[COLUMN_DETAIL]),
			                    matinee_entry_kind (entry) == MATINEE_ENTRY_VIDEO
			                      ? "Video" : "Audio");
		}
	}
}

static void playlist_activated(GtkListView *view, guint position, MatineeWindow *window)
{
    (void)view;
    select_entry(window, position, TRUE);
}

static void
create_playlist_selection (MatineeWindow *window)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
	GtkSelectionModel *selection;
	GtkSingleSelection *single;

	g_signal_connect (factory, "setup", G_CALLBACK (entry_setup), NULL);
	g_signal_connect (factory, "bind", G_CALLBACK (entry_bind), NULL);
	single = gtk_single_selection_new (G_LIST_MODEL (g_object_ref (window->playlist)));
	gtk_single_selection_set_autoselect (single, FALSE);
	gtk_single_selection_set_can_unselect (single, TRUE);
	selection = GTK_SELECTION_MODEL (single);
	gtk_list_view_set_model (GTK_LIST_VIEW (window->playlist_view), selection);
	g_signal_connect(window->playlist_view, "activate", G_CALLBACK(playlist_activated), window);
	gtk_list_view_set_factory (GTK_LIST_VIEW (window->playlist_view), factory);
	g_object_unref (selection);
	g_object_unref (factory);
}

/*** Surface ***/

/* The visualiser only runs when there is no video to show. */
static gboolean
visual_tick (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
	MatineeWindow *window = data;
	(void) widget;
	(void) clock;
	if (window->show_visual)
	{
		gint64 now = g_get_monotonic_time ();
		if (now - window->visual_last_time_us >= 33333)
		{
			window->visual_last_time_us = now;
			render_visualizer (window);
		}
	}
	return G_SOURCE_CONTINUE;
}

static void
set_surface_mode (MatineeWindow *window)
{
	gboolean playing_something = matinee_engine_state (window->engine) != MATINEE_STATE_IDLE;
	gboolean advancing = matinee_engine_state(window->engine) == MATINEE_STATE_PLAYING;
	gboolean has_video = matinee_engine_has_video (window->engine);

	window->show_visual = playing_something && !has_video;
	matinee_engine_set_spectrum_enabled (window->engine,
	                                     window->show_visual && advancing
	                                     && gtk_widget_get_mapped (window->surface));
	if (window->show_visual && advancing && window->visual_tick_id == 0)
		window->visual_tick_id = gtk_widget_add_tick_callback (window->surface, visual_tick, window, NULL);
	else if ((!window->show_visual || !advancing) && window->visual_tick_id != 0)
	{
		gtk_widget_remove_tick_callback (window->surface, window->visual_tick_id);
		window->visual_tick_id = 0;
	}
	if (advancing && window->position_timer == 0)
		window->position_timer = g_timeout_add (MATINEE_SEEK_UPDATE_MS, position_tick, window);
	else if (!advancing && window->position_timer != 0)
	{
		g_source_remove (window->position_timer);
		window->position_timer = 0;
	}
	update_position (window);
	gtk_widget_set_visible (window->empty_page, !playing_something);
	if (!playing_something)
		gtk_picture_set_paintable (GTK_PICTURE (window->surface), NULL);
}

static void
on_surface_mapped_changed (GtkWidget *widget, GParamSpec *pspec, gpointer data)
{
	(void) widget;
	(void) pspec;
	set_surface_mode (MATINEE_WINDOW (data));
}

static void
on_video_frame (MatineeEngine *engine, GdkTexture *texture, gpointer data)
{
	MatineeWindow *window = data;
	(void) engine;
	/* Frames only reach here on the main thread. */
	if (!window->show_visual)
		gtk_picture_set_paintable (GTK_PICTURE (window->surface), GDK_PAINTABLE (texture));
}

static void
render_visualizer (MatineeWindow *window)
{
	MatineeBands bands;
	GdkTexture *texture;

	matinee_engine_get_bands (window->engine, &bands);
	matinee_visual_render (window->visual, &bands, g_get_monotonic_time ());
	texture = matinee_visual_texture (window->visual);
	if (texture != NULL)
		gtk_picture_set_paintable (GTK_PICTURE (window->surface), GDK_PAINTABLE (texture));
}

/*** Transport state ***/

static void
update_play_button (MatineeWindow *window)
{
	MatineeState state = matinee_engine_state (window->engine);
	const char *icon = "media-playback-start-symbolic";
	const char *tip = "Play (Space)";

	if (state == MATINEE_STATE_PLAYING)
	{
		icon = "media-playback-pause-symbolic";
		tip = "Pause (Space)";
	}
	gtk_button_set_icon_name (GTK_BUTTON (window->play_button), icon);
	gtk_widget_set_tooltip_text (window->play_button, tip);
}

static void
on_seek_changed (GtkRange *range, gpointer data)
{
	MatineeWindow *window = data;

	if (!window->updating_seek)
	{
		if (!matinee_engine_seek_fraction (window->engine,
		                                   gtk_range_get_value (range) / MATINEE_POSITION_UPPER))
		{
			GtkWindow *dialog = luxe_choice_dialog_new ("Cannot seek",
			    "This media could not be moved to the requested position.");
			luxe_choice_add_response (GTK_DIALOG (dialog), "close", "Close");
			luxe_dialog_present (dialog, GTK_WIDGET (window));
		}
		update_position (window);
	}
}

static void
on_volume_changed (GtkRange *range, gpointer data)
{
	MatineeWindow *window = data;
	double volume = gtk_range_get_value (range) / 100.0;

	matinee_engine_set_volume (window->engine, volume);
	g_settings_set_double (window->settings, "volume", volume);
}

static void
update_position (MatineeWindow *window)
{
	double position = matinee_engine_position (window->engine);
	double duration = matinee_engine_duration (window->engine);
	char *label;

	if (duration > 0.0)
	{
		double fraction = CLAMP (position / duration, 0.0, 1.0);
		GtkAdjustment *adjustment = gtk_range_get_adjustment (GTK_RANGE (window->seek));

		window->updating_seek = TRUE;
		gtk_adjustment_set_value (adjustment, fraction * MATINEE_POSITION_UPPER);
		window->updating_seek = FALSE;
	}
	label = g_strdup_printf ("%d:%02d / %d:%02d",
	                         (int) position / 60, (int) position % 60,
	                         (int) duration / 60, (int) duration % 60);
	gtk_label_set_text (GTK_LABEL (window->position_label), label);
	g_free (label);
}

static void
update_now_playing (MatineeWindow *window)
{
	if(matinee_engine_state(window->engine)==MATINEE_STATE_ERROR){
		g_autofree char *error=matinee_engine_dup_error_message(window->engine);
		gtk_label_set_text(GTK_LABEL(window->now_playing),"Playback failed");
		gtk_widget_set_tooltip_text(window->now_playing,error ? error : "Playback failed. Try another file or audio device.");
		return;
	}
	const char *title = matinee_engine_title (window->engine);
	const char *artist = matinee_engine_artist (window->engine);
	char *text;

	if (title == NULL || *title == '\0')
	{
		gtk_label_set_text (GTK_LABEL (window->now_playing), "");
		return;
	}
	if (artist != NULL && *artist != '\0')
		text = g_strdup_printf ("%s — %s", artist, title);
	else
		text = g_strdup (title);
	gtk_label_set_text (GTK_LABEL (window->now_playing), text);
	gtk_widget_set_tooltip_text (window->now_playing, text);
	g_free (text);
}

static gboolean
position_tick (gpointer data)
{
	MatineeWindow *window = data;
	update_position (window);
	return G_SOURCE_CONTINUE;
}

static void
on_state_changed (MatineeEngine *engine, gint state, gpointer data)
{
	MatineeWindow *window = data;
	(void) engine;
	update_play_button (window);
	set_surface_mode (window);
	gtk_widget_set_visible (window->seek, state != MATINEE_STATE_IDLE);
	gtk_widget_set_sensitive (window->stop_button, state != MATINEE_STATE_IDLE);
	update_now_playing (window);
}

/*** Actions ***/

static void
play_current (MatineeWindow *window)
{
	matinee_engine_play (window->engine);
	update_play_button (window);
}

static void
select_entry (MatineeWindow *window, guint position, gboolean play)
{
	MatineeEntry *entry = matinee_playlist_at (window->playlist, position);

	if (entry == NULL)
		return;
	window->open_generation++;
	if (window->opening_engine != NULL)
	{
		matinee_engine_cancel_io (window->opening_engine);
		g_clear_object (&window->opening_engine);
	}
	matinee_playlist_set_current (window->playlist, (gint) position);
	GtkSelectionModel *selection = gtk_list_view_get_model(GTK_LIST_VIEW(window->playlist_view));
	gtk_single_selection_set_selected(GTK_SINGLE_SELECTION(selection), position);
	matinee_engine_pause (window->engine);
	OpenRequest *request = g_new0 (OpenRequest, 1);
	g_weak_ref_init (&request->window, window);
	request->engine = matinee_engine_new ();
	request->path = g_strdup (matinee_entry_path (entry));
	request->generation = window->open_generation;
	request->play = play;
	window->opening_engine = g_object_ref (request->engine);
	GTask *task = g_task_new (NULL, NULL, open_done, NULL);
	g_task_set_task_data (task, request, (GDestroyNotify) open_request_free);
	g_task_run_in_thread (task, open_worker);
	g_object_unref (task);
}

static void
on_finished (MatineeEngine *engine, gpointer data)
{
	MatineeWindow *window = data;
	(void) engine;
	if (matinee_playlist_move_next (window->playlist))
		select_entry (window, (guint) matinee_playlist_current (window->playlist), TRUE);
}

static void
open_request_free (OpenRequest *request)
{
	g_weak_ref_clear (&request->window);
	g_object_unref (request->engine);
	g_free (request->path);
	g_free (request);
}

static void
open_worker (GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
	OpenRequest *request = task_data;
	GError *error = NULL;
	(void) source;
	(void) cancellable;
	if (matinee_engine_open (request->engine, request->path, &error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, error);
}

static void
open_done (GObject *source, GAsyncResult *result, gpointer data)
{
	GTask *task = G_TASK (result);
	OpenRequest *request = g_task_get_task_data (task);
	g_autoptr (MatineeWindow) window = g_weak_ref_get (&request->window);
	GError *error = NULL;
	gboolean opened = g_task_propagate_boolean (task, &error);
	(void) source;
	(void) data;
	if (window != NULL && request->generation == window->open_generation)
	{
		if (window->opening_engine == request->engine)
			g_clear_object (&window->opening_engine);
		if (opened)
		{
			g_signal_handlers_disconnect_by_data (window->engine, window);
			matinee_engine_stop (window->engine);
			g_clear_object (&window->engine);
			window->engine = g_object_ref (request->engine);
			g_signal_connect (window->engine, "video-frame", G_CALLBACK (on_video_frame), window);
			g_signal_connect (window->engine, "state-changed", G_CALLBACK (on_state_changed), window);
			g_signal_connect (window->engine, "finished", G_CALLBACK (on_finished), window);
			matinee_engine_set_volume (window->engine,
			    gtk_range_get_value (GTK_RANGE (window->volume)) / 100.0);
			set_surface_mode (window);
			refresh_playlist_visibility (window);
			if (request->play)
				play_current (window);
			update_now_playing (window);
			update_position (window);
		}
		else if (error != NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
			luxe_status_set_title (GTK_BOX (window->empty_page), "Cannot Play File");
			luxe_status_set_description (GTK_BOX (window->empty_page), error->message);
		}
	}
	g_clear_error (&error);
}

static void
action_play_pause (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	(void) action;
	(void) parameter;
	if (matinee_engine_state (window->engine) == MATINEE_STATE_PLAYING)
		matinee_engine_pause (window->engine);
	else
		play_current (window);
	update_play_button (window);
}

static void
action_stop (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	(void) action;
	(void) parameter;
	matinee_engine_stop (window->engine);
	update_play_button (window);
	set_surface_mode (window);
}

static void
action_next (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	(void) action;
	(void) parameter;
	if (matinee_playlist_move_next (window->playlist))
		select_entry (window, (guint) matinee_playlist_current (window->playlist), TRUE);
}

static void
action_previous (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	(void) action;
	(void) parameter;
	if (matinee_playlist_move_prev (window->playlist))
		select_entry (window, (guint) matinee_playlist_current (window->playlist), TRUE);
}

static void
action_clear_playlist (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	(void) action;
	(void) parameter;
	matinee_engine_stop (window->engine);
	matinee_playlist_clear (window->playlist);
	refresh_playlist_visibility (window);
	set_surface_mode (window);
	update_play_button (window);
}

const MatineeShortcut matinee_shortcuts[] = {
	{ "Open Files",     "win.open",       "<Control>o" },
	{ "Play / Pause",   "win.play-pause", "space" },
	{ "Stop",           "win.stop",       "<Control>s" },
	{ "Next Track",     "win.next",       "<Control>Right" },
	{ "Previous Track", "win.previous",   "<Control>Left" },
};
const gsize matinee_shortcuts_count = G_N_ELEMENTS (matinee_shortcuts);

static void
action_shortcuts (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	GtkWindow *dialog;
	GtkWidget *list;
	GtkWidget *scrolled;

	(void) action;
	(void) parameter;
	list = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
	gtk_widget_add_css_class (list, "boxed-list");
	for (gsize i = 0; i < matinee_shortcuts_count; i++)
	{
		GtkWidget *row = gtk_list_box_row_new ();
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
		GtkWidget *name = gtk_label_new (matinee_shortcuts[i].label);
		GtkWidget *keys = gtk_label_new (matinee_shortcuts[i].accelerator);
		gtk_widget_set_hexpand (box, TRUE);
		gtk_label_set_xalign (GTK_LABEL (name), 0.0f);
		gtk_label_set_xalign (GTK_LABEL (keys), 1.0f);
		gtk_widget_add_css_class (keys, "dim-label");
		gtk_widget_add_css_class (keys, "monospace");
		gtk_box_append (GTK_BOX (box), name);
		gtk_box_append (GTK_BOX (box), keys);
		gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
		gtk_list_box_append (GTK_LIST_BOX (list), row);
	}
	scrolled = gtk_scrolled_window_new ();
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), list);
	gtk_widget_set_margin_top (scrolled, 10);
	gtk_widget_set_margin_bottom (scrolled, 10);
	gtk_widget_set_margin_start (scrolled, 10);
	gtk_widget_set_margin_end (scrolled, 10);
	gtk_widget_set_vexpand (scrolled, TRUE);
	dialog = luxe_dialog_new ();
	gtk_window_set_title (dialog, "Keyboard Shortcuts");
	luxe_dialog_set_width (dialog, 420);
	luxe_window_set_content (dialog, scrolled);
	luxe_dialog_present (dialog, GTK_WIDGET (window));
}

static void
action_about (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	static const char *authors[] = { "Jerry Howell", NULL };
	(void) action;
	(void) parameter;
	gtk_show_about_dialog (GTK_WINDOW (window),
	                       "program-name", "Matinee",
	                       "version", VERSION,
	                       "comments", "A GTK4 media player for the Luxe desktop",
	                       "authors", authors,
	                       "copyright", "Copyright (C) 2026 Jerry Howell",
	                       "license-type", GTK_LICENSE_GPL_3_0,
	                       "logo-icon-name", "org.luxe.Matinee",
	                       "website", "https://github.com/jerrimus/luxe",
	                       NULL);
}

/*** File opening and library ***/

static void
add_uris (MatineeWindow *window, char **uris, gboolean replace)
{
	gboolean first = TRUE;

	if (replace)
		matinee_playlist_clear (window->playlist);
	for (gsize i = 0; uris != NULL && uris[i] != NULL; i++)
	{
		char *path = g_filename_from_uri (uris[i], NULL, NULL);
		MatineeEntry *entry;
		if (path == NULL)
			continue;
		entry = matinee_entry_new (path, 0.0, MATINEE_ENTRY_AUDIO);
		matinee_playlist_append (window->playlist, entry);
		g_object_unref (entry);
		g_free (path);
		first = FALSE;
	}
	refresh_playlist_visibility (window);
	if (!first)
	{
		select_entry (window, replace ? 0 : (guint) MAX (0, (int) matinee_playlist_length (window->playlist) - 1), TRUE);
	}
}

static void
on_files_chosen (GObject *source, GAsyncResult *result, gpointer data)
{
	ChooserRequest *request = data;
	g_autoptr(MatineeWindow) window = g_weak_ref_get (&request->window);
	GtkFileDialog *dialog = GTK_FILE_DIALOG (source);
	GError *error = NULL;
	GListModel *files = gtk_file_dialog_open_multiple_finish (dialog, result, &error);

	if (files == NULL)
	{
		if (error && !g_cancellable_is_cancelled (request->cancel) &&
		    !g_error_matches (error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
			g_warning ("matinee: file chooser failed: %s", error->message);
		g_clear_error (&error);
		chooser_request_free (request);
		return;
	}
	if (window && !g_cancellable_is_cancelled (request->cancel)) {
		guint n = g_list_model_get_n_items (files);
		char **uris = g_new0 (char *, n + 1);
		for (guint i = 0; i < n; i++)
		{
			g_autoptr (GFile) file = g_list_model_get_item (files, i);
			uris[i] = g_file_get_uri (file);
		}
		add_uris (window, uris, request->replace);
		g_strfreev (uris);
	}
	g_object_unref (files);
	chooser_request_free (request);
}

static void
action_open (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	GtkFileDialog *dialog;
	GListStore *filters;
	GtkFileFilter *media;
	GtkFileFilter *all;

	(void) action;
	(void) parameter;
	dialog = gtk_file_dialog_new ();
	gtk_file_dialog_set_title (dialog, "Open Media");

	media = gtk_file_filter_new ();
	gtk_file_filter_set_name (media, "Media files");
	gtk_file_filter_add_mime_type (media, "audio/*");
	gtk_file_filter_add_mime_type (media, "video/*");
	gtk_file_filter_add_pattern (media, "*.mkv");
	gtk_file_filter_add_pattern (media, "*.webm");
	gtk_file_filter_add_pattern (media, "*.m4a");
	gtk_file_filter_add_pattern (media, "*.flac");
	all = gtk_file_filter_new ();
	gtk_file_filter_set_name (all, "All files");
	gtk_file_filter_add_pattern (all, "*");

	filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
	g_list_store_append (filters, media);
	g_list_store_append (filters, all);
	gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
	gtk_file_dialog_set_default_filter (dialog, media);

	gtk_file_dialog_open_multiple (dialog, GTK_WINDOW (window), window->chooser_cancel,
	                               on_files_chosen, chooser_request_new (window, TRUE));
	g_object_unref (dialog);
	g_object_unref (filters);
	g_object_unref (media);
	g_object_unref (all);
}

static void
action_add_files (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	GtkFileDialog *dialog = gtk_file_dialog_new ();
	(void) action;
	(void) parameter;
	gtk_file_dialog_set_title (dialog, "Add Media");
	gtk_file_dialog_open_multiple (dialog, GTK_WINDOW (window), window->chooser_cancel,
	                               on_files_chosen, chooser_request_new (window, FALSE));
	g_object_unref (dialog);
}

static void
on_folder_chosen (GObject *source, GAsyncResult *result, gpointer data)
{
	ChooserRequest *request = data;
	g_autoptr(MatineeWindow) window = g_weak_ref_get (&request->window);
	GtkFileDialog *dialog = GTK_FILE_DIALOG (source);
	GError *error = NULL;
	GFile *folder = gtk_file_dialog_select_folder_finish (dialog, result, &error);

	if (folder == NULL)
	{
		g_clear_error (&error);
		chooser_request_free (request);
		return;
	}
	if (window && !g_cancellable_is_cancelled (request->cancel)) {
		char *path = g_file_get_path (folder);
		if (path != NULL)
		{
			g_settings_set_string (window->settings, "library-path", path);
			action_rescan_library (NULL, NULL, window);
		}
		g_free (path);
	}
	g_object_unref (folder);
	chooser_request_free (request);
}

static void
action_choose_library (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	GtkFileDialog *dialog = gtk_file_dialog_new ();
	(void) action;
	(void) parameter;
	gtk_file_dialog_set_title (dialog, "Choose Library Folder");
	gtk_file_dialog_select_folder (dialog, GTK_WINDOW (window), window->chooser_cancel,
	                               on_folder_chosen, chooser_request_new (window, FALSE));
	g_object_unref (dialog);
}

typedef struct {
	GPtrArray *paths;
	char *warning;
	gboolean truncated;
} LibraryResult;

typedef struct {
	GWeakRef window;
	char *root_path;
	guint generation;
} LibraryRequest;

static void
library_request_free (LibraryRequest *request)
{
	g_weak_ref_clear (&request->window);
	g_free (request->root_path);
	g_free (request);
}

static void
library_result_free (LibraryResult *result)
{
	if (result == NULL)
		return;
	g_ptr_array_unref (result->paths);
	g_free (result->warning);
	g_free (result);
}

static void
scan_directory (GFile *folder, int depth, guint *visited, LibraryResult *result,
		GCancellable *cancel)
{
	GFileEnumerator *enumerator;
	GError *error = NULL;
	static const char *extensions[] = {
		".aac", ".avi", ".flac", ".m4a", ".mka", ".mkv", ".mov",
		".mp3", ".mp4", ".mpeg", ".mpg", ".oga", ".ogg", ".opus",
		".wav", ".webm", ".wma", ".wmv"
	};

	if (g_cancellable_is_cancelled (cancel))
		return;
	if (depth > MATINEE_MAX_LIBRARY_DEPTH || *visited >= MATINEE_MAX_LIBRARY_ENTRIES ||
	    result->paths->len >= MATINEE_MAX_LIBRARY_FILES)
	{
		result->truncated = TRUE;
		return;
	}
	enumerator = g_file_enumerate_children (folder,
		G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
		G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &error);
	if (enumerator == NULL)
	{
		if (!result->warning)
			result->warning = g_strdup_printf ("Some library folders could not be read: %s",
			                                  error ? error->message : "unknown error");
		g_clear_error (&error);
		return;
	}
	for (;;)
	{
		GFileInfo *info = g_file_enumerator_next_file (enumerator, cancel, &error);
		if (info == NULL)
			break;
		(*visited)++;
		GFileType type = g_file_info_get_file_type (info);
		g_autoptr(GFile) child = g_file_get_child (folder, g_file_info_get_name (info));
		if (type == G_FILE_TYPE_DIRECTORY)
			scan_directory (child, depth + 1, visited, result, cancel);
		else if (type == G_FILE_TYPE_REGULAR)
		{
			g_autofree char *path = g_file_get_path (child);
			if (path != NULL)
			{
				g_autofree char *lower = g_ascii_strdown (path, -1);
				for (guint i = 0; i < G_N_ELEMENTS (extensions); ++i)
					if (g_str_has_suffix (lower, extensions[i]))
					{
						g_ptr_array_add (result->paths, g_steal_pointer (&path));
						break;
					}
			}
		}
		g_object_unref (info);
		if (g_cancellable_is_cancelled (cancel))
			break;
		if (*visited >= MATINEE_MAX_LIBRARY_ENTRIES ||
		    result->paths->len >= MATINEE_MAX_LIBRARY_FILES)
		{
			result->truncated = TRUE;
			break;
		}
	}
	if (error != NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
	    result->warning == NULL)
		result->warning = g_strdup_printf ("Some library folders could not be read: %s", error->message);
	g_clear_error (&error);
	g_object_unref (enumerator);
}

static gint
library_path_compare (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (*(char * const *) a, *(char * const *) b);
}

static void
library_scan_worker (GTask *task, gpointer source, gpointer task_data,
		GCancellable *cancel)
{
	LibraryRequest *request = task_data;
	LibraryResult *result = g_new0 (LibraryResult, 1);
	guint visited = 0;
	(void) source;
	result->paths = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GFile) root = g_file_new_for_path (request->root_path);
	scan_directory (root, 0, &visited, result, cancel);
	if (g_cancellable_set_error_if_cancelled (cancel, NULL))
	{
		library_result_free (result);
		g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
		                         "Library scan cancelled");
		return;
	}
	if (result->truncated && result->warning == NULL)
		result->warning = g_strdup ("Library scan reached its safety limit; results may be incomplete.");
	g_ptr_array_sort (result->paths, library_path_compare);
	g_task_return_pointer (task, result, (GDestroyNotify) library_result_free);
}

static gboolean
playlist_contains_path (MatineeWindow *window, const char *path)
{
	for (guint i = 0; i < matinee_playlist_length (window->playlist); ++i)
	{
		MatineeEntry *entry = matinee_playlist_at (window->playlist, i);
		if (g_strcmp0 (matinee_entry_path (entry), path) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
apply_library_result (MatineeWindow *window, LibraryResult *result)
{
	g_autoptr(GHashTable) found = g_hash_table_new (g_str_hash, g_str_equal);
	const char *current_path = NULL;
	gint current = matinee_playlist_current (window->playlist);

	for (guint i = 0; i < result->paths->len; ++i)
		g_hash_table_add (found, g_ptr_array_index (result->paths, i));
	if (current >= 0 && current < (gint) matinee_playlist_length (window->playlist))
	{
		MatineeEntry *entry = matinee_playlist_at (window->playlist, (guint) current);
		current_path = matinee_entry_path (entry);
	}
	for (gint i = (gint) matinee_playlist_length (window->playlist) - 1; i >= 0; --i)
	{
		MatineeEntry *entry = matinee_playlist_at (window->playlist, (guint) i);
		const char *path = matinee_entry_path (entry);
		if (!g_hash_table_contains (window->library_entries, path) ||
		    g_hash_table_contains (found, path))
			continue;
		if (g_strcmp0 (path, current_path) == 0)
			g_hash_table_remove (window->library_entries, path);
		else
		{
			matinee_playlist_remove_at (window->playlist, (guint) i);
			g_hash_table_remove (window->library_entries, path);
		}
	}
	for (guint i = 0; i < result->paths->len; ++i)
	{
		const char *path = g_ptr_array_index (result->paths, i);
		if (!playlist_contains_path (window, path))
		{
			g_autoptr(MatineeEntry) entry = matinee_entry_new (path, 0.0, MATINEE_ENTRY_AUDIO);
			matinee_playlist_append (window->playlist, entry);
			g_hash_table_add (window->library_entries, g_strdup (path));
		}
	}
	gtk_label_set_text (GTK_LABEL (window->library_status), result->warning ? result->warning : "");
	gtk_widget_set_visible (window->library_status, result->warning != NULL);
	refresh_playlist_visibility (window);
}

static void
library_scan_done (GObject *source, GAsyncResult *async_result, gpointer data)
{
	GTask *task = G_TASK (async_result);
	LibraryRequest *request = g_task_get_task_data (task);
	GError *error = NULL;
	LibraryResult *result = g_task_propagate_pointer (task, &error);
	g_autoptr(MatineeWindow) window = g_weak_ref_get (&request->window);
	(void) source;
	if (window && request->generation != window->library_generation)
	{
		if (window->library_rescan_pending)
		{
			window->library_rescan_pending = FALSE;
			g_clear_object (&window->library_cancel);
			action_rescan_library (NULL, NULL, window);
		}
	}
	else if (window)
	{
		g_clear_object (&window->library_cancel);
		if (result != NULL)
			apply_library_result (window, result);
		else if (error != NULL && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		{
			gtk_label_set_text (GTK_LABEL (window->library_status), error->message);
			gtk_widget_set_visible (window->library_status, TRUE);
		}
	}
	g_clear_error (&error);
	if (result != NULL)
		library_result_free (result);
	(void) data;
}

static void
action_rescan_library (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	MatineeWindow *window = data;
	g_autofree char *path = g_settings_get_string (window->settings, "library-path");
	LibraryRequest *request;
	(void) action;
	(void) parameter;
	if (window->library_cancel != NULL)
	{
		window->library_generation++;
		window->library_rescan_pending = TRUE;
		g_cancellable_cancel (window->library_cancel);
		gtk_label_set_text (GTK_LABEL (window->library_status), "Refreshing media library…");
		gtk_widget_set_visible (window->library_status, TRUE);
		return;
	}
	window->library_generation++;
	if (path == NULL || *path == '\0')
	{
		LibraryResult empty = { g_ptr_array_new_with_free_func (g_free), NULL, FALSE };
		apply_library_result (window, &empty);
		g_ptr_array_unref (empty.paths);
		return;
	}
	window->library_cancel = g_cancellable_new ();
	gtk_label_set_text (GTK_LABEL (window->library_status), "Scanning media library…");
	gtk_widget_set_visible (window->library_status, TRUE);
	request = g_new0 (LibraryRequest, 1);
	g_weak_ref_init (&request->window, window);
	request->root_path = g_strdup (path);
	request->generation = window->library_generation;
	GTask *task = g_task_new (NULL, window->library_cancel, library_scan_done, NULL);
	g_task_set_task_data (task, request, (GDestroyNotify) library_request_free);
	g_task_run_in_thread (task, library_scan_worker);
	g_object_unref (task);
}

/*** Playlist glue ***/

static void
refresh_playlist_visibility (MatineeWindow *window)
{
	gboolean has_entries = matinee_playlist_length (window->playlist) > 0;
	gtk_widget_set_visible (window->playlist_scroll, has_entries);
}

static const GActionEntry window_actions[] = {
	{ .name = "open",             .activate = action_open },
	{ .name = "add-files",        .activate = action_add_files },
	{ .name = "play-pause",       .activate = action_play_pause },
	{ .name = "stop",             .activate = action_stop },
	{ .name = "next",             .activate = action_next },
	{ .name = "previous",         .activate = action_previous },
	{ .name = "clear-playlist",   .activate = action_clear_playlist },
	{ .name = "choose-library",   .activate = action_choose_library },
	{ .name = "rescan-library",   .activate = action_rescan_library },
	{ .name = "shortcuts",        .activate = action_shortcuts },
	{ .name = "about",            .activate = action_about },
};

/*** GObject boilerplate ***/

static void
matinee_window_dispose (GObject *object)
{
	MatineeWindow *window = MATINEE_WINDOW (object);
	if (window->chooser_cancel)
		g_cancellable_cancel (window->chooser_cancel);
	window->library_rescan_pending = FALSE;
	window->library_generation++;
	if (window->library_cancel)
		g_cancellable_cancel (window->library_cancel);
	window->open_generation++;
	if (window->opening_engine != NULL)
		matinee_engine_cancel_io (window->opening_engine);
	if (window->visual_tick_id != 0)
	{
		gtk_widget_remove_tick_callback (window->surface, window->visual_tick_id);
		window->visual_tick_id = 0;
	}
	if (window->position_timer != 0)
	{
		g_source_remove (window->position_timer);
		window->position_timer = 0;
	}
	if (window->engine != NULL)
	{
		g_signal_handlers_disconnect_by_data (window->engine, window);
		matinee_engine_stop (window->engine);
	}
	g_clear_object (&window->playlist);
	g_clear_object (&window->engine);
	g_clear_object (&window->opening_engine);
	g_clear_pointer (&window->visual, matinee_visual_free);
	g_clear_object (&window->settings);
	g_clear_object (&window->chooser_cancel);
	g_clear_object (&window->library_cancel);
	g_clear_pointer (&window->library_entries, g_hash_table_unref);
	/* GtkWindow owns child/titlebar teardown. Drop those links before
	 * disposing template references so the parent never sees stale widgets. */
	gtk_window_set_child (GTK_WINDOW(window), NULL);
	gtk_window_set_titlebar (GTK_WINDOW(window), NULL);
	gtk_widget_dispose_template (GTK_WIDGET (window), MATINEE_TYPE_WINDOW);
	G_OBJECT_CLASS (matinee_window_parent_class)->dispose (object);
}

static void
matinee_window_class_init (MatineeWindowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = matinee_window_dispose;
	gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
	                                             "/org/luxe/Matinee/data/matinee.ui");
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, header);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, open_button);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, menu_button);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, paned);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, surface);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, empty_page);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, playlist_scroll);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, playlist_view);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, seek);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, play_button);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, stop_button);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, position_label);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, library_status);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, now_playing);
	gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), MatineeWindow, volume);
}

static void
matinee_window_init (MatineeWindow *window)
{
	window->chooser_cancel = g_cancellable_new ();
	window->library_entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	gtk_widget_init_template (GTK_WIDGET (window));
	if (window->header == NULL || window->open_button == NULL || window->menu_button == NULL
	    || window->paned == NULL || window->surface == NULL || window->empty_page == NULL
	    || window->playlist_scroll == NULL || window->playlist_view == NULL
	    || window->seek == NULL || window->play_button == NULL || window->stop_button == NULL
	    || window->position_label == NULL || window->library_status == NULL
	    || window->now_playing == NULL || window->volume == NULL)
		return;
	/* Style classes are set here rather than in the template: a nested
	 * <style><class><style> block is rejected by GtkBuilder. */
	gtk_widget_add_css_class (window->surface, "card");
	gtk_widget_add_css_class (window->empty_page, "compact");
	gtk_widget_add_css_class (window->playlist_view, "navigation-sidebar");
	gtk_widget_add_css_class (window->play_button, "suggested-action");
	gtk_widget_add_css_class (window->position_label, "dim-label");
	gtk_widget_add_css_class (window->position_label, "numeric");
	gtk_widget_add_css_class (window->now_playing, "dim-label");
	gtk_widget_add_css_class (window->now_playing, "caption");
	window->engine = matinee_engine_new ();
	window->playlist = matinee_playlist_new ();
	window->visual = matinee_visual_new ();
	window->settings = g_settings_new ("org.luxe.Matinee");

	g_action_map_add_action_entries (G_ACTION_MAP (window), window_actions,
	                                   G_N_ELEMENTS (window_actions), window);

	g_signal_connect (window->engine, "video-frame", G_CALLBACK (on_video_frame), window);
	g_signal_connect (window->engine, "state-changed", G_CALLBACK (on_state_changed), window);
	g_signal_connect (window->engine, "finished", G_CALLBACK (on_finished), window);

	gtk_range_set_value (GTK_RANGE (window->volume),
	                     g_settings_get_double (window->settings, "volume") * 100.0);
	matinee_engine_set_volume (window->engine,
	                          gtk_range_get_value (GTK_RANGE (window->volume)) / 100.0);
	g_signal_connect (window->seek, "value-changed", G_CALLBACK (on_seek_changed), window);
	g_signal_connect (window->volume, "value-changed", G_CALLBACK (on_volume_changed), window);
	g_signal_connect (window->surface, "notify::mapped", G_CALLBACK (on_surface_mapped_changed), window);

	action_rescan_library (NULL, NULL, window);
	update_play_button (window);
	set_surface_mode (window);
	refresh_playlist_visibility (window);
	create_playlist_selection (window);
}

MatineeWindow *
matinee_window_new (GtkApplication *app)
{
	return g_object_new (MATINEE_TYPE_WINDOW, "application", app, NULL);
}

void
matinee_window_open_files (MatineeWindow *window, const char * const *files)
{
	char **uris;
	guint n = 0;

	if (files == NULL)
		return;
	while (files[n] != NULL)
		n += 1;
	uris = g_new0 (char *, n + 1);
	for (guint i = 0; i < n; i++)
		uris[i] = g_filename_to_uri (files[i], NULL, NULL);
	add_uris (window, uris, TRUE);
	g_strfreev (uris);
}
