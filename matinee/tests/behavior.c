/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* Behaviour coverage for Matinee.  Media is generated in-process (a real WAV
 * container and a real Matinee entry list), so the suite carries no binary
 * fixtures. MP3 is generated with ffmpeg when available. Decode/output tests
 * use a private ALSA null sink and never touch the user's sound device. */

#include "luxe-gtk-ui.h"
#include <glib/gstdio.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <alsa/asoundlib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

/* Decode/resample/write for real, but never resolve the user's default PCM. */
static gint nonzero_audio;
static gint peak_audio, audio_writes;
static gint fail_audio_write;
static unsigned int forced_output_rate;
static gboolean simulate_short_writes;
static guint scripted_step, accepted_frames, recover_calls, wait_calls;
static gint16 accepted_samples[64];
static GCancellable *cancel_after_write;
static GMutex drain_lock;
static GCond drain_cond;
static gboolean block_drain, drain_entered, release_drain;
static int test_pcm_open (snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode)
{
	snd_config_t *config = NULL;
	g_assert_cmpstr (name, ==, "default");
	int result = snd_config_load_string (&config, "pcm.test_sink { type null }", 0);
	if (result >= 0)
		result = snd_pcm_open_lconf (pcm, "test_sink", stream, mode, config);
	if (config) snd_config_delete (config);
	return result;
}
static snd_pcm_sframes_t test_pcm_write (snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	const gint16 *samples = buffer;
	if(g_atomic_int_get(&fail_audio_write))return -EIO;
	if(simulate_short_writes) {
		if(scripted_step++==0)return -EINTR;
		if(scripted_step==2)return -EAGAIN;
		if(scripted_step==3)return -EPIPE;
		guint n=MIN(size,3);
		g_assert_cmpuint(accepted_frames+n,<=,32);
		memcpy(accepted_samples+accepted_frames*2,samples,n*2*sizeof(gint16));
		accepted_frames+=n;
		if(cancel_after_write)g_cancellable_cancel(cancel_after_write);
		return n;
	}
	int peak = 0;
	for (gsize i = 0; i < size * 2; i++)
		peak = MAX(peak, abs((int)samples[i]));
    if (peak) g_atomic_int_set(&nonzero_audio, 1);
    g_atomic_int_set(&peak_audio, MAX(peak, g_atomic_int_get(&peak_audio)));
    g_atomic_int_inc(&audio_writes);
	return snd_pcm_writei (pcm, buffer, size);
}
static int test_pcm_wait(snd_pcm_t *pcm,int timeout)
{ if(simulate_short_writes){wait_calls++;return 1;}return snd_pcm_wait(pcm,timeout); }
static int test_pcm_recover(snd_pcm_t *pcm,int error,int silent)
{ if(simulate_short_writes){g_assert_cmpint(error,==,-EPIPE);recover_calls++;return 0;}return snd_pcm_recover(pcm,error,silent); }
static int test_pcm_drain(snd_pcm_t *pcm)
{
	g_mutex_lock(&drain_lock);
	if(block_drain){drain_entered=TRUE;g_cond_broadcast(&drain_cond);while(!release_drain)g_cond_wait(&drain_cond,&drain_lock);}
	g_mutex_unlock(&drain_lock);
	return snd_pcm_drain(pcm);
}
static int test_pcm_drop(snd_pcm_t *pcm)
{
	g_mutex_lock(&drain_lock);release_drain=TRUE;g_cond_broadcast(&drain_cond);g_mutex_unlock(&drain_lock);
	return snd_pcm_drop(pcm);
}
static int test_pcm_rate_near(snd_pcm_t *pcm,snd_pcm_hw_params_t *params,unsigned int *rate,int *dir)
{
	if(forced_output_rate)*rate=forced_output_rate;
	return snd_pcm_hw_params_set_rate_near(pcm,params,rate,dir);
}
static gboolean fail_seek;
static gint fail_demux_read;
static gint fail_audio_packet;
static int test_avcodec_send_packet(AVCodecContext *context, const AVPacket *packet)
{
    if (packet && g_atomic_int_get(&fail_audio_packet)) return AVERROR_INVALIDDATA;
    return avcodec_send_packet(context, packet);
}
static gint demux_read_count;
static int test_av_read_frame(AVFormatContext *format, AVPacket *packet)
{
    if (g_atomic_int_get(&fail_demux_read)) {
        g_atomic_int_inc(&demux_read_count);
        return AVERROR(EIO);
    }
    return av_read_frame(format, packet);
}
static int test_av_seek_frame(AVFormatContext *format, int stream, int64_t timestamp, int flags)
{
    return fail_seek ? AVERROR(ESPIPE) : av_seek_frame(format, stream, timestamp, flags);
}

#define av_seek_frame test_av_seek_frame
#define av_read_frame test_av_read_frame
#define avcodec_send_packet test_avcodec_send_packet
#define snd_pcm_open test_pcm_open
#define snd_pcm_writei test_pcm_write
#define snd_pcm_wait test_pcm_wait
#define snd_pcm_recover test_pcm_recover
#define snd_pcm_drain test_pcm_drain
#define snd_pcm_drop test_pcm_drop
#define snd_pcm_hw_params_set_rate_near test_pcm_rate_near
#include "../matinee-engine.c"
#undef av_seek_frame
#undef av_read_frame
#undef avcodec_send_packet
#undef snd_pcm_open
#undef snd_pcm_writei
#undef snd_pcm_wait
#undef snd_pcm_recover
#undef snd_pcm_drain
#undef snd_pcm_drop
#undef snd_pcm_hw_params_set_rate_near
#include "../matinee-model.c"
#include "../matinee-visual.c"
static const char *watched_library_path;
static guint finalized_library_roots;
static void library_root_finalized(gpointer data, GObject *object)
{ (void)data; (void)object; finalized_library_roots++; }
static GFile *tracked_library_file(const char *path)
{
    GFile *file = g_file_new_for_path(path);
    if (watched_library_path && !strcmp(path, watched_library_path))
        g_object_weak_ref(G_OBJECT(file), library_root_finalized, NULL);
    return file;
}
#define g_file_new_for_path tracked_library_file
static GTask *pending_chooser;
static guint finalized_chooser_filters;
static void chooser_filter_finalized(gpointer data, GObject *object)
{
    (void)data; (void)object;
    finalized_chooser_filters++;
}
static void fake_open_multiple(GtkFileDialog *dialog, GtkWindow *parent, GCancellable *cancel,
                               GAsyncReadyCallback callback, gpointer data)
{
    (void)parent;
    g_assert_null(pending_chooser);
    GListModel *filters = gtk_file_dialog_get_filters(dialog);
    if (filters) for (guint i = 0; i < g_list_model_get_n_items(filters); i++) {
        g_autoptr(GObject) filter = g_list_model_get_item(filters, i);
        g_object_weak_ref(filter, chooser_filter_finalized, NULL);
    }
    pending_chooser = g_task_new(dialog, cancel, callback, data);
    g_task_set_check_cancellable(pending_chooser, FALSE);
}
static GListModel *fake_open_finish(GtkFileDialog *dialog, GAsyncResult *result, GError **error)
{
    (void)dialog;
    return g_task_propagate_pointer(G_TASK(result), error);
}
static void fake_select_folder(GtkFileDialog *dialog, GtkWindow *parent, GCancellable *cancel,
                               GAsyncReadyCallback callback, gpointer data)
{
    fake_open_multiple(dialog, parent, cancel, callback, data);
}
static GFile *fake_folder_finish(GtkFileDialog *dialog, GAsyncResult *result, GError **error)
{
    (void)dialog;
    return g_task_propagate_pointer(G_TASK(result), error);
}
#define gtk_file_dialog_open_multiple fake_open_multiple
#define gtk_file_dialog_open_multiple_finish fake_open_finish
#define gtk_file_dialog_select_folder fake_select_folder
#define gtk_file_dialog_select_folder_finish fake_folder_finish
#include "../matinee-window.c"
#undef gtk_file_dialog_open_multiple
#undef gtk_file_dialog_open_multiple_finish
#undef gtk_file_dialog_select_folder
#undef gtk_file_dialog_select_folder_finish
#undef g_file_new_for_path
#include "../matinee-app.c"

/* Write a real, playable 16-bit PCM WAV of a 440 Hz tone. */
static char *
write_tone_wav (const char *directory, const char *name, double seconds, int rate)
{
	char *path = g_build_filename (directory, name, NULL);
	gsize data_bytes = (gsize) (seconds * rate) * 2;
	gsize total = 44 + data_bytes;
	guint8 *buffer = g_malloc0 (total);
	gint16 *samples = (gint16 *) (buffer + 44);
	guint32 pcm = (guint32) data_bytes;

	memcpy (buffer, "RIFF", 4);
	guint32 riff_size = (guint32) (total - 8);
	memcpy (buffer + 4, &riff_size, 4);
	memcpy (buffer + 8, "WAVEfmt ", 8);
	guint32 fmt_size = 16;
	memcpy (buffer + 16, &fmt_size, 4);
	guint16 one = 1, channels = 1, bits = 16, block = 2;
	memcpy (buffer + 20, &one, 2);
	memcpy (buffer + 22, &channels, 2);
	memcpy (buffer + 24, &rate, 4);
	guint32 byte_rate = (guint32) rate * 2;
	memcpy (buffer + 28, &byte_rate, 4);
	memcpy (buffer + 32, &block, 2);
	memcpy (buffer + 34, &bits, 2);
	memcpy (buffer + 36, "data", 4);
	memcpy (buffer + 40, &pcm, 4);
	for (gsize i = 0; i < data_bytes / 2; i++)
		samples[i] = (gint16) (sinf (2.0f * (float) G_PI * 440.0f * (float) i / (float) rate) * 20000.0f);
	g_file_set_contents (path, (const char *) buffer, total, NULL);
	g_free (buffer);
	return path;
}

static void
test_playlist (void)
{
	gchar *directory = g_dir_make_tmp ("matinee-model-XXXXXX", NULL);
	char *first = write_tone_wav (directory, "one.wav", 0.5, 8000);
	char *second = write_tone_wav (directory, "two.wav", 0.5, 8000);
	char *third = write_tone_wav (directory, "three.wav", 0.5, 8000);
	g_autoptr (MatineePlaylist) playlist = matinee_playlist_new ();
	g_autoptr (MatineeEntry) a = matinee_entry_new (first, 0.5, MATINEE_ENTRY_AUDIO);
	g_autoptr (MatineeEntry) b = matinee_entry_new (second, 0.5, MATINEE_ENTRY_AUDIO);
	g_autoptr (MatineeEntry) c = matinee_entry_new (third, 0.5, MATINEE_ENTRY_AUDIO);

	g_assert_nonnull (directory);
	g_assert_cmpuint (matinee_playlist_length (playlist), ==, 0);
	g_assert_cmpint (matinee_playlist_current (playlist), ==, -1);

	matinee_playlist_append (playlist, a);
	matinee_playlist_append (playlist, b);
	matinee_playlist_append (playlist, c);
	g_assert_cmpuint (matinee_playlist_length (playlist), ==, 3);
	/* The list model must agree with the helper accessors. */
	g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (playlist)), ==, 3);
	{
		g_autoptr (MatineeEntry) fetched = g_list_model_get_item (G_LIST_MODEL (playlist), 1);
		g_assert_true (fetched == b);
	}
	g_assert_null (g_list_model_get_item (G_LIST_MODEL (playlist), 99));

	/* display name drops the extension but path keeps it */
	g_assert_cmpstr (matinee_entry_display (a), ==, "one");
	g_assert_cmpstr (matinee_entry_path (a), ==, first);
	g_assert_cmpfloat (matinee_entry_length (a), ==, 0.5);
	g_assert_cmpint (matinee_entry_kind (a), ==, MATINEE_ENTRY_AUDIO);

	matinee_playlist_set_current (playlist, 1);
	g_assert_cmpint (matinee_playlist_current (playlist), ==, 1);
	g_assert_true (matinee_playlist_move_next (playlist));
	g_assert_cmpint (matinee_playlist_current (playlist), ==, 2);
	/* No wrap-around at the end. */
	g_assert_false (matinee_playlist_move_next (playlist));
	g_assert_true (matinee_playlist_move_prev (playlist));
	g_assert_cmpint (matinee_playlist_current (playlist), ==, 1);

	/* Reordering keeps the selection on the same entry. */
	matinee_playlist_move (playlist, 0, 2);
	g_assert_true (matinee_playlist_at (playlist, 2) == a);
	g_assert_cmpint (matinee_playlist_current (playlist), ==, 0);
	g_assert_true(matinee_playlist_at(playlist, matinee_playlist_current(playlist)) == b);

	matinee_playlist_remove_at (playlist, 1);
	g_assert_cmpuint (matinee_playlist_length (playlist), ==, 2);
	matinee_playlist_clear (playlist);
	g_assert_cmpuint (matinee_playlist_length (playlist), ==, 0);
	g_assert_cmpint (matinee_playlist_current (playlist), ==, -1);

	g_assert_cmpint (g_unlink (first), ==, 0);
	g_assert_cmpint (g_unlink (second), ==, 0);
	g_assert_cmpint (g_unlink (third), ==, 0);
	g_assert_cmpint (g_rmdir (directory), ==, 0);
	g_free (directory);
	g_free (first);
	g_free (second);
	g_free (third);
}

static void
test_engine_open (void)
{
	gchar *directory = g_dir_make_tmp ("matinee-engine-XXXXXX", NULL);
	char *path = write_tone_wav (directory, "tone.wav", 0.5, 8000);
	g_autofree char *uri = g_filename_to_uri (path, NULL, NULL);
	g_autoptr (MatineeEngine) engine = matinee_engine_new ();
	g_autoptr (GError) error = NULL;

	g_assert_nonnull (directory);
	g_assert_true (matinee_engine_open (engine, uri, &error));
	g_assert_no_error (error);
	g_assert_false (matinee_engine_has_video (engine));
	g_assert_cmpfloat (matinee_engine_duration (engine), >, 0.4);
	g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_IDLE);
	g_assert_cmpfloat (matinee_engine_position (engine), ==, 0.0);

	/* A missing file must fail cleanly and leave the engine idle. */
	g_assert_false (matinee_engine_open (engine, "/nonexistent/nope.wav", &error));
	g_assert_nonnull (error);
	g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_IDLE);
	g_clear_error (&error);
	{
		g_autoptr (MatineeEngine) cancelled = matinee_engine_new ();
		matinee_engine_cancel_io (cancelled);
		gint64 started = g_get_monotonic_time ();
		g_assert_false (matinee_engine_open (cancelled, path, &error));
		g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
		g_assert_cmpint (g_get_monotonic_time () - started, <, 100000);
		g_clear_error (&error);
	}

	g_assert_cmpint (g_unlink (path), ==, 0);
	g_assert_cmpint (g_rmdir (directory), ==, 0);
	g_free (directory);
	g_free (path);
}

static void
test_visual (void)
{
	enum { N = 1024 };
	float samples[N];
	float mags[N / 2];
	MatineeVisual *visual = matinee_visual_new ();
	int peak = 0;
	MatineeBands bands = {0};

	/* A 440 Hz tone must land in the expected bin at 48 kHz. */
	for (int i = 0; i < N; i++)
		samples[i] = sinf (2.0f * (float) G_PI * 440.0f * (float) i / 48000.0f);
	matinee_fft_magnitude (samples, N, mags);
	for (int i = 1; i < N / 2; i++)
		if (mags[i] > mags[peak])
			peak = i;
	g_assert_cmpint (peak, >=, 7);
	g_assert_cmpint (peak, <=, 11);
	g_assert_cmpfloat (mags[peak], >, 1.0f);

	g_assert_cmpint (matinee_visual_width (visual), ==, 320);
	g_assert_cmpint (matinee_visual_height (visual), ==, 240);
	/* Before the first render there is no texture. */
	g_assert_null (matinee_visual_texture (visual));

	/* Silence must still animate rather than freeze: the floors keep the
	 * field alive, and a texture is produced. */
	for (int i = 0; i < N; i++)
		samples[i] = 0.0f;
	matinee_fft_magnitude (samples, N, mags);
	for (int b = 0; b < MATINEE_BANDS; b++)
		bands.level[b] = mags[b];
	bands.time_us = 1000000;
	bands.generation = 1;
	matinee_visual_render (visual, &bands, bands.time_us);
	g_assert_nonnull (matinee_visual_texture (visual));

	/* A track change resets the feedback buffer. */
	matinee_visual_reset (visual);
	matinee_visual_render (visual, &bands, 2000000);
	g_assert_nonnull (matinee_visual_texture (visual));
	matinee_visual_free (visual);
}

static GThread *ui_thread;
static guint delivered_frames;

static void test_negotiated_output_rate(void)
{
	g_autoptr(MatineeEngine) engine=matinee_engine_new();
	forced_output_rate=32000;
	g_assert_true(open_audio_output(engine));
	forced_output_rate=0;
	g_assert_cmpuint(engine->output_rate,==,32000);
	engine->audio_context=avcodec_alloc_context3(NULL);
	g_assert_nonnull(engine->audio_context);
	engine->audio_context->sample_rate=48000;
	engine->audio_context->sample_fmt=AV_SAMPLE_FMT_S16;
	av_channel_layout_default(&engine->audio_context->ch_layout,2);
	g_assert_true(prepare_resampler(engine));
	gint16 input[960]={0}, output[960]={0};
	const uint8_t *in=(const uint8_t *)input;uint8_t *out=(uint8_t *)output;
	int samples=swr_convert(engine->resampler,&out,480,&in,480);
	g_assert_cmpint(samples,>,250);g_assert_cmpint(samples,<,350);
	engine->frames_written=32000;update_clock(engine);
	g_assert_cmpfloat_with_epsilon(engine->clock,1.0,0.001);
	stop_threads(engine);g_assert_null(engine->resampler);
	forced_output_rate=48000;g_assert_true(open_audio_output(engine));forced_output_rate=0;
	g_assert_cmpuint(engine->output_rate,==,48000);g_assert_true(prepare_resampler(engine));
}

static void test_upsample_capacity(void)
{
	g_autoptr(MatineeEngine) engine=matinee_engine_new();
	g_assert_true(open_audio_output(engine));
	g_assert_cmpuint(engine->output_rate,==,48000);
	engine->audio_context=avcodec_alloc_context3(NULL);
	g_assert_nonnull(engine->audio_context);
	engine->audio_context->sample_rate=8000;
	engine->audio_context->sample_fmt=AV_SAMPLE_FMT_S16;
	av_channel_layout_default(&engine->audio_context->ch_layout,2);
	g_assert_true(prepare_resampler(engine));
	int capacity=resampler_output_capacity(engine->resampler,80);
	g_assert_cmpint(capacity,>=,480);
	gint16 input[160]={0};g_autofree gint16 *output=g_new0(gint16,capacity*2);
	const uint8_t *in=(const uint8_t *)input;uint8_t *out=(uint8_t *)output;
	int converted=swr_convert(engine->resampler,&out,capacity,&in,80);
	g_assert_cmpint(converted,>,80);g_assert_cmpint(converted,<=,capacity);
}

static void test_resampler_duration_rates(void)
{
	static const int rates[] = {8000, 22050, 44100, 48000, 96000};
	static const unsigned int output_rates[] = {32000, 44100, 48000};
	for (guint o = 0; o < G_N_ELEMENTS(output_rates); ++o) {
	for (guint i = 0; i < G_N_ELEMENTS(rates); ++i) {
		MatineeEngine *engine = matinee_engine_new();
		forced_output_rate = output_rates[o];
		g_assert_true(open_audio_output(engine));
		forced_output_rate = 0;
		g_assert_cmpuint(engine->output_rate, ==, output_rates[o]);
		engine->playing = TRUE;
		engine->audio_context = avcodec_alloc_context3(NULL);
		g_assert_nonnull(engine->audio_context);
		engine->audio_context->sample_rate = rates[i];
		engine->audio_context->sample_fmt = AV_SAMPLE_FMT_S16;
		av_channel_layout_default(&engine->audio_context->ch_layout, 2);
		g_assert_true(prepare_resampler(engine));
		int input_frames = rates[i] / 10;
		g_autofree gint16 *input = g_new0(gint16, (gsize)input_frames * 2);
		int capacity = resampler_output_capacity(engine->resampler, input_frames);
		g_assert_cmpint(capacity, >, 0);
		g_autofree gint16 *output = g_new0(gint16, (gsize)capacity * 2);
		const uint8_t *in = (const uint8_t *)input;
		uint8_t *out = (uint8_t *)output;
		int converted = swr_convert(engine->resampler, &out, capacity, &in, input_frames);
		g_assert_cmpint(converted, >=, 0);
		g_assert_true(output_converted_audio(engine, output, converted));
		gsize output_bytes = (gsize)capacity * 2 * sizeof(gint16);
		g_assert_true(drain_resampler(engine, &output, &output_bytes));
		g_assert_cmpint(llabs((gint64)engine->frames_written - output_rates[o] / 10), <=, 1);
		update_clock(engine);
		g_assert_cmpfloat_with_epsilon(engine->clock, 0.1, 0.001);
		stop_threads(engine);
		g_object_unref(engine);
	}
	}
}

static void test_short_audio_writes(void)
{
	g_autoptr(MatineeEngine) engine=matinee_engine_new();
	g_assert_true(open_audio_output(engine));
	engine->playing=TRUE;
	gint16 samples[20];for(guint i=0;i<G_N_ELEMENTS(samples);i++)samples[i]=(gint16)(i+1);
	scripted_step=accepted_frames=recover_calls=wait_calls=0;
	simulate_short_writes=TRUE;
	g_assert_true(write_audio_frames(engine,samples,10));
	simulate_short_writes=FALSE;
	g_assert_cmpuint(accepted_frames,==,10);
	g_assert_cmpmem(samples,sizeof(samples),accepted_samples,sizeof(samples));
	g_assert_cmpuint(engine->frames_written,==,10);
	g_assert_cmpuint(wait_calls,==,1);g_assert_cmpuint(recover_calls,==,1);
	scripted_step=3;accepted_frames=0;cancel_after_write=engine->cancellable;
	simulate_short_writes=TRUE;
	g_assert_false(write_audio_frames(engine,samples,10));
	simulate_short_writes=FALSE;cancel_after_write=NULL;
	g_assert_cmpuint(accepted_frames,==,3);
	g_assert_cmpuint(engine->frames_written,==,13);
}

static void test_audio_worker_failure(void)
{
	g_autofree char *dir=g_dir_make_tmp("matinee-audio-error-XXXXXX",NULL);
	g_autofree char *path=write_tone_wav(dir,"error.wav",0.2,48000);
	g_autoptr(MatineeEngine) engine=matinee_engine_new();
	g_autoptr(GError) error=NULL;
	g_assert_true(matinee_engine_open(engine,path,&error));g_assert_no_error(error);
	g_atomic_int_set(&fail_audio_write,1);matinee_engine_play(engine);
	gint64 deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
	while(matinee_engine_state(engine)!=MATINEE_STATE_ERROR && g_get_monotonic_time()<deadline){
		while(g_main_context_iteration(NULL,FALSE)) { }
		g_usleep(1000);
	}
	g_assert_cmpint(matinee_engine_state(engine),==,MATINEE_STATE_ERROR);
	g_autofree char *message=matinee_engine_dup_error_message(engine);
	g_assert_nonnull(message);g_assert_nonnull(strstr(message,"Input/output error"));
	g_atomic_int_set(&fail_audio_write,0);
	gint writes_before=g_atomic_int_get(&audio_writes);matinee_engine_play(engine);
	deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
	while(g_atomic_int_get(&audio_writes)==writes_before && g_get_monotonic_time()<deadline){
		while(g_main_context_iteration(NULL,FALSE)) { }
		g_usleep(1000);
	}
	g_assert_cmpint(g_atomic_int_get(&audio_writes),>,writes_before);
	matinee_engine_stop(engine);
	g_assert_cmpint(g_unlink(path),==,0);g_assert_cmpint(g_rmdir(dir),==,0);
}

static guint finished_count;
static void finished_on_ui(MatineeEngine *engine, gpointer data);
static void test_audio_packet_failure(void)
{
    g_autofree char *dir=g_dir_make_tmp("matinee-packet-error-XXXXXX",NULL);
    g_autofree char *path=write_tone_wav(dir,"error.wav",0.2,48000);
    g_autoptr(MatineeEngine) engine=matinee_engine_new();
    g_autoptr(GError) error=NULL;
    g_assert_true(matinee_engine_open(engine,path,&error)); g_assert_no_error(error);
    finished_count=0;
    g_signal_connect(engine,"finished",G_CALLBACK(finished_on_ui),NULL);
    g_atomic_int_set(&fail_audio_packet,1); matinee_engine_play(engine);
    gint64 deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
    while(matinee_engine_state(engine)!=MATINEE_STATE_ERROR && g_get_monotonic_time()<deadline) {
        while(g_main_context_iteration(NULL,FALSE)) { }
        g_usleep(1000);
    }
    g_assert_cmpint(matinee_engine_state(engine),==,MATINEE_STATE_ERROR);
    g_assert_cmpuint(finished_count,==,0);
    g_atomic_int_set(&fail_audio_packet,0); matinee_engine_stop(engine);
    g_assert_cmpint(g_unlink(path),==,0); g_assert_cmpint(g_rmdir(dir),==,0);
}

static void test_visual_variety (void)
{
	MatineeVisual *visual = matinee_visual_new ();
	MatineeBands quiet = { 0 }, loud = { 0 };
	const gsize size = VISUAL_WIDTH * VISUAL_HEIGHT * 4;
	const gint64 uptime = 40 * G_TIME_SPAN_DAY;
	for (guint i = 0; i < MATINEE_BANDS; i++) loud.level[i] = 1.0f;
	matinee_visual_render (visual, &quiet, uptime);
	g_autofree guint8 *first = g_memdup2 (visual->previous, size);
	/* Silence still animates at 30 Hz after long system uptime. */
	matinee_visual_render (visual, &quiet, uptime + 33333);
	g_assert_cmpint (memcmp (first, visual->previous, size), !=, 0);
	for (guint scene = 1; scene < 3; scene++)
	{
		matinee_visual_render (visual, &quiet, uptime + scene * 18 * G_TIME_SPAN_SECOND);
		g_assert_cmpint (memcmp (first, visual->previous, size), !=, 0);
		guint8 darkest = 255, brightest = 0;
		for (gsize i = 0; i < size; i += 4)
		{
			darkest = MIN (darkest, visual->previous[i]);
			brightest = MAX (brightest, visual->previous[i]);
			g_assert_cmpuint (visual->previous[i + 3], ==, 255);
		}
		g_assert_cmpuint (brightest - darkest, >, 50);
	}
	matinee_visual_reset (visual);
	g_assert_null (matinee_visual_texture (visual));
	matinee_visual_render (visual, &quiet, 1000000);
	g_assert_cmpmem (first, size, visual->previous, size);
	matinee_visual_reset (visual);
	matinee_visual_render (visual, &loud, 1000000);
	g_assert_cmpint (memcmp (first, visual->previous, size), !=, 0);
	matinee_visual_free (visual);
}

static void frame_on_ui (MatineeEngine *engine, GdkTexture *texture, gpointer data)
{
	(void) engine; (void) texture; (void) data;
	g_assert_true (g_thread_self () == ui_thread);
	delivered_frames++;
}

static gpointer post_from_worker (gpointer data)
{
	const guint8 pixel[] = { 0x20, 0x80, 0xc0, 0xff };
	g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof pixel);
	g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1,
		GDK_MEMORY_R8G8B8A8, bytes, 4);
	post_video_frame (data, texture, 0.0);
	return NULL;
}

static void test_frame_dispatch (void)
{
	g_autoptr (MatineeEngine) engine = matinee_engine_new ();
	ui_thread = g_thread_self ();
	delivered_frames = 0;
	g_signal_connect (engine, "video-frame", G_CALLBACK (frame_on_ui), NULL);
	/* Leave the default context unowned: invoke_full may run inline here. */
	g_thread_join (g_thread_new ("frame-fixture", post_from_worker, engine));
	g_assert_cmpuint (delivered_frames, ==, 0);
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_cmpuint (delivered_frames, ==, 1);
	/* Stop and destruction must cancel already-queued UI delivery. */
	g_thread_join (g_thread_new ("frame-fixture", post_from_worker, engine));
	matinee_engine_stop (engine);
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_cmpuint (delivered_frames, ==, 1);
	g_thread_join (g_thread_new ("frame-fixture", post_from_worker, engine));
	g_clear_object (&engine);
	while (g_main_context_iteration (NULL, FALSE));
	g_assert_cmpuint (delivered_frames, ==, 1);
}

static guint finished_count;
static void finished_on_ui (MatineeEngine *engine, gpointer data)
{
	(void) engine; (void) data;
	g_assert_true (g_thread_self () == ui_thread);
	finished_count++;
}

static gpointer finish_from_worker (gpointer data)
{
	signal_end (data);
	return NULL;
}

static guint audio_error_signals;
static void audio_error_on_ui(MatineeEngine *engine,gint state,gpointer data)
{
	(void)engine;(void)data;
	if(state==MATINEE_STATE_ERROR){g_assert_true(g_thread_self()==ui_thread);audio_error_signals++;}
}
static gpointer audio_error_from_worker(gpointer data)
{ signal_playback_error(data,NULL);return NULL; }
static void test_audio_error_dispatch(void)
{
	g_autoptr(MatineeEngine) engine=matinee_engine_new();ui_thread=g_thread_self();audio_error_signals=0;
	g_signal_connect(engine,"state-changed",G_CALLBACK(audio_error_on_ui),NULL);
	engine->clock=2.0;engine->clock_anchor=g_get_monotonic_time();engine->clock_running=TRUE;
	g_thread_join(g_thread_new("audio-error",audio_error_from_worker,engine));
	g_assert_false(engine->clock_running);
	g_assert_cmpuint(audio_error_signals,==,0);
	while(g_main_context_iteration(NULL,FALSE));
	g_assert_cmpuint(audio_error_signals,==,1);
	matinee_engine_stop(engine);
	g_thread_join(g_thread_new("audio-error",audio_error_from_worker,engine));
	matinee_engine_stop(engine);
	while(g_main_context_iteration(NULL,FALSE));
	g_assert_cmpuint(audio_error_signals,==,1);
}

static void test_finished_dispatch (void)
{
	g_autoptr (MatineeEngine) engine = matinee_engine_new ();
	ui_thread = g_thread_self ();
	finished_count = 0;
	g_signal_connect (engine, "finished", G_CALLBACK (finished_on_ui), NULL);
	for (guint pass = 0; pass < 3; pass++)
	{
		engine->playing = TRUE;
		engine->reached_end = FALSE;
		g_thread_join (g_thread_new ("end-fixture", finish_from_worker, engine));
		g_assert_cmpuint (finished_count, ==, pass == 0 ? 0 : 1);
		if (pass == 1) matinee_engine_stop (engine);
		if (pass == 2) g_clear_object (&engine);
		while (g_main_context_iteration (NULL, FALSE));
		g_assert_cmpuint (finished_count, ==, 1);
	}
}

/* A real YUV4MPEG stream avoids depending on an encoder or binary fixture. */
static char *write_video (const char *directory)
{
	char *path = g_build_filename (directory, "Video #1 – 100%25.y4m", NULL);
	g_autoptr (GString) stream = g_string_new ("YUV4MPEG2 W96 H64 F24:1 Ip A1:1 C420jpeg\n");
	guint8 frame[96 * 64 * 3 / 2];
	memset (frame, 128, sizeof frame);
	for (guint i = 0; i < 24; i++)
	{
		memset (frame, 32 + i * 6, 96 * 64);
		g_string_append (stream, "FRAME\n");
		g_string_append_len (stream, (const char *) frame, sizeof frame);
	}
	g_assert_true (g_file_set_contents (path, stream->str, stream->len, NULL));
	return path;
}

static void test_audio_short_video_tail(void)
{
	g_autofree char *ffmpeg=g_find_program_in_path("ffmpeg");
	if(!ffmpeg){g_test_skip("ffmpeg is required for the A/V fixture");return;}
	g_autofree char *dir=g_dir_make_tmp("matinee-av-tail-XXXXXX",NULL);
	g_autofree char *video=write_video(dir);
	g_autofree char *audio=write_tone_wav(dir,"short.wav",0.2,48000);
	g_autofree char *mux=g_build_filename(dir,"combined.mkv",NULL);
	char *argv[]={ffmpeg,"-nostdin","-v","error","-y","-i",video,"-i",audio,
	              "-c:v","ffv1","-c:a","pcm_s16le",mux,NULL};
	gint status=0;g_autoptr(GError) error=NULL;
	g_assert_true(g_spawn_sync(NULL,argv,NULL,0,NULL,NULL,NULL,NULL,&status,&error));
	g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);
	g_autoptr(MatineeEngine) engine=matinee_engine_new();
	g_assert_true(matinee_engine_open(engine,mux,&error));g_assert_no_error(error);
	g_assert_cmpint(engine->audio_index,>=,0);g_assert_cmpint(engine->video_index,>=,0);
	ui_thread=g_thread_self();finished_count=delivered_frames=0;
	g_signal_connect(engine,"finished",G_CALLBACK(finished_on_ui),NULL);
	g_signal_connect(engine,"video-frame",G_CALLBACK(frame_on_ui),NULL);
	gint64 start=g_get_monotonic_time();matinee_engine_play(engine);
	gint64 deadline=start+3*G_TIME_SPAN_SECOND;
	while(!g_atomic_int_get(&engine->audio_done) && g_get_monotonic_time()<deadline){
		while(g_main_context_iteration(NULL,FALSE)) { }
		g_usleep(1000);
	}
	g_assert_true(g_atomic_int_get(&engine->audio_done));g_assert_cmpuint(finished_count,==,0);
	matinee_engine_pause(engine);double paused=matinee_engine_position(engine);
	gint64 pause_end=g_get_monotonic_time()+120000;
	while(g_get_monotonic_time()<pause_end){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
	g_assert_cmpfloat_with_epsilon(matinee_engine_position(engine),paused,0.01);
	g_assert_cmpuint(finished_count,==,0);matinee_engine_play(engine);
	while(!finished_count && g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
	g_assert_cmpuint(finished_count,==,1);
	g_assert_cmpint(g_get_monotonic_time()-start,>=,850000);
	/* UI delivery coalesces stale textures; the wall-clock assertion proves
	 * the tail duration while this lower bound catches an early decode stop. */
	g_assert_cmpuint(delivered_frames,>=,18);matinee_engine_stop(engine);
	g_assert_cmpint(g_unlink(mux),==,0);g_assert_cmpint(g_unlink(audio),==,0);g_assert_cmpint(g_unlink(video),==,0);g_assert_cmpint(g_rmdir(dir),==,0);
}

static void test_video_finishes_before_audio(void)
{
	g_autofree char *ffmpeg=g_find_program_in_path("ffmpeg");
	if(!ffmpeg){g_test_skip("ffmpeg is required for the A/V fixture");return;}
	g_autofree char *dir=g_dir_make_tmp("matinee-av-audio-XXXXXX",NULL);
	g_autofree char *video=write_video(dir);
	g_autofree char *audio=write_tone_wav(dir,"long.wav",1.5,48000);
	g_autofree char *mux=g_build_filename(dir,"combined.mkv",NULL);
	char *argv[]={ffmpeg,"-nostdin","-v","error","-y","-i",video,"-i",audio,
	              "-c:v","ffv1","-c:a","pcm_s16le",mux,NULL};
	gint status=0;g_autoptr(GError) error=NULL;
	g_assert_true(g_spawn_sync(NULL,argv,NULL,0,NULL,NULL,NULL,NULL,&status,&error));
	g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);
	g_autoptr(MatineeEngine) engine=matinee_engine_new();g_assert_true(matinee_engine_open(engine,mux,&error));g_assert_no_error(error);
	ui_thread=g_thread_self();finished_count=0;g_signal_connect(engine,"finished",G_CALLBACK(finished_on_ui),NULL);
	g_mutex_lock(&drain_lock);block_drain=TRUE;drain_entered=FALSE;release_drain=FALSE;g_mutex_unlock(&drain_lock);
	matinee_engine_play(engine);
	gboolean video_done=FALSE,entered=FALSE;gint64 deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
	while((!video_done||!entered)&&g_get_monotonic_time()<deadline){
		while(g_main_context_iteration(NULL,FALSE)) { }
		g_usleep(1000);
		g_mutex_lock(&engine->state_lock);video_done=engine->video_done;g_mutex_unlock(&engine->state_lock);
		g_mutex_lock(&drain_lock);entered=drain_entered;g_mutex_unlock(&drain_lock);
	}
	g_assert_true(video_done);g_assert_true(entered);g_assert_cmpuint(finished_count,==,0);
	g_mutex_lock(&drain_lock);release_drain=TRUE;g_cond_broadcast(&drain_cond);g_mutex_unlock(&drain_lock);
	while(!finished_count&&g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
	g_assert_cmpuint(finished_count,==,1);matinee_engine_stop(engine);
	g_mutex_lock(&drain_lock);block_drain=FALSE;g_mutex_unlock(&drain_lock);
	g_assert_cmpint(g_unlink(mux),==,0);g_assert_cmpint(g_unlink(audio),==,0);g_assert_cmpint(g_unlink(video),==,0);g_assert_cmpint(g_rmdir(dir),==,0);
}

static void test_audio_end_drain(void)
{
	g_autofree char *dir=g_dir_make_tmp("matinee-drain-XXXXXX",NULL);
	g_autofree char *path=write_tone_wav(dir,"tone.wav",0.2,8000);
	ui_thread=g_thread_self();
	for(int pass=0;pass<2;pass++){
		g_autoptr(MatineeEngine) engine=matinee_engine_new();
		g_assert_true(matinee_engine_open(engine,path,NULL));finished_count=0;
		g_signal_connect(engine,"finished",G_CALLBACK(finished_on_ui),NULL);
		g_mutex_lock(&drain_lock);block_drain=TRUE;drain_entered=FALSE;release_drain=FALSE;g_mutex_unlock(&drain_lock);
		matinee_engine_play(engine);
		gboolean entered=FALSE;gint64 deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
		while(!entered && g_get_monotonic_time()<deadline){
			while(g_main_context_iteration(NULL,FALSE)) { }
			g_usleep(1000);
			g_mutex_lock(&drain_lock);entered=drain_entered;g_mutex_unlock(&drain_lock);
		}
		g_assert_true(entered);g_assert_cmpuint(finished_count,==,0);
		g_assert_cmpuint(engine->frames_written,==,9600);
		if(pass==0){
			g_mutex_lock(&drain_lock);release_drain=TRUE;g_cond_broadcast(&drain_cond);g_mutex_unlock(&drain_lock);
			deadline=g_get_monotonic_time()+3*G_TIME_SPAN_SECOND;
			while(!finished_count && g_get_monotonic_time()<deadline){while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}
			g_assert_cmpuint(finished_count,==,1);matinee_engine_stop(engine);
		}else{
			gint64 started=g_get_monotonic_time();matinee_engine_stop(engine);
			g_assert_cmpint(g_get_monotonic_time()-started,<,G_TIME_SPAN_SECOND);
			while(g_main_context_iteration(NULL,FALSE)) { }
			g_assert_cmpuint(finished_count,==,0);
		}
		g_mutex_lock(&drain_lock);block_drain=FALSE;g_mutex_unlock(&drain_lock);
	}
	g_assert_cmpint(g_unlink(path),==,0);g_assert_cmpint(g_rmdir(dir),==,0);
}

static void test_video_playback (void)
{
	g_autofree char *directory = g_dir_make_tmp ("matinee-video-XXXXXX", NULL);
	g_autofree char *path = write_video (directory);
	g_autofree char *compressed = g_build_filename (directory, "delayed.mp4", NULL);
	g_autofree char *ffmpeg = g_find_program_in_path ("ffmpeg");
	if (ffmpeg) {
		g_autoptr (GSubprocess) encoder = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, NULL,
			ffmpeg, "-nostdin", "-v", "error", "-i", path, "-an", "-c:v", "libx264",
			"-bf", "2", compressed, NULL);
		g_assert_nonnull (encoder);
		g_assert_true (g_subprocess_wait_check (encoder, NULL, NULL));
	} else {
		g_test_message ("ffmpeg absent: compressed/delayed-frame coverage unavailable");
	}
	ui_thread = g_thread_self ();
	for (guint pass = 0; pass < (ffmpeg ? 4 : 3); pass++)
	{
		g_autoptr (MatineeEngine) engine = matinee_engine_new ();
		g_autoptr (GError) error = NULL;
		g_assert_true (matinee_engine_open (engine, pass == 3 ? compressed : path, &error));
		g_assert_no_error (error);
		g_assert_cmpint (engine->audio_index, <, 0);
		g_assert_cmpint (engine->video_index, >=, 0);
		if (pass == 3)
			g_assert_cmpint (engine->video_context->has_b_frames, >, 0);
		delivered_frames = finished_count = 0;
		g_signal_connect (engine, "video-frame", G_CALLBACK (frame_on_ui), NULL);
		g_signal_connect (engine, "finished", G_CALLBACK (finished_on_ui), NULL);
		matinee_engine_play (engine);
		gint64 started = g_get_monotonic_time ();
		gint64 deadline = started + 5 * G_TIME_SPAN_SECOND;
		while (!finished_count && g_get_monotonic_time () < deadline)
		{
			while (g_main_context_iteration (NULL, FALSE));
			g_usleep (1000);
		}
		g_assert_cmpuint (finished_count, ==, 1);
		g_assert_cmpint (g_get_monotonic_time () - started, >=, 850000);
		g_assert_cmpuint (delivered_frames, >=, pass == 3 ? 24 : 20);
		/* Restart and release during decode, without giving pending callbacks
		 * a chance to dispatch until after the engine has been destroyed. */
		matinee_engine_play (engine);
		g_usleep (20000);
		g_clear_object (&engine);
		while (g_main_context_iteration (NULL, FALSE));
	}
	g_unlink (path);
	g_unlink (compressed);
	g_rmdir (directory);
}

static void pump_for (gint64 duration)
{
	gint64 end = g_get_monotonic_time () + duration;
	while (g_get_monotonic_time () < end) {
		while (g_main_context_iteration (NULL, FALSE));
		g_usleep (1000);
	}
}

/* Count only the decoder, not this test's main-loop pumping. On Linux this
 * catches returning to an 8-ms pause poll even when CPU ticks round to zero. */
static guint64 video_worker_switches(void)
{
    guint64 total=0;
#ifdef __linux__
    g_autoptr(GDir) tasks=g_dir_open("/proc/self/task",0,NULL);
    g_assert_nonnull(tasks);
    const char *id;
    while((id=g_dir_read_name(tasks))) {
        g_autofree char *name_path=g_strdup_printf("/proc/self/task/%s/comm",id);
        g_autofree char *name=NULL;
        if(!g_file_get_contents(name_path,&name,NULL,NULL) || !g_str_has_prefix(name,"matinee-video")) continue;
        g_autofree char *status_path=g_strdup_printf("/proc/self/task/%s/status",id);
        g_autofree char *status=NULL;
        g_assert_true(g_file_get_contents(status_path,&status,NULL,NULL));
        const char *line=strstr(status,"\nvoluntary_ctxt_switches:");
        g_assert_nonnull(line);
        total+=g_ascii_strtoull(strchr(line,':')+1,NULL,10);
    }
#endif
    return total;
}

static void test_video_clock_controls (void)
{
	g_autofree char *directory = g_dir_make_tmp ("matinee-clock-XXXXXX", NULL);
	g_autofree char *path = write_video (directory);
	g_autoptr (MatineeEngine) engine = matinee_engine_new ();
	g_assert_true (matinee_engine_open (engine, path, NULL));
	ui_thread = g_thread_self ();
	finished_count = delivered_frames = 0;
	g_signal_connect (engine, "video-frame", G_CALLBACK (frame_on_ui), NULL);
	g_signal_connect (engine, "finished", G_CALLBACK (finished_on_ui), NULL);
	gint64 started = g_get_monotonic_time ();
	matinee_engine_play (engine);
	pump_for (220000);
	matinee_engine_pause (engine);
	pump_for (20000); /* Drain a frame already posted before Pause. */
	double paused_at = matinee_engine_position (engine);
	guint paused_frames = delivered_frames;
	guint64 switches=video_worker_switches();
	pump_for (200000);
	guint64 paused_switches=video_worker_switches()-switches;
	g_test_message("Paused video worker: %"G_GUINT64_FORMAT" voluntary switches in 200 ms",paused_switches);
	g_assert_cmpuint(paused_switches,<=,1);
	g_assert_cmpfloat (matinee_engine_position (engine), ==, paused_at);
	g_assert_cmpuint (delivered_frames, ==, paused_frames);
	g_assert_cmpuint (finished_count, ==, 0);
	g_assert_true (matinee_engine_seek_fraction (engine, 0.2));
	g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_PAUSED);
	g_assert_cmpfloat (matinee_engine_position (engine), >=, 0.19);
	g_assert_null (engine->audio_thread);
	g_assert_null (engine->video_thread);
	g_assert_false (matinee_engine_seek_fraction (engine, NAN));
	g_assert_cmpfloat (matinee_engine_position (engine), >=, 0.19);
	matinee_engine_play (engine);
	while (!finished_count && g_get_monotonic_time () - started < 3000000)
		pump_for (10000);
	g_assert_cmpuint (finished_count, ==, 1);
	g_assert_cmpint (g_get_monotonic_time () - started, >=, 1150000);
	g_assert_cmpuint (delivered_frames, >=, 20);

	/* Seeking after Ended must not trigger Play's restart-from-zero path. */
	finished_count = 0;
	started = g_get_monotonic_time ();
	matinee_engine_seek_fraction (engine, 0.5);
	g_assert_cmpfloat (matinee_engine_position (engine), >=, 0.49);
	while (!finished_count && g_get_monotonic_time () - started < 2000000)
		pump_for (10000);
	g_assert_cmpuint (finished_count, ==, 1);
	g_assert_cmpint (g_get_monotonic_time () - started, >=, 400000);
	g_assert_cmpint (g_get_monotonic_time () - started, <, 850000);

	matinee_engine_play (engine);
	pump_for (100000);
	matinee_engine_pause(engine);
	pump_for(20000);
	double before_failed_seek = matinee_engine_position (engine);
	fail_seek = TRUE;
	g_assert_false (matinee_engine_seek_fraction (engine, 0.8));
	g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_ERROR);
	g_assert_cmpfloat (matinee_engine_position (engine), ==, before_failed_seek);
	matinee_engine_stop (engine);
	g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_ERROR);
	g_assert_cmpfloat (matinee_engine_position (engine), ==, before_failed_seek);
	matinee_engine_play (engine);
	g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_ERROR);
	g_assert_null (engine->video_thread);
	fail_seek = FALSE;
	started = g_get_monotonic_time ();
	matinee_engine_stop (engine);
	g_assert_cmpint (g_get_monotonic_time () - started, <, 150000);
	g_assert_cmpfloat (matinee_engine_position (engine), ==, 0.0);
	g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_IDLE);
	g_unlink (path);
	g_rmdir (directory);
}

static void test_av_seek_discards_preroll (void)
{
	g_autofree char *ffmpeg = g_find_program_in_path ("ffmpeg");
	if (ffmpeg == NULL)
	{
		g_test_skip ("ffmpeg is required for the A/V seek fixture");
		return;
	}
	g_autofree char *directory = g_dir_make_tmp ("matinee-av-seek-XXXXXX", NULL);
	g_autofree char *video = write_video (directory);
	g_autofree char *audio = write_tone_wav (directory, "seek.wav", 1.0, 48000);
	g_autofree char *mux = g_build_filename (directory, "seek.mkv", NULL);
	gint status = 0;
	g_autoptr (GError) error = NULL;
	char *argv[] = { ffmpeg, "-nostdin", "-v", "error", "-y", "-i", video, "-i", audio,
	                 "-c:v", "ffv1", "-c:a", "pcm_s16le", mux, NULL };
	g_assert_true (g_spawn_sync (NULL, argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error));
	g_assert_no_error (error);
	g_assert_true (g_spawn_check_wait_status (status, &error));
	g_assert_no_error (error);
	g_autoptr (MatineeEngine) engine = matinee_engine_new ();
	g_assert_true (matinee_engine_open (engine, mux, &error));
	g_assert_no_error (error);
	g_assert_true (matinee_engine_seek_fraction (engine, 0.55));
	gint64 deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
	gint64 first_video_us = -1;
	guint64 discarded = 0;
	while (g_get_monotonic_time () < deadline)
	{
		while (g_main_context_iteration (NULL, FALSE));
		g_mutex_lock (&engine->frame_lock);
		first_video_us = engine->last_video_frame_us;
		g_mutex_unlock (&engine->frame_lock);
		g_mutex_lock (&engine->state_lock);
		discarded = engine->audio_preroll_samples_discarded;
		g_mutex_unlock (&engine->state_lock);
		if (first_video_us >= 0)
			break;
		g_usleep (1000);
	}
	g_assert_cmpint (first_video_us, >=, 540000);
	g_test_message ("Audio preroll discarded: %" G_GUINT64_FORMAT " samples", discarded);
	g_assert_cmpfloat (matinee_engine_position (engine), >=, 0.55);
	matinee_engine_stop (engine);
	g_unlink (mux);
	g_unlink (audio);
	g_unlink (video);
	g_rmdir (directory);
}

static void test_window_playback_close (void)
{
	/* GTK 4.18 warns during initial GtkPaned/Overlay size negotiation.
	 * Leave warnings visible; invalid objects and other criticals stay fatal. */
	GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
	g_autofree char *directory = g_dir_make_tmp ("matinee-window-XXXXXX", NULL);
	g_autofree char *path = write_video (directory);
	g_autoptr (MatineeApp) app = matinee_app_new ();
	g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
	for (guint pass = 0; pass < 3; pass++)
	{
		g_application_activate (G_APPLICATION (app));
		MatineeWindow *window = app->window;
		g_assert_nonnull (window);
		g_assert_cmpuint (window->visual_tick_id, ==, 0);
		g_assert_cmpuint (window->position_timer, ==, 0);
		g_autoptr (MatineePlaylist) playlist = g_object_ref (window->playlist);
		matinee_window_open_files (window, (const char *[]) { path, NULL });
		/* Dispatch actual video to GtkPicture before destroying the window. */
		gint64 deadline = g_get_monotonic_time () + 5000000;
		while (g_get_monotonic_time () < deadline)
		{
			while (g_main_context_iteration (NULL, FALSE));
			if (gtk_picture_get_paintable (GTK_PICTURE (window->surface)) != NULL
			    && window->position_timer != 0)
				break;
			g_usleep (1000);
		}
		g_assert_nonnull (gtk_picture_get_paintable (GTK_PICTURE (window->surface)));
		g_assert_cmpuint (window->visual_tick_id, ==, 0);
		g_assert_cmpuint (window->position_timer, !=, 0);
		g_autoptr (MatineeEngine) engine = g_object_ref (window->engine);
		matinee_engine_stop (engine);
		g_assert_cmpuint (window->visual_tick_id, ==, 0);
		g_assert_cmpuint (window->position_timer, ==, 0);
		gtk_window_destroy (GTK_WINDOW (window));
		while (g_main_context_iteration (NULL, FALSE));
		g_assert_null (app->window);
		g_assert_cmpuint (matinee_playlist_length (playlist), ==, 1);
		/* A retained engine must not signal the destroyed window. */
		set_state (engine, MATINEE_STATE_ENDED);
	}
	g_unlink (path);
	g_rmdir (directory);
	g_log_set_always_fatal (fatal);
}

static void test_demux_read_failure (void)
{
	g_autofree char *directory = g_dir_make_tmp ("matinee-demux-XXXXXX", NULL);
	g_autofree char *audio = write_tone_wav (directory, "tone.wav", 1.0, 44100);
	g_autofree char *video = write_video (directory);
	const char *paths[] = { audio, video };
	for (guint i = 0; i < G_N_ELEMENTS(paths); i++) {
		g_autoptr(MatineeEngine) engine = matinee_engine_new ();
		g_assert_true (matinee_engine_open (engine, paths[i], NULL));
		g_atomic_int_set (&demux_read_count, 0);
		g_atomic_int_set (&fail_demux_read, 1);
		matinee_engine_play (engine);
		gint64 deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
		while (matinee_engine_state (engine) != MATINEE_STATE_ERROR && g_get_monotonic_time () < deadline)
			pump_for (10000);
		g_assert_cmpint (matinee_engine_state (engine), ==, MATINEE_STATE_ERROR);
		g_assert_cmpint (g_atomic_int_get (&demux_read_count), ==, 1);
		matinee_engine_stop (engine);
		g_atomic_int_set (&fail_demux_read, 0);
	}
	g_unlink (audio); g_unlink (video); g_rmdir (directory);
}

static void test_chooser_after_close (void)
{
	GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
	g_autoptr(MatineeApp) app = matinee_app_new ();
	g_application_set_flags (G_APPLICATION (app), G_APPLICATION_NON_UNIQUE);
	g_application_set_application_id (G_APPLICATION (app), "org.luxe.Matinee.ChooserTest");
	g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
	finalized_chooser_filters = 0;
	for (int iteration = 0; iteration < 6; iteration++) {
		int kind = iteration % 3;
		g_application_activate (G_APPLICATION (app));
		MatineeWindow *window = app->window;
		g_autoptr(MatineePlaylist) playlist = g_object_ref (window->playlist);
		g_autoptr(GSettings) settings = g_object_ref (window->settings);
		g_autofree char *before = g_settings_get_string (settings, "library-path");
		if (kind == 0) action_open (NULL, NULL, window);
		else if (kind == 1) action_add_files (NULL, NULL, window);
		else action_choose_library (NULL, NULL, window);
		g_assert_nonnull (pending_chooser);
		gtk_window_destroy (GTK_WINDOW (window));
		while (g_main_context_iteration (NULL, FALSE));
		g_assert_true (g_cancellable_is_cancelled (g_task_get_cancellable (pending_chooser)));
		GTask *task = g_steal_pointer (&pending_chooser);
		if (kind == 2)
			g_task_return_pointer (task, g_file_new_for_path ("/tmp"), g_object_unref);
		else {
			GListStore *files = g_list_store_new (G_TYPE_OBJECT);
			g_autoptr(GFile) file = g_file_new_for_path ("/tmp/fake.wav");
			g_list_store_append (files, file);
			g_task_return_pointer (task, files, g_object_unref);
		}
		g_object_unref (task);
		while (g_main_context_iteration (NULL, FALSE));
		g_assert_cmpuint (matinee_playlist_length (playlist), ==, 0);
		g_autofree char *after = g_settings_get_string (settings, "library-path");
		g_assert_cmpstr (after, ==, before);
	}
	g_assert_cmpuint (finalized_chooser_filters, ==, 4);
	g_log_set_always_fatal (fatal);
}

static void test_spectrum_windows (void)
{
	g_autoptr(MatineeEngine) engine = matinee_engine_new ();
	gint16 samples[MATINEE_FFT_SIZE * 2] = {0};
	float input[MATINEE_FFT_SIZE], expected[MATINEE_FFT_SIZE / 2], actual[MATINEE_FFT_SIZE / 2];
	for (int i = 0; i < MATINEE_FFT_SIZE; i++) input[i] = sin (2 * G_PI * 13 * i / MATINEE_FFT_SIZE);
	matinee_fft_magnitude (input, MATINEE_FFT_SIZE, expected);
	matinee_fft_compute (engine->fft, input, actual);
	for (int i = 0; i < MATINEE_FFT_SIZE / 2; i++)
		g_assert_cmpfloat_with_epsilon (actual[i], expected[i], 0.00001);
	memset (input, 0, sizeof input);
	matinee_fft_compute (engine->fft, input, actual);
	for (int i = 0; i < MATINEE_FFT_SIZE / 2; i++) g_assert_cmpfloat (actual[i], ==, 0.0);
	for (int i = 0; i < MATINEE_FFT_SIZE; i++)
		samples[i * 2 + 1] = (gint16) (20000 * sin (2 * G_PI * 16 * i / MATINEE_FFT_SIZE));
	update_bands (engine, samples, MATINEE_FFT_SIZE / 2);
	g_assert_cmpuint (engine->bands.generation, ==, 0);
	update_bands (engine, samples + MATINEE_FFT_SIZE, MATINEE_FFT_SIZE / 2);
	g_assert_cmpuint (engine->bands.generation, ==, 1);
	gboolean nonzero = FALSE;
	for (int i = 0; i < MATINEE_BANDS; i++) nonzero |= engine->bands.level[i] > 0;
	g_assert_true (nonzero);
	matinee_engine_set_spectrum_enabled (engine, FALSE);
	update_bands (engine, samples, MATINEE_FFT_SIZE);
	g_assert_cmpuint (engine->bands.generation, ==, 1);
	matinee_engine_set_spectrum_enabled (engine, TRUE);
	update_bands (engine, samples, 1);
	stop_threads (engine);
	update_bands (engine, samples, MATINEE_FFT_SIZE - 1);
	g_assert_cmpuint (engine->bands.generation, ==, 1);
	stop_threads (engine);
	engine->video_index = 0;
	update_bands (engine, samples, MATINEE_FFT_SIZE);
	g_assert_cmpuint (engine->bands.generation, ==, 1);
	g_assert_cmpint (engine->fft_filled, ==, 0);
}

static void test_metadata_instances (void)
{
	g_autoptr(MatineeEngine) first = matinee_engine_new ();
	g_autoptr(MatineeEngine) second = matinee_engine_new ();
	first->format = avformat_alloc_context ();
	second->format = avformat_alloc_context ();
	av_dict_set (&first->format->metadata, "title", "First title", 0);
	av_dict_set (&first->format->metadata, "artist", "First artist", 0);
	av_dict_set (&second->format->metadata, "title", "Second title", 0);
	av_dict_set (&second->format->metadata, "artist", "Second artist", 0);
	const char *title = matinee_engine_title (first);
	const char *artist = matinee_engine_artist (first);
	g_assert_cmpstr (matinee_engine_title (second), ==, "Second title");
	g_assert_cmpstr (matinee_engine_artist (second), ==, "Second artist");
	g_assert_cmpstr (title, ==, "First title");
	g_assert_cmpstr (artist, ==, "First artist");
}

static void test_mixed_open_files (void)
{
	GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
	g_autofree char *directory = g_dir_make_tmp ("matinee-open-XXXXXX", NULL);
	g_autofree char *a = write_tone_wav (directory, "a.wav", 0.1, 44100);
	g_autofree char *b = write_tone_wav (directory, "b.wav", 0.1, 44100);
	g_autoptr(GFile) remote = g_file_new_for_uri ("https://example.invalid/music.wav");
	g_autoptr(GFile) first = g_file_new_for_path (a);
	g_autoptr(GFile) second = g_file_new_for_path (b);
	g_autoptr(MatineeApp) app = matinee_app_new ();
	g_application_set_flags (G_APPLICATION (app), G_APPLICATION_NON_UNIQUE);
	g_application_set_application_id (G_APPLICATION (app), "org.luxe.Matinee.MixedOpenTest");
	g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
	GFile *files[] = { remote, first, remote, second };
	matinee_app_open (G_APPLICATION (app), files, 4, "");
	for (gsize i = 0; i < matinee_shortcuts_count; i++) {
		g_auto(GStrv) accels = gtk_application_get_accels_for_action (GTK_APPLICATION (app), matinee_shortcuts[i].action);
		g_assert_nonnull (accels[0]);
		guint expected_key, actual_key;
		GdkModifierType expected_mod, actual_mod;
		g_assert_true (gtk_accelerator_parse (matinee_shortcuts[i].accelerator, &expected_key, &expected_mod));
		g_assert_true (gtk_accelerator_parse (accels[0], &actual_key, &actual_mod));
		g_assert_cmpuint (actual_key, ==, expected_key);
		g_assert_cmpuint (actual_mod, ==, expected_mod);
		g_assert_true (g_action_group_has_action (G_ACTION_GROUP (app->window), matinee_shortcuts[i].action + 4));
	}
	g_action_group_activate_action (G_ACTION_GROUP (app->window), "stop", NULL);
	g_assert_cmpint (matinee_engine_state (app->window->engine), ==, MATINEE_STATE_IDLE);
	g_assert_cmpuint (matinee_playlist_length (app->window->playlist), ==, 2);
	g_assert_cmpstr (matinee_entry_path (matinee_playlist_at (app->window->playlist, 0)), ==, a);
	g_assert_cmpstr (matinee_entry_path (matinee_playlist_at (app->window->playlist, 1)), ==, b);
	GtkWindow *dialog = luxe_window_get_visible_dialog (GTK_WINDOW (app->window));
	g_assert_nonnull (dialog);
	luxe_dialog_close (dialog);
	matinee_app_open (G_APPLICATION (app), files, 1, "");
	g_assert_cmpuint (matinee_playlist_length (app->window->playlist), ==, 2);
	dialog = luxe_window_get_visible_dialog (GTK_WINDOW (app->window));
	g_assert_nonnull (dialog);
	luxe_dialog_close (dialog);
	fail_seek = TRUE;
	gtk_range_set_value (GTK_RANGE (app->window->seek), 700);
	fail_seek = FALSE;
	dialog = luxe_window_get_visible_dialog (GTK_WINDOW (app->window));
	g_assert_nonnull (dialog);
	g_assert_cmpstr (gtk_window_get_title (dialog), ==, "Cannot seek");
	luxe_dialog_close (dialog);
	gtk_window_destroy (GTK_WINDOW (app->window));
	while (g_main_context_iteration (NULL, FALSE));
	g_unlink (a); g_unlink (b); g_rmdir (directory);
	g_log_set_always_fatal (fatal);
}

static void test_mp3_playback (void)
{
	g_autofree char *directory = g_dir_make_tmp ("matinee-mp3-XXXXXX", NULL);
	g_autofree char *wave = write_tone_wav (directory, "input.wav", 5.0, 44100);
	g_autofree char *generated = g_build_filename (directory, "Song #1 – 100%25.mp3", NULL);
	const char *path = g_getenv ("MATINEE_TEST_MP3");
	if (path == NULL)
	{
		g_autofree char *ffmpeg = g_find_program_in_path ("ffmpeg");
		if (!ffmpeg) {
			g_test_skip ("ffmpeg is required to generate the MP3 fixture");
			g_unlink (wave); g_rmdir (directory);
			return;
		}
		g_autoptr (GSubprocess) encoder = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, NULL,
			ffmpeg, "-nostdin", "-v", "error", "-i", wave, "-ac", "2", "-c:a", "libmp3lame", generated, NULL);
		g_assert_nonnull (encoder);
		g_assert_true (g_subprocess_wait_check (encoder, NULL, NULL));
		path = generated;
	}
	GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
	g_autoptr (MatineeApp) app = matinee_app_new ();
	g_application_set_flags (G_APPLICATION (app), G_APPLICATION_NON_UNIQUE);
	g_application_set_application_id (G_APPLICATION (app), "org.luxe.Matinee.MP3Test");
	g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
	g_application_activate (G_APPLICATION (app));
	ui_thread = g_thread_self ();
	finished_count = 0;
	g_atomic_int_set (&nonzero_audio, 0);
	matinee_window_open_files (app->window, (const char *[]) { path, NULL });
	MatineeEngine *engine = app->window->opening_engine;
	g_assert_nonnull (engine);
	g_signal_connect (engine, "finished", G_CALLBACK (finished_on_ui), NULL);
	gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
	while (g_get_monotonic_time () < deadline)
	{
		while (g_main_context_iteration (NULL, FALSE));
		if (app->window->engine == engine && engine->audio_context != NULL)
			break;
		g_usleep (1000);
	}
	g_assert_nonnull (engine->audio_context);
	g_assert_cmpint (engine->audio_context->codec_id, ==, AV_CODEC_ID_MP3);
	g_assert_true (matinee_engine_state (engine) == MATINEE_STATE_PLAYING
	               || matinee_engine_state (engine) == MATINEE_STATE_ENDED);
	deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
	while (!finished_count && g_get_monotonic_time () < deadline) {
		while (g_main_context_iteration (NULL, FALSE));
		g_usleep (1000);
	}
	g_assert_cmpuint (finished_count, ==, 1);
	/* Join first; frames_written belongs to the worker until it exits. */
	g_thread_join (engine->audio_thread);
	engine->audio_thread = NULL;
	g_assert_cmpuint (engine->frames_written, >, MATINEE_OUTPUT_RATE / 2);
	g_assert_cmpint (g_atomic_int_get (&nonzero_audio), ==, 1);
	matinee_engine_stop (engine);
	g_assert_cmpuint (app->window->visual_tick_id, ==, 0);
	g_assert_cmpuint (app->window->position_timer, ==, 0);
	gtk_window_destroy (GTK_WINDOW (app->window));
	while (g_main_context_iteration (NULL, FALSE));
	g_log_set_always_fatal (fatal);
	g_unlink (generated); g_unlink (wave); g_rmdir (directory);
}

static void test_video_packet_queue_bounds (void)
{
	g_autoptr (MatineeEngine) engine = matinee_engine_new ();
	AVPacket *packet = av_packet_alloc ();
	g_assert_nonnull (packet);
	packet->size = 64 * 1024 * 1024 + 1;
	g_assert_false (queue_video_packet (engine, packet));
	g_assert_cmpuint (engine->video_queue_count, ==, 0);
	av_packet_free (&packet);
	for (guint i = 0; i < 512; i++)
	{
		packet = av_packet_alloc ();
		g_assert_nonnull (packet);
		g_assert_true (queue_video_packet (engine, packet));
		av_packet_free (&packet);
	}
	packet = av_packet_alloc ();
	g_assert_false (queue_video_packet (engine, packet));
	av_packet_free (&packet);
	g_assert_cmpuint (engine->video_queue_count, ==, 512);
	clear_video_packets (engine);
	g_assert_cmpuint (engine->video_queue_count, ==, 0);
	g_assert_cmpuint (engine->video_queue_bytes, ==, 0);
}

typedef struct {
	MatineeEngine *engine;
	gint entered, done;
	gboolean ready;
} WaitProbe;

static gpointer
paused_probe (gpointer data)
{
	WaitProbe *probe = data;
	g_atomic_int_set (&probe->entered, 1);
	probe->ready = wait_playing (probe->engine);
	g_atomic_int_set (&probe->done, 1);
	return NULL;
}

static void
test_paused_wakeup (void)
{
	/* Exercise EOF and Stop while the same helper used by both decoders is
	 * blocked, without an audio device or scheduling-sensitive media EOF. */
	for (guint cancel = 0; cancel < 2; cancel++) {
		g_autoptr (MatineeEngine) engine = matinee_engine_new ();
		WaitProbe probe = { .engine = engine };
		engine->video_thread = g_thread_new ("paused-probe", paused_probe, &probe);
		gint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
		while (!g_atomic_int_get (&probe.entered) && g_get_monotonic_time () < deadline)
			g_usleep (1000);
		g_assert_cmpint (g_atomic_int_get (&probe.entered), ==, 1);
		g_usleep (50000);
		g_assert_cmpint (g_atomic_int_get (&probe.done), ==, 0);
		if (cancel) stop_threads (engine);
		else {
			signal_end (engine);
			g_thread_join (engine->video_thread);
			engine->video_thread = NULL;
		}
		g_assert_cmpint (g_atomic_int_get (&probe.done), ==, 1);
		g_assert_cmpint (probe.ready, ==, !cancel);
	}
}

static void test_library_root_lifetime(void)
{
    GLogLevelFlags fatal = g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_autofree char *directory = g_dir_make_tmp("matinee-library-lifetime-XXXXXX", NULL);
    g_autofree char *path = g_build_filename(directory, "readme.txt", NULL);
    g_assert_true(g_file_set_contents(path, "ignored media fixture", -1, NULL));
    g_autoptr(MatineeApp) app = matinee_app_new();
    g_application_set_flags(G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);
    g_application_set_application_id(G_APPLICATION(app), "org.luxe.Matinee.LibraryLifetimeTest");
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    g_application_activate(G_APPLICATION(app));
    watched_library_path = directory;
    finalized_library_roots = 0;
    g_settings_set_string(app->window->settings, "library-path", directory);
    for (guint pass = 1; pass <= 3; ++pass) {
        action_rescan_library(NULL, NULL, app->window);
        gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
        while (app->window->library_cancel != NULL && g_get_monotonic_time() < deadline)
            pump_for(1000);
        g_assert_null(app->window->library_cancel);
        g_assert_cmpuint(finalized_library_roots, ==, pass);
    }
    watched_library_path = NULL;
    g_settings_set_string(app->window->settings, "library-path", "");
    gtk_window_destroy(GTK_WINDOW(app->window));
    pump_for(10000);
    g_unlink(path); g_rmdir(directory);
    g_log_set_always_fatal(fatal);
}

static void wait_library_scan (MatineeWindow *window)
{
	gint64 deadline = g_get_monotonic_time () + 2 * G_TIME_SPAN_SECOND;
	while (window->library_cancel != NULL && g_get_monotonic_time () < deadline)
		pump_for (1000);
	g_assert_null (window->library_cancel);
}

static void test_library_scan_reconcile (void)
{
	g_autofree char *directory = g_dir_make_tmp ("matinee-library-reconcile-XXXXXX", NULL);
	g_autofree char *one = g_build_filename (directory, "one.wav", NULL);
	g_autofree char *two = g_build_filename (directory, "two.mp3", NULL);
	g_autofree char *text = g_build_filename (directory, "notes.txt", NULL);
	g_autoptr(MatineeApp) app = matinee_app_new ();
	g_application_set_flags (G_APPLICATION (app), G_APPLICATION_NON_UNIQUE);
	g_application_set_application_id (G_APPLICATION (app), "org.luxe.Matinee.LibraryReconcileTest");
	g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
	g_application_activate (G_APPLICATION (app));
	MatineeWindow *window = app->window;
	g_assert_true (g_file_set_contents (one, "audio", -1, NULL));
	g_assert_true (g_file_set_contents (two, "audio", -1, NULL));
	g_assert_true (g_file_set_contents (text, "not media", -1, NULL));
	g_settings_set_string (window->settings, "library-path", directory);
	action_rescan_library (NULL, NULL, window);
	wait_library_scan (window);
	g_assert_cmpuint (matinee_playlist_length (window->playlist), ==, 2);
	g_assert_cmpuint (g_hash_table_size (window->library_entries), ==, 2);
	/* Repeated scans update the library-owned subset instead of appending it. */
	action_rescan_library (NULL, NULL, window);
	wait_library_scan (window);
	g_assert_cmpuint (matinee_playlist_length (window->playlist), ==, 2);
	/* Playlist entries added by the user remain when the scan reconciles. */
	g_autofree char *user_path = g_build_filename (directory, "user-added.mp3", NULL);
	g_autoptr(MatineeEntry) user_entry = matinee_entry_new (user_path, 0, MATINEE_ENTRY_AUDIO);
	matinee_playlist_append (window->playlist, user_entry);
	g_assert_true (g_file_set_contents (user_path, "user", -1, NULL));
	g_assert_cmpint (g_remove (two), ==, 0);
	matinee_playlist_set_current (window->playlist, 2);
	action_rescan_library (NULL, NULL, window);
	wait_library_scan (window);
	g_assert_cmpuint (matinee_playlist_length (window->playlist), ==, 2);
	g_assert_cmpint (matinee_playlist_current (window->playlist), ==, 1);
	g_assert_cmpstr (matinee_entry_path (matinee_playlist_at (window->playlist, 1)), ==, user_path);
	/* A stale library entry is removed, but the currently selected one remains. */
	matinee_playlist_set_current (window->playlist, 0);
	g_assert_cmpint (g_remove (one), ==, 0);
	action_rescan_library (NULL, NULL, window);
	wait_library_scan (window);
	g_assert_cmpuint (matinee_playlist_length (window->playlist), ==, 2);
	g_assert_cmpstr (matinee_entry_path (matinee_playlist_at (window->playlist, 0)), ==, one);
	g_assert_false (g_hash_table_contains (window->library_entries, one));
	/* Invalid folders surface an incomplete-scan reason without clearing user entries. */
	g_settings_set_string (window->settings, "library-path", text);
	action_rescan_library (NULL, NULL, window);
	wait_library_scan (window);
	g_assert_true (gtk_widget_get_visible (window->library_status));
	g_assert_cmpuint (matinee_playlist_length (window->playlist), ==, 2);
	gtk_window_destroy (GTK_WINDOW (window));
	pump_for (10000);
	g_remove (one); g_remove (two); g_remove (text); g_remove (user_path); g_rmdir (directory);
}

static void test_library_scan_cancellation (void)
{
	g_autofree char *directory = g_dir_make_tmp ("matinee-library-cancel-XXXXXX", NULL);
	g_autofree char *path = g_build_filename (directory, "candidate.wav", NULL);
	g_autoptr(MatineeApp) app = matinee_app_new ();
	g_application_set_flags (G_APPLICATION (app), G_APPLICATION_NON_UNIQUE);
	g_application_set_application_id (G_APPLICATION (app), "org.luxe.Matinee.LibraryCancelTest");
	g_assert_true (g_application_register (G_APPLICATION (app), NULL, NULL));
	g_application_activate (G_APPLICATION (app));
	MatineeWindow *window = app->window;
	g_assert_true (g_file_set_contents (path, "audio", -1, NULL));
	g_settings_set_string (window->settings, "library-path", directory);
	action_rescan_library (NULL, NULL, window);
	g_autoptr(GCancellable) cancel = g_object_ref (window->library_cancel);
	g_autoptr(MatineePlaylist) playlist = g_object_ref (window->playlist);
	gtk_window_destroy (GTK_WINDOW (window));
	g_assert_true (g_cancellable_is_cancelled (cancel));
	pump_for (10000);
	g_assert_cmpuint (matinee_playlist_length (playlist), ==, 0);
	g_remove (path); g_rmdir (directory);
}

static int output_peak(const char *uri, double volume)
{
    g_autoptr(MatineeEngine) engine = matinee_engine_new();
    g_assert_true(matinee_engine_open(engine, uri, NULL));
    matinee_engine_set_volume(engine, volume);
    g_atomic_int_set(&peak_audio, 0); g_atomic_int_set(&audio_writes, 0);
    matinee_engine_play(engine);
    gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (matinee_engine_state(engine) != MATINEE_STATE_ENDED && g_get_monotonic_time() < deadline)
        pump_for(1000);
    matinee_engine_stop(engine); // Join the writer before reading/resetting its counters.
    g_assert_cmpint(g_atomic_int_get(&audio_writes), >, 0);
    return g_atomic_int_get(&peak_audio);
}
static void test_volume_output(void)
{
    g_autofree char *directory = g_dir_make_tmp("matinee-volume-XXXXXX", NULL);
    g_autofree char *path = write_tone_wav(directory, "tone.wav", 0.1, 48000);
    g_autofree char *uri = g_filename_to_uri(path, NULL, NULL);
    int full = output_peak(uri, 1.0);
    int quarter = output_peak(uri, 0.25);
    g_assert_cmpint(full, >, 1000);
    g_assert_cmpint(abs(quarter * 4 - full), <=, 4);
    g_assert_cmpint(output_peak(uri, 0.0), ==, 0);
    g_assert_cmpint(output_peak(uri, 2.0), ==, full);
    g_assert_cmpint(output_peak(uri, NAN), ==, 0);
    g_unlink(path); g_rmdir(directory);
}

static void model_changed(GListModel *model, guint position, guint removed, guint added, gpointer data)
{
    guint *previous_count = data;
    g_assert_cmpuint(position + removed, <=, *previous_count);
    *previous_count = *previous_count - removed + added;
    g_assert_cmpuint(g_list_model_get_n_items(model), ==, *previous_count);
}
static void test_playlist_identity(void)
{
    for (guint from = 0; from < 3; ++from) for (guint to = 0; to < 3; ++to)
    for (guint current = 0; current < 3; ++current) {
        g_autoptr(MatineePlaylist) playlist = matinee_playlist_new();
        for (guint i = 0; i < 3; ++i) {
            g_autofree char *name = g_strdup_printf("entry-%u.wav", i);
            g_autoptr(MatineeEntry) entry = matinee_entry_new(name, 0, MATINEE_ENTRY_AUDIO);
            matinee_playlist_append(playlist, entry);
        }
        guint count = 3;
        g_signal_connect(playlist, "items-changed", G_CALLBACK(model_changed), &count);
        g_autoptr(GtkSingleSelection) selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(playlist)));
        gtk_single_selection_set_autoselect(selection, FALSE);
        gtk_single_selection_set_can_unselect(selection, TRUE);
        gtk_single_selection_set_selected(selection, current);
        matinee_playlist_set_current(playlist, current);
        g_autoptr(MatineeEntry) chosen = g_object_ref(matinee_playlist_at(playlist, current));
        matinee_playlist_move(playlist, from, to);
        g_assert_true(matinee_playlist_at(playlist, matinee_playlist_current(playlist)) == chosen);
        g_assert_true(gtk_single_selection_get_selected_item(selection) == chosen);
        g_autoptr(MatineeEntry) extra = matinee_entry_new("extra.wav", 0, MATINEE_ENTRY_AUDIO);
        matinee_playlist_insert(playlist, extra, 0);
        g_assert_true(matinee_playlist_at(playlist, matinee_playlist_current(playlist)) == chosen);
        matinee_playlist_remove_at(playlist, 0);
        g_assert_true(matinee_playlist_at(playlist, matinee_playlist_current(playlist)) == chosen);
        matinee_playlist_remove_at(playlist, matinee_playlist_current(playlist));
        g_assert_cmpint(matinee_playlist_current(playlist), ==, -1);
        g_assert_null(gtk_single_selection_get_selected_item(selection));
    }
}
static void test_playlist_activation(void)
{
    GLogLevelFlags fatal = g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_autofree char *directory = g_dir_make_tmp("matinee-activate-XXXXXX", NULL);
    g_autofree char *path = write_tone_wav(directory, "track.wav", 0.1, 48000);
    g_autoptr(MatineeApp) app = matinee_app_new();
    g_application_set_flags(G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);
    g_application_set_application_id(G_APPLICATION(app), "org.luxe.Matinee.PlaylistTest");
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    g_application_activate(G_APPLICATION(app));
    MatineeWindow *window = app->window;
    g_autoptr(MatineeEntry) entry = matinee_entry_new(path, 0, MATINEE_ENTRY_AUDIO);
    matinee_playlist_append(window->playlist, entry);
    matinee_playlist_append(window->playlist, entry);
    g_signal_emit_by_name(window->playlist_view, "activate", 1u);
    g_assert_cmpint(matinee_playlist_current(window->playlist), ==, 1);
    GtkSingleSelection *selection = GTK_SINGLE_SELECTION(gtk_list_view_get_model(GTK_LIST_VIEW(window->playlist_view)));
    g_assert_cmpuint(gtk_single_selection_get_selected(selection), ==, 1);
    action_previous(NULL, NULL, window);
    g_assert_cmpint(matinee_playlist_current(window->playlist), ==, 0);
    g_assert_cmpuint(gtk_single_selection_get_selected(selection), ==, 0);
    gtk_window_destroy(GTK_WINDOW(window)); pump_for(10000);
    g_unlink(path); g_rmdir(directory);
    g_log_set_always_fatal(fatal);
}
static void test_inactive_rendering(void)
{
    GLogLevelFlags fatal = g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
    g_autoptr(MatineeApp) app = matinee_app_new();
    g_application_set_flags(G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);
    g_application_set_application_id(G_APPLICATION(app), "org.luxe.Matinee.IdleTimersTest");
    g_assert_true(g_application_register(G_APPLICATION(app), NULL, NULL));
    g_application_activate(G_APPLICATION(app));
    MatineeWindow *window = app->window;
    // Use the real state-change/UI path without starting any decoder/device.
    set_state(window->engine, MATINEE_STATE_PLAYING);
    g_assert_cmpuint(window->visual_tick_id, !=, 0);
    g_assert_cmpuint(window->position_timer, !=, 0);
    render_visualizer(window);
    GdkPaintable *frame = g_object_ref(gtk_picture_get_paintable(GTK_PICTURE(window->surface)));
    const MatineeState inactive[] = {MATINEE_STATE_PAUSED, MATINEE_STATE_ENDED, MATINEE_STATE_ERROR};
    for (guint i = 0; i < G_N_ELEMENTS(inactive); ++i) {
        set_state(window->engine, inactive[i]);
        g_assert_cmpuint(window->visual_tick_id, ==, 0);
        g_assert_cmpuint(window->position_timer, ==, 0);
        g_assert_true(gtk_picture_get_paintable(GTK_PICTURE(window->surface)) == frame);
    }
    set_state(window->engine, MATINEE_STATE_PLAYING);
    g_assert_cmpuint(window->visual_tick_id, !=, 0);
    g_assert_cmpuint(window->position_timer, !=, 0);
    g_object_unref(frame);
    gtk_window_destroy(GTK_WINDOW(window)); pump_for(10000);
    g_log_set_always_fatal(fatal);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	luxe_ui_init ();
    g_test_add_func("/matinee/library-root-lifetime", test_library_root_lifetime);
    g_test_add_func("/matinee/library-scan-reconcile", test_library_scan_reconcile);
    g_test_add_func("/matinee/library-scan-cancellation", test_library_scan_cancellation);
    g_test_add_func("/matinee/volume-output", test_volume_output);
    g_test_add_func("/matinee/playlist-identity", test_playlist_identity);
    g_test_add_func("/matinee/playlist-activation", test_playlist_activation);
    g_test_add_func("/matinee/inactive-rendering", test_inactive_rendering);
    g_test_add_func("/matinee/negotiated-output-rate", test_negotiated_output_rate);
    g_test_add_func("/matinee/upsample-capacity", test_upsample_capacity);
    g_test_add_func("/matinee/resampler-duration-rates", test_resampler_duration_rates);
    g_test_add_func("/matinee/short-audio-writes", test_short_audio_writes);
    g_test_add_func("/matinee/audio-worker-failure", test_audio_worker_failure);
    g_test_add_func("/matinee/audio-packet-failure", test_audio_packet_failure);
    g_test_add_func("/matinee/audio-error-dispatch", test_audio_error_dispatch);
	g_test_add_func ("/matinee/playlist", test_playlist);
	g_test_add_func ("/matinee/engine-open", test_engine_open);
	g_test_add_func ("/matinee/visual", test_visual);
	g_test_add_func ("/matinee/visual-variety", test_visual_variety);
	g_test_add_func ("/matinee/frame-dispatch", test_frame_dispatch);
	g_test_add_func ("/matinee/finished-dispatch", test_finished_dispatch);
	g_test_add_func ("/matinee/audio-end-drain", test_audio_end_drain);
	g_test_add_func ("/matinee/audio-short-video-tail", test_audio_short_video_tail);
	g_test_add_func ("/matinee/video-finishes-before-audio", test_video_finishes_before_audio);
	g_test_add_func ("/matinee/video-playback", test_video_playback);
	g_test_add_func ("/matinee/video-clock-controls", test_video_clock_controls);
	g_test_add_func ("/matinee/av-seek-discards-preroll", test_av_seek_discards_preroll);
	g_test_add_func ("/matinee/paused-wakeup", test_paused_wakeup);
	g_test_add_func ("/matinee/window-playback-close", test_window_playback_close);
	g_test_add_func ("/matinee/mixed-open-files", test_mixed_open_files);
	g_test_add_func ("/matinee/metadata-instances", test_metadata_instances);
	g_test_add_func ("/matinee/spectrum-windows", test_spectrum_windows);
	g_test_add_func ("/matinee/chooser-after-close", test_chooser_after_close);
	g_test_add_func ("/matinee/demux-read-failure", test_demux_read_failure);
	g_test_add_func ("/matinee/mp3-playback", test_mp3_playback);
	g_test_add_func ("/matinee/video-packet-queue-bounds", test_video_packet_queue_bounds);
	return g_test_run ();
}
