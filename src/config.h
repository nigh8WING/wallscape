/*
 * config.h — Configuration file load/save for live-wallpaper.
 *
 * Stores settings in a plain text key=value file at:
 *   ~/.config/live-wallpaper/config.txt
 *
 * Supported keys:
 *   video_path=          Last active live wallpaper video file
 *   live_folder=         Last loaded live wallpaper folder
 *   static_folder=       Last loaded static image folder
 */

#ifndef LIVE_WALLPAPER_CONFIG_H
#define LIVE_WALLPAPER_CONFIG_H

#include <stdbool.h>
#include "common.h"

/* Load the saved video path. Returns true if found. */
bool config_load(char *path_out, int max_len);

/* Save the video path to the config file. */
bool config_save(const char *video_path);

/* Load the last-used live video folder. Returns true if found. */
bool config_load_live_folder(char *folder_out, int max_len);

/* Save the last-used live video folder. */
bool config_save_live_folder(const char *folder);

/* Load the last-used static image folder. Returns true if found. */
bool config_load_static_folder(char *folder_out, int max_len);

/* Save the last-used static image folder. */
bool config_save_static_folder(const char *folder);

#endif /* LIVE_WALLPAPER_CONFIG_H */
