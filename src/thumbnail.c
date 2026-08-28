/*
 * thumbnail.c — Fast, leak-free video & image thumbnail generator.
 */

#include "thumbnail.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static gboolean is_image_ext(const char *path)
{
    if (!path) return FALSE;
    const char *exts[] = { ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".svg", ".gif", NULL };
    size_t len = strlen(path);
    for (int i = 0; exts[i] != NULL; i++) {
        size_t elen = strlen(exts[i]);
        if (len >= elen && strcasecmp(path + (len - elen), exts[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

GdkPixbuf *thumbnail_generate(const char *filepath, int target_w, int target_h)
{
    if (!filepath || !filepath[0] || target_w <= 0 || target_h <= 0) {
        return NULL;
    }

    /* Fast path for static image files */
    if (is_image_ext(filepath)) {
        GError *err = NULL;
        GdkPixbuf *img_pixbuf = gdk_pixbuf_new_from_file_at_scale(filepath, target_w, target_h, TRUE, &err);
        if (img_pixbuf) {
            return img_pixbuf;
        }
        if (err) g_error_free(err);
        /* Fall back to FFmpeg if GdkPixbuf failed to load (e.g. specialized format) */
    }

    /* Video frame extraction via FFmpeg */
    AVFormatContext   *fmt_ctx   = NULL;
    AVCodecContext    *codec_ctx = NULL;
    struct SwsContext *sws_ctx   = NULL;
    AVFrame           *raw_frame = NULL;
    AVPacket          *pkt       = NULL;
    GdkPixbuf         *pixbuf    = NULL;
    int                v_idx     = -1;

    if (avformat_open_input(&fmt_ctx, filepath, NULL, NULL) < 0) {
        return NULL;
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        goto cleanup;
    }

    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            v_idx = (int)i;
            break;
        }
    }

    if (v_idx < 0) goto cleanup;

    AVCodecParameters *codecpar = fmt_ctx->streams[v_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) goto cleanup;

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) goto cleanup;

    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0 ||
        avcodec_open2(codec_ctx, codec, NULL) < 0) {
        goto cleanup;
    }

    /* Seek slightly into the video (5%) to avoid black initial frames */
    int64_t target_ts = 0;
    if (fmt_ctx->streams[v_idx]->duration > 0) {
        target_ts = fmt_ctx->streams[v_idx]->duration / 20;
    } else if (fmt_ctx->duration > 0) {
        target_ts = (fmt_ctx->duration / AV_TIME_BASE) / 20;
    }
    if (target_ts > 0) {
        av_seek_frame(fmt_ctx, v_idx, target_ts, AVSEEK_FLAG_BACKWARD);
    }

    raw_frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!raw_frame || !pkt) goto cleanup;

    int frame_decoded = 0;
    int max_attempts = 100;

    while (max_attempts-- > 0 && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == v_idx) {
            if (avcodec_send_packet(codec_ctx, pkt) >= 0) {
                if (avcodec_receive_frame(codec_ctx, raw_frame) == 0) {
                    frame_decoded = 1;
                    av_packet_unref(pkt);
                    break;
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (frame_decoded) {
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, target_w, target_h);
        if (pixbuf) {
            uint8_t *pixels = gdk_pixbuf_get_pixels(pixbuf);
            int rowstride   = gdk_pixbuf_get_rowstride(pixbuf);

            sws_ctx = sws_getContext(
                codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                target_w, target_h, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, NULL, NULL, NULL);

            if (sws_ctx) {
                uint8_t *dst_data[4] = { pixels, NULL, NULL, NULL };
                int dst_linesize[4]  = { rowstride, 0, 0, 0 };

                sws_scale(sws_ctx,
                          (const uint8_t *const *)raw_frame->data,
                          raw_frame->linesize,
                          0, codec_ctx->height,
                          dst_data, dst_linesize);

                sws_freeContext(sws_ctx);
            } else {
                g_object_unref(pixbuf);
                pixbuf = NULL;
            }
        }
    }

cleanup:
    if (raw_frame) av_frame_free(&raw_frame);
    if (pkt) av_packet_free(&pkt);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);

    return pixbuf;
}
