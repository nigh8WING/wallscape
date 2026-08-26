/*
 * config.h — Configuration file load/save for live-wallpaper.
 *
 * Stores a single setting (last-used video path) in a plain text file at:
 *   ~/.config/live-wallpaper/config.txt
 *
 * Format:
 *   video_path=/path/to/video.mp4
 */

#ifndef LIVE_WALLPAPER_CONFIG_H
#define LIVE_WALLPAPER_CONFIG_H

#include <stdbool.h>
#include "common.h"

/*
 * Load the saved video path from the config file.
 * If the file exists and contains a valid path, copies it into path_out
 * (up to LW_MAX_PATH bytes) and returns true.
 * Returns false if the config file doesn't exist or is empty.
 */
bool config_load(char *path_out, int max_len);

/*
 * Save the video path to the config file.
 * Creates ~/.config/live-wallpaper/ if it doesn't exist.
 * Returns true on success.
 */
bool config_save(const char *video_path);

#endif /* LIVE_WALLPAPER_CONFIG_H */
