/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MATINEE_ENGINE_H
#define MATINEE_ENGINE_H

#include <gio/gio.h>
#include <gdk/gdk.h>

#include "matinee-visual.h"

G_BEGIN_DECLS

#define MATINEE_ENGINE_ERROR (matinee_engine_error_quark ())

typedef enum
{
	MATINEE_STATE_IDLE,
	MATINEE_STATE_PLAYING,
	MATINEE_STATE_PAUSED,
	MATINEE_STATE_ENDED,
	MATINEE_STATE_ERROR
} MatineeState;

GQuark matinee_engine_error_quark (void);

#define MATINEE_TYPE_ENGINE (matinee_engine_get_type ())
G_DECLARE_FINAL_TYPE (MatineeEngine, matinee_engine, MATINEE, ENGINE, GObject)

MatineeEngine *matinee_engine_new (void);

/* Load a native filename (not a percent-escaped file URI) or FFmpeg URL and
 * prepare decoders. Returns FALSE and sets `error` on
 * failure; no state changes on error. */
gboolean matinee_engine_open (MatineeEngine *engine, const char *uri, GError **error);
/* Interrupts an in-progress FFmpeg probe/read and wakes paused workers. */
void matinee_engine_cancel_io (MatineeEngine *engine);

void matinee_engine_play  (MatineeEngine *engine);
void matinee_engine_pause (MatineeEngine *engine);
void matinee_engine_stop  (MatineeEngine *engine);
gboolean matinee_engine_seek_fraction (MatineeEngine *engine, double fraction);
void matinee_engine_set_volume (MatineeEngine *engine, double volume);
/* Skip FFT work when the audio-only visualizer has no visible consumer. */
void matinee_engine_set_spectrum_enabled (MatineeEngine *engine, gboolean enabled);

MatineeState  matinee_engine_state       (MatineeEngine *engine);
/* Caller owns the returned diagnostic string; NULL means no playback error. */
char *       matinee_engine_dup_error_message (MatineeEngine *engine);
double       matinee_engine_position    (MatineeEngine *engine);
double       matinee_engine_duration    (MatineeEngine *engine);
gboolean     matinee_engine_has_video   (MatineeEngine *engine);
/* Borrowed values remain valid until the same getter is called again on this
 * engine or the engine is finalized. */
const char  *matinee_engine_title       (MatineeEngine *engine);
const char  *matinee_engine_artist      (MatineeEngine *engine);
/* Copies the most recent spectrum under the engine lock.  The caller must not
 * hold a pointer into the engine, because the audio thread updates it. */
void matinee_engine_get_bands (MatineeEngine *engine, MatineeBands *out_bands);

G_END_DECLS

#endif /* MATINEE_ENGINE_H */
