/*
 * gui.h — GTK3 control panel interface for live-wallpaper.
 *
 * Provides a small window (~400×300) with controls:
 *   - Choose Video (file picker with .mp4/.mkv/.webm filter)
 *   - Pause / Resume toggle
 *   - Stop (hides wallpaper)
 *   - Quit
 *   - Status label showing the current video path
 *
 * The GUI also hosts the render timer (g_timeout_add at ~16ms = 60fps)
 * that drives SDL rendering from the GTK main loop, ensuring all SDL
 * calls happen on the main thread.
 */

#ifndef LIVE_WALLPAPER_GUI_H
#define LIVE_WALLPAPER_GUI_H

#include "common.h"
#include "wallpaper.h"

/* Opaque GUI context */
typedef struct GuiCtx GuiCtx;

/*
 * Create the GTK3 control panel window.
 * Takes references to the shared app state and wallpaper context.
 * Returns NULL on failure.
 */
GuiCtx *gui_create(AppState *state, WallpaperCtx *wallpaper);

/*
 * Destroy the GUI and free resources.
 */
void gui_destroy(GuiCtx *ctx);

/*
 * Show the control panel window (brings to front).
 */
void gui_show(GuiCtx *ctx);

/*
 * Start the render timer (~60fps) that drives SDL rendering.
 * Call this after a video has been loaded and the wallpaper texture is ready.
 */
void gui_start_render_timer(GuiCtx *ctx);

/*
 * Stop the render timer.
 * Call this when playback is stopped or the app is shutting down.
 */
void gui_stop_render_timer(GuiCtx *ctx);

/*
 * Update the status label text (e.g., show current file path).
 */
void gui_set_status(GuiCtx *ctx, const char *text);

#endif /* LIVE_WALLPAPER_GUI_H */
