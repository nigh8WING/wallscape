/*
 * thumbnail.h — Video thumbnail extraction using FFmpeg and GdkPixbuf.
 *
 * Extracts a frame from a video file, scales it to the target dimensions,
 * and returns a GdkPixbuf* ready for GTK3 rendering.
 */

#ifndef LIVE_WALLPAPER_THUMBNAIL_H
#define LIVE_WALLPAPER_THUMBNAIL_H

#include <gdk-pixbuf/gdk-pixbuf.h>

/*
 * Generate a thumbnail from a video file.
 * Returns a new GdkPixbuf* (caller must g_object_unref when done).
 * Returns NULL if thumbnail generation fails.
 */
GdkPixbuf *thumbnail_generate(const char *video_path, int target_w, int target_h);

#endif /* LIVE_WALLPAPER_THUMBNAIL_H */
