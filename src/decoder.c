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
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <SDL2/SDL.h>

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

    /* Audio stream & decoding */
    int                audio_stream_idx;
    AVCodecContext    *audio_codec_ctx;
    SwrContext        *swr_ctx;
    AVFrame           *frame_audio;
    uint8_t           *audio_buf;
    int                audio_buf_size;
    SDL_AudioDeviceID  audio_dev;

    /* Timing */
    double             time_base;       /* seconds per PTS tick            */
    double             frame_delay;     /* 1.0 / fps                       */
} DecoderCtx;

/* ─────────────────────────────────────────────────────────────────────────────
 * Precise sleep avoiding busy-waiting.
 * ──────────────────────────────────────────────────────────────────────────── */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void sleep_seconds(double seconds)
{
    if (seconds <= 0.0) return;
    DWORD ms = (DWORD)(seconds * 1000.0);
    if (ms == 0) ms = 1;
    Sleep(ms);
}
#else
static void sleep_seconds(double seconds)
{
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec  = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}
#endif

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

    /* Prioritized list of hardware device types */
    const enum AVHWDeviceType hw_types[] = {
#ifdef _WIN32
        AV_HWDEVICE_TYPE_D3D11VA,  /* Windows 11 Primary Direct3D 11 */
        AV_HWDEVICE_TYPE_DXVA2,    /* Windows DirectX Video Acceleration 2 */
        AV_HWDEVICE_TYPE_CUDA,     /* NVIDIA proprietary NVDEC */
        AV_HWDEVICE_TYPE_QSV,      /* Intel Quick Sync Video */
        AV_HWDEVICE_TYPE_VULKAN,   /* Vulkan Video */
#else
        AV_HWDEVICE_TYPE_VAAPI,    /* Intel & AMD Mesa / iHD / radeonsi */
        AV_HWDEVICE_TYPE_CUDA,     /* NVIDIA proprietary NVDEC */
        AV_HWDEVICE_TYPE_VDPAU,    /* Legacy NVIDIA / AMD */
        AV_HWDEVICE_TYPE_VULKAN,   /* Universal cross-vendor Vulkan */
        AV_HWDEVICE_TYPE_DRM,      /* ARM / Embedded / Rockchip / Pi */
#endif
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

    /* Search for audio stream */
    d->audio_stream_idx = -1;
    for (unsigned i = 0; i < d->fmt_ctx->nb_streams; i++) {
        if (d->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            d->audio_stream_idx = (int)i;
            break;
        }
    }

    if (d->audio_stream_idx >= 0) {
        AVStream *astream = d->fmt_ctx->streams[d->audio_stream_idx];
        AVCodecParameters *acodecpar = astream->codecpar;
        const AVCodec *acodec = avcodec_find_decoder(acodecpar->codec_id);
        if (acodec) {
            d->audio_codec_ctx = avcodec_alloc_context3(acodec);
            if (d->audio_codec_ctx && avcodec_parameters_to_context(d->audio_codec_ctx, acodecpar) >= 0) {
                if (avcodec_open2(d->audio_codec_ctx, acodec, NULL) == 0) {
                    d->frame_audio = av_frame_alloc();

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
                    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
                    swr_alloc_set_opts2(&d->swr_ctx,
                                        &out_layout, AV_SAMPLE_FMT_S16, 48000,
                                        &d->audio_codec_ctx->ch_layout, d->audio_codec_ctx->sample_fmt, d->audio_codec_ctx->sample_rate,
                                        0, NULL);
#else
                    int64_t in_layout = d->audio_codec_ctx->channel_layout ? d->audio_codec_ctx->channel_layout : av_get_default_channel_layout(d->audio_codec_ctx->channels);
                    d->swr_ctx = swr_alloc_set_opts(NULL,
                                                    AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 48000,
                                                    in_layout, d->audio_codec_ctx->sample_fmt, d->audio_codec_ctx->sample_rate,
                                                    0, NULL);
#endif
                    if (d->swr_ctx && swr_init(d->swr_ctx) == 0) {
                        SDL_AudioSpec wanted, obtained;
                        memset(&wanted, 0, sizeof(wanted));
                        wanted.freq = 48000;
                        wanted.format = AUDIO_S16SYS;
                        wanted.channels = 2;
                        wanted.samples = 1024;
                        wanted.callback = NULL;

                        d->audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
                        if (d->audio_dev > 0) {
                            SDL_PauseAudioDevice(d->audio_dev, 0);
                            atomic_store(&state->has_audio, true);
                            fprintf(stderr, "[decoder] audio ready: %s -> 48000Hz S16 Stereo\n", acodec->name);
                        }
                    }
                }
            }
        }
    }
    if (d->audio_dev <= 0) {
        atomic_store(&state->has_audio, false);
    }

    fprintf(stderr, "[decoder] opened '%s': %dx%d @ %.2f fps, codec=%s, accel=%s, audio=%s\n",
            filepath, w, h, state->video_fps, codec->name, state->hw_accel_name,
            atomic_load(&state->has_audio) ? "yes" : "no");

    atomic_store(&state->decoder_ready, true);
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Close all FFmpeg contexts and free memory.
 * ──────────────────────────────────────────────────────────────────────────── */
static void decoder_ctx_close(DecoderCtx *d)
{
    if (d->audio_dev > 0) {
        SDL_CloseAudioDevice(d->audio_dev);
        d->audio_dev = 0;
    }
    if (d->swr_ctx)         { swr_free(&d->swr_ctx);           d->swr_ctx = NULL; }
    if (d->frame_audio)     { av_frame_free(&d->frame_audio); }
    if (d->audio_codec_ctx) { avcodec_free_context(&d->audio_codec_ctx); }
    if (d->audio_buf)       { av_free(d->audio_buf);           d->audio_buf = NULL; }

    if (d->sws_ctx)         { sws_freeContext(d->sws_ctx);    d->sws_ctx = NULL; }
    if (d->frame_raw)       { av_frame_free(&d->frame_raw); }
    if (d->frame_sw)        { av_frame_free(&d->frame_sw); }
    if (d->frame_yuv)       { av_frame_free(&d->frame_yuv); }
    if (d->yuv_buffer)      { av_free(d->yuv_buffer);         d->yuv_buffer = NULL; }
    if (d->hw_device_ctx)   { av_buffer_unref(&d->hw_device_ctx); }
    if (d->codec_ctx)       { avcodec_free_context(&d->codec_ctx); }
    if (d->fmt_ctx)         { avformat_close_input(&d->fmt_ctx); }
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
 * Audio decoding and queueing to SDL2 audio device
 * ──────────────────────────────────────────────────────────────────────────── */
static void decode_and_queue_audio(DecoderCtx *d, AppState *state, AVPacket *pkt)
{
    if (!d->audio_codec_ctx || !d->swr_ctx || d->audio_dev <= 0) return;

    int ret = avcodec_send_packet(d->audio_codec_ctx, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) return;

    while (ret >= 0) {
        ret = avcodec_receive_frame(d->audio_codec_ctx, d->frame_audio);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        bool audio_on = atomic_load(&state->audio_enabled) && !atomic_load(&state->paused);
        if (audio_on) {
            int max_out_samples = (int)av_rescale_rnd(
                swr_get_delay(d->swr_ctx, d->audio_codec_ctx->sample_rate) + d->frame_audio->nb_samples,
                48000, d->audio_codec_ctx->sample_rate, AV_ROUND_UP);

            int out_bytes = max_out_samples * 2 * (int)sizeof(int16_t);
            if (out_bytes > d->audio_buf_size) {
                d->audio_buf = (uint8_t *)av_realloc(d->audio_buf, (size_t)(out_bytes + 2048));
                d->audio_buf_size = out_bytes + 2048;
            }

            if (d->audio_buf) {
                uint8_t *out_data[1] = { d->audio_buf };
                int converted_samples = swr_convert(d->swr_ctx,
                                                    out_data, max_out_samples,
                                                    (const uint8_t **)d->frame_audio->data, d->frame_audio->nb_samples);
                if (converted_samples > 0) {
                    Uint32 pcm_len = (Uint32)(converted_samples * 2 * sizeof(int16_t));
                    /* Keep audio buffer bounded (~350ms max) to prevent drift */
                    Uint32 queued = SDL_GetQueuedAudioSize(d->audio_dev);
                    Uint32 max_queue = (Uint32)(48000 * 2 * sizeof(int16_t) * 0.35);
                    if (queued < max_queue) {
                        SDL_QueueAudio(d->audio_dev, d->audio_buf, pcm_len);
                    }
                }
            }
        } else {
            /* If muted or paused, clear queued audio so unmuting doesn't play stale audio */
            if (SDL_GetQueuedAudioSize(d->audio_dev) > 0) {
                SDL_ClearQueuedAudio(d->audio_dev);
            }
        }
        av_frame_unref(d->frame_audio);
    }
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
            if (d.audio_dev > 0) {
                SDL_PauseAudioDevice(d.audio_dev, 1);
                SDL_ClearQueuedAudio(d.audio_dev);
            }

            pthread_mutex_lock(&state->pause_mutex);
            while (atomic_load(&state->paused) && !DECODER_SHOULD_QUIT()) {
                pthread_cond_wait(&state->pause_cond, &state->pause_mutex);
            }
            pthread_mutex_unlock(&state->pause_mutex);

            if (d.audio_dev > 0) {
                SDL_PauseAudioDevice(d.audio_dev, 0);
            }

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
                av_seek_frame(d.fmt_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);

                /* Step 3: Flush the codec's internal buffers */
                avcodec_flush_buffers(d.codec_ctx);
                if (d.audio_codec_ctx) {
                    avcodec_flush_buffers(d.audio_codec_ctx);
                }
                if (d.audio_dev > 0) {
                    SDL_ClearQueuedAudio(d.audio_dev);
                }

                /* Reset frame timer for the new loop iteration */
                start_time = av_gettime_relative();
                frame_timer = 0.0;
                continue;
            }

            fprintf(stderr, "[decoder] read error: %d, continuing\n", ret);
            continue;
        }

        /* Process audio packets */
        if (d.audio_stream_idx >= 0 && pkt->stream_index == d.audio_stream_idx) {
            decode_and_queue_audio(&d, state, pkt);
            av_packet_unref(pkt);
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
