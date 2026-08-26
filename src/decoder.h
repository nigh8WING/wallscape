/*
 * decoder.h — FFmpeg video decoder interface for live-wallpaper.
 *
 * The decoder runs on a dedicated thread, reading and decoding video packets
 * from the input file, converting them to YUV420P, and pushing them into
 * the frame queue for the renderer to consume.
 *
 * On EOF the decoder seeks back to timestamp 0 for infinite looping.
 * Audio streams are completely ignored.
 */

#ifndef LIVE_WALLPAPER_DECODER_H
#define LIVE_WALLPAPER_DECODER_H

#include "common.h"

/*
 * Start the decoder thread for the given video file.
 *
 * Opens the file with FFmpeg, finds the first video stream, and spawns
 * a thread that decodes frames and pushes them to state->queue.
 *
 * The decoder thread respects:
 *   state->quit   → exit the thread
 *   state->paused → sleep until resumed
 *
 * Populates state->video_width, video_height, video_fps on success.
 *
 * Returns 0 on success, -1 on error (bad file, no video stream, etc.).
 */
int decoder_start(AppState *state, const char *filepath);

/*
 * Stop the decoder thread and close all FFmpeg resources.
 *
 * Signals the decoder to quit, aborts the frame queue (to unblock any
 * waiting push), and joins the thread.  Safe to call even if no decoder
 * is running (no-op in that case).
 */
void decoder_stop(AppState *state);

/*
 * Pause the decoder thread.
 * The thread will sleep on state->pause_cond until decoder_resume() is called.
 */
void decoder_pause(AppState *state);

/*
 * Resume the decoder thread after a pause.
 */
void decoder_resume(AppState *state);

#endif /* LIVE_WALLPAPER_DECODER_H */
