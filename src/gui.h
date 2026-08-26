/*
 * gui.h — Dual-Tab Sidebar Studio Interface for Live and Static Wallpapers.
 */

#ifndef LIVE_WALLPAPER_GUI_H
#define LIVE_WALLPAPER_GUI_H

#include "common.h"
#include "wallpaper.h"

typedef struct GuiCtx GuiCtx;

/*
 * Create the dual-mode GTK3 studio window with sidebar navigation.
 */
GuiCtx *gui_create(AppState *state, WallpaperCtx *wallpaper);

/*
 * Destroy GUI resources cleanly.
 */
void gui_destroy(GuiCtx *ctx);

/*
 * Show the studio window.
 */
void gui_show(GuiCtx *ctx);

/*
 * Start/stop render timer (~60fps).
 */
void gui_start_render_timer(GuiCtx *ctx);
void gui_stop_render_timer(GuiCtx *ctx);

/*
 * Update status label.
 */
void gui_set_status(GuiCtx *ctx, const char *text);

/*
 * Autoload initial video and scan folder on startup.
 */
void gui_load_video_and_scan_folder(GuiCtx *ctx, const char *filepath);

#endif /* LIVE_WALLPAPER_GUI_H */
