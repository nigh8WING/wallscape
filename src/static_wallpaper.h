/*
 * static_wallpaper.h — Native GNOME Desktop Background (Static Wallpaper) manager.
 *
 * Provides functions to:
 *   - Apply static image/GIF wallpapers directly to GNOME / Zorin OS desktop via GSettings
 *   - Retrieve the currently active GNOME static wallpaper path
 *   - Validate supported static image formats (.jpg, .jpeg, .png, .webp, .bmp, .svg, .gif)
 */

#ifndef LIVE_WALLPAPER_STATIC_WALLPAPER_H
#define LIVE_WALLPAPER_STATIC_WALLPAPER_H

#include <stdbool.h>

/*
 * Check if a filename is a supported static image / GIF format.
 */
bool static_wallpaper_is_supported(const char *filename);

/*
 * Apply an image as the GNOME / Zorin OS desktop background (both Light and Dark modes).
 * Returns true on success.
 */
bool static_wallpaper_apply(const char *image_path);

/*
 * Get the currently active GNOME desktop background image path.
 * Returns true if a path was successfully extracted.
 */
bool static_wallpaper_get_current(char *out_path, int max_len);

/*
 * Clear the GNOME desktop background by resetting picture-uri and picture-uri-dark
 * in GSettings to empty strings, restoring the system default.
 * Returns true on success.
 */
bool static_wallpaper_clear(void);

#endif /* LIVE_WALLPAPER_STATIC_WALLPAPER_H */
