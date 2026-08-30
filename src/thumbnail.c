/*
 * thumbnail.c — Fast, leak-free video & image thumbnail generator.
 */

#include "thumbnail.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

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

static char *get_thumbnail_cache_path(const char *filepath, int target_w, int target_h)
{
    struct stat st;
    if (stat(filepath, &st) != 0) {
        return NULL;
    }

    const char *user_cache = g_get_user_cache_dir();
    char cache_dir[512];
    snprintf(cache_dir, sizeof(cache_dir), "%s/live-wallpaper/thumbnails", user_cache);
    g_mkdir_with_parents(cache_dir, 0755);

    char key[1024];
    snprintf(key, sizeof(key), "%s:%ld:%ld:%dx%d", filepath, (long)st.st_mtime, (long)st.st_size, target_w, target_h);
    char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, key, -1);
    if (!hash) return NULL;

    char *cache_path = g_strdup_printf("%s/%s.jpg", cache_dir, hash);
    g_free(hash);
    return cache_path;
}

GdkPixbuf *thumbnail_generate(const char *filepath, int target_w, int target_h)
{
    if (!filepath || !filepath[0] || target_w <= 0 || target_h <= 0) {
        return NULL;
    }

    /* Fast path 1: Check disk cache first */
    char *cache_path = get_thumbnail_cache_path(filepath, target_w, target_h);
    if (cache_path && g_file_test(cache_path, G_FILE_TEST_EXISTS)) {
        GError *err = NULL;
        GdkPixbuf *cached_pixbuf = gdk_pixbuf_new_from_file(cache_path, &err);
        if (cached_pixbuf) {
            g_free(cache_path);
            return cached_pixbuf;
        }
        if (err) g_error_free(err);
    }

    /* Fast path 2: Static image files via GdkPixbuf */
    if (is_image_ext(filepath)) {
        GError *err = NULL;
        GdkPixbuf *img_pixbuf = gdk_pixbuf_new_from_file_at_scale(filepath, target_w, target_h, TRUE, &err);
        if (img_pixbuf) {
            if (cache_path) {
                gdk_pixbuf_save(img_pixbuf, cache_path, "jpeg", NULL, "quality", "85", NULL);
                g_free(cache_path);
            }
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
        if (cache_path) g_free(cache_path);
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
        int src_w = codec_ctx->width;
        int src_h = codec_ctx->height;

        /* Compute letterbox/pillarbox dimensions that fit src inside target
         * without distortion.  Scale to the largest size that fits. */
        int scaled_w, scaled_h;
        double src_ratio = (double)src_w / (double)src_h;
        double dst_ratio = (double)target_w / (double)target_h;

        if (src_ratio > dst_ratio) {
            /* Wider than target — fit to width, letterbox top/bottom */
            scaled_w = target_w;
            scaled_h = (int)(target_w / src_ratio);
        } else {
            /* Taller than target — fit to height, pillarbox left/right */
            scaled_h = target_h;
            scaled_w = (int)(target_h * src_ratio);
        }
        /* Clamp to at least 1 pixel */
        if (scaled_w < 1) scaled_w = 1;
        if (scaled_h < 1) scaled_h = 1;

        int offset_x = (target_w - scaled_w) / 2;
        int offset_y = (target_h - scaled_h) / 2;

        /* Allocate the output pixbuf at full target size (with alpha for
         * the black bars) */
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, target_w, target_h);
        if (pixbuf) {
            /* Fill with black */
            gdk_pixbuf_fill(pixbuf, 0x000000ff);

            uint8_t *pixels  = gdk_pixbuf_get_pixels(pixbuf);
            int      rowstride = gdk_pixbuf_get_rowstride(pixbuf);

            /* Scale into a temporary buffer at (scaled_w × scaled_h) */
            sws_ctx = sws_getContext(
                src_w, src_h, codec_ctx->pix_fmt,
                scaled_w, scaled_h, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, NULL, NULL, NULL);

            if (sws_ctx) {
                /* Temporary RGB buffer for the scaled content */
                int tmp_rowstride = scaled_w * 3;
                uint8_t *tmp_buf  = (uint8_t *)av_malloc((size_t)(tmp_rowstride * scaled_h));
                if (tmp_buf) {
                    uint8_t *dst_data[4]    = { tmp_buf, NULL, NULL, NULL };
                    int      dst_linesize[4] = { tmp_rowstride, 0, 0, 0 };

                    sws_scale(sws_ctx,
                              (const uint8_t *const *)raw_frame->data,
                              raw_frame->linesize,
                              0, src_h,
                              dst_data, dst_linesize);

                    /* Blit scaled content into the correct offset in pixbuf */
                    for (int row = 0; row < scaled_h; row++) {
                        uint8_t *src_row = tmp_buf + row * tmp_rowstride;
                        uint8_t *dst_row = pixels
                                           + (offset_y + row) * rowstride
                                           + offset_x * 3;
                        memcpy(dst_row, src_row, (size_t)tmp_rowstride);
                    }
                    av_free(tmp_buf);
                } else {
                    g_object_unref(pixbuf);
                    pixbuf = NULL;
                }
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

    if (pixbuf && cache_path) {
        gdk_pixbuf_save(pixbuf, cache_path, "jpeg", NULL, "quality", "85", NULL);
    }
    if (cache_path) g_free(cache_path);

    return pixbuf;
}
