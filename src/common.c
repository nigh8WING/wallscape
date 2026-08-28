/*
 * common.c — Implementation of the frame queue and app state utilities.
 *
 * The frame queue is the heart of the decoder→renderer pipeline.  It uses
 * a fixed-size ring buffer with pre-allocated slot buffers that grow lazily
 * when frame dimensions change (which in practice only happens once, when
 * the first frame is pushed after opening a new video).
 */

#include "common.h"
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Frame Queue
 * ═══════════════════════════════════════════════════════════════════════════ */

void frame_queue_init(FrameQueue *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_push, NULL);
    pthread_cond_init(&q->cond_pop, NULL);
    atomic_store(&q->abort, false);
}

void frame_queue_destroy(FrameQueue *q)
{
    /* Free any remaining slot buffers */
    for (int i = 0; i < FRAME_QUEUE_CAPACITY; i++) {
        free(q->frames[i].y);
        free(q->frames[i].u);
        free(q->frames[i].v);
        q->frames[i].y = NULL;
        q->frames[i].u = NULL;
        q->frames[i].v = NULL;
    }
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond_push);
    pthread_cond_destroy(&q->cond_pop);
}

bool frame_queue_push(FrameQueue *q,
                      const uint8_t *y, int y_pitch,
                      const uint8_t *u, const uint8_t *v, int uv_pitch,
                      int width, int height)
{
    pthread_mutex_lock(&q->mutex);

    /* Block until there's a free slot or we're told to abort. */
    while (q->count == FRAME_QUEUE_CAPACITY && !atomic_load(&q->abort)) {
        pthread_cond_wait(&q->cond_push, &q->mutex);
    }

    if (atomic_load(&q->abort)) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    VideoFrame *slot = &q->frames[q->write_idx];

    /*
     * Lazily (re)allocate the slot buffers if the frame dimensions changed.
     * For a single video this only happens on the first push.
     */
    if (slot->width != width || slot->height != height ||
        slot->y_pitch != y_pitch || slot->uv_pitch != uv_pitch) {
        free(slot->y);
        free(slot->u);
        free(slot->v);

        int y_size  = y_pitch * height;
        int uv_size = uv_pitch * (height / 2);

        slot->y  = (uint8_t *)malloc((size_t)y_size);
        slot->u  = (uint8_t *)malloc((size_t)uv_size);
        slot->v  = (uint8_t *)malloc((size_t)uv_size);
        slot->y_pitch  = y_pitch;
        slot->uv_pitch = uv_pitch;
        slot->width    = width;
        slot->height   = height;

        if (!slot->y || !slot->u || !slot->v) {
            fprintf(stderr, "[frame_queue] allocation failed for %dx%d frame\n",
                    width, height);
            /* Free any partially-allocated buffers to avoid a leak */
            free(slot->y); slot->y = NULL;
            free(slot->u); slot->u = NULL;
            free(slot->v); slot->v = NULL;
            slot->width = 0; slot->height = 0;
            pthread_mutex_unlock(&q->mutex);
            return false;
        }
    }

    /* Copy the YUV plane data into the slot. */
    memcpy(slot->y, y, (size_t)(y_pitch * height));
    memcpy(slot->u, u, (size_t)(uv_pitch * (height / 2)));
    memcpy(slot->v, v, (size_t)(uv_pitch * (height / 2)));

    q->write_idx = (q->write_idx + 1) % FRAME_QUEUE_CAPACITY;
    q->count++;

    /* Wake the renderer if it's waiting for a frame. */
    pthread_cond_signal(&q->cond_pop);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

bool frame_queue_pop(FrameQueue *q, VideoFrame *out)
{
    pthread_mutex_lock(&q->mutex);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    VideoFrame *slot = &q->frames[q->read_idx];

    /*
     * Deep-copy the frame data into *out so the renderer owns the memory.
     * This prevents data races: once we advance read_idx, the decoder is
     * free to overwrite this slot with a new frame.
     */
    int y_size  = slot->y_pitch * slot->height;
    int uv_size = slot->uv_pitch * (slot->height / 2);

    out->width    = slot->width;
    out->height   = slot->height;
    out->y_pitch  = slot->y_pitch;
    out->uv_pitch = slot->uv_pitch;
    out->y = (uint8_t *)malloc((size_t)y_size);
    out->u = (uint8_t *)malloc((size_t)uv_size);
    out->v = (uint8_t *)malloc((size_t)uv_size);

    if (!out->y || !out->u || !out->v) {
        free(out->y); free(out->u); free(out->v);
        out->y = out->u = out->v = NULL;
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    memcpy(out->y, slot->y, (size_t)y_size);
    memcpy(out->u, slot->u, (size_t)uv_size);
    memcpy(out->v, slot->v, (size_t)uv_size);

    q->read_idx = (q->read_idx + 1) % FRAME_QUEUE_CAPACITY;
    q->count--;

    /* Wake the decoder if it was blocked waiting for a free slot. */
    pthread_cond_signal(&q->cond_push);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

void frame_queue_flush(FrameQueue *q)
{
    pthread_mutex_lock(&q->mutex);
    q->read_idx  = 0;
    q->write_idx = 0;
    q->count     = 0;
    /* Zero slot dimensions so the next video's first push triggers realloc.
     * The underlying buffers are kept allocated for potential reuse. */
    for (int i = 0; i < FRAME_QUEUE_CAPACITY; i++) {
        q->frames[i].width  = 0;
        q->frames[i].height = 0;
    }
    pthread_cond_broadcast(&q->cond_push);
    pthread_mutex_unlock(&q->mutex);
}

void frame_queue_abort(FrameQueue *q)
{
    atomic_store(&q->abort, true);
    pthread_mutex_lock(&q->mutex);
    pthread_cond_broadcast(&q->cond_push);
    pthread_cond_broadcast(&q->cond_pop);
    pthread_mutex_unlock(&q->mutex);
}

void frame_queue_reset(FrameQueue *q)
{
    pthread_mutex_lock(&q->mutex);
    q->read_idx  = 0;
    q->write_idx = 0;
    q->count     = 0;
    atomic_store(&q->abort, false);
    pthread_mutex_unlock(&q->mutex);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * App State
 * ═══════════════════════════════════════════════════════════════════════════ */

void app_state_init(AppState *s)
{
    memset(s, 0, sizeof(*s));
    atomic_store(&s->quit,         false);
    atomic_store(&s->decoder_quit, false);
    atomic_store(&s->paused,       false);
    atomic_store(&s->playing,      false);
    atomic_store(&s->decoder_ready,false);
    atomic_store(&s->video_width,  0);
    atomic_store(&s->video_height, 0);

    frame_queue_init(&s->queue);

    pthread_mutex_init(&s->pause_mutex, NULL);
    pthread_cond_init(&s->pause_cond, NULL);
}

void app_state_destroy(AppState *s)
{
    frame_queue_destroy(&s->queue);
    pthread_mutex_destroy(&s->pause_mutex);
    pthread_cond_destroy(&s->pause_cond);
}
