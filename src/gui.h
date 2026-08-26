/*
 * gui.h — GTK3 control panel & wallpaper selector interface for live-wallpaper.
 *
 * Provides a dedicated gallery & control panel window:
 *   - Displays all video wallpapers found in the current folder in a selectable list
 *   - Highlights the currently active wallpaper
 *   - Allows selecting/switching wallpapers instantly with a single click
 *   - Choose Video & Choose Folder buttons
 *   - Playback controls: Pause / Resume, Stop, Quit
 *   - Drives SDL2 rendering (~60fps) on the main GTK loop
 */

#ifndef LIVE_WALLPAPER_GUI_H
#define LIVE_WALLPAPER_GUI_H

#include "common.h"
#include "wallpaper.h"

/* Opaque GUI context */
typedef struct GuiCtx GuiCtx;

/*
 * Create the GTK3 control panel and wallpaper selector window.
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
 */
void gui_start_render_timer(GuiCtx *ctx);

/*
 * Stop the render timer.
 */
void gui_stop_render_timer(GuiCtx *ctx);

/*
 * Update the status label text.
 */
void gui_set_status(GuiCtx *ctx, const char *text);

/*
 * Load a video file and automatically scan its parent folder to populate the wallpaper list.
 */
void gui_load_video_and_scan_folder(GuiCtx *ctx, const char *filepath);

#endif /* LIVE_WALLPAPER_GUI_H */
