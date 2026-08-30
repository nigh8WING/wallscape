/*
 * static_wallpaper.c — Native GNOME Desktop Background (Static Wallpaper) manager.
 *
 * Uses GLib's GSettings API to interface with org.gnome.desktop.background.
 * Ensures strict resource cleanup (g_object_unref, g_free) with zero memory leaks.
 */

#include "static_wallpaper.h"
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

static const char *const STATIC_EXTS[] = {
    ".jpg", ".jpeg", ".png", ".webp", ".bmp", ".svg", ".gif", NULL
};

bool static_wallpaper_is_supported(const char *filename)
{
    if (!filename) return false;
    size_t name_len = strlen(filename);

    for (int i = 0; STATIC_EXTS[i] != NULL; i++) {
        size_t ext_len = strlen(STATIC_EXTS[i]);
        if (name_len >= ext_len) {
            if (strcasecmp(filename + (name_len - ext_len), STATIC_EXTS[i]) == 0) {
                return true;
            }
        }
    }
    return false;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool static_wallpaper_apply(const char *image_path)
{
    if (!image_path || !image_path[0]) return false;

    wchar_t wpath[MAX_PATH];
    int res = MultiByteToWideChar(CP_UTF8, 0, image_path, -1, wpath, MAX_PATH);
    if (res == 0) {
        fprintf(stderr, "[static_wallpaper] Failed to convert path to wide string: %s\n", image_path);
        return false;
    }

    BOOL ok = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void *)wpath, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    if (!ok) {
        fprintf(stderr, "[static_wallpaper] SystemParametersInfoW failed (error %lu)\n", GetLastError());
        return false;
    }

    fprintf(stderr, "[static_wallpaper] Applied Windows static wallpaper: %s\n", image_path);
    return true;
}

bool static_wallpaper_clear(void)
{
    BOOL ok = SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void *)L"", SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    fprintf(stderr, "[static_wallpaper] Cleared static wallpaper on Windows.\n");
    return (ok != FALSE);
}

bool static_wallpaper_get_current(char *out_path, int max_len)
{
    if (!out_path || max_len <= 0) return false;

    wchar_t wpath[MAX_PATH] = {0};
    BOOL ok = SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, (void *)wpath, 0);
    if (!ok || wpath[0] == L'\0') {
        return false;
    }

    int res = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out_path, max_len, NULL, NULL);
    return (res > 0);
}
#else
bool static_wallpaper_apply(const char *image_path)
{
    if (!image_path || !image_path[0]) return false;

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) return false;

    GSettingsSchema *schema = g_settings_schema_source_lookup(source, "org.gnome.desktop.background", TRUE);
    if (!schema) {
        fprintf(stderr, "[static_wallpaper] Schema org.gnome.desktop.background not found\n");
        return false;
    }
    g_settings_schema_unref(schema);

    GSettings *settings = g_settings_new("org.gnome.desktop.background");
    if (!settings) return false;

    /* Build file:// URI */
    char *uri = NULL;
    if (strncmp(image_path, "file://", 7) == 0) {
        uri = g_strdup(image_path);
    } else {
        uri = g_filename_to_uri(image_path, NULL, NULL);
    }

    if (!uri) {
        g_object_unref(settings);
        return false;
    }

    /* Set for both Light and Dark GNOME modes */
    g_settings_set_string(settings, "picture-uri", uri);
    g_settings_set_string(settings, "picture-uri-dark", uri);
    g_settings_set_string(settings, "picture-options", "zoom");
    g_settings_sync();

    g_free(uri);
    g_object_unref(settings);

    fprintf(stderr, "[static_wallpaper] Applied static wallpaper: %s\n", image_path);
    return true;
}

bool static_wallpaper_clear(void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) return false;

    GSettingsSchema *schema = g_settings_schema_source_lookup(source, "org.gnome.desktop.background", TRUE);
    if (!schema) return false;
    g_settings_schema_unref(schema);

    GSettings *settings = g_settings_new("org.gnome.desktop.background");
    if (!settings) return false;

    /* Reset picture URIs to empty — GNOME falls back to its default solid colour. */
    g_settings_set_string(settings, "picture-uri", "");
    g_settings_set_string(settings, "picture-uri-dark", "");
    g_settings_sync();

    g_object_unref(settings);
    fprintf(stderr, "[static_wallpaper] Cleared static wallpaper (reset GSettings).\n");
    return true;
}

bool static_wallpaper_get_current(char *out_path, int max_len)
{
    if (!out_path || max_len <= 0) return false;

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) return false;

    GSettingsSchema *schema = g_settings_schema_source_lookup(source, "org.gnome.desktop.background", TRUE);
    if (!schema) return false;
    g_settings_schema_unref(schema);

    GSettings *settings = g_settings_new("org.gnome.desktop.background");
    if (!settings) return false;

    char *uri = g_settings_get_string(settings, "picture-uri");
    if (!uri || !uri[0]) {
        if (uri) g_free(uri);
        uri = g_settings_get_string(settings, "picture-uri-dark");
    }

    bool success = false;
    if (uri && uri[0]) {
        char *filename = g_filename_from_uri(uri, NULL, NULL);
        if (filename) {
            snprintf(out_path, (size_t)max_len, "%s", filename);
            g_free(filename);
            success = true;
        }
    }

    if (uri) g_free(uri);
    g_object_unref(settings);
    return success;
}
#endif
