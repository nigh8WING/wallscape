/*
 * wallpaper.h — SDL2 wallpaper window interface for live-wallpaper.
 *
 * Creates a borderless fullscreen SDL2 window and uses Xlib to set the
 * X11 _NET_WM_WINDOW_TYPE_DESKTOP hint, which tells Mutter/GNOME to place
 * the window behind desktop icons — just like xwinwrap and Komorebi do.
 *
 * On Wayland sessions (Zorin OS 18 default), the SDL window runs under
 * XWayland.  Mutter still honors _NET_WM_WINDOW_TYPE_DESKTOP for XWayland
 * clients, so the technique works transparently.
 */

#ifndef LIVE_WALLPAPER_WALLPAPER_H
#define LIVE_WALLPAPER_WALLPAPER_H

#include "common.h"
#include <stdbool.h>

/* Opaque wallpaper context — hides SDL internals from other modules. */
typedef struct WallpaperCtx WallpaperCtx;

/*
 * Create the wallpaper window.
 *
 * 1. Creates a borderless, fullscreen SDL2 window covering the primary display.
 * 2. Retrieves the X11 Window ID via SDL_GetWindowWMInfo().
 * 3. Sets X11 window properties:
 *    - _NET_WM_WINDOW_TYPE  → _NET_WM_WINDOW_TYPE_DESKTOP
 *    - _NET_WM_STATE        → _NET_WM_STATE_SKIP_TASKBAR | _NET_WM_STATE_SKIP_PAGER
 *    - _NET_WM_STATE_BELOW
 * 4. Creates an SDL_Renderer with hardware acceleration + vsync.
 *
 * Stores the screen dimensions in *out_w and *out_h.
 * Returns NULL on failure.
 */
WallpaperCtx *wallpaper_create(int *out_w, int *out_h);

/*
 * Destroy the wallpaper window and free all SDL resources.
 */
void wallpaper_destroy(WallpaperCtx *ctx);

/*
 * Set the video dimensions (call once after decoder opens a new file).
 * This recreates the SDL texture at the appropriate size.
 */
void wallpaper_set_video_size(WallpaperCtx *ctx, int video_w, int video_h);

/*
 * Update the texture with a new YUV420P frame and render it.
 *
 * Scales the video to cover the screen (maintaining aspect ratio) and
 * crops any excess — "cover" mode, like CSS background-size:cover.
 */
void wallpaper_render_frame(WallpaperCtx *ctx, const VideoFrame *frame);

/*
 * Process pending SDL events (call periodically from the main loop).
 * Returns false if SDL received a quit event.
 */
bool wallpaper_process_events(WallpaperCtx *ctx);

/*
 * Show or hide the wallpaper window.
 */
void wallpaper_show(WallpaperCtx *ctx);
void wallpaper_hide(WallpaperCtx *ctx);

#endif /* LIVE_WALLPAPER_WALLPAPER_H */
