/*
 * config.c — Read/write the live-wallpaper configuration file.
 *
 * File location: ~/.config/live-wallpaper/config.txt
 *
 * Format (key=value, one per line):
 *   video_path=/home/user/Videos/video.mp4
 *   live_folder=/home/user/Videos/Wallpapers
 *   static_folder=/home/user/Pictures/Wallpapers
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define CONFIG_DIR_REL   "/.config/live-wallpaper"
#define CONFIG_FILE_REL  "/.config/live-wallpaper/config.txt"

/* ─── Internal helpers ────────────────────────────────────────────────────── */

static bool get_config_path(char *buf, int max_len)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    int n = snprintf(buf, (size_t)max_len, "%s%s", home, CONFIG_FILE_REL);
    return (n > 0 && n < max_len);
}

static bool get_config_dir(char *buf, int max_len)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    int n = snprintf(buf, (size_t)max_len, "%s%s", home, CONFIG_DIR_REL);
    return (n > 0 && n < max_len);
}

static bool ensure_config_dir(void)
{
    char dir[LW_MAX_PATH];
    if (!get_config_dir(dir, sizeof(dir))) return false;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[config] failed to create %s: %s\n", dir, strerror(errno));
        return false;
    }
    return true;
}

/*
 * Generic load: reads the value for a given key from the config file.
 */
static bool config_load_key(const char *key, char *out, int max_len)
{
    char filepath[LW_MAX_PATH];
    if (!get_config_path(filepath, sizeof(filepath))) return false;

    FILE *fp = fopen(filepath, "r");
    if (!fp) return false;

    char line[LW_MAX_PATH + 64];
    bool found = false;
    size_t keylen = strlen(key);

    while (fgets(line, (int)sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (strncmp(line, key, keylen) == 0) {
            const char *value = line + keylen;
            if (value[0] != '\0') {
                snprintf(out, (size_t)max_len, "%s", value);
                found = true;
            }
            break;
        }
    }

    fclose(fp);
    return found;
}

/*
 * Generic save: updates or inserts a key=value pair in the config file.
 * Preserves all other existing keys.
 */
static bool config_save_key(const char *key, const char *value)
{
    if (!ensure_config_dir()) return false;

    char filepath[LW_MAX_PATH];
    if (!get_config_path(filepath, sizeof(filepath))) return false;

    /* Read existing lines */
    char lines[16][LW_MAX_PATH + 64];
    int count = 0;
    size_t keylen = strlen(key);
    bool updated = false;

    FILE *fp = fopen(filepath, "r");
    if (fp) {
        while (count < 16 && fgets(lines[count], (int)sizeof(lines[count]), fp)) {
            size_t len = strlen(lines[count]);
            while (len > 0 && (lines[count][len - 1] == '\n' || lines[count][len - 1] == '\r'))
                lines[count][--len] = '\0';

            if (strncmp(lines[count], key, keylen) == 0) {
                snprintf(lines[count], sizeof(lines[count]), "%s%s", key, value);
                updated = true;
            }
            count++;
        }
        fclose(fp);
    }

    if (!updated && count < 16) {
        snprintf(lines[count], sizeof(lines[count]), "%s%s", key, value);
        count++;
    }

    FILE *out = fopen(filepath, "w");
    if (!out) {
        fprintf(stderr, "[config] failed to write %s: %s\n", filepath, strerror(errno));
        return false;
    }

    for (int i = 0; i < count; i++) {
        fprintf(out, "%s\n", lines[i]);
    }
    fclose(out);
    return true;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

bool config_load(char *path_out, int max_len)
{
    return config_load_key("video_path=", path_out, max_len);
}

bool config_save(const char *video_path)
{
    return config_save_key("video_path=", video_path);
}

bool config_load_live_folder(char *folder_out, int max_len)
{
    return config_load_key("live_folder=", folder_out, max_len);
}

bool config_save_live_folder(const char *folder)
{
    return config_save_key("live_folder=", folder);
}

bool config_load_static_folder(char *folder_out, int max_len)
{
    return config_load_key("static_folder=", folder_out, max_len);
}

bool config_save_static_folder(const char *folder)
{
    return config_save_key("static_folder=", folder);
}
