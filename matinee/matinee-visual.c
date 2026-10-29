/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* Original audio-reactive plasma, kaleidoscope and ripple fields. Patterns
 * crossfade slowly into each other, with warped RGB feedback for trails.
 * Fixed 320x240 rendering keeps cost independent of the window's size. */

#include "matinee-visual.h"

#include <math.h>
#include <string.h>

#define VISUAL_WIDTH  320
#define VISUAL_HEIGHT 240
#define VISUAL_MAX_BAND_AMPLITUDE 22.0f
#define VISUAL_SCENE_SECONDS 18.0f

struct _MatineeVisual
{
	guint8 *previous;			/* RGBA feedback buffer */
	guint8 *scratch;			/* RGBA frame being built */
	gboolean have_previous;
	gint64 origin_us;
	float *radius;
	float *angle;
	GdkTexture *texture;
};

static inline guchar
clamp_u8 (float value)
{
	if (value <= 0.0f) return 0;
	if (value >= 255.0f) return 255;
	return (guchar) (value + 0.5f);
}

/* Cheap HSV to RGB with hue in turns.  Keeping the maths inline avoids a
 * per-pixel function call at this resolution. */
static void
hsv_to_rgb (float hue, float saturation, float value,
            guchar *r, guchar *g, guchar *b)
{
	float sector, fraction;
	int index;

	hue = hue - floorf (hue);
	sector = hue * 6.0f;
	index = (int) sector;
	fraction = sector - index;

	switch (index)
	{
	case 0: *r = value * 255.0f;                *g = value * 255.0f * fraction;  *b = 0; break;
	case 1: *r = value * 255.0f * (1.0f - fraction); *g = value * 255.0f;        *b = 0; break;
	case 2: *r = 0;                            *g = value * 255.0f;            *b = value * 255.0f * fraction; break;
	case 3: *r = 0;                            *g = value * 255.0f * (1.0f - fraction); *b = value * 255.0f; break;
	case 4: *r = value * 255.0f * fraction;    *g = 0;                          *b = value * 255.0f; break;
	default:*r = value * 255.0f;               *g = 0;                          *b = value * 255.0f * (1.0f - fraction); break;
	}
	if (saturation < 1.0f)
	{
		/* Desaturate toward luminance. */
		float grey = (*r + *g + *b) / 3.0f;
		*r = (guchar) (grey + (*r - grey) * saturation + 0.5f);
		*g = (guchar) (grey + (*g - grey) * saturation + 0.5f);
		*b = (guchar) (grey + (*b - grey) * saturation + 0.5f);
	}
}

MatineeVisual *
matinee_visual_new (void)
{
	MatineeVisual *visual = g_new0 (MatineeVisual, 1);
	visual->previous = g_malloc0 (VISUAL_WIDTH * VISUAL_HEIGHT * 4);
	visual->scratch = g_malloc0 (VISUAL_WIDTH * VISUAL_HEIGHT * 4);
	visual->radius = g_new (float, VISUAL_WIDTH * VISUAL_HEIGHT);
	visual->angle = g_new (float, VISUAL_WIDTH * VISUAL_HEIGHT);
	visual->origin_us = -1;
	for (int y = 0; y < VISUAL_HEIGHT; y++)
		for (int x = 0; x < VISUAL_WIDTH; x++)
		{
			float px = (x - VISUAL_WIDTH * 0.5f) / (VISUAL_HEIGHT * 0.5f);
			float py = (y - VISUAL_HEIGHT * 0.5f) / (VISUAL_HEIGHT * 0.5f);
			visual->radius[y * VISUAL_WIDTH + x] = hypotf (px, py);
			visual->angle[y * VISUAL_WIDTH + x] = atan2f (py, px);
		}
	return visual;
}

void
matinee_visual_free (MatineeVisual *visual)
{
	if (visual == NULL)
		return;
	g_clear_object (&visual->texture);
	g_free (visual->previous);
	g_free (visual->scratch);
	g_free (visual->radius);
	g_free (visual->angle);
	g_free (visual);
}

void
matinee_visual_reset (MatineeVisual *visual)
{
	g_return_if_fail (visual != NULL);
	visual->have_previous = FALSE;
	visual->origin_us = -1;
	g_clear_object (&visual->texture);
	memset (visual->previous, 0, VISUAL_WIDTH * VISUAL_HEIGHT * 4);
}

/* A continuous hue field and its ridges, not a hard-edged preset switch. */
static float
visual_field (guint scene, float px, float py, float radius, float angle,
              float t, float bass)
{
	switch (scene)
	{
	case 0: /* Liquid plasma. */
		return (sinf (px * 5.0f + t * 0.6f)
		        + sinf (py * 6.0f - t * 0.45f)
		        + sinf (radius * 9.0f - t * 0.8f)) / 3.0f;
	case 1: /* Six-fold rotating petals. */
		return sinf (angle * 6.0f + t * 0.35f
		             + sinf (radius * 5.0f - t * 0.5f) * 2.0f)
		       * cosf (radius * (8.0f + bass * 3.0f) - t * 0.8f);
	default: /* Interfering ripples through a twisting tunnel. */
		return sinf (radius * (13.0f + bass * 4.0f) - t
		             + sinf (angle * 3.0f + t * 0.25f))
		       * cosf (py * 4.0f + px * 3.0f + t * 0.3f);
	}
}

void
matinee_visual_render (MatineeVisual *visual,
                      const MatineeBands *bands,
                      gint64              time_us)
{
	float t;
	float bass, mid, treble, overall;
	float amplitude;
	float hue_turn;
	int x, y;
	guint8 *source, *target;

	g_return_if_fail (visual != NULL);

	/* Audio bands arrive smoothed; derive the three drive levels.  A soft
	 * floor keeps a tunnel drifting during silence instead of freezing. */
	bass = 0.0f; mid = 0.0f; treble = 0.0f;
	if (bands != NULL)
	{
		for (int i = 0; i < 16 && i < MATINEE_BANDS; i++)
			bass += bands->level[i];
		for (int i = 16; i < 40 && i < MATINEE_BANDS; i++)
			mid += bands->level[i];
		for (int i = 40; i < MATINEE_BANDS; i++)
			treble += bands->level[i];
		bass /= 16.0f;
		mid /= 24.0f;
		treble /= (MATINEE_BANDS - 40);
	}
	/* Floors keep the field alive when nothing is playing. */
	bass = MAX (bass, 0.06f);
	mid = MAX (mid, 0.05f);
	treble = MAX (treble, 0.04f);
	overall = (bass + mid + treble) / 3.0f;

	if (visual->origin_us < 0 || time_us < visual->origin_us)
		visual->origin_us = time_us;
	/* Subtract before converting: float uptime loses sub-frame precision. */
	t = (float) (((double) time_us - visual->origin_us) / 1000000.0);
	amplitude = 3.0f + bass * VISUAL_MAX_BAND_AMPLITUDE;
	hue_turn = t * 0.025f + mid * 0.2f;
	float cycle = fmodf (t / VISUAL_SCENE_SECONDS, 3.0f);
	guint scene = (guint) cycle;
	/* Spend most of each scene settled, then blend over its final 6 s. */
	float blend = CLAMP ((cycle - scene - 2.0f / 3.0f) * 3.0f, 0.0f, 1.0f);
	blend = blend * blend * (3.0f - 2.0f * blend);

	/* First frame has nothing to warp; seed the buffer with a dark field. */
	if (!visual->have_previous)
	{
		memset (visual->previous, 0, VISUAL_WIDTH * VISUAL_HEIGHT * 4);
		visual->have_previous = TRUE;
	}

	source = visual->previous;
	target = visual->scratch;
	for (y = 0; y < VISUAL_HEIGHT; y++)
	{
		for (x = 0; x < VISUAL_WIDTH; x++)
		{
			float dx = sinf ((float) y * 0.013f + t * 0.7f)
			         + sinf ((float) x * 0.011f - t * 0.5f);
			float dy = cosf ((float) x * 0.012f + t * 0.6f)
			         + cosf ((float) y * 0.014f - t * 0.4f);
			int sx, sy;
			int index = y * VISUAL_WIDTH + x;
			float px = (x - VISUAL_WIDTH * 0.5f) / (VISUAL_HEIGHT * 0.5f);
			float py = (y - VISUAL_HEIGHT * 0.5f) / (VISUAL_HEIGHT * 0.5f);
			float field = visual_field (scene, px, py, visual->radius[index],
			                            visual->angle[index], t, bass);
			if (blend > 0.0f)
				field += blend * (visual_field ((scene + 1) % 3, px, py,
				                  visual->radius[index], visual->angle[index], t, bass) - field);
			guint8 r, g, b;

			/* Warp feedback gently; always seed real spatial detail so the
			 * field cannot collapse into a nearly uniform colour wash. */
			sx = (int) ((float) x + dx * amplitude);
			sy = (int) ((float) y + dy * amplitude);
			sx = CLAMP (sx, 0, VISUAL_WIDTH - 1);
			sy = CLAMP (sy, 0, VISUAL_HEIGHT - 1);

			float ridge = 1.0f - fabsf (field);
			float shade = 0.16f + ridge * ridge * (0.6f + treble * 0.18f);
			hsv_to_rgb (hue_turn + field * 0.5f + visual->radius[index] * 0.16f,
			            0.8f + overall * 0.15f, CLAMP (shade, 0.0f, 1.0f),
			            &r, &g, &b);
			guint sample = (sy * VISUAL_WIDTH + sx) * 4;
			target[index * 4 + 0] = clamp_u8 (r * 0.58f + source[sample] * 0.4f);
			target[index * 4 + 1] = clamp_u8 (g * 0.58f + source[sample + 1] * 0.4f);
			target[index * 4 + 2] = clamp_u8 (b * 0.58f + source[sample + 2] * 0.4f);
			target[index * 4 + 3] = 255;
		}
	}

	/* Swap buffers and publish a fresh texture. */
	visual->previous = target;
	visual->scratch = source;
	g_clear_object (&visual->texture);
	{
		const gsize size = (gsize) VISUAL_WIDTH * VISUAL_HEIGHT * 4;
		guchar *copy = g_malloc (size);
		memcpy (copy, visual->previous, size);
		GBytes *bytes = g_bytes_new_take (copy, size);
		visual->texture = gdk_memory_texture_new (VISUAL_WIDTH, VISUAL_HEIGHT,
		                                          GDK_MEMORY_R8G8B8A8,
		                                          bytes, VISUAL_WIDTH * 4);
		g_bytes_unref (bytes);
	}
}

GdkTexture *
matinee_visual_texture (MatineeVisual *visual)
{
	g_return_val_if_fail (visual != NULL, NULL);
	return visual->texture;
}

int
matinee_visual_width (MatineeVisual *visual)
{
	(void) visual;
	return VISUAL_WIDTH;
}

int
matinee_visual_height (MatineeVisual *visual)
{
	(void) visual;
	return VISUAL_HEIGHT;
}

/* ------------------------------------------------------------------------ */
/* Radix-2 Cooley-Tukey FFT.  Small, iterative, in a scratch buffer so it is
 * safe to call from the audio thread. */

static void
fft_complex (float *re, float *im, int n)
{
	for (int i = 1, j = 0; i < n; i++)
	{
		int bit = n >> 1;
		float temp;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
		{
			temp = re[i]; re[i] = re[j]; re[j] = temp;
			temp = im[i]; im[i] = im[j]; im[j] = temp;
		}
	}
	for (int length = 2; length <= n; length <<= 1)
	{
		float angle = -2.0f * (float) G_PI / (float) length;
		float w_real = cosf (angle);
		float w_imag = sinf (angle);
		for (int i = 0; i < n; i += length)
		{
			float cur_real = 1.0f, cur_imag = 0.0f;
			for (int j = 0; j < length / 2; j++)
			{
				float u_real = re[i + j], u_imag = im[i + j];
				float v_real = re[i + j + length / 2] * cur_real
				             - im[i + j + length / 2] * cur_imag;
				float v_imag = re[i + j + length / 2] * cur_imag
				             + im[i + j + length / 2] * cur_real;
				re[i + j] = u_real + v_real;
				im[i + j] = u_imag + v_imag;
				re[i + j + length / 2] = u_real - v_real;
				im[i + j + length / 2] = u_imag - v_imag;
				float next_real = cur_real * w_real - cur_imag * w_imag;
				cur_imag = cur_real * w_imag + cur_imag * w_real;
				cur_real = next_real;
			}
		}
	}
}

struct _MatineeFft
{
	int n;
	float *re, *im, *window;
};

MatineeFft *
matinee_fft_new (int n)
{
	g_return_val_if_fail (n > 1 && (n & (n - 1)) == 0, NULL);
	MatineeFft *fft = g_new0 (MatineeFft, 1);
	fft->n = n;
	fft->re = g_new (float, n);
	fft->im = g_new (float, n);
	fft->window = g_new (float, n);
	for (int i = 0; i < n; i++)
		fft->window[i] = 0.5f * (1.0f - cosf (2.0f * (float) G_PI * (float) i / (float) (n - 1)));
	return fft;
}

void
matinee_fft_free (MatineeFft *fft)
{
	if (!fft) return;
	g_free (fft->re);
	g_free (fft->im);
	g_free (fft->window);
	g_free (fft);
}

void
matinee_fft_compute (MatineeFft *fft, const float *samples, float *out_mags)
{
	g_return_if_fail (fft && samples && out_mags);
	for (int i = 0; i < fft->n; i++)
	{
		fft->re[i] = samples[i] * fft->window[i];
		fft->im[i] = 0.0f;
	}
	fft_complex (fft->re, fft->im, fft->n);
	for (int i = 0; i < fft->n / 2; i++)
		out_mags[i] = sqrtf (fft->re[i] * fft->re[i] + fft->im[i] * fft->im[i]);
}

void
matinee_fft_magnitude (const float *samples, int n, float *out_mags)
{
	if (n <= 1 || out_mags == NULL) return;
	MatineeFft *fft = matinee_fft_new (n);
	if (fft == NULL) return;
	matinee_fft_compute (fft, samples, out_mags);
	matinee_fft_free (fft);
}
