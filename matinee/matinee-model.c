/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* Playlist model and the on-demand library scan.  The scan is deliberately a
 * plain directory walk on demand: no file monitor and no database, so there is
 * no background state to keep correct.  Every path is bounded and untrusted,
 * so the walk refuses symlinks, special files, and anything oversized. */

#include "matinee-model.h"

#include <string.h>
#include <libavformat/avformat.h>

#define MATINEE_MAX_LIBRARY_DEPTH 12
#define MATINEE_MAX_LIBRARY_FILES 20000

struct _MatineeEntry
{
	GObject parent_instance;
	char *path;
	char *display;
	double length;
	MatineeEntryKind kind;
};

G_DEFINE_TYPE (MatineeEntry, matinee_entry, G_TYPE_OBJECT)

static void
matinee_entry_finalize (GObject *object)
{
	MatineeEntry *entry = MATINEE_ENTRY (object);
	g_free (entry->path);
	g_free (entry->display);
	G_OBJECT_CLASS (matinee_entry_parent_class)->finalize (object);
}

static void
matinee_entry_class_init (MatineeEntryClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = matinee_entry_finalize;
}

static void
matinee_entry_init (MatineeEntry *entry)
{
	(void) entry;
}

MatineeEntry *
matinee_entry_new (const char *path, double length, MatineeEntryKind kind)
{
	MatineeEntry *entry;
	char *base;

	g_return_val_if_fail (path != NULL, NULL);
	entry = g_object_new (MATINEE_TYPE_ENTRY, NULL);
	entry->path = g_strdup (path);
	base = g_path_get_basename (path);
	/* Strip the extension for display; the full path stays in ->path. */
	{
		char *dot = strrchr (base, '.');
		if (dot != NULL && dot != base)
			*dot = '\0';
	}
	entry->display = g_filename_display_name (base);
	g_free (base);
	entry->length = MAX (length, 0.0);
	entry->kind = kind;
	return entry;
}

const char *
matinee_entry_path (MatineeEntry *entry)
{
	return entry->path;
}

const char *
matinee_entry_display (MatineeEntry *entry)
{
	return entry->display;
}

double
matinee_entry_length (MatineeEntry *entry)
{
	return entry->length;
}

MatineeEntryKind
matinee_entry_kind (MatineeEntry *entry)
{
	return entry->kind;
}

/*** Playlist ***/

struct _MatineePlaylist
{
	GObject parent_instance;
	GPtrArray *entries;	/* MatineeEntry* */
	gint current;		/* -1 when nothing is selected */
};

static void matinee_playlist_list_model_init (GListModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MatineePlaylist, matinee_playlist, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL,
                                                matinee_playlist_list_model_init))

static void
matinee_playlist_dispose (GObject *object)
{
	MatineePlaylist *playlist = MATINEE_PLAYLIST (object);
	g_clear_pointer (&playlist->entries, g_ptr_array_unref);
	G_OBJECT_CLASS (matinee_playlist_parent_class)->dispose (object);
}

static void
matinee_playlist_class_init (MatineePlaylistClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = matinee_playlist_dispose;
}

static void
matinee_playlist_init (MatineePlaylist *playlist)
{
	playlist->entries = g_ptr_array_new_with_free_func (g_object_unref);
	playlist->current = -1;
}

static GType
matinee_playlist_get_item_type (GListModel *model)
{
	(void) model;
	return MATINEE_TYPE_ENTRY;
}

static guint
matinee_playlist_get_n_items (GListModel *model)
{
	return MATINEE_PLAYLIST (model)->entries->len;
}

static gpointer
matinee_playlist_get_item (GListModel *model, guint position)
{
	MatineePlaylist *playlist = MATINEE_PLAYLIST (model);
	if (position >= playlist->entries->len)
		return NULL;
	return g_object_ref (g_ptr_array_index (playlist->entries, position));
}

static void
matinee_playlist_list_model_init (GListModelInterface *iface)
{
	iface->get_item_type = matinee_playlist_get_item_type;
	iface->get_n_items = matinee_playlist_get_n_items;
	iface->get_item = matinee_playlist_get_item;
}

MatineePlaylist *
matinee_playlist_new (void)
{
	return g_object_new (MATINEE_TYPE_PLAYLIST, NULL);
}

guint
matinee_playlist_length (MatineePlaylist *playlist)
{
	return playlist->entries->len;
}

MatineeEntry *
matinee_playlist_at (MatineePlaylist *playlist, guint position)
{
	if (position >= playlist->entries->len)
		return NULL;
	return g_ptr_array_index (playlist->entries, position);
}

gint
matinee_playlist_index_of (MatineePlaylist *playlist, MatineeEntry *entry)
{
	for (guint i = 0; i < playlist->entries->len; i++)
		if (g_ptr_array_index (playlist->entries, i) == entry)
			return (gint) i;
	return -1;
}

void
matinee_playlist_insert (MatineePlaylist *playlist, MatineeEntry *entry, guint position)
{
	g_return_if_fail (MATINEE_IS_PLAYLIST (playlist));
	g_return_if_fail (MATINEE_IS_ENTRY (entry));
	position = MIN (position, playlist->entries->len);
	g_ptr_array_insert (playlist->entries, (int) position, g_object_ref (entry));
	if (playlist->current >= (gint)position)
		playlist->current++;
	g_list_model_items_changed (G_LIST_MODEL (playlist), position, 0, 1);
}

void
matinee_playlist_append (MatineePlaylist *playlist, MatineeEntry *entry)
{
	matinee_playlist_insert (playlist, entry, playlist->entries->len);
}

void
matinee_playlist_remove_at (MatineePlaylist *playlist, guint position)
{
	g_return_if_fail (MATINEE_IS_PLAYLIST (playlist));
	if (position >= playlist->entries->len)
		return;
	g_ptr_array_remove_index (playlist->entries, position);
	if (playlist->current == (gint) position)
		playlist->current = -1;
	else if (playlist->current > (gint)position)
		playlist->current--;
	g_list_model_items_changed (G_LIST_MODEL (playlist), position, 1, 0);
}

void
matinee_playlist_move (MatineePlaylist *playlist, guint from, guint to)
{
	MatineeEntry *entry;
	gint current;

	g_return_if_fail (MATINEE_IS_PLAYLIST (playlist));
	if (from >= playlist->entries->len || to >= playlist->entries->len || from == to)
		return;
	entry = g_object_ref (g_ptr_array_index (playlist->entries, from));
	current = playlist->current;
	g_ptr_array_remove_index (playlist->entries, from);
	g_ptr_array_insert (playlist->entries, (int) to, entry);
	/* Keep the selection pointing at the same entry after the move. */
	if (current == (gint) from)
		playlist->current = (gint) to;
	else if (current > (gint)from && current <= (gint)to)
		playlist->current--;
	else if (current >= (gint)to && current < (gint)from)
		playlist->current++;
	guint start = MIN(from, to), span = MAX(from, to) - start + 1;
	g_list_model_items_changed (G_LIST_MODEL (playlist),
	                            start, span, span);
}

void
matinee_playlist_clear (MatineePlaylist *playlist)
{
	guint n;

	g_return_if_fail (MATINEE_IS_PLAYLIST (playlist));
	n = playlist->entries->len;
	if (n == 0)
		return;
	playlist->current = -1;
	g_ptr_array_remove_range (playlist->entries, 0, n);
	g_list_model_items_changed (G_LIST_MODEL (playlist), 0, n, 0);
}

gint
matinee_playlist_current (MatineePlaylist *playlist)
{
	return playlist->current;
}

void
matinee_playlist_set_current (MatineePlaylist *playlist, gint index)
{
	playlist->current = (index >= 0 && index < (gint) playlist->entries->len) ? index : -1;
}

/* Advance the selection by one entry.  Returns FALSE at the end of the list so
 * the caller can stop rather than loop forever. */
gboolean
matinee_playlist_move_next (MatineePlaylist *playlist)
{
	gint next = playlist->current + 1;

	if (next < 0 || next >= (gint) playlist->entries->len)
		return FALSE;
	playlist->current = next;
	return TRUE;
}

gboolean
matinee_playlist_move_prev (MatineePlaylist *playlist)
{
	gint previous = playlist->current - 1;

	if (playlist->current < 0 || previous < 0)
		return FALSE;
	playlist->current = previous;
	return TRUE;
}
