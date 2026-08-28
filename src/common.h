/*
 * common.h — Shared types, frame queue, and application state for live-wallpaper.
 *
 * This header defines the core data structures used across all components:
 *   - VideoFrame: a decoded video frame in YUV420P format
 *   - FrameQueue: a bounded, thread-safe FIFO for passing frames decoder→renderer
 *   - AppState:   the central application state shared by all modules
 *
 * Design notes:
 *   - No SDL or GTK headers are included here to keep dependencies minimal.
 *   - Atomic flags are used for cross-thread communication (quit, pause, playing).
 *   - The frame queue uses pthreads mutex + condvars with blocking push (backpressure)
 *     and non-blocking pop (renderer never stalls).
 */

#ifndef LIVE_WALLPAPER_COMMON_H
#define LIVE_WALLPAPER_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* Maximum number of buffered decoded frames (decoder → renderer).
 * 8 slots provides enough headroom for 60fps video without stalling the
 * decoder thread while the renderer catches up on a slow frame. */
#define FRAME_QUEUE_CAPACITY 8

/* Maximum file path length. */
#define LW_MAX_PATH 4096

/* ─────────────────────────────────────────────────────────────────────────────
 * VideoFrame — a single decoded video frame in YUV420P format.
 *
 * YUV420P stores luma (Y) at full resolution, and chroma (U, V) at half
 * resolution in each dimension.  This matches SDL_PIXELFORMAT_IYUV, allowing
 * direct upload via SDL_UpdateYUVTexture() with no further conversion.
 *
 * Memory layout:
 *   y  → y_pitch × height      bytes  (luma plane)
 *   u  → uv_pitch × height/2   bytes  (chroma-blue plane)
 *   v  → uv_pitch × height/2   bytes  (chroma-red plane)
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t *y;          /* Luma plane data (caller-allocated)       */
    uint8_t *u;          /* Chroma-blue plane data                   */
    uint8_t *v;          /* Chroma-red plane data                    */
    int      y_pitch;    /* Bytes per row in Y plane                 */
    int      uv_pitch;   /* Bytes per row in U and V planes          */
    int      width;      /* Frame width in pixels                    */
    int      height;     /* Frame height in pixels                   */
} VideoFrame;

/* Free the heap-allocated plane buffers inside a VideoFrame.
 * Call this on frames returned by frame_queue_pop(). */
static inline void video_frame_free(VideoFrame *f)
{
    if (f) {
        free(f->y);  f->y = NULL;
        free(f->u);  f->u = NULL;
        free(f->v);  f->v = NULL;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * FrameQueue — bounded, thread-safe FIFO queue.
 *
 * The decoder thread pushes decoded frames, and the render callback (on the
 * main thread) pops them.  The queue uses:
 *   - Blocking push:     if the queue is full, the decoder sleeps until a
 *                         slot opens up (natural backpressure).
 *   - Non-blocking pop:  the renderer never stalls; if the queue is empty,
 *                         pop returns false and the renderer re-displays the
 *                         previous frame.
 *   - Abort flag:        wakes all blocked pushers so threads can exit cleanly.
 *
 * Each queue slot maintains its own pre-allocated buffers; push memcpy's into
 * a slot, and pop deep-copies out (so the renderer owns the data safely).
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    VideoFrame  frames[FRAME_QUEUE_CAPACITY]; /* ring buffer slots            */
    int         read_idx;                     /* next slot to pop from        */
    int         write_idx;                    /* next slot to push into       */
    int         count;                        /* current number of frames     */
    pthread_mutex_t mutex;
    pthread_cond_t  cond_push;   /* signaled when a slot becomes available    */
    pthread_cond_t  cond_pop;    /* signaled when a frame is pushed           */
    atomic_bool     abort;       /* when true, all waiters wake and return    */
} FrameQueue;

/* Initialize a frame queue (zeroes all slots, creates mutex/condvars). */
void frame_queue_init(FrameQueue *q);

/* Destroy a frame queue (frees all slot buffers, destroys mutex/condvars). */
void frame_queue_destroy(FrameQueue *q);

/*
 * Push a decoded frame into the queue.
 *
 * Copies the YUV plane data into the next available slot.  If the queue is
 * full, the calling thread blocks until either a slot opens (renderer pops)
 * or frame_queue_abort() is called.
 *
 * Parameters:
 *   y, u, v        — source plane pointers (from sws_scale output)
 *   y_pitch        — bytes per row in Y plane
 *   uv_pitch       — bytes per row in U/V planes
 *   width, height  — frame dimensions
 *
 * Returns true on success, false if the queue was aborted.
 */
bool frame_queue_push(FrameQueue *q,
                      const uint8_t *y, int y_pitch,
                      const uint8_t *u, const uint8_t *v, int uv_pitch,
                      int width, int height);

/*
 * Pop a frame from the queue (non-blocking).
 *
 * If a frame is available, deep-copies it into *out (allocates new buffers).
 * The caller MUST call video_frame_free(out) when done.
 *
 * Returns true if a frame was popped, false if the queue is empty.
 */
bool frame_queue_pop(FrameQueue *q, VideoFrame *out);

/* Discard all queued frames and free their buffers. */
void frame_queue_flush(FrameQueue *q);

/* Signal all waiting threads to wake up and exit.
 * Call this before joining the decoder thread during shutdown. */
void frame_queue_abort(FrameQueue *q);

/* Reset the abort flag so the queue can be reused for a new video. */
void frame_queue_reset(FrameQueue *q);

/* ─────────────────────────────────────────────────────────────────────────────
 * AppState — central application state shared by all modules.
 *
 * Atomic booleans are used for flags that are read/written from multiple
 * threads (decoder thread + main thread).  The pause_mutex/pause_cond pair
 * is used to put the decoder to sleep while playback is paused.
 * ──────────────────────────────────────────────────────────────────────────── */
typedef struct {
    /* ── Control flags (atomic for cross-thread access) ── */
    atomic_bool quit;           /* true → application is shutting down        */
    atomic_bool decoder_quit;   /* true → current decoder thread must exit
                                 * (set by decoder_stop; reset by decoder_start;
                                 * scoped to one video, NOT the whole app)     */
    atomic_bool paused;         /* true → playback is paused                  */
    atomic_bool playing;        /* true → a video is loaded and active        */
    atomic_bool decoder_ready;  /* true → decoder opened stream, metadata set */

    /* ── Current video metadata ── */
    char   video_path[LW_MAX_PATH];
    atomic_int video_width;     /* atomic: written by decoder, read by GUI    */
    atomic_int video_height;    /* atomic: written by decoder, read by GUI    */
    double video_fps;           /* frames per second (set before ready flag)  */

    /* ── Screen / display info ── */
    int    screen_width;
    int    screen_height;

    /* ── Frame queue (decoder → renderer) ── */
    FrameQueue queue;

    /* ── Decoder thread ── */
    pthread_t decoder_tid;
    bool      decoder_running;  /* only accessed from main thread             */

    /* ── Pause synchronization ── */
    pthread_mutex_t pause_mutex;
    pthread_cond_t  pause_cond;
} AppState;

/* Zero-initialize all fields and create synchronization primitives. */
void app_state_init(AppState *s);

/* Destroy synchronization primitives and free queue resources. */
void app_state_destroy(AppState *s);

#endif /* LIVE_WALLPAPER_COMMON_H */
