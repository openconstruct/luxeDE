/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MATINEE_MODEL_H
#define MATINEE_MODEL_H

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
	MATINEE_ENTRY_AUDIO,
	MATINEE_ENTRY_VIDEO
} MatineeEntryKind;

#define MATINEE_TYPE_ENTRY (matinee_entry_get_type ())
G_DECLARE_FINAL_TYPE (MatineeEntry, matinee_entry, MATINEE, ENTRY, GObject)

const char *matinee_entry_path    (MatineeEntry *entry);
const char *matinee_entry_display (MatineeEntry *entry);
double      matinee_entry_length  (MatineeEntry *entry);
MatineeEntryKind matinee_entry_kind (MatineeEntry *entry);

MatineeEntry *matinee_entry_new (const char *path, double length, MatineeEntryKind kind);

/* Ordered playlist of entries.  Reordering mutates in place and emits
 * items-changed, so views follow without being rebuilt. */
#define MATINEE_TYPE_PLAYLIST (matinee_playlist_get_type ())
G_DECLARE_FINAL_TYPE (MatineePlaylist, matinee_playlist, MATINEE, PLAYLIST, GObject)

MatineePlaylist *matinee_playlist_new       (void);
guint           matinee_playlist_length    (MatineePlaylist *playlist);
MatineeEntry    *matinee_playlist_at        (MatineePlaylist *playlist, guint position);
gint            matinee_playlist_index_of  (MatineePlaylist *playlist, MatineeEntry *entry);
void            matinee_playlist_append    (MatineePlaylist *playlist, MatineeEntry *entry);
void            matinee_playlist_insert    (MatineePlaylist *playlist, MatineeEntry *entry, guint position);
void            matinee_playlist_remove_at (MatineePlaylist *playlist, guint position);
void            matinee_playlist_move      (MatineePlaylist *playlist, guint from, guint to);
void            matinee_playlist_clear     (MatineePlaylist *playlist);
gint            matinee_playlist_current   (MatineePlaylist *playlist);
void            matinee_playlist_set_current (MatineePlaylist *playlist, gint index);
gboolean        matinee_playlist_move_next (MatineePlaylist *playlist);
gboolean        matinee_playlist_move_prev (MatineePlaylist *playlist);

G_END_DECLS

#endif /* MATINEE_MODEL_H */
