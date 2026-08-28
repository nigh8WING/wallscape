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
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal decoder context — lives on the decoder thread.
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    AVFormatContext   *fmt_ctx;
    AVCodecContext    *codec_ctx;
    struct SwsContext *sws_ctx;
    int                video_stream_idx;

    /* Hardware acceleration */
    AVBufferRef       *hw_device_ctx;
    enum AVPixelFormat hw_pix_fmt;
    bool               hw_accel_enabled;
    AVFrame           *frame_sw;        /* software frame after GPU transfer */

    /* Destination frame (YUV420P) */
    AVFrame           *frame_raw;       /* as-decoded frame (may be HW surface) */
    AVFrame           *frame_yuv;       /* converted to YUV420P            */
    uint8_t           *yuv_buffer;      /* pixel buffer for frame_yuv      */

    /* Timing */
    double             time_base;       /* seconds per PTS tick            */
    double             frame_delay;     /* 1.0 / fps                       */
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
 * application is shutting down.
 * ──────────────────────────────────────────────────────────────────────────── */
static int interrupt_callback(void *opaque)
{
    AppState *state = (AppState *)opaque;
    return (atomic_load(&state->decoder_quit) || atomic_load(&state->quit)) ? 1 : 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Hardware Acceleration Callback & Device Probing
 * ──────────────────────────────────────────────────────────────────────────── */
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    DecoderCtx *d = (DecoderCtx *)ctx->opaque;
    if (!d) return AV_PIX_FMT_NONE;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == d->hw_pix_fmt) {
            return *p;
        }
    }

    fprintf(stderr, "[decoder] Failed to negotiate HW pixel format, falling back to software\n");
    return AV_PIX_FMT_NONE;
}

static bool try_init_hw_device(DecoderCtx *d, const AVCodec *codec, enum AVHWDeviceType type, const char *dev_name, AppState *state)
{
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) break;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == type) {
            pix_fmt = config->pix_fmt;
            break;
        }
    }

    if (pix_fmt == AV_PIX_FMT_NONE) {
        return false;
    }

    AVBufferRef *hw_ctx = NULL;
    int ret = av_hwdevice_ctx_create(&hw_ctx, type, NULL, NULL, 0);
    if (ret < 0) {
        return false;
    }

    d->hw_device_ctx = hw_ctx;
    d->hw_pix_fmt = pix_fmt;
    d->hw_accel_enabled = true;
    d->codec_ctx->hw_device_ctx = av_buffer_ref(hw_ctx);
    d->codec_ctx->opaque = d;
    d->codec_ctx->get_format = get_hw_format;

    const char *type_name = av_hwdevice_get_type_name(type);
    snprintf(state->hw_accel_name, sizeof(state->hw_accel_name), "%s", type_name ? type_name : dev_name);
    state->hw_accel_active = true;

    fprintf(stderr, "[decoder] ⚡ Hardware acceleration enabled: %s (pixel format: %s)\n",
            state->hw_accel_name, av_get_pix_fmt_name(pix_fmt));
    return true;
}

static void init_hardware_acceleration(DecoderCtx *d, const AVCodec *codec, AppState *state)
{
    d->hw_accel_enabled = false;
    d->hw_device_ctx = NULL;
    d->hw_pix_fmt = AV_PIX_FMT_NONE;
    state->hw_accel_active = false;
    snprintf(state->hw_accel_name, sizeof(state->hw_accel_name), "Software (CPU)");

    /* Prioritized list of hardware device types for Linux */
    const enum AVHWDeviceType hw_types[] = {
        AV_HWDEVICE_TYPE_VAAPI,    /* Intel & AMD Mesa / iHD / radeonsi */
        AV_HWDEVICE_TYPE_CUDA,     /* NVIDIA proprietary NVDEC */
        AV_HWDEVICE_TYPE_VDPAU,    /* Legacy NVIDIA / AMD */
        AV_HWDEVICE_TYPE_VULKAN,   /* Universal cross-vendor Vulkan */
        AV_HWDEVICE_TYPE_DRM,      /* ARM / Embedded / Rockchip / Pi */
        AV_HWDEVICE_TYPE_NONE
    };

    for (int i = 0; hw_types[i] != AV_HWDEVICE_TYPE_NONE; i++) {
        const char *name = av_hwdevice_get_type_name(hw_types[i]);
        if (try_init_hw_device(d, codec, hw_types[i], name, state)) {
            return;
        }
    }

    fprintf(stderr, "[decoder] Using multi-threaded CPU software decoding.\n");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Open the video file and initialize all FFmpeg contexts.
 * Returns 0 on success, -1 on failure.
 * ──────────────────────────────────────────────────────────────────────────── */
static int decoder_ctx_open(DecoderCtx *d, AppState *state, const char *filepath)
{
    memset(d, 0, sizeof(*d));

    d->fmt_ctx = avformat_alloc_context();
    if (!d->fmt_ctx) return -1;
    d->fmt_ctx->interrupt_callback.callback = interrupt_callback;
    d->fmt_ctx->interrupt_callback.opaque   = state;

    int ret = avformat_open_input(&d->fmt_ctx, filepath, NULL, NULL);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "[decoder] failed to open '%s': %s\n", filepath, errbuf);
        return -1;
    }

    if (avformat_find_stream_info(d->fmt_ctx, NULL) < 0) {
        fprintf(stderr, "[decoder] failed to find stream info\n");
        return -1;
    }

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

    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "[decoder] unsupported codec: %d\n", codecpar->codec_id);
        return -1;
    }

    d->codec_ctx = avcodec_alloc_context3(codec);
    if (!d->codec_ctx) return -1;

    if (avcodec_parameters_to_context(d->codec_ctx, codecpar) < 0) return -1;

    /* Multi-threaded CPU decoding config (used if HW accel unavailable/fails) */
    d->codec_ctx->thread_count = 0;

    /* Initialize universal GPU hardware acceleration */
    init_hardware_acceleration(d, codec, state);

    if (avcodec_open2(d->codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "[decoder] failed to open codec\n");
        return -1;
    }

    int w = d->codec_ctx->width;
    int h = d->codec_ctx->height;

    /* If not using hardware acceleration, initialize swscale directly */
    if (!d->hw_accel_enabled) {
        d->sws_ctx = sws_getContext(
            w, h, d->codec_ctx->pix_fmt,
            w, h, AV_PIX_FMT_YUV420P,
            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    }

    /* Allocate frames */
    d->frame_raw = av_frame_alloc();
    d->frame_sw  = av_frame_alloc();
    d->frame_yuv = av_frame_alloc();
    if (!d->frame_raw || !d->frame_sw || !d->frame_yuv) return -1;

    /* Allocate pixel buffer for the YUV420P destination frame */
    int buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 32);
    d->yuv_buffer = (uint8_t *)av_malloc((size_t)buf_size);
    if (!d->yuv_buffer) return -1;

    av_image_fill_arrays(d->frame_yuv->data, d->frame_yuv->linesize,
                         d->yuv_buffer, AV_PIX_FMT_YUV420P, w, h, 32);

    d->time_base = av_q2d(vstream->time_base);

    AVRational fps_r = vstream->avg_frame_rate;
    if (fps_r.num > 0 && fps_r.den > 0) {
        state->video_fps = av_q2d(fps_r);
    } else {
        fps_r = vstream->r_frame_rate;
        state->video_fps = (fps_r.num > 0 && fps_r.den > 0)
                           ? av_q2d(fps_r) : 30.0;
    }
    d->frame_delay = 1.0 / state->video_fps;

    atomic_store(&state->video_width,  w);
    atomic_store(&state->video_height, h);

    fprintf(stderr, "[decoder] opened '%s': %dx%d @ %.2f fps, codec=%s, accel=%s\n",
            filepath, w, h, state->video_fps, codec->name, state->hw_accel_name);

    atomic_store(&state->decoder_ready, true);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Close all FFmpeg contexts and free memory.
 * ──────────────────────────────────────────────────────────────────────────── */
static void decoder_ctx_close(DecoderCtx *d)
{
    if (d->sws_ctx)       { sws_freeContext(d->sws_ctx);    d->sws_ctx = NULL; }
    if (d->frame_raw)     { av_frame_free(&d->frame_raw); }
    if (d->frame_sw)      { av_frame_free(&d->frame_sw); }
    if (d->frame_yuv)     { av_frame_free(&d->frame_yuv); }
    if (d->yuv_buffer)    { av_free(d->yuv_buffer);         d->yuv_buffer = NULL; }
    if (d->hw_device_ctx) { av_buffer_unref(&d->hw_device_ctx); }
    if (d->codec_ctx)     { avcodec_free_context(&d->codec_ctx); }
    if (d->fmt_ctx)       { avformat_close_input(&d->fmt_ctx); }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Frame processing & queue pushing (handles both HW surfaces and SW frames)
 * ──────────────────────────────────────────────────────────────────────────── */
static bool process_and_push_frame(DecoderCtx *d, AppState *state, AVFrame *src_frame, double *frame_timer, int64_t start_time)
{
    AVFrame *frame_to_scale = src_frame;

    /* Transfer hardware frame to system memory if needed */
    if (src_frame->format == d->hw_pix_fmt && d->hw_accel_enabled) {
        if (!d->frame_sw) {
            d->frame_sw = av_frame_alloc();
            if (!d->frame_sw) return false;
        }
        int ret = av_hwframe_transfer_data(d->frame_sw, src_frame, 0);
        if (ret < 0) {
            fprintf(stderr, "[decoder] av_hwframe_transfer_data failed: %d\n", ret);
            return true;
        }
        d->frame_sw->pts = src_frame->pts;
        frame_to_scale = d->frame_sw;
    }

    int w = frame_to_scale->width > 0 ? frame_to_scale->width : d->codec_ctx->width;
    int h = frame_to_scale->height > 0 ? frame_to_scale->height : d->codec_ctx->height;

    /* Allocate or update swscale context if pixel format or dimensions require it */
    if (!d->sws_ctx) {
        d->sws_ctx = sws_getContext(
            w, h, frame_to_scale->format,
            w, h, AV_PIX_FMT_YUV420P,
            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    }

    if (d->sws_ctx) {
        sws_scale(d->sws_ctx,
                  (const uint8_t *const *)frame_to_scale->data,
                  frame_to_scale->linesize,
                  0, h,
                  d->frame_yuv->data, d->frame_yuv->linesize);

        /* Frame pacing */
        if (frame_timer) {
            double pts = 0.0;
            if (frame_to_scale->pts != AV_NOPTS_VALUE) {
                pts = (double)frame_to_scale->pts * d->time_base;
            } else {
                pts = *frame_timer;
            }

            double elapsed = (double)(av_gettime_relative() - start_time) / 1e6;
            double delay = pts - elapsed;
            if (delay > 0.0 && delay < 1.0) {
                sleep_seconds(delay);
            }
            *frame_timer = pts + d->frame_delay;
        }

        bool pushed = frame_queue_push(&state->queue,
                                       d->frame_yuv->data[0], d->frame_yuv->linesize[0],
                                       d->frame_yuv->data[1], d->frame_yuv->data[2],
                                       d->frame_yuv->linesize[1],
                                       w, h);
        if (d->frame_sw) {
            av_frame_unref(d->frame_sw);
        }
        return pushed;
    }

    if (d->frame_sw) {
        av_frame_unref(d->frame_sw);
    }
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Decoder thread function.
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

#define DECODER_SHOULD_QUIT() \
    (atomic_load(&state->decoder_quit) || atomic_load(&state->quit))

    while (!DECODER_SHOULD_QUIT()) {

        /* ── Handle pause ── */
        if (atomic_load(&state->paused)) {
            pthread_mutex_lock(&state->pause_mutex);
            while (atomic_load(&state->paused) && !DECODER_SHOULD_QUIT()) {
                pthread_cond_wait(&state->pause_cond, &state->pause_mutex);
            }
            pthread_mutex_unlock(&state->pause_mutex);

            start_time = av_gettime_relative();
            frame_timer = 0.0;

            if (DECODER_SHOULD_QUIT()) break;
        }

        /* ── Read a packet from the container ── */
        int ret = av_read_frame(d.fmt_ctx, pkt);

        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(d.fmt_ctx->pb)) {
                /* Step 1: Flush decoder — send NULL packet to drain buffered frames */
                avcodec_send_packet(d.codec_ctx, NULL);
                while (avcodec_receive_frame(d.codec_ctx, d.frame_raw) == 0) {
                    if (DECODER_SHOULD_QUIT()) {
                        av_frame_unref(d.frame_raw);
                        break;
                    }

                    bool pushed = process_and_push_frame(&d, state, d.frame_raw, NULL, start_time);
                    av_frame_unref(d.frame_raw);
                    if (!pushed) break;
                }

                if (DECODER_SHOULD_QUIT()) break;

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

            fprintf(stderr, "[decoder] read error: %d, continuing\n", ret);
            continue;
        }

        /* Skip non-video packets */
        if (pkt->stream_index != d.video_stream_idx) {
            av_packet_unref(pkt);
            continue;
        }

        /* ── Send packet to decoder ── */
        while (!DECODER_SHOULD_QUIT()) {
            ret = avcodec_send_packet(d.codec_ctx, pkt);
            if (ret == 0) {
                av_packet_unref(pkt);
                break;
            } else if (ret == AVERROR(EAGAIN)) {
                ret = avcodec_receive_frame(d.codec_ctx, d.frame_raw);
                if (ret < 0) {
                    av_packet_unref(pkt);
                    break;
                }
                process_and_push_frame(&d, state, d.frame_raw, &frame_timer, start_time);
                av_frame_unref(d.frame_raw);
            } else {
                fprintf(stderr, "[decoder] send_packet error: %d\n", ret);
                av_packet_unref(pkt);
                break;
            }
        }

        /* ── Receive decoded frames ── */
        while (!DECODER_SHOULD_QUIT()) {
            ret = avcodec_receive_frame(d.codec_ctx, d.frame_raw);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                fprintf(stderr, "[decoder] receive_frame error: %d\n", ret);
                break;
            }

            if (!process_and_push_frame(&d, state, d.frame_raw, &frame_timer, start_time)) {
                av_frame_unref(d.frame_raw);
                break;
            }

            av_frame_unref(d.frame_raw);
        }
    }

#undef DECODER_SHOULD_QUIT

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
    atomic_store(&state->decoder_quit,  false);   /* fresh start for this video */
    atomic_store(&state->paused,        false);
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

    /* Signal the decoder thread to quit (does NOT affect app-level quit flag) */
    atomic_store(&state->decoder_quit, true);
    atomic_store(&state->paused, false);

    /* Wake the decoder if it's sleeping on the pause condvar */
    pthread_mutex_lock(&state->pause_mutex);
    pthread_cond_signal(&state->pause_cond);
    pthread_mutex_unlock(&state->pause_mutex);

    /* Abort the frame queue to unblock any push() call */
    frame_queue_abort(&state->queue);

    /* Wait for the thread to finish */
    pthread_join(state->decoder_tid, NULL);
    state->decoder_running = false;

    /* Reset decoder_quit so the next start() call can use a fresh thread */
    atomic_store(&state->decoder_quit, false);

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
