/*
 * decoder.c — FFmpeg video decoder thread for live-wallpaper.
 *
 * This module handles all video decoding:
 *   1. Opens the input file with libavformat
 *   2. Finds the first video stream (audio is completely ignored)
 *   3. Opens the codec with libavcodec
 *   4. Initializes libswscale to convert any pixel format → YUV420P
 *   5. Runs a decode loop on a dedicated thread, pushing frames to the queue
 *   6. On EOF, seeks back to timestamp 0 for infinite seamless looping
 *
 * The decode loop respects two atomic flags from AppState:
 *   - quit:   immediately exit the thread
 *   - paused: sleep on a condvar until resumed
 *
 * Frame pacing: the decoder uses the video's time_base and PTS to compute
 * a per-frame delay, sleeping between frames to match the original framerate.
 * The bounded frame queue (3 slots) also provides natural backpressure.
 */

#include "decoder.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

/* FFmpeg headers */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal decoder context — lives on the decoder thread.
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    AVFormatContext  *fmt_ctx;
    AVCodecContext   *codec_ctx;
    struct SwsContext *sws_ctx;
    int               video_stream_idx;

    /* Destination frame (YUV420P) */
    AVFrame          *frame_raw;    /* as-decoded frame                */
    AVFrame          *frame_yuv;    /* converted to YUV420P            */
    uint8_t          *yuv_buffer;   /* pixel buffer for frame_yuv      */

    /* Timing */
    double            time_base;    /* seconds per PTS tick            */
    double            frame_delay;  /* 1.0 / fps                       */
} DecoderCtx;

/* ─────────────────────────────────────────────────────────────────────────────
 * Precise sleep using clock_nanosleep (avoids busy-waiting).
 * ──────────────────────────────────────────────────────────────────────────── */
static void sleep_seconds(double seconds)
{
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec  = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * AVIOInterruptCB callback — returns 1 to abort blocking FFmpeg I/O when the
 * application is shutting down.  This prevents pthread_join() from hanging
 * while av_read_frame() blocks on a slow filesystem or network stream.
 * ──────────────────────────────────────────────────────────────────────────── */
static int interrupt_callback(void *opaque)
{
    AppState *state = (AppState *)opaque;
    return atomic_load(&state->quit) ? 1 : 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Open the video file and initialize all FFmpeg contexts.
 * Returns 0 on success, -1 on failure.
 * ──────────────────────────────────────────────────────────────────────────── */
static int decoder_ctx_open(DecoderCtx *d, AppState *state, const char *filepath)
{
    memset(d, 0, sizeof(*d));

    /* Pre-allocate the format context so we can register the interrupt
     * callback BEFORE avformat_open_input() performs any blocking I/O. */
    d->fmt_ctx = avformat_alloc_context();
    if (!d->fmt_ctx) return -1;
    d->fmt_ctx->interrupt_callback.callback = interrupt_callback;
    d->fmt_ctx->interrupt_callback.opaque   = state;

    /* Open input file */
    int ret = avformat_open_input(&d->fmt_ctx, filepath, NULL, NULL);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "[decoder] failed to open '%s': %s\n", filepath, errbuf);
        return -1;
    }

    /* Read stream info */
    if (avformat_find_stream_info(d->fmt_ctx, NULL) < 0) {
        fprintf(stderr, "[decoder] failed to find stream info\n");
        return -1;
    }

    /* Find the first video stream (skip audio entirely). */
    d->video_stream_idx = -1;
    for (unsigned i = 0; i < d->fmt_ctx->nb_streams; i++) {
        if (d->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            d->video_stream_idx = (int)i;
            break;
        }
    }
    if (d->video_stream_idx < 0) {
        fprintf(stderr, "[decoder] no video stream found in '%s'\n", filepath);
        return -1;
    }

    AVStream *vstream = d->fmt_ctx->streams[d->video_stream_idx];
    AVCodecParameters *codecpar = vstream->codecpar;

    /* Find and open the decoder */
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "[decoder] unsupported codec: %d\n", codecpar->codec_id);
        return -1;
    }

    d->codec_ctx = avcodec_alloc_context3(codec);
    if (!d->codec_ctx) return -1;

    if (avcodec_parameters_to_context(d->codec_ctx, codecpar) < 0) return -1;

    /* Enable multi-threaded decoding for performance */
    d->codec_ctx->thread_count = 0;  /* auto-detect thread count */

    if (avcodec_open2(d->codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "[decoder] failed to open codec\n");
        return -1;
    }

    int w = d->codec_ctx->width;
    int h = d->codec_ctx->height;

    /* Initialize the software scaler: source format → YUV420P
     *
     * Even if the source is already YUV420P, sws_scale handles the copy
     * and any necessary stride alignment.  SWS_FAST_BILINEAR is used as
     * we are doing a same-size format conversion (no resize), so quality
     * is equivalent to SWS_BILINEAR but with better throughput. */
    d->sws_ctx = sws_getContext(
        w, h, d->codec_ctx->pix_fmt,   /* source */
        w, h, AV_PIX_FMT_YUV420P,      /* destination */
        SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!d->sws_ctx) {
        fprintf(stderr, "[decoder] failed to create swscale context\n");
        return -1;
    }

    /* Allocate frames */
    d->frame_raw = av_frame_alloc();
    d->frame_yuv = av_frame_alloc();
    if (!d->frame_raw || !d->frame_yuv) return -1;

    /* Allocate pixel buffer for the YUV420P destination frame */
    int buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 1);
    d->yuv_buffer = (uint8_t *)av_malloc((size_t)buf_size);
    if (!d->yuv_buffer) return -1;

    av_image_fill_arrays(d->frame_yuv->data, d->frame_yuv->linesize,
                         d->yuv_buffer, AV_PIX_FMT_YUV420P, w, h, 1);

    /* Compute timing info */
    d->time_base = av_q2d(vstream->time_base);

    /* Compute FPS from the stream's average framerate */
    AVRational fps_r = vstream->avg_frame_rate;
    if (fps_r.num > 0 && fps_r.den > 0) {
        state->video_fps = av_q2d(fps_r);
    } else {
        /* Fallback: use r_frame_rate or assume 30 fps */
        fps_r = vstream->r_frame_rate;
        state->video_fps = (fps_r.num > 0 && fps_r.den > 0)
                           ? av_q2d(fps_r) : 30.0;
    }
    d->frame_delay = 1.0 / state->video_fps;

    /* Store video dimensions in app state (atomic to prevent race with GUI thread) */
    atomic_store(&state->video_width,  w);
    atomic_store(&state->video_height, h);

    fprintf(stderr, "[decoder] opened '%s': %dx%d @ %.2f fps, codec=%s\n",
            filepath, w, h, state->video_fps, codec->name);

    /* Signal the main thread that metadata is ready.
     * Set this LAST, after video_width/height/fps are all populated. */
    atomic_store(&state->decoder_ready, true);

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Close all FFmpeg contexts and free memory.
 * ──────────────────────────────────────────────────────────────────────────── */
static void decoder_ctx_close(DecoderCtx *d)
{
    if (d->sws_ctx)   { sws_freeContext(d->sws_ctx);    d->sws_ctx = NULL; }
    if (d->frame_raw) { av_frame_free(&d->frame_raw); }
    if (d->frame_yuv) { av_frame_free(&d->frame_yuv); }
    if (d->yuv_buffer){ av_free(d->yuv_buffer);         d->yuv_buffer = NULL; }
    if (d->codec_ctx) { avcodec_free_context(&d->codec_ctx); }
    if (d->fmt_ctx)   { avformat_close_input(&d->fmt_ctx); }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Decoder thread function.
 *
 * Main loop:
 *   1. Read a packet (av_read_frame)
 *   2. If it's from the video stream, send to decoder
 *   3. Receive decoded frames
 *   4. Convert to YUV420P via sws_scale
 *   5. Push to frame queue (blocks if full — natural backpressure)
 *   6. Sleep based on frame delay for correct pacing
 *   7. On EOF: seek to timestamp 0 and continue (infinite loop)
 *   8. On pause: sleep on condvar until resumed
 *   9. On quit: exit cleanly
 *
 * The infinite loop logic:
 *   When av_read_frame() returns AVERROR_EOF, we've reached the end of the
 *   video file.  To loop seamlessly:
 *     a) Flush the codec (send a NULL packet to get remaining buffered frames)
 *     b) Seek back to the very beginning: av_seek_frame(fmt_ctx, -1, 0, ...)
 *     c) Flush the codec's internal buffers: avcodec_flush_buffers()
 *     d) Continue the loop — the next av_read_frame() reads from the start
 *   This gives seamless, infinite playback without reopening the file.
 * ──────────────────────────────────────────────────────────────────────────── */
static void *decoder_thread_func(void *arg)
{
    AppState *state = (AppState *)arg;
    DecoderCtx d;

    if (decoder_ctx_open(&d, state, state->video_path) < 0) {
        fprintf(stderr, "[decoder] failed to open video, thread exiting\n");
        atomic_store(&state->playing, false);
        return NULL;
    }

    atomic_store(&state->playing, true);

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        decoder_ctx_close(&d);
        atomic_store(&state->playing, false);
        return NULL;
    }

    double frame_timer = 0.0;
    int64_t start_time = av_gettime_relative();

    while (!atomic_load(&state->quit)) {

        /* ── Handle pause ──
         * When paused, the decoder sleeps on a condition variable.
         * This uses zero CPU while paused.  decoder_resume() signals
         * the condvar to wake us up. */
        if (atomic_load(&state->paused)) {
            pthread_mutex_lock(&state->pause_mutex);
            while (atomic_load(&state->paused) && !atomic_load(&state->quit)) {
                pthread_cond_wait(&state->pause_cond, &state->pause_mutex);
            }
            pthread_mutex_unlock(&state->pause_mutex);

            /* Reset timer after unpause so we don't fast-forward */
            start_time = av_gettime_relative();
            frame_timer = 0.0;

            if (atomic_load(&state->quit)) break;
        }

        /* ── Read a packet from the container ── */
        int ret = av_read_frame(d.fmt_ctx, pkt);

        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(d.fmt_ctx->pb)) {
                /*
                 * ═══════════════════════════════════════════════════════════
                 * INFINITE LOOP LOGIC
                 *
                 * We've reached the end of the video file.  To loop:
                 *   1. Flush the decoder to get any remaining buffered frames
                 *   2. Seek the format context back to timestamp 0
                 *   3. Flush the codec's internal state
                 *   4. Continue the decode loop from the beginning
                 *
                 * This is more efficient than closing and reopening the file,
                 * and provides seamless looping with no visible gap.
                 * ═══════════════════════════════════════════════════════════
                 */

                /* Step 1: Flush decoder — send NULL packet to drain buffered frames */
                avcodec_send_packet(d.codec_ctx, NULL);
                while (avcodec_receive_frame(d.codec_ctx, d.frame_raw) == 0) {
                    if (atomic_load(&state->quit)) {
                        av_frame_unref(d.frame_raw);
                        break;
                    }

                    sws_scale(d.sws_ctx,
                              (const uint8_t *const *)d.frame_raw->data,
                              d.frame_raw->linesize,
                              0, d.codec_ctx->height,
                              d.frame_yuv->data, d.frame_yuv->linesize);

                    bool pushed = frame_queue_push(&state->queue,
                                          d.frame_yuv->data[0], d.frame_yuv->linesize[0],
                                          d.frame_yuv->data[1], d.frame_yuv->data[2],
                                          d.frame_yuv->linesize[1],
                                          d.codec_ctx->width, d.codec_ctx->height);
                    av_frame_unref(d.frame_raw);
                    if (!pushed) {
                        break;  /* queue aborted */
                    }
                }

                if (atomic_load(&state->quit)) break;

                /* Step 2: Seek back to the beginning of the file */
                av_seek_frame(d.fmt_ctx, d.video_stream_idx, 0,
                              AVSEEK_FLAG_BACKWARD);

                /* Step 3: Flush the codec's internal buffers */
                avcodec_flush_buffers(d.codec_ctx);

                /* Reset frame timer for the new loop iteration */
                start_time = av_gettime_relative();
                frame_timer = 0.0;

                continue;
            }

            /* Some other read error — skip and continue */
            fprintf(stderr, "[decoder] read error: %d, continuing\n", ret);
            continue;
        }

        /* Skip non-video packets (audio, subtitles, etc.) */
        if (pkt->stream_index != d.video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        /* ── Send packet to decoder ──
         * avcodec_send_packet() returns EAGAIN when the codec's internal buffer
         * is full and must be drained first.  We do NOT unref the packet until
         * we have successfully sent it. */
        while (!atomic_load(&state->quit)) {
            ret = avcodec_send_packet(d.codec_ctx, pkt);
            if (ret == 0) {
                /* Packet accepted — release it */
                av_packet_unref(pkt);
                break;
            } else if (ret == AVERROR(EAGAIN)) {
                /* Decoder full — drain one frame then retry */
                ret = avcodec_receive_frame(d.codec_ctx, d.frame_raw);
                if (ret < 0) {
                    av_packet_unref(pkt);
                    break;
                }
                /* Process the drained frame inline (same as the receive loop below) */
                sws_scale(d.sws_ctx,
                          (const uint8_t *const *)d.frame_raw->data,
                          d.frame_raw->linesize,
                          0, d.codec_ctx->height,
                          d.frame_yuv->data, d.frame_yuv->linesize);
                frame_queue_push(&state->queue,
                                 d.frame_yuv->data[0], d.frame_yuv->linesize[0],
                                 d.frame_yuv->data[1], d.frame_yuv->data[2],
                                 d.frame_yuv->linesize[1],
                                 d.codec_ctx->width, d.codec_ctx->height);
                av_frame_unref(d.frame_raw);
                /* Loop to retry avcodec_send_packet */
            } else {
                fprintf(stderr, "[decoder] send_packet error: %d\n", ret);
                av_packet_unref(pkt);
                break;
            }
        }

        /* ── Receive decoded frames ── */
        while (!atomic_load(&state->quit)) {
            ret = avcodec_receive_frame(d.codec_ctx, d.frame_raw);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                fprintf(stderr, "[decoder] receive_frame error: %d\n", ret);
                break;
            }

            /* ── Convert to YUV420P ──
             * sws_scale handles format conversion and stride alignment.
             * If the source is already YUV420P, this is essentially a memcpy. */
            sws_scale(d.sws_ctx,
                      (const uint8_t *const *)d.frame_raw->data,
                      d.frame_raw->linesize,
                      0, d.codec_ctx->height,
                      d.frame_yuv->data, d.frame_yuv->linesize);

            /* ── Frame pacing ──
             * Compute how long we should wait before displaying this frame.
             * Uses the video's PTS (presentation timestamp) to maintain the
             * original playback speed. */
            double pts = 0.0;
            if (d.frame_raw->pts != AV_NOPTS_VALUE) {
                pts = (double)d.frame_raw->pts * d.time_base;
            } else {
                pts = frame_timer;
            }

            double elapsed = (double)(av_gettime_relative() - start_time) / 1e6;
            double delay = pts - elapsed;
            if (delay > 0.0 && delay < 1.0) {
                sleep_seconds(delay);
            }

            frame_timer = pts + d.frame_delay;

            /* ── Push to frame queue ──
             * This blocks if the queue is full (3 frames), providing natural
             * backpressure.  Returns false if frame_queue_abort() was called. */
            if (!frame_queue_push(&state->queue,
                                  d.frame_yuv->data[0], d.frame_yuv->linesize[0],
                                  d.frame_yuv->data[1], d.frame_yuv->data[2],
                                  d.frame_yuv->linesize[1],
                                  d.codec_ctx->width, d.codec_ctx->height)) {
                break;  /* queue aborted — shutting down */
            }

            av_frame_unref(d.frame_raw);
        }
    }

    av_packet_free(&pkt);
    decoder_ctx_close(&d);
    atomic_store(&state->playing, false);

    fprintf(stderr, "[decoder] thread exiting\n");
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

int decoder_start(AppState *state, const char *filepath)
{
    /* Safety: stop any existing decoder first */
    if (state->decoder_running) {
        decoder_stop(state);
    }

    /* Store the video path */
    snprintf(state->video_path, LW_MAX_PATH, "%s", filepath);

    /* Reset state for new video */
    atomic_store(&state->paused, false);
    atomic_store(&state->decoder_ready, false);
    frame_queue_reset(&state->queue);

    /* Spawn the decoder thread */
    int err = pthread_create(&state->decoder_tid, NULL,
                             decoder_thread_func, state);
    if (err != 0) {
        fprintf(stderr, "[decoder] failed to create thread: %d\n", err);
        return -1;
    }

    state->decoder_running = true;
    return 0;
}

void decoder_stop(AppState *state)
{
    if (!state->decoder_running) return;

    fprintf(stderr, "[decoder] stopping...\n");

    /* Signal the decoder to quit */
    atomic_store(&state->quit, true);
    atomic_store(&state->paused, false);

    /* Wake the decoder if it's paused */
    pthread_mutex_lock(&state->pause_mutex);
    pthread_cond_signal(&state->pause_cond);
    pthread_mutex_unlock(&state->pause_mutex);

    /* Abort the frame queue to unblock any push() call */
    frame_queue_abort(&state->queue);

    /* Wait for the thread to finish */
    pthread_join(state->decoder_tid, NULL);
    state->decoder_running = false;

    /* Flush remaining frames from the queue */
    frame_queue_flush(&state->queue);

    fprintf(stderr, "[decoder] stopped\n");
}

void decoder_pause(AppState *state)
{
    atomic_store(&state->paused, true);
}

void decoder_resume(AppState *state)
{
    atomic_store(&state->paused, false);

    /* Wake the decoder thread from its condvar wait */
    pthread_mutex_lock(&state->pause_mutex);
    pthread_cond_signal(&state->pause_cond);
    pthread_mutex_unlock(&state->pause_mutex);
}
