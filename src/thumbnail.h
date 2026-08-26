/*
 * thumbnail.h — Video and Image thumbnail extraction module.
 *
 * Fast, leak-free thumbnail extraction for video and static image files.
 */

#ifndef LIVE_WALLPAPER_THUMBNAIL_H
#define LIVE_WALLPAPER_THUMBNAIL_H

#include <gdk-pixbuf/gdk-pixbuf.h>

/* Standard compact grid thumbnail dimensions */
#define THUMB_WIDTH  130
#define THUMB_HEIGHT 75

/*
 * Generate a thumbnail from any video or image file.
 * Returns a new GdkPixbuf* (caller must call g_object_unref() when done).
 * Returns NULL if thumbnail generation fails.
 */
GdkPixbuf *thumbnail_generate(const char *filepath, int target_w, int target_h);

#endif /* LIVE_WALLPAPER_THUMBNAIL_H */
