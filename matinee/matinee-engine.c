/* Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-3.0-or-later */

/* Playback engine built directly on FFmpeg's libraries.  An audio thread
 * decodes, resamples and writes to ALSA, and its drain position is the master
 * clock; a video thread decodes frames and schedules them against that clock.
 * For audio/video files the audio thread owns the demuxer and routes video
 * packets to the video thread; video-only files are demuxed by the video thread.
 * Audio is resampled once to stereo S16 for the device and the same frames are
 * mixed down to mono for the visualiser, so the spectrum costs no extra
 * conversion. */

#include "matinee-engine.h"

#include <alsa/asoundlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#define MATINEE_OUTPUT_RATE     48000
#define MATINEE_OUTPUT_CHANNELS 2
#define MATINEE_FFT_SIZE        1024
#define MATINEE_MAX_DIMENSION   8192
#define MATINEE_ALSA_LATENCY_US 120000
#define MATINEE_ALSA_MAX_LATENCY_US 250000
#define MATINEE_MAX_VIDEO_QUEUE_BYTES (64 * 1024 * 1024)
#define MATINEE_MAX_VIDEO_QUEUE_PACKETS 512
#define MATINEE_IO_TIMEOUT_US (15 * G_USEC_PER_SEC)
/* Bound a single resampler output allocation to 8 MiB of stereo S16. */
#define MATINEE_MAX_OUTPUT_FRAMES (2 * 1024 * 1024)

G_DEFINE_QUARK (matinee-engine-error-quark, matinee_engine_error)

struct _MatineeEngine
{
	GObject parent_instance;

	AVFormatContext *format;
	char *title_cache;
	char *artist_cache;
	int audio_index;
	int video_index;

	/* Audio decode/output. */
	AVCodecContext *audio_context;
	AVFrame *audio_frame;
	struct SwrContext *resampler;
	snd_pcm_t *pcm;
	int output_channels;
	unsigned int output_rate;
	guint64 frames_written;
	double audio_clock_base;
	double seek_target_seconds;
	gboolean discard_audio_preroll;
	gboolean discard_video_preroll;
	guint64 audio_preroll_samples_discarded;

	/* Video decode. */
	AVCodecContext *video_context;
	AVFrame *video_frame;
	struct SwsContext *scaler;
	int video_width;
	int video_height;

	/* State shared between threads and the caller. */
	GMutex state_lock;
	GCond playback_changed;
	MatineeState state;
	gboolean playing;
	gboolean flushing;
	gboolean reached_end;
	gint audio_done; /* Atomic: read by playback clock resume. */
	gboolean video_done; /* Protected by state_lock. */
	GSource *finished_source;
	GSource *audio_error_source;
	gchar *playback_error;
	double volume;
	gint64 start_time;

	GMutex clock_lock;
	double clock;
	gint64 clock_anchor;
	gboolean clock_running; /* Monotonic clock for video-only media. */

	GMutex bands_lock;
	MatineeBands bands;
	GMutex frame_lock;
	GdkTexture *pending_texture;
	gint64 last_video_frame_us;
	GSource *video_source;
	float *fft_input;
	int fft_filled;
	float *fft_output;
	MatineeFft *fft;
	gint spectrum_enabled;
	float *band_smoothed;

	GAsyncQueue *video_packets;
	GMutex video_queue_lock;
	gsize video_queue_bytes;
	guint video_queue_count;
	GThread *audio_thread;
	GThread *video_thread;
	GCancellable *cancellable;
	GMainContext *main_context;
	gint io_deadline_seconds;
	gint io_cancelled;
};

enum
{
	SIGNAL_VIDEO_FRAME,
	SIGNAL_STATE_CHANGED,
	SIGNAL_FINISHED,
	SIGNAL_N
};

static guint signals[SIGNAL_N];

G_DEFINE_TYPE (MatineeEngine, matinee_engine, G_TYPE_OBJECT)

static void stop_threads (MatineeEngine *engine);
static void clear_decoders (MatineeEngine *engine);
static void set_state (MatineeEngine *engine, MatineeState state);
static gboolean report_finished (gpointer data);
static gboolean report_audio_error (gpointer data);

static int
interrupt_media_io (void *opaque)
{
	MatineeEngine *engine = opaque;
	gint deadline = g_atomic_int_get (&engine->io_deadline_seconds);
	return g_atomic_int_get (&engine->io_cancelled)
	       || g_cancellable_is_cancelled (engine->cancellable)
	       || (deadline > 0 && g_get_monotonic_time () / G_USEC_PER_SEC >= deadline);
}

/* clock_lock is held. Audio retains its device-derived clock. */
static double
playback_position (MatineeEngine *engine)
{
	return engine->clock + (engine->clock_running
	       ? (g_get_monotonic_time () - engine->clock_anchor) / 1000000.0 : 0.0);
}

/* A paused decoder has no work. Resume, EOF and cancellation wake it; the
 * existing timestamp waits still pace frames during active playback. */
static gboolean wait_playing(MatineeEngine *engine)
{
	g_mutex_lock(&engine->state_lock);
	while (!engine->playing && !engine->reached_end &&
	       !g_cancellable_is_cancelled(engine->cancellable))
		g_cond_wait(&engine->playback_changed, &engine->state_lock);
	gboolean ready = !g_cancellable_is_cancelled(engine->cancellable);
	g_mutex_unlock(&engine->state_lock);
	return ready;
}

static gboolean
wait_for_video_time (MatineeEngine *engine, double time)
{
	while (!g_cancellable_is_cancelled (engine->cancellable))
	{
		if (!wait_playing(engine)) return FALSE;
		g_mutex_lock (&engine->state_lock);
		gboolean playing = engine->playing;
		g_mutex_unlock (&engine->state_lock);
		if (playing && matinee_engine_position (engine) >= time)
			return TRUE;
		g_usleep (8000); /* Recheck pause/resume, audio progress and cancellation. */
	}
	return FALSE;
}

/* invoke_full() may run inline on a decoder thread when the context is idle.
 * Explicit sources always dispatch through the UI loop. The engine retains
 * each source and destroys it after joining workers, before releasing itself. */
static GSource *
queue_ui_delivery (MatineeEngine *engine, GSourceFunc callback)
{
	GSource *source = g_idle_source_new ();
	g_source_set_priority (source, G_PRIORITY_DEFAULT);
	g_source_set_callback (source, callback, engine, NULL);
	g_source_attach (source, engine->main_context);
	return source;
}

static void
cancel_ui_delivery (GSource **source)
{
	if (*source != NULL)
		g_source_destroy (*source);
	g_clear_pointer (source, g_source_unref);
}

/*** Audio ***/

static void
signal_end (MatineeEngine *engine)
{
	gboolean was_playing;
	g_mutex_lock (&engine->clock_lock);
	engine->clock = playback_position (engine);
	engine->clock_running = FALSE;
	g_mutex_unlock (&engine->clock_lock);

	g_mutex_lock (&engine->state_lock);
	was_playing = engine->playing && !engine->reached_end;
	engine->reached_end = TRUE;
	engine->playing = FALSE;
	g_cond_broadcast(&engine->playback_changed);
	if (was_playing && engine->finished_source == NULL)
		engine->finished_source = queue_ui_delivery (engine, report_finished);
	g_mutex_unlock (&engine->state_lock);
}

static void
set_playback_error(MatineeEngine *engine, const char *message)
{
	g_mutex_lock(&engine->state_lock);
	g_free(engine->playback_error);
	engine->playback_error=g_strdup(message);
	g_mutex_unlock(&engine->state_lock);
}

static void
signal_playback_error(MatineeEngine *engine, const char *message)
{
	if(g_cancellable_is_cancelled(engine->cancellable))return;
	if(message)set_playback_error(engine,message);
	else {
		g_mutex_lock(&engine->state_lock);
		if(!engine->playback_error)engine->playback_error=g_strdup("The decoder or audio output failed.");
		g_mutex_unlock(&engine->state_lock);
	}
	g_cancellable_cancel(engine->cancellable);
	g_mutex_lock(&engine->clock_lock);
	engine->clock=playback_position(engine);engine->clock_running=FALSE;
	g_mutex_unlock(&engine->clock_lock);
	g_mutex_lock(&engine->state_lock);
	engine->playing=FALSE;
	engine->reached_end=FALSE;
	cancel_ui_delivery(&engine->finished_source);
	g_cond_broadcast(&engine->playback_changed);
	if(!engine->audio_error_source)
		engine->audio_error_source=queue_ui_delivery(engine,report_audio_error);
	g_mutex_unlock(&engine->state_lock);
}

static void
audio_stream_finished(MatineeEngine *engine)
{
	/* After the device drains, a monotonic clock paces any remaining video. */
	g_mutex_lock(&engine->clock_lock);
	g_mutex_lock(&engine->state_lock);
	g_atomic_int_set(&engine->audio_done,TRUE);
	if(engine->video_index>=0 && engine->playing){
		engine->clock_anchor=g_get_monotonic_time();engine->clock_running=TRUE;
	}
	gboolean complete=engine->video_index<0 || engine->video_done;
	g_mutex_unlock(&engine->state_lock);
	g_mutex_unlock(&engine->clock_lock);
	if(complete)signal_end(engine);
}

static gboolean
queue_video_packet (MatineeEngine *engine, AVPacket *packet)
{
	AVPacket *queued = av_packet_alloc ();

	if (queued == NULL)
		return FALSE;
	g_mutex_lock (&engine->video_queue_lock);
	if (packet->size < 0 || packet->size > MATINEE_MAX_VIDEO_QUEUE_BYTES
	    || engine->video_queue_count >= MATINEE_MAX_VIDEO_QUEUE_PACKETS
	    || (gsize) packet->size > MATINEE_MAX_VIDEO_QUEUE_BYTES - engine->video_queue_bytes)
	{
		g_mutex_unlock (&engine->video_queue_lock);
		av_packet_free (&queued);
		return FALSE;
	}
	engine->video_queue_bytes += (gsize) packet->size;
	engine->video_queue_count++;
	g_mutex_unlock (&engine->video_queue_lock);
	av_packet_move_ref (queued, packet);
	g_async_queue_push (engine->video_packets, queued);
	return TRUE;
}

static gboolean
queue_video_eof (MatineeEngine *engine)
{
	AVPacket *eof = av_packet_alloc ();

	if (eof == NULL)
		return FALSE;
	eof->stream_index = -1;
	g_mutex_lock (&engine->video_queue_lock);
	engine->video_queue_count++;
	g_mutex_unlock (&engine->video_queue_lock);
	g_async_queue_push (engine->video_packets, eof);
	return TRUE;
}

static void
free_video_packet (MatineeEngine *engine, AVPacket **packet)
{
	if (*packet == NULL)
		return;
	g_mutex_lock (&engine->video_queue_lock);
	if ((*packet)->stream_index >= 0)
		engine->video_queue_bytes -= MIN (engine->video_queue_bytes, (gsize) MAX ((*packet)->size, 0));
	if (engine->video_queue_count > 0)
		engine->video_queue_count--;
	g_mutex_unlock (&engine->video_queue_lock);
	av_packet_free (packet);
}

static void
account_video_packet_removed (MatineeEngine *engine, AVPacket *packet)
{
	g_mutex_lock (&engine->video_queue_lock);
	if (packet->stream_index >= 0)
		engine->video_queue_bytes -= MIN (engine->video_queue_bytes, (gsize) MAX (packet->size, 0));
	if (engine->video_queue_count > 0)
		engine->video_queue_count--;
	g_mutex_unlock (&engine->video_queue_lock);
}

static void
clear_video_packets (MatineeEngine *engine)
{
	AVPacket *packet;

	if (engine->video_packets == NULL)
		return;
	while ((packet = g_async_queue_try_pop (engine->video_packets)) != NULL)
		free_video_packet (engine, &packet);
}

static gboolean
open_audio_output (MatineeEngine *engine)
{
	snd_pcm_hw_params_t *params;
	unsigned int rate = MATINEE_OUTPUT_RATE;
	unsigned int period_time = MATINEE_ALSA_LATENCY_US / 4;
	unsigned int buffer_time = MATINEE_ALSA_LATENCY_US;
	int direction = 0;
	gboolean ok = FALSE;

	if (engine->pcm != NULL)
		return TRUE;
	if (snd_pcm_open (&engine->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
	{
		engine->pcm = NULL;
		return FALSE;
	}
	if (snd_pcm_hw_params_malloc (&params) < 0)
		goto out;
	if (snd_pcm_hw_params_any (engine->pcm, params) < 0)
		goto out_params;
	if (snd_pcm_hw_params_set_access (engine->pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
		goto out_params;
	if (snd_pcm_hw_params_set_format (engine->pcm, params, SND_PCM_FORMAT_S16_LE) < 0)
		goto out_params;
	if (snd_pcm_hw_params_set_channels (engine->pcm, params, MATINEE_OUTPUT_CHANNELS) < 0)
		goto out_params;
	if (snd_pcm_hw_params_set_period_time_near (engine->pcm, params, &period_time, &direction) < 0)
		goto out_params;
	direction = 0;
	if (snd_pcm_hw_params_set_buffer_time_near (engine->pcm, params, &buffer_time, &direction) < 0
	    || buffer_time > MATINEE_ALSA_MAX_LATENCY_US)
		goto out_params;
	if (snd_pcm_hw_params_set_rate_near (engine->pcm, params, &rate, NULL) < 0)
		goto out_params;
	if (rate == 0)
		goto out_params;
	if (snd_pcm_hw_params (engine->pcm, params) < 0)
		goto out_params;
	engine->output_channels = MATINEE_OUTPUT_CHANNELS;
	engine->output_rate = rate;
	ok = TRUE;
out_params:
	snd_pcm_hw_params_free (params);
out:
	if (!ok)
	{
		snd_pcm_close (engine->pcm);
		engine->pcm = NULL;
	}
	return ok;
}

static gboolean
prepare_resampler (MatineeEngine *engine)
{
	AVChannelLayout out_layout;
	int result;

	if (engine->resampler != NULL)
		return TRUE;

	av_channel_layout_default (&out_layout, MATINEE_OUTPUT_CHANNELS);
	result = swr_alloc_set_opts2 (&engine->resampler,
	                               &out_layout, AV_SAMPLE_FMT_S16, engine->output_rate,
	                               &engine->audio_context->ch_layout,
	                               engine->audio_context->sample_fmt,
	                               engine->audio_context->sample_rate,
	                               0, NULL);
	av_channel_layout_uninit (&out_layout);
	if (result < 0)
	{
		engine->resampler = NULL;
		return FALSE;
	}
	return swr_init (engine->resampler) >= 0;
}

/* Mix the interleaved S16 output down to mono and update the spectrum. */
static void
update_bands (MatineeEngine *engine, const gint16 *interleaved, int frames)
{
	if (!g_atomic_int_get (&engine->spectrum_enabled))
		return;
	/* Video occupies the display; there is no spectrum consumer for it. */
	if (engine->video_index >= 0)
		return;
	g_mutex_lock (&engine->bands_lock);
	for (int i = 0; i < frames; i++) {
		int left = interleaved[i * MATINEE_OUTPUT_CHANNELS];
		int right = interleaved[i * MATINEE_OUTPUT_CHANNELS + 1];
		engine->fft_input[engine->fft_filled++] = (left + right) / 65536.0f;
		if (engine->fft_filled < MATINEE_FFT_SIZE)
			continue;
		matinee_fft_compute (engine->fft, engine->fft_input, engine->fft_output);
		int bins = MATINEE_FFT_SIZE / 2;
		for (int band = 0; band < MATINEE_BANDS; band++)
		{
			int from = (int) ((double) band / MATINEE_BANDS * bins);
			int to = (int) ((double) (band + 1) / MATINEE_BANDS * bins);
			float peak = 0.0f;
			for (int b = from; b < to && b < bins; b++)
				peak = MAX (peak, engine->fft_output[b]);
			/* Log-ish compression keeps quiet detail visible. */
			float value = logf (1.0f + peak * 24.0f) / logf (25.0f);
			engine->band_smoothed[band] = engine->band_smoothed[band] * 0.6f
			                             + CLAMP (value, 0.0f, 1.0f) * 0.4f;
			engine->bands.level[band] = engine->band_smoothed[band];
		}
		engine->bands.time_us = g_get_monotonic_time ();
		engine->bands.generation++;
		engine->fft_filled = 0;
	}
	g_mutex_unlock (&engine->bands_lock);
}

static void
update_clock (MatineeEngine *engine)
{
	snd_pcm_sframes_t delay = 0;
	gdouble seconds;

	if (engine->pcm != NULL)
		snd_pcm_delay (engine->pcm, &delay);
	seconds = engine->audio_clock_base +
	          (gdouble) ((gint64) engine->frames_written - (gint64) MAX (delay, 0))
	        / (gdouble) engine->output_rate;
	if (seconds < 0.0)
		seconds = 0.0;
	g_mutex_lock (&engine->clock_lock);
	engine->clock = seconds;
	g_mutex_unlock (&engine->clock_lock);
}

static int
resampler_output_capacity(struct SwrContext *resampler, int input_samples)
{
	int frames = swr_get_out_samples(resampler, input_samples);
	return frames >= 0 && frames <= MATINEE_MAX_OUTPUT_FRAMES ? MAX(frames,1) : -1;
}

static gboolean
write_audio_frames(MatineeEngine *engine, const gint16 *samples, int frames)
{
	int offset=0, recoveries=0;
	while(offset<frames && !g_cancellable_is_cancelled(engine->cancellable)) {
		if(!wait_playing(engine))return FALSE;
		snd_pcm_sframes_t written=snd_pcm_writei(engine->pcm,
		    samples+(gsize)offset*MATINEE_OUTPUT_CHANNELS,frames-offset);
		if(written>0) {
			if(written>frames-offset){set_playback_error(engine,"The audio device reported an invalid write length.");return FALSE;}
			offset+=(int)written;recoveries=0;
			engine->frames_written+=(guint64)written;update_clock(engine);
		} else if(written==-EINTR) {
			continue;
		} else if(written==0 || written==-EAGAIN) {
			int result=snd_pcm_wait(engine->pcm,100);
			if(result<0 && result!=-EINTR){
				g_autofree char *message=g_strdup_printf("Waiting for the audio device failed: %s",snd_strerror(result));
				set_playback_error(engine,message);return FALSE;
			}
		} else {
			int recovered=++recoveries>8 ? 0 : snd_pcm_recover(engine->pcm,(int)written,1);
			if(recoveries>8 || recovered<0){
				g_autofree char *message=g_strdup_printf("Audio output failed: %s",snd_strerror(recovered<0?recovered:(int)written));
				set_playback_error(engine,message);return FALSE;
			}
		}
	}
	return offset==frames;
}

static gboolean
output_converted_audio(MatineeEngine *engine,gint16 *buffer,int frames)
{
	update_bands(engine,buffer,frames);
	g_mutex_lock(&engine->state_lock);double volume=engine->volume;g_mutex_unlock(&engine->state_lock);
	if(volume==0.0)memset(buffer,0,(gsize)frames*MATINEE_OUTPUT_CHANNELS*sizeof(gint16));
	else if(volume!=1.0)
		for(gsize i=0;i<(gsize)frames*MATINEE_OUTPUT_CHANNELS;i++)buffer[i]=(gint16)lrint(buffer[i]*volume);
	return write_audio_frames(engine,buffer,frames);
}

static gboolean
drain_resampler(MatineeEngine *engine,gint16 **buffer,gsize *capacity)
{
	while(!g_cancellable_is_cancelled(engine->cancellable)) {
		int frames=resampler_output_capacity(engine->resampler,0);
		if(frames<0)return FALSE;
		gsize needed=(gsize)frames*MATINEE_OUTPUT_CHANNELS*sizeof(gint16);
		if(needed>*capacity) {
			gint16 *larger=g_try_realloc(*buffer,needed);
			if(!larger)return FALSE;
			*buffer=larger;*capacity=needed;
		}
		uint8_t *out=(uint8_t *)*buffer;
		int converted=swr_convert(engine->resampler,&out,frames,NULL,0);
		if(converted<0)return FALSE;
		if(converted==0)return TRUE;
		if(!output_converted_audio(engine,*buffer,converted))return FALSE;
	}
	return FALSE;
}

static gpointer
audio_thread_func (gpointer data)
{
	MatineeEngine *engine = data;
	gint16 *output_buffer = NULL;
	gsize output_capacity = 0;
	AVPacket *packet = av_packet_alloc ();
	gboolean draining = FALSE;
	gboolean failed = FALSE;

	if (packet == NULL)
	{
		signal_playback_error(engine,"Could not allocate an audio decode packet.");
		return NULL;
	}
	while (!g_cancellable_is_cancelled (engine->cancellable))
	{
		int received;
		if (!wait_playing(engine)) break;

		received = avcodec_receive_frame (engine->audio_context, engine->audio_frame);
		if (received == 0)
		{
			int samples = engine->audio_frame->nb_samples;
			int converted = 0;
			g_autofree const uint8_t **offset_data = NULL;
			const uint8_t * const *input_data = (const uint8_t * const *) engine->audio_frame->extended_data;
			if (engine->discard_audio_preroll && samples > 0 && engine->audio_context->sample_rate > 0)
			{
				int64_t timestamp = engine->audio_frame->best_effort_timestamp;
				if (timestamp == AV_NOPTS_VALUE)
					timestamp = engine->audio_frame->pts;
				if (timestamp != AV_NOPTS_VALUE)
				{
					AVStream *stream = engine->format->streams[engine->audio_index];
					double frame_time = (double) timestamp * av_q2d (stream->time_base)
					                  - (double) engine->start_time / AV_TIME_BASE;
					double frame_end = frame_time + (double) samples / engine->audio_context->sample_rate;
					if (frame_end <= engine->seek_target_seconds)
					{
						g_mutex_lock (&engine->state_lock);
						engine->audio_preroll_samples_discarded += samples;
						g_mutex_unlock (&engine->state_lock);
						av_frame_unref (engine->audio_frame);
						continue;
					}
					int drop = (int) ceil (MAX (0.0, engine->seek_target_seconds - frame_time)
					                       * engine->audio_context->sample_rate);
					if (drop > 0)
					{
						int bytes = av_get_bytes_per_sample (engine->audio_context->sample_fmt);
						int channels = engine->audio_frame->ch_layout.nb_channels;
						gboolean planar = av_sample_fmt_is_planar (engine->audio_context->sample_fmt);
						int planes = planar ? channels : 1;
						if (bytes <= 0 || channels <= 0 || drop >= samples)
						{ failed = TRUE; break; }
						offset_data = g_new (const uint8_t *, planes);
						for (int i = 0; i < planes; i++)
							offset_data[i] = engine->audio_frame->extended_data[i]
							               + (gsize) drop * bytes * (planar ? 1 : channels);
						input_data = offset_data;
						samples -= drop;
						g_mutex_lock (&engine->state_lock);
						engine->audio_preroll_samples_discarded += drop;
						g_mutex_unlock (&engine->state_lock);
					}
					engine->discard_audio_preroll = FALSE;
				}
			}
			if (samples > 0)
			{
				int capacity = resampler_output_capacity(engine->resampler, samples);
				if (capacity < 0) {failed=TRUE;break;}
				gsize needed = (gsize) capacity * MATINEE_OUTPUT_CHANNELS * sizeof (gint16);
				uint8_t *out_ptr;
				const uint8_t * const *in_ptr;
				if (needed > output_capacity)
				{
					gint16 *larger = g_try_realloc (output_buffer, needed);
					if (!larger) {failed=TRUE;break;}
					output_buffer = larger;
					output_capacity = needed;
				}
				out_ptr = (uint8_t *) output_buffer;
				in_ptr = input_data;
				converted = swr_convert (engine->resampler, &out_ptr, capacity,
				                         in_ptr, samples);
				if(converted<0){failed=TRUE;break;}
			}
			if (converted > 0 && output_buffer != NULL)
			{
				if (!output_converted_audio(engine,output_buffer,converted)) {failed=TRUE;break;}
			}
			continue;
		}
		if (received != AVERROR (EAGAIN) && received != AVERROR_EOF)
			{failed=TRUE;break;}
		if (draining)
			break;

		/* The audio thread owns the demuxer for audio/video files. */
		av_packet_unref (packet);
		received = av_read_frame (engine->format, packet);
		if (received < 0)
		{
			if (received == AVERROR (EAGAIN)) { g_usleep (10000); continue; }
			if (received == AVERROR_EOF)
			{
				int sent=avcodec_send_packet (engine->audio_context, NULL);
				if(sent<0 && sent!=AVERROR_EOF){failed=TRUE;break;}
				draining = TRUE;
				if (engine->video_index >= 0 && !queue_video_eof (engine))
					{failed = TRUE;break;}
			}
			else {failed=TRUE;break;}
			continue;
		}
		if (packet->stream_index == engine->audio_index)
		{
			/* We have just drained receive_frame to EAGAIN, so the decoder
			 * must accept input or report an error. Silently discarding a
			 * rejected packet loses audio and can falsely finish the track. */
			if (avcodec_send_packet (engine->audio_context, packet) < 0) {
				failed = TRUE;
				break;
			}
		}
		else if (packet->stream_index == engine->video_index)
		{
			if (!queue_video_packet (engine, packet)) { failed = TRUE; break; }
		}
		else
			av_packet_unref (packet);
	}
	if(draining && !failed && !g_cancellable_is_cancelled(engine->cancellable)) {
		if(!drain_resampler(engine,&output_buffer,&output_capacity))failed=TRUE;
		else if(!g_cancellable_is_cancelled(engine->cancellable) && snd_pcm_drain(engine->pcm)<0)failed=TRUE;
		else if(!g_cancellable_is_cancelled(engine->cancellable)) {update_clock(engine);audio_stream_finished(engine);}
	}
	av_packet_free (&packet);
	g_free (output_buffer);
	if(failed)signal_playback_error(engine,NULL);
	return NULL;
}

static gboolean
report_audio_error(gpointer data)
{
	g_autoptr(MatineeEngine) engine=g_object_ref(data);
	g_mutex_lock(&engine->state_lock);
	g_clear_pointer(&engine->audio_error_source,g_source_unref);
	g_mutex_unlock(&engine->state_lock);
	set_state(engine,MATINEE_STATE_ERROR);
	return G_SOURCE_REMOVE;
}

static gboolean
report_finished (gpointer data)
{
	g_autoptr (MatineeEngine) engine = g_object_ref (data);
	gboolean ended;

	g_mutex_lock (&engine->state_lock);
	g_clear_pointer (&engine->finished_source, g_source_unref);
	ended = engine->reached_end;
	g_mutex_unlock (&engine->state_lock);
	if (!ended)
		return G_SOURCE_REMOVE;
	set_state (engine, MATINEE_STATE_ENDED);
	g_signal_emit (engine, signals[SIGNAL_FINISHED], 0);
	return G_SOURCE_REMOVE;
}

/*** Video ***/

static gboolean
prepare_scaler (MatineeEngine *engine, int width, int height)
{
	if (engine->scaler != NULL
	    && engine->video_width == width && engine->video_height == height)
		return TRUE;
	sws_freeContext (engine->scaler);
	engine->scaler = NULL;
	engine->scaler = sws_getContext (width, height, engine->video_context->pix_fmt,
	                                 width, height, AV_PIX_FMT_RGBA,
	                                 SWS_BILINEAR, NULL, NULL, NULL);
	engine->video_width = width;
	engine->video_height = height;
	return engine->scaler != NULL;
}

/* Runs on the main thread: emitting the signal here keeps all GTK work
 * (and any listener touching widgets) on the thread that owns them. */
static gboolean
deliver_video_frame (gpointer data)
{
	g_autoptr (MatineeEngine) engine = g_object_ref (data);
	GdkTexture *texture;

	g_mutex_lock (&engine->frame_lock);
	texture = engine->pending_texture;
	engine->pending_texture = NULL;
	g_clear_pointer (&engine->video_source, g_source_unref);
	g_mutex_unlock (&engine->frame_lock);
	if (texture != NULL)
	{
		g_signal_emit (engine, signals[SIGNAL_VIDEO_FRAME], 0, texture);
		g_object_unref (texture);
	}
	return G_SOURCE_REMOVE;
}

/* The decode thread never touches GTK directly.  It stores the frame and asks
 * the main context to deliver it; a newer frame supersedes a pending one so
 * the UI never lags behind playback. */
static void
post_video_frame (MatineeEngine *engine, GdkTexture *texture, double relative_time)
{
	g_mutex_lock (&engine->frame_lock);
	engine->last_video_frame_us = (gint64) (relative_time * G_USEC_PER_SEC);
	g_clear_object (&engine->pending_texture);
	engine->pending_texture = g_object_ref (texture);
	if (engine->video_source == NULL)
		engine->video_source = queue_ui_delivery (engine, deliver_video_frame);
	g_mutex_unlock (&engine->frame_lock);
}

static gpointer
video_thread_func (gpointer data)
{
	MatineeEngine *engine = data;
	gboolean draining = FALSE;
	gboolean failed = FALSE;
	double last_frame_time = 0.0;
	AVRational rate = av_guess_frame_rate (engine->format,
	                                     engine->format->streams[engine->video_index], NULL);
	double frame_duration = rate.num > 0 && rate.den > 0 ? av_q2d (av_inv_q (rate)) : 1.0 / 25;

	while (!g_cancellable_is_cancelled (engine->cancellable))
	{
		int received;
		if (!wait_playing(engine)) break;

		received = avcodec_receive_frame (engine->video_context, engine->video_frame);
		if (received == 0)
		{
			int width = engine->video_frame->width;
			int height = engine->video_frame->height;
			int stride;
			GdkTexture *texture;
			gsize needed;
			/* Some decoders leave AVFrame.pts unset; best_effort_timestamp is
			 * the decoder's presentation timestamp in the stream time base. */
			int64_t timestamp = engine->video_frame->best_effort_timestamp;
			if (timestamp == AV_NOPTS_VALUE)
				timestamp = engine->video_frame->pts;
			double frame_time = timestamp != AV_NOPTS_VALUE
				? (double) timestamp * av_q2d (engine->format->streams[engine->video_index]->time_base)
				: -1.0;
			if (engine->discard_video_preroll && frame_time >= 0.0
			    && frame_time - (double) engine->start_time / AV_TIME_BASE < engine->seek_target_seconds)
			{
				av_frame_unref (engine->video_frame);
				continue;
			}
			if (engine->discard_video_preroll && frame_time >= 0.0)
				engine->discard_video_preroll = FALSE;

			if (width <= 0 || height <= 0
			    || width > MATINEE_MAX_DIMENSION || height > MATINEE_MAX_DIMENSION)
			{
				av_frame_unref (engine->video_frame);
				continue;
			}
			if (!prepare_scaler (engine, width, height))
			{
				av_frame_unref (engine->video_frame);
				continue;
			}
			stride = width * 4;
			needed = (gsize) stride * height;
			/* Convert directly into the immutable texture's owned storage.
			 * A persistent scratch frame plus a full duplicate costs another
			 * 32 MiB at 4K and copies every decoded frame unnecessarily. */
			guchar *rgba = g_malloc (needed);
			{
				const uint8_t * const *source = (const uint8_t * const *) engine->video_frame->data;
				uint8_t *destination[4] = { rgba, NULL, NULL, NULL };
				const int *source_stride = engine->video_frame->linesize;
				int destination_stride[4] = { stride, 0, 0, 0 };
				sws_scale (engine->scaler, source, source_stride, 0, height,
				           destination, destination_stride);
			}
			GBytes *bytes = g_bytes_new_take (rgba, needed);
			texture = gdk_memory_texture_new (width, height, GDK_MEMORY_R8G8B8A8,
			                                  bytes, stride);
			g_bytes_unref (bytes);
			double relative_time = frame_time >= 0.0
				? MAX (frame_time - (double) engine->start_time / AV_TIME_BASE, 0.0)
				: last_frame_time;
			if (!wait_for_video_time (engine, relative_time))
			{
				g_object_unref (texture);
				break;
			}
			post_video_frame (engine, texture, relative_time);
			g_object_unref (texture);
			av_frame_unref (engine->video_frame);
			last_frame_time = relative_time + frame_duration;
			continue;
		}
		if (received != AVERROR (EAGAIN) && received != AVERROR_EOF)
			{failed=TRUE;break;}
		if (draining)
			break;

		{
			AVPacket *packet = NULL;
			if (engine->audio_index >= 0)
			{
				packet = g_async_queue_timeout_pop (engine->video_packets, 20000);
				if (packet == NULL)
					continue;
				account_video_packet_removed (engine, packet);
			}
			else
			{
				packet = av_packet_alloc ();
				if (packet == NULL)
					{failed=TRUE;break;}
				received = av_read_frame (engine->format, packet);
				if (received < 0)
				{
					av_packet_free (&packet);
					if (received == AVERROR_EOF)
					{
						int sent = avcodec_send_packet (engine->video_context, NULL);
						if (sent < 0 && sent != AVERROR_EOF) { failed = TRUE; break; }
						draining = TRUE;
					}
					else if(received==AVERROR(EAGAIN))g_usleep(10000);
					else {failed=TRUE;break;}
					continue;
				}
			}
			if (packet->stream_index < 0)
			{
				int sent = avcodec_send_packet (engine->video_context, NULL);
				av_packet_free (&packet);
				if (sent < 0 && sent != AVERROR_EOF) { failed = TRUE; break; }
				draining = TRUE;
				continue;
			}
			if (packet->stream_index == engine->video_index)
			{
				int sent = avcodec_send_packet (engine->video_context, packet);
				if (sent < 0) { failed = TRUE; }
			}
			av_packet_free (&packet);
			if (failed) break;
		}
	}
	/* Completion follows the final frame interval and both selected streams. */
	if (!g_cancellable_is_cancelled (engine->cancellable) && draining
	    && wait_for_video_time (engine, last_frame_time)) {
		g_mutex_lock(&engine->state_lock);
		engine->video_done=TRUE;
		gboolean complete=engine->audio_index<0 || g_atomic_int_get(&engine->audio_done);
		g_mutex_unlock(&engine->state_lock);
		if(complete)signal_end(engine);
	}
	if(failed)signal_playback_error(engine,NULL);
	return NULL;
}

/*** Public API ***/

static void
clear_decoders (MatineeEngine *engine)
{
	if (engine->audio_context)
		avcodec_free_context (&engine->audio_context);
	if (engine->video_context)
		avcodec_free_context (&engine->video_context);
	if (engine->audio_frame)
		av_frame_free (&engine->audio_frame);
	if (engine->video_frame)
		av_frame_free (&engine->video_frame);
	if (engine->resampler)
		swr_free (&engine->resampler);
	if (engine->scaler)
		sws_freeContext (engine->scaler);
	engine->scaler = NULL;
	engine->audio_index = -1;
	engine->video_index = -1;
	engine->video_width = 0;
	engine->video_height = 0;
}

MatineeEngine *
matinee_engine_new (void)
{
	MatineeEngine *engine = g_object_new (MATINEE_TYPE_ENGINE, NULL);
	return engine;
}

static void
matinee_engine_dispose (GObject *object)
{
	MatineeEngine *engine = MATINEE_ENGINE (object);
	stop_threads (engine);
	if (engine->format != NULL)
	{
		avformat_close_input (&engine->format);
	}
	g_clear_object (&engine->cancellable);
	G_OBJECT_CLASS (matinee_engine_parent_class)->dispose (object);
}

static void
matinee_engine_finalize (GObject *object)
{
	MatineeEngine *engine = MATINEE_ENGINE (object);
	if (engine->audio_context) avcodec_free_context (&engine->audio_context);
	if (engine->video_context) avcodec_free_context (&engine->video_context);
	if (engine->audio_frame) av_frame_free (&engine->audio_frame);
	if (engine->video_frame) av_frame_free (&engine->video_frame);
	sws_freeContext (engine->scaler);
	engine->scaler = NULL;
	if (engine->resampler) swr_free (&engine->resampler);
	if (engine->pcm) snd_pcm_close (engine->pcm);
	g_clear_object (&engine->pending_texture);
	g_clear_pointer (&engine->video_packets, g_async_queue_unref);
	g_clear_pointer (&engine->main_context, g_main_context_unref);
	g_free (engine->fft_input);
	g_free (engine->fft_output);
	matinee_fft_free (engine->fft);
	g_free (engine->band_smoothed);
	g_free (engine->title_cache);
	g_free (engine->artist_cache);
	g_free (engine->playback_error);
	g_mutex_clear (&engine->state_lock);
	g_cond_clear (&engine->playback_changed);
	g_mutex_clear (&engine->clock_lock);
	g_mutex_clear (&engine->bands_lock);
	g_mutex_clear (&engine->video_queue_lock);
	g_mutex_clear (&engine->frame_lock);
	G_OBJECT_CLASS (matinee_engine_parent_class)->finalize (object);
}

static void
matinee_engine_class_init (MatineeEngineClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = matinee_engine_dispose;
	object_class->finalize = matinee_engine_finalize;

	signals[SIGNAL_VIDEO_FRAME] =
		g_signal_new ("video-frame", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, GDK_TYPE_TEXTURE);
	signals[SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);
	signals[SIGNAL_FINISHED] =
		g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
matinee_engine_init (MatineeEngine *engine)
{
	g_mutex_init (&engine->state_lock);
	g_cond_init (&engine->playback_changed);
	g_mutex_init (&engine->clock_lock);
	g_mutex_init (&engine->bands_lock);
	g_mutex_init (&engine->frame_lock);
	g_mutex_init (&engine->video_queue_lock);
	g_mutex_init (&engine->video_queue_lock);
	engine->audio_index = -1;
	engine->video_index = -1;
	engine->state = MATINEE_STATE_IDLE;
	engine->last_video_frame_us = -1;
	engine->volume = 0.8;
	g_atomic_int_set (&engine->spectrum_enabled, TRUE);
	engine->main_context = g_main_context_ref_thread_default ();
	engine->cancellable = g_cancellable_new ();
	engine->fft_input = g_new0 (float, MATINEE_FFT_SIZE);
	engine->fft_output = g_new0 (float, MATINEE_FFT_SIZE / 2);
	engine->fft = matinee_fft_new (MATINEE_FFT_SIZE);
	engine->band_smoothed = g_new0 (float, MATINEE_BANDS);
	engine->video_packets = g_async_queue_new ();
}

gboolean
matinee_engine_open (MatineeEngine *engine, const char *uri, GError **error)
{
	const AVCodec *audio_codec;
	const AVCodec *video_codec;

	g_return_val_if_fail (MATINEE_IS_ENGINE (engine), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	stop_threads (engine);
	if (g_atomic_int_get (&engine->io_cancelled))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Opening the media file was cancelled");
		return FALSE;
	}
	clear_decoders (engine);
	g_mutex_lock(&engine->state_lock);
	g_clear_pointer(&engine->playback_error,g_free);
	g_mutex_unlock(&engine->state_lock);
	if (engine->format != NULL)
		avformat_close_input (&engine->format);

	engine->format = avformat_alloc_context ();
	if (engine->format == NULL)
	{
		g_set_error (error, MATINEE_ENGINE_ERROR, 0, "Could not allocate the media reader");
		return FALSE;
	}
	engine->format->interrupt_callback.callback = interrupt_media_io;
	engine->format->interrupt_callback.opaque = engine;
	g_atomic_int_set (&engine->io_deadline_seconds,
	                  (gint) (g_get_monotonic_time () / G_USEC_PER_SEC) + MATINEE_IO_TIMEOUT_US / G_USEC_PER_SEC);
	int open_result = avformat_open_input (&engine->format, uri, NULL, NULL);
	g_atomic_int_set (&engine->io_deadline_seconds, 0);
	if (open_result < 0)
	{
		engine->format = NULL;
		g_set_error (error, MATINEE_ENGINE_ERROR, 0,
		             g_cancellable_is_cancelled (engine->cancellable)
		             ? "Opening the media file was cancelled"
		             : open_result == AVERROR_EXIT
	               ? "Opening the media file timed out"
	               : "Could not open the media file");
		return FALSE;
	}
	g_atomic_int_set (&engine->io_deadline_seconds,
	                  (gint) (g_get_monotonic_time () / G_USEC_PER_SEC) + MATINEE_IO_TIMEOUT_US / G_USEC_PER_SEC);
	int info_result = avformat_find_stream_info (engine->format, NULL);
	g_atomic_int_set (&engine->io_deadline_seconds, 0);
	if (info_result < 0)
	{
		g_set_error (error, MATINEE_ENGINE_ERROR, 0,
		             g_cancellable_is_cancelled (engine->cancellable)
		             ? "Reading media information was cancelled"
		             : info_result == AVERROR_EXIT
	               ? "Reading media information timed out"
	               : "Could not read the media stream");
		goto fail;
	}
	engine->audio_index = av_find_best_stream (engine->format, AVMEDIA_TYPE_AUDIO, -1, -1, &audio_codec, 0);
	engine->video_index = av_find_best_stream (engine->format, AVMEDIA_TYPE_VIDEO, -1, -1, &video_codec, 0);
	if (engine->audio_index < 0 && engine->video_index < 0)
	{
		g_set_error (error, MATINEE_ENGINE_ERROR, 0, "The file contains no playable streams");
		goto fail;
	}
	if (engine->audio_index >= 0)
	{
		engine->audio_context = avcodec_alloc_context3 (audio_codec);
		if (avcodec_parameters_to_context (engine->audio_context,
		                                    engine->format->streams[engine->audio_index]->codecpar) < 0
		    || avcodec_open2 (engine->audio_context, audio_codec, NULL) < 0)
		{
			g_set_error (error, MATINEE_ENGINE_ERROR, 0, "Could not open the audio decoder");
			goto fail;
		}
		engine->audio_frame = av_frame_alloc ();
	}
	if (engine->video_index >= 0)
	{
		engine->video_context = avcodec_alloc_context3 (video_codec);
		if (avcodec_parameters_to_context (engine->video_context,
		                                    engine->format->streams[engine->video_index]->codecpar) < 0
		    || avcodec_open2 (engine->video_context, video_codec, NULL) < 0)
		{
			g_set_error (error, MATINEE_ENGINE_ERROR, 0, "Could not open the video decoder");
			goto fail;
		}
		engine->video_frame = av_frame_alloc ();
	}
	engine->start_time = engine->format->start_time == AV_NOPTS_VALUE
	                   ? 0 : engine->format->start_time;
	engine->seek_target_seconds = 0.0;
	engine->discard_audio_preroll = FALSE;
	engine->discard_video_preroll = FALSE;
	g_mutex_lock (&engine->clock_lock);
	engine->clock = 0.0;
	engine->audio_clock_base = 0.0;
	engine->clock_running = FALSE;
	g_mutex_unlock (&engine->clock_lock);
	set_state (engine, MATINEE_STATE_IDLE);
	return TRUE;

fail:
	g_atomic_int_set (&engine->io_deadline_seconds, 0);
	if (engine->format != NULL)
		avformat_close_input (&engine->format);
	return FALSE;
}

void
matinee_engine_cancel_io (MatineeEngine *engine)
{
	g_return_if_fail (MATINEE_IS_ENGINE (engine));
	g_atomic_int_set (&engine->io_cancelled, TRUE);
	g_cancellable_cancel (engine->cancellable);
	g_mutex_lock (&engine->state_lock);
	g_cond_broadcast (&engine->playback_changed);
	g_mutex_unlock (&engine->state_lock);
}

static void
set_state (MatineeEngine *engine, MatineeState state)
{
	g_mutex_lock (&engine->state_lock);
	if (engine->state != state)
	{
		engine->state = state;
		g_mutex_unlock (&engine->state_lock);
		g_signal_emit (engine, signals[SIGNAL_STATE_CHANGED], 0, (gint) state);
	}
	else
	{
		g_mutex_unlock (&engine->state_lock);
	}
}

static void
stop_threads (MatineeEngine *engine)
{
	if (engine->audio_thread != NULL || engine->video_thread != NULL)
		g_cancellable_cancel (engine->cancellable);
	g_mutex_lock(&engine->state_lock);
	g_cond_broadcast(&engine->playback_changed);
	g_mutex_unlock(&engine->state_lock);

	/* Drop queued samples before joining: snd_pcm_drain() waits for the
	 * device to play everything, which used to freeze Stop, seek and track
	 * changes on the GTK thread. */
	if (engine->pcm != NULL)
		snd_pcm_drop (engine->pcm);
	if (engine->audio_thread != NULL)
	{
		g_thread_join (engine->audio_thread);
		engine->audio_thread = NULL;
	}
	if (engine->video_thread != NULL)
	{
		g_thread_join (engine->video_thread);
		engine->video_thread = NULL;
	}
	if (engine->cancellable != NULL)
		g_cancellable_reset (engine->cancellable);
	if (engine->pcm != NULL)
	{
		snd_pcm_close (engine->pcm);
		engine->pcm = NULL;
	}
	/* A new device may negotiate a different rate, and seek/stop must not
	 * reuse delayed samples from the previous playback position. */
	if (engine->resampler != NULL)
		swr_free (&engine->resampler);
	clear_video_packets (engine);
	g_mutex_lock (&engine->state_lock);
	cancel_ui_delivery (&engine->finished_source);
	cancel_ui_delivery (&engine->audio_error_source);
	g_mutex_unlock (&engine->state_lock);
	g_mutex_lock (&engine->frame_lock);
	g_clear_object (&engine->pending_texture);
	cancel_ui_delivery (&engine->video_source);
	g_mutex_unlock (&engine->frame_lock);
	engine->frames_written = 0;
	engine->audio_clock_base = 0.0;
	g_mutex_lock (&engine->bands_lock);
	engine->fft_filled = 0;
	g_mutex_unlock (&engine->bands_lock);
	g_atomic_int_set(&engine->audio_done,FALSE);
	g_mutex_lock(&engine->state_lock);engine->video_done=FALSE;g_mutex_unlock(&engine->state_lock);
	g_mutex_lock (&engine->clock_lock);
	engine->clock = playback_position (engine);
	engine->clock_running = FALSE;
	g_mutex_unlock (&engine->clock_lock);
}

void
matinee_engine_play (MatineeEngine *engine)
{
	g_return_if_fail (MATINEE_IS_ENGINE (engine));
	if (engine->format == NULL)
		return;
	if (matinee_engine_state (engine) == MATINEE_STATE_ENDED || matinee_engine_state (engine) == MATINEE_STATE_ERROR)
	{
		matinee_engine_stop (engine);
		if (matinee_engine_state (engine) == MATINEE_STATE_ERROR)
			return;
	}
	g_mutex_lock(&engine->state_lock);
	g_clear_pointer(&engine->playback_error,g_free);
	g_mutex_unlock(&engine->state_lock);
	if (engine->audio_index >= 0)
	{
		if (!open_audio_output (engine))
		{
			set_playback_error(engine,"Could not open or configure the ALSA playback device.");
			set_state (engine, MATINEE_STATE_ERROR);
			return;
		}
		if (!prepare_resampler (engine))
		{
			set_playback_error(engine,"Could not initialize audio resampling for this stream.");
			set_state (engine, MATINEE_STATE_ERROR);
			return;
		}
	}
	if (engine->pcm != NULL)
		snd_pcm_pause (engine->pcm, 0);
	g_mutex_lock (&engine->clock_lock);
	if ((engine->audio_index < 0 || g_atomic_int_get(&engine->audio_done)) && !engine->clock_running)
	{
		engine->clock_anchor = g_get_monotonic_time ();
		engine->clock_running = TRUE;
	}
	g_mutex_unlock (&engine->clock_lock);
	g_mutex_lock (&engine->state_lock);
	engine->playing = TRUE;
	engine->flushing = FALSE;
	engine->reached_end = FALSE;
	g_cond_broadcast(&engine->playback_changed);
	g_mutex_unlock (&engine->state_lock);
	if (engine->audio_thread == NULL && engine->audio_index >= 0)
		engine->audio_thread = g_thread_new ("matinee-audio", audio_thread_func, engine);
	if (engine->video_thread == NULL && engine->video_index >= 0)
		engine->video_thread = g_thread_new ("matinee-video", video_thread_func, engine);
	set_state (engine, MATINEE_STATE_PLAYING);
}

void
matinee_engine_pause (MatineeEngine *engine)
{
	g_return_if_fail (MATINEE_IS_ENGINE (engine));
	g_mutex_lock (&engine->clock_lock);
	engine->clock = playback_position (engine);
	engine->clock_running = FALSE;
	g_mutex_unlock (&engine->clock_lock);
	g_mutex_lock (&engine->state_lock);
	engine->playing = FALSE;
	g_mutex_unlock (&engine->state_lock);
	if (engine->pcm != NULL)
		snd_pcm_pause (engine->pcm, 1);
	set_state (engine, MATINEE_STATE_PAUSED);
}

void
matinee_engine_stop (MatineeEngine *engine)
{
	g_return_if_fail (MATINEE_IS_ENGINE (engine));
	stop_threads (engine);
	if (engine->format != NULL &&
	    av_seek_frame (engine->format, -1, engine->start_time, AVSEEK_FLAG_BACKWARD) < 0)
	{
		g_mutex_lock (&engine->state_lock);
		engine->playing = FALSE;
		engine->flushing = FALSE;
		engine->reached_end = FALSE;
		g_mutex_unlock (&engine->state_lock);
		set_state (engine, MATINEE_STATE_ERROR);
		return;
	}
	g_mutex_lock (&engine->state_lock);
	engine->playing = FALSE;
	engine->flushing = FALSE;
	engine->reached_end = FALSE;
	g_mutex_unlock (&engine->state_lock);
	if (engine->audio_context) avcodec_flush_buffers (engine->audio_context);
	if (engine->video_context) avcodec_flush_buffers (engine->video_context);
	g_mutex_lock (&engine->clock_lock);
	engine->clock = 0.0;
	engine->audio_clock_base = 0.0;
	g_mutex_unlock (&engine->clock_lock);
	engine->seek_target_seconds = 0.0;
	engine->discard_audio_preroll = FALSE;
	engine->discard_video_preroll = FALSE;
	set_state (engine, MATINEE_STATE_IDLE);
}

gboolean
matinee_engine_seek_fraction (MatineeEngine *engine, double fraction)
{
	gint64 target;
	MatineeState previous;

	g_return_val_if_fail (MATINEE_IS_ENGINE (engine), FALSE);
	if (engine->format == NULL || engine->format->duration <= 0 || !isfinite (fraction))
		return FALSE;
	previous = matinee_engine_state (engine);
	fraction = CLAMP (fraction, 0.0, 1.0);
	target = engine->start_time + (gint64) (fraction * engine->format->duration);
	stop_threads (engine);
	if (av_seek_frame (engine->format, -1, target, AVSEEK_FLAG_BACKWARD) < 0)
	{
		g_mutex_lock (&engine->state_lock);
		engine->playing = FALSE;
		g_mutex_unlock (&engine->state_lock);
		set_state (engine, MATINEE_STATE_ERROR);
		return FALSE;
	}
	if (engine->audio_context) avcodec_flush_buffers (engine->audio_context);
	if (engine->video_context) avcodec_flush_buffers (engine->video_context);
	g_mutex_lock (&engine->clock_lock);
	engine->clock = (double) (target - engine->start_time) / AV_TIME_BASE;
	engine->audio_clock_base = engine->clock;
	g_mutex_unlock (&engine->clock_lock);
	engine->seek_target_seconds = engine->clock;
	engine->discard_audio_preroll = engine->audio_index >= 0;
	engine->discard_video_preroll = engine->video_index >= 0;
	g_mutex_lock (&engine->state_lock);
	engine->audio_preroll_samples_discarded = 0;
	g_mutex_unlock (&engine->state_lock);
	/* Play's Ended path intentionally restarts at zero; an explicit seek
	 * has already positioned the demuxer and must not take that path. */
	if (matinee_engine_state (engine) == MATINEE_STATE_ENDED)
		set_state (engine, MATINEE_STATE_IDLE);
	if (previous != MATINEE_STATE_PAUSED)
		matinee_engine_play (engine);
	return TRUE;
}

void
matinee_engine_set_volume (MatineeEngine *engine, double volume)
{
	g_return_if_fail (MATINEE_IS_ENGINE (engine));
	g_mutex_lock (&engine->state_lock);
	engine->volume = isnan(volume) ? 0.0 : CLAMP (volume, 0.0, 1.0);
	g_mutex_unlock (&engine->state_lock);
}

void
matinee_engine_set_spectrum_enabled (MatineeEngine *engine, gboolean enabled)
{
	g_return_if_fail (MATINEE_IS_ENGINE (engine));
	g_atomic_int_set (&engine->spectrum_enabled, enabled);
}

MatineeState
matinee_engine_state (MatineeEngine *engine)
{
	MatineeState state;
	g_mutex_lock (&engine->state_lock);
	state = engine->state;
	g_mutex_unlock (&engine->state_lock);
	return state;
}

char *
matinee_engine_dup_error_message (MatineeEngine *engine)
{
	char *message;
	g_mutex_lock(&engine->state_lock);
	message=g_strdup(engine->playback_error);
	g_mutex_unlock(&engine->state_lock);
	return message;
}

double
matinee_engine_position (MatineeEngine *engine)
{
	double clock;
	g_mutex_lock (&engine->clock_lock);
	clock = playback_position (engine);
	g_mutex_unlock (&engine->clock_lock);
	return clock;
}

double
matinee_engine_duration (MatineeEngine *engine)
{
	if (engine->format == NULL)
		return 0.0;
	return (double) engine->format->duration / AV_TIME_BASE;
}

gboolean
matinee_engine_has_video (MatineeEngine *engine)
{
	return engine->video_index >= 0;
}

static char *
metadata_for (MatineeEngine *engine, const char *key)
{
	AVDictionaryEntry *entry;
	char *value = NULL;

	if (engine->format != NULL)
	{
		entry = av_dict_get (engine->format->metadata, key, NULL, 0);
		if (entry != NULL && entry->value != NULL)
			value = g_strdup (entry->value);
	}
	return value;
}

const char *
matinee_engine_title (MatineeEngine *engine)
{
	char *value = metadata_for (engine, "title");
	if (value == NULL)
		value = g_path_get_basename (engine->format ? engine->format->url : "");
	g_free (engine->title_cache);
	engine->title_cache = value;
	return engine->title_cache;
}

const char *
matinee_engine_artist (MatineeEngine *engine)
{
	g_free (engine->artist_cache);
	engine->artist_cache = metadata_for (engine, "artist");
	return engine->artist_cache != NULL ? engine->artist_cache : "";
}

void
matinee_engine_get_bands (MatineeEngine *engine, MatineeBands *out_bands)
{
	g_mutex_lock (&engine->bands_lock);
	*out_bands = engine->bands;
	g_mutex_unlock (&engine->bands_lock);
}
