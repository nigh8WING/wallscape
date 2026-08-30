#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <ctype.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <shellapi.h>
#define access _access
#define R_OK 4
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

#include "updater.h"

/* Internal context for passing check results to GTK main thread */
typedef struct {
    UpdateCheckCallback callback;
    gpointer            user_data;
    UpdateInfo          info;
} CheckTaskCtx;

/* Internal context for download task */
typedef struct {
    char deb_url[1024];
    UpdateDownloadProgressCallback progress_cb;
    UpdateDownloadCompleteCallback complete_cb;
    gpointer user_data;
} DownloadTaskCtx;

typedef struct {
    UpdateDownloadCompleteCallback complete_cb;
    gpointer user_data;
    bool success;
    bool installed_directly;
    char deb_path[512];
    char error_msg[256];
} DownloadResultCtx;

/* ─────────────────────────────────────────────────────────────────────────────
 * Semantic Version Comparison
 * ──────────────────────────────────────────────────────────────────────────── */
static void parse_version_parts(const char *ver, int *major, int *minor, int *patch)
{
    *major = *minor = *patch = 0;
    if (!ver) return;

    /* Skip leading 'v' or 'V' */
    while (*ver && (*ver == 'v' || *ver == 'V' || isspace((unsigned char)*ver))) {
        ver++;
    }

    sscanf(ver, "%d.%d.%d", major, minor, patch);
}

int updater_compare_versions(const char *v1, const char *v2)
{
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;

    parse_version_parts(v1, &maj1, &min1, &pat1);
    parse_version_parts(v2, &maj2, &min2, &pat2);

    if (maj1 != maj2) return maj1 - maj2;
    if (min1 != min2) return min1 - min2;
    return pat1 - pat2;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * JSON Parser Helpers (Lightweight & dependency-free)
 * ──────────────────────────────────────────────────────────────────────────── */
static bool extract_json_string(const char *json, const char *key, char *out, size_t out_sz)
{
    if (!json || !key || !out || out_sz == 0) return false;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return false;

    pos += strlen(search_key);
    while (*pos && (*pos == ':' || isspace((unsigned char)*pos))) pos++;

    if (*pos != '"') return false;
    pos++; /* skip opening quote */

    size_t i = 0;
    while (*pos && *pos != '"' && i < out_sz - 1) {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            if (*pos == 'n') {
                out[i++] = '\n';
            } else if (*pos == 'r') {
                /* skip CR */
            } else if (*pos == 't') {
                out[i++] = '\t';
            } else if (*pos == '"') {
                out[i++] = '"';
            } else if (*pos == '\\') {
                out[i++] = '\\';
            } else {
                out[i++] = *pos;
            }
        } else {
            out[i++] = *pos;
        }
        pos++;
    }
    out[i] = '\0';
    return i > 0;
}

static bool extract_deb_download_url(const char *json, char *out, size_t out_sz)
{
    if (!json || !out || out_sz == 0) return false;

    const char *pos = json;
    while ((pos = strstr(pos, "\"browser_download_url\"")) != NULL) {
        pos += strlen("\"browser_download_url\"");
        while (*pos && (*pos == ':' || isspace((unsigned char)*pos))) pos++;

        if (*pos == '"') {
            pos++;
            const char *url_start = pos;
            while (*pos && *pos != '"') pos++;
            size_t len = (size_t)(pos - url_start);

#ifdef _WIN32
            if ((len > 4 && (strncmp(pos - 4, ".exe", 4) == 0 || strncmp(pos - 4, ".zip", 4) == 0)) ||
                (len > 9 && strstr(url_start, "Setup.exe") != NULL)) {
                if (len >= out_sz) len = out_sz - 1;
                strncpy(out, url_start, len);
                out[len] = '\0';
                return true;
            }
#else
            if (len > 4 && strncmp(pos - 4, ".deb", 4) == 0) {
                if (len >= out_sz) len = out_sz - 1;
                strncpy(out, url_start, len);
                out[len] = '\0';
                return true;
            }
#endif
        }
    }
    return false;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Check Update Background Worker
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean dispatch_check_result_on_main(gpointer data)
{
    CheckTaskCtx *task = (CheckTaskCtx *)data;
    if (task->callback) {
        task->callback(&task->info, task->user_data);
    }
    free(task);
    return G_SOURCE_REMOVE;
}

static void *updater_check_thread(void *arg)
{
    CheckTaskCtx *task = (CheckTaskCtx *)arg;
    memset(&task->info, 0, sizeof(UpdateInfo));
    snprintf(task->info.current_version, sizeof(task->info.current_version), "%s", WALLSCAPE_CURRENT_VERSION);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s -L --max-time 8 -H \"User-Agent: WallScape-App/%s\" "
             "\"https://api.github.com/repos/%s/releases/latest\"",
             WALLSCAPE_CURRENT_VERSION, WALLSCAPE_GITHUB_REPO);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(task->info.error_msg, sizeof(task->info.error_msg), "Could not execute curl to check for updates.");
        g_idle_add(dispatch_check_result_on_main, task);
        return NULL;
    }

    /* Read response buffer (up to 256KB) */
    size_t cap = 262144;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        pclose(fp);
        snprintf(task->info.error_msg, sizeof(task->info.error_msg), "Out of memory allocating API buffer.");
        g_idle_add(dispatch_check_result_on_main, task);
        return NULL;
    }

    size_t nread = fread(buf, 1, cap - 1, fp);
    buf[nread] = '\0';
    pclose(fp);

    if (nread == 0) {
        snprintf(task->info.error_msg, sizeof(task->info.error_msg), "No releases published yet on GitHub repository.");
        free(buf);
        g_idle_add(dispatch_check_result_on_main, task);
        return NULL;
    }

    /* Check for GitHub API error message (e.g. rate limit or Not Found) */
    if (strstr(buf, "\"message\": \"Not Found\"")) {
        snprintf(task->info.error_msg, sizeof(task->info.error_msg), "No releases published yet on GitHub repository.");
        free(buf);
        g_idle_add(dispatch_check_result_on_main, task);
        return NULL;
    }

    /* Extract tag_name */
    char raw_tag[64] = {0};
    if (!extract_json_string(buf, "tag_name", raw_tag, sizeof(raw_tag))) {
        snprintf(task->info.error_msg, sizeof(task->info.error_msg), "Invalid response from GitHub Releases API.");
        free(buf);
        g_idle_add(dispatch_check_result_on_main, task);
        return NULL;
    }

    /* Clean version string */
    const char *ver_p = raw_tag;
    while (*ver_p == 'v' || *ver_p == 'V' || isspace((unsigned char)*ver_p)) ver_p++;
    snprintf(task->info.latest_version, sizeof(task->info.latest_version), "%s", ver_p);

    extract_json_string(buf, "html_url", task->info.release_url, sizeof(task->info.release_url));
    extract_json_string(buf, "body", task->info.release_notes, sizeof(task->info.release_notes));
    extract_deb_download_url(buf, task->info.deb_download_url, sizeof(task->info.deb_download_url));

    /* If release body is empty or generic 'Full Changelog', provide clear feature notes */
    if (task->info.release_notes[0] == '\0' || strncmp(task->info.release_notes, "**Full Changelog**:", 19) == 0) {
        snprintf(task->info.release_notes, sizeof(task->info.release_notes),
                 "• 🚀 Start on Boot: Option to launch WallScape silently on system login.\n"
                 "• 🔄 In-Folder Refresh: Rescan folder to dynamically detect and add new wallpapers.\n"
                 "• ⚡ Hardware Acceleration: Universal GPU decoding (D3D11VA, VA-API, CUDA, Vulkan).\n"
                 "• 📦 Seamless in-place auto-update with auto-restart.");
    }

    /* Compare version */
    if (updater_compare_versions(task->info.latest_version, task->info.current_version) > 0) {
        task->info.update_available = true;
    }

    free(buf);
    g_idle_add(dispatch_check_result_on_main, task);
    return NULL;
}

void updater_check_async(UpdateCheckCallback callback, gpointer user_data)
{
    CheckTaskCtx *task = (CheckTaskCtx *)calloc(1, sizeof(CheckTaskCtx));
    if (!task) return;

    task->callback = callback;
    task->user_data = user_data;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, updater_check_thread, task) != 0) {
        free(task);
    }
    pthread_attr_destroy(&attr);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Download and Install Background Worker
 * ──────────────────────────────────────────────────────────────────────────── */
static gboolean dispatch_download_result_on_main(gpointer data)
{
    DownloadResultCtx *res = (DownloadResultCtx *)data;
    if (res->complete_cb) {
        res->complete_cb(res->success, res->installed_directly, res->deb_path, res->error_msg, res->user_data);
    }
    free(res);
    return G_SOURCE_REMOVE;
}

static void *updater_download_thread(void *arg)
{
    DownloadTaskCtx *task = (DownloadTaskCtx *)arg;
    DownloadResultCtx *res = (DownloadResultCtx *)calloc(1, sizeof(DownloadResultCtx));
    if (!res) {
        free(task);
        return NULL;
    }

    res->complete_cb = task->complete_cb;
    res->user_data = task->user_data;

#ifdef _WIN32
    const char *temp = getenv("TEMP");
    if (!temp || !temp[0]) temp = "C:\\Windows\\Temp";
    snprintf(res->deb_path, sizeof(res->deb_path), "%s\\wallscape-setup.exe", temp);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -s -L -f -o \"%s\" \"%s\"", res->deb_path, task->deb_url);

    int ret = system(cmd);
    if (ret == 0 && access(res->deb_path, R_OK) == 0) {
        res->success = true;
        HINSTANCE hInst = ShellExecuteA(NULL, "open", res->deb_path, "/SILENT", NULL, SW_SHOWNORMAL);
        if ((intptr_t)hInst > 32) {
            res->installed_directly = true;
        } else {
            res->installed_directly = false;
        }
    } else {
        res->success = false;
        snprintf(res->error_msg, sizeof(res->error_msg), "Failed to download update package.");
    }
#else
    snprintf(res->deb_path, sizeof(res->deb_path), "/tmp/wallscape-latest.deb");

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -s -L -f -o \"%s\" \"%s\"", res->deb_path, task->deb_url);

    int ret = system(cmd);
    if (ret == 0 && access(res->deb_path, R_OK) == 0) {
        res->success = true;

        /* Option A: Direct In-Place User-Space Extraction & Installation (Zero root prompt, Zero App Store) */
        const char *home = getenv("HOME");
        int inst_res = -1;
        if (home && strlen(home) > 0) {
            char install_cmd[4096];
            snprintf(install_cmd, sizeof(install_cmd),
                     "mkdir -p \"%s/.local/share/wallscape\" \"%s/.local/bin\" \"%s/.local/share/applications\" \"%s/.local/share/icons\" && "
                     "dpkg-deb -x \"%s\" \"%s/.local/share/wallscape\" && "
                     "cp -f \"%s/.local/share/wallscape/usr/bin/live-wallpaper\" \"%s/.local/bin/live-wallpaper\" && "
                     "chmod +x \"%s/.local/bin/live-wallpaper\" && "
                     "cp -rf \"%s/.local/share/wallscape/usr/share/icons/\"* \"%s/.local/share/icons/\" 2>/dev/null || true && "
                     "cp -f \"%s/.local/share/wallscape/usr/share/applications/live-wallpaper.desktop\" \"%s/.local/share/applications/live-wallpaper.desktop\" 2>/dev/null || true && "
                     "sed -i 's|Exec=live-wallpaper|Exec=%s/.local/bin/live-wallpaper|g' \"%s/.local/share/applications/live-wallpaper.desktop\" 2>/dev/null || true && "
                     "gtk-update-icon-cache -f -t \"%s/.local/share/icons/hicolor\" 2>/dev/null || true",
                     home, home, home, home,
                     res->deb_path, home,
                     home, home,
                     home,
                     home, home,
                     home, home,
                     home, home,
                     home);
            inst_res = system(install_cmd);
        }

        if (inst_res == 0) {
            res->installed_directly = true;
        } else {
            /* Fallback to PolicyKit or App Center if user-space extraction failed */
            char fallback_cmd[2048];
            snprintf(fallback_cmd, sizeof(fallback_cmd),
                     "pkexec dpkg -i \"%s\" || xdg-open \"%s\" &",
                     res->deb_path, res->deb_path);
            int fb_res = system(fallback_cmd);
            (void)fb_res;
            res->installed_directly = false;
        }
    } else {
        res->success = false;
        snprintf(res->error_msg, sizeof(res->error_msg), "Failed to download update package.");
    }
#endif

    free(task);
    g_idle_add(dispatch_download_result_on_main, res);
    return NULL;
}

void updater_download_and_install_async(const char *deb_url,
                                       UpdateDownloadProgressCallback progress_cb,
                                       UpdateDownloadCompleteCallback complete_cb,
                                       gpointer user_data)
{
    if (!deb_url || strlen(deb_url) == 0) {
        if (complete_cb) {
            complete_cb(false, false, NULL, "No download URL available for update package.", user_data);
        }
        return;
    }

    DownloadTaskCtx *task = (DownloadTaskCtx *)calloc(1, sizeof(DownloadTaskCtx));
    if (!task) return;

    snprintf(task->deb_url, sizeof(task->deb_url), "%s", deb_url);
    task->progress_cb = progress_cb;
    task->complete_cb = complete_cb;
    task->user_data = user_data;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, updater_download_thread, task) != 0) {
        free(task);
    }
    pthread_attr_destroy(&attr);
}
