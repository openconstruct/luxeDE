/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MATINEE_VISUAL_H
#define MATINEE_VISUAL_H

#include <glib.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

/* Number of logarithmically spaced magnitude bands handed to the renderer. */
#define MATINEE_BANDS 64

typedef struct
{
	float level[MATINEE_BANDS];		/* normalised 0..1 magnitudes */
	gint64 time_us;				/* timestamp of this analysis */
	guint64 generation;			/* increments on every update */
} MatineeBands;

/* Radix-2 FFT magnitude with a Hann window. Writes n/2 magnitudes. */
typedef struct _MatineeFft MatineeFft;
MatineeFft *matinee_fft_new (int n);
void matinee_fft_free (MatineeFft *fft);
void matinee_fft_compute (MatineeFft *fft, const float *samples, float *out_mags);
void matinee_fft_magnitude (const float *samples,
                           int           n,
                           float        *out_mags);

/* Slowly morphing plasma, kaleidoscope and ripple visualiser, rendered at a
 * fixed low resolution with RGB feedback. Original code, no third-party
 * source; time_us is a monotonic animation timestamp, independent of audio. */
typedef struct _MatineeVisual MatineeVisual;

MatineeVisual *matinee_visual_new   (void);
void          matinee_visual_free  (MatineeVisual *visual);
void          matinee_visual_reset (MatineeVisual *visual);
void          matinee_visual_render (MatineeVisual *visual,
                                    const MatineeBands *bands,
                                    gint64              time_us);
GdkTexture   *matinee_visual_texture (MatineeVisual *visual);
int           matinee_visual_width  (MatineeVisual *visual);
int           matinee_visual_height (MatineeVisual *visual);

G_END_DECLS

#endif /* MATINEE_VISUAL_H */
