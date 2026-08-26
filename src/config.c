/*
 * config.c — Read/write the live-wallpaper configuration file.
 *
 * The config file is intentionally simple: a single key=value pair
 * stored in plain text.  No JSON library is needed.
 *
 * File location: ~/.config/live-wallpaper/config.txt
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

/* Config directory and file paths (relative to $HOME). */
#define CONFIG_DIR_REL  "/.config/live-wallpaper"
#define CONFIG_FILE_REL "/.config/live-wallpaper/config.txt"
#define CONFIG_KEY      "video_path="

/*
 * Build the full config file path from $HOME.
 * Returns false if $HOME is not set or the path would be too long.
 */
static bool get_config_path(char *buf, int max_len)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        fprintf(stderr, "[config] $HOME is not set\n");
        return false;
    }
    int n = snprintf(buf, (size_t)max_len, "%s%s", home, CONFIG_FILE_REL);
    return (n > 0 && n < max_len);
}

/*
 * Build the config directory path from $HOME.
 */
static bool get_config_dir(char *buf, int max_len)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    int n = snprintf(buf, (size_t)max_len, "%s%s", home, CONFIG_DIR_REL);
    return (n > 0 && n < max_len);
}

/*
 * Create the config directory if it doesn't already exist.
 * Uses mkdir with mode 0755.  Succeeds silently if the directory exists.
 */
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

bool config_load(char *path_out, int max_len)
{
    char filepath[LW_MAX_PATH];
    if (!get_config_path(filepath, sizeof(filepath))) return false;

    FILE *fp = fopen(filepath, "r");
    if (!fp) return false;  /* config file doesn't exist yet — not an error */

    char line[LW_MAX_PATH + 32];
    bool found = false;

    while (fgets(line, (int)sizeof(line), fp)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        /* Look for "video_path=" prefix */
        if (strncmp(line, CONFIG_KEY, strlen(CONFIG_KEY)) == 0) {
            const char *value = line + strlen(CONFIG_KEY);
            if (value[0] != '\0') {
                snprintf(path_out, (size_t)max_len, "%s", value);
                found = true;
            }
            break;
        }
    }

    fclose(fp);
    return found;
}

bool config_save(const char *video_path)
{
    if (!ensure_config_dir()) return false;

    char filepath[LW_MAX_PATH];
    if (!get_config_path(filepath, sizeof(filepath))) return false;

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        fprintf(stderr, "[config] failed to write %s: %s\n",
                filepath, strerror(errno));
        return false;
    }

    fprintf(fp, "%s%s\n", CONFIG_KEY, video_path);
    fclose(fp);
    return true;
}
