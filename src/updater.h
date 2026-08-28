/*
 * updater.h — Automatic Update Checker and Installer for WallScape.
 *
 * Checks GitHub Releases API for new version releases and downloads/installs .deb packages.
 */

#ifndef WALLSCAPE_UPDATER_H
#define WALLSCAPE_UPDATER_H

#include <stdbool.h>
#include <gtk/gtk.h>

#define WALLSCAPE_CURRENT_VERSION "1.0.0"
#define WALLSCAPE_GITHUB_REPO     "nigh8WING/wallscape"

typedef struct {
    bool update_available;
    char current_version[32];
    char latest_version[32];
    char release_url[512];
    char deb_download_url[1024];
    char release_notes[2048];
    char error_msg[256];
} UpdateInfo;

typedef void (*UpdateCheckCallback)(const UpdateInfo *info, gpointer user_data);
typedef void (*UpdateDownloadProgressCallback)(double fraction, const char *status, gpointer user_data);
typedef void (*UpdateDownloadCompleteCallback)(bool success, const char *deb_path, const char *error_msg, gpointer user_data);

/* Asynchronously checks GitHub Releases for a newer version in a background thread.
 * The callback is guaranteed to be invoked on the main GTK thread. */
void updater_check_async(UpdateCheckCallback callback, gpointer user_data);

/* Asynchronously downloads the .deb package and launches the installer. */
void updater_download_and_install_async(const char *deb_url,
                                       UpdateDownloadProgressCallback progress_cb,
                                       UpdateDownloadCompleteCallback complete_cb,
                                       gpointer user_data);

/* Compare semantic versions (e.g. "1.1.0" vs "1.0.0").
 * Returns >0 if v1 > v2, 0 if equal, <0 if v1 < v2. */
int updater_compare_versions(const char *v1, const char *v2);

#endif /* WALLSCAPE_UPDATER_H */
