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
#include <unistd.h>
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
    char lines[64][LW_MAX_PATH + 64];
    int count = 0;
    size_t keylen = strlen(key);
    bool updated = false;

    FILE *fp = fopen(filepath, "r");
    if (fp) {
        while (count < 64 && fgets(lines[count], (int)sizeof(lines[count]), fp)) {
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

    if (!updated && count < 64) {
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

static bool config_load_folder_list(const char *key, const char *fallback_key,
                                    char folders[][LW_MAX_PATH], int max_count, int *out_count)
{
    if (!folders || max_count <= 0 || !out_count) return false;
    *out_count = 0;

    char raw[LW_MAX_PATH * 32 + 128] = {0};
    if (config_load_key(key, raw, sizeof(raw)) && raw[0] != '\0') {
        char *saveptr = NULL;
        char *token = strtok_r(raw, "|;\n\r", &saveptr);
        while (token && *out_count < max_count) {
            while (*token == ' ' || *token == '\t') token++;
            if (*token != '\0') {
                snprintf(folders[*out_count], LW_MAX_PATH, "%s", token);
                (*out_count)++;
            }
            token = strtok_r(NULL, "|;\n\r", &saveptr);
        }
    }

    if (*out_count == 0 && fallback_key) {
        char single[LW_MAX_PATH] = {0};
        if (config_load_key(fallback_key, single, sizeof(single)) && single[0] != '\0') {
            snprintf(folders[0], LW_MAX_PATH, "%s", single);
            *out_count = 1;
        }
    }

    return (*out_count > 0);
}

static bool config_save_folder_list(const char *key, const char *single_key,
                                    const char folders[][LW_MAX_PATH], int count)
{
    char buf[LW_MAX_PATH * 32 + 128] = {0};
    size_t off = 0;

    for (int i = 0; i < count && i < MAX_CONFIG_FOLDERS; i++) {
        if (!folders[i][0]) continue;
        int n = snprintf(buf + off, sizeof(buf) - off, "%s%s", (off > 0 ? "|" : ""), folders[i]);
        if (n > 0 && (size_t)n < sizeof(buf) - off) {
            off += (size_t)n;
        }
    }

    config_save_key(key, buf);
    if (single_key) {
        config_save_key(single_key, (count > 0 && folders[0][0]) ? folders[0] : "");
    }
    return true;
}

bool config_load_live_folders(char folders[][LW_MAX_PATH], int max_count, int *out_count)
{
    return config_load_folder_list("live_folders=", "live_folder=", folders, max_count, out_count);
}

bool config_save_live_folders(const char folders[][LW_MAX_PATH], int count)
{
    return config_save_folder_list("live_folders=", "live_folder=", folders, count);
}

bool config_load_static_folders(char folders[][LW_MAX_PATH], int max_count, int *out_count)
{
    return config_load_folder_list("static_folders=", "static_folder=", folders, max_count, out_count);
}

bool config_save_static_folders(const char folders[][LW_MAX_PATH], int count)
{
    return config_save_folder_list("static_folders=", "static_folder=", folders, count);
}

bool config_load_static_path(char *path_out, int max_len)
{
    return config_load_key("static_path=", path_out, max_len);
}

bool config_save_static_path(const char *path)
{
    return config_save_key("static_path=", path);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Autostart Management (~/.config/autostart/live-wallpaper.desktop)
 * ──────────────────────────────────────────────────────────────────────────── */

static bool get_autostart_desktop_path(char *out, size_t out_sz)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    snprintf(out, out_sz, "%s/.config/autostart/live-wallpaper.desktop", home);
    return true;
}

bool autostart_is_enabled(void)
{
    char path[LW_MAX_PATH];
    if (!get_autostart_desktop_path(path, sizeof(path))) return false;
    if (access(path, F_OK) != 0) return false;

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[256];
    bool enabled = true;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "X-GNOME-Autostart-enabled=false", 31) == 0 ||
            strncmp(line, "Hidden=true", 11) == 0) {
            enabled = false;
            break;
        }
    }
    fclose(fp);
    return enabled;
}

bool autostart_set_enabled(bool enabled)
{
    char path[LW_MAX_PATH];
    if (!get_autostart_desktop_path(path, sizeof(path))) return false;

    if (!enabled) {
        remove(path);
        return true;
    }

    const char *home = getenv("HOME");
    char dir[LW_MAX_PATH];
    snprintf(dir, sizeof(dir), "%s/.config/autostart", home);
    mkdir(dir, 0755);

    FILE *fp = fopen(path, "w");
    if (!fp) return false;

    fprintf(fp,
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=WallScape\n"
            "Comment=Live Video & Static Desktop Wallpaper Studio\n"
            "Exec=live-wallpaper --no-gui\n"
            "Icon=live-wallpaper\n"
            "Terminal=false\n"
            "Categories=Utility;Graphics;Settings;\n"
            "X-GNOME-Autostart-enabled=true\n"
            "StartupNotify=false\n");
    fclose(fp);
    return true;
}
