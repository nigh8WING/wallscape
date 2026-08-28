/*
 * main.c — Entry point, initialization, and lifecycle management for live-wallpaper.
 *
 * This program plays video files as desktop wallpapers on Zorin OS 17/18 and Ubuntu 24.04.
 *
 * Key mechanisms:
 *   1. Environment detection: automatically sets SDL_VIDEODRIVER=x11 and GDK_BACKEND=x11
 *      when running under Wayland sessions, enabling the X11 desktop type trick
 *      via XWayland under GNOME/Mutter.
 *   2. Modular architecture:
 *      - decoder: FFmpeg 6.1 multi-threaded video decoding & frame pacing on a background thread.
 *      - wallpaper: SDL2 hardware-accelerated video rendering into a borderless fullscreen window
 *        with _NET_WM_WINDOW_TYPE_DESKTOP and _NET_WM_STATE hints.
 *      - gui: GTK3 control panel on the main thread driving the render loop via g_timeout_add.
 *      - config: persistent ~/.config/live-wallpaper/config.txt file storage.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <SDL2/SDL.h>

#include "common.h"
#include "wallpaper.h"
#include "decoder.h"
#include "gui.h"
#include "config.h"

/* Global state pointer for signal handlers */
static AppState     *g_state     = NULL;
static WallpaperCtx *g_wallpaper = NULL;
static GuiCtx       *g_gui       = NULL;

/* ─────────────────────────────────────────────────────────────────────────────
 * Clean Signal Handling (SIGINT, SIGTERM)
 * ──────────────────────────────────────────────────────────────────────────── */
static void signal_handler(int sig)
{
    (void)sig;
    if (g_state) {
        atomic_store(&g_state->quit, true);
    }
    if (gtk_main_level() > 0) {
        gtk_main_quit();
    }
}

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Display Server Setup (Wayland & X11 compatibility)
 * ──────────────────────────────────────────────────────────────────────────── */
static void setup_display_environment(void)
{
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    const char *session_type = getenv("XDG_SESSION_TYPE");

    bool is_wayland = (wayland_display != NULL && wayland_display[0] != '\0') ||
                      (session_type != NULL && strcmp(session_type, "wayland") == 0);

    if (is_wayland) {
        fprintf(stderr, "[main] Wayland session detected. Enabling XWayland compatibility for wallpaper...\n");
        /* Force X11 backend ONLY for SDL2 so the wallpaper surface can use X11 root desktop layering.
         * GTK uses native Wayland backend so GUI window actions (minimize, restore) are fully isolated
         * and cannot affect other application windows. */
        setenv("SDL_VIDEODRIVER", "x11", 1);
    } else {
        fprintf(stderr, "[main] Native X11 session detected.\n");
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Print usage / help
 * ──────────────────────────────────────────────────────────────────────────── */
static void print_usage(const char *prog_name)
{
    printf("WallScape — Live Wallpaper Manager for Zorin OS / GNOME (X11 & Wayland)\n\n");
    printf("Usage:\n");
    printf("  %s [options] [video_file]\n\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help       Show this help message\n");
    printf("  -v, --version    Show version information\n");
    printf("  --no-gui         Start wallpaper in background without showing control panel\n\n");
    printf("Supported video formats: .mp4, .mkv, .webm, .avi, .mov\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Entry Point
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[])
{
    char initial_video[LW_MAX_PATH] = {0};
    bool show_gui_window = true;

    /* Parse CLI arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("WallScape version 1.0.0 (C11, FFmpeg, SDL2, GTK3)\n");
            return 0;
        } else if (strcmp(argv[i], "--no-gui") == 0) {
            show_gui_window = false;
        } else if (argv[i][0] != '-') {
            snprintf(initial_video, sizeof(initial_video), "%s", argv[i]);
        }
    }

    /* Configure environment before initializing GUI/SDL */
    setup_display_environment();
    setup_signals();

    /* Initialize GTK3 */
    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "[main] ERROR: Failed to initialize GTK3. Is a display server running?\n");
        return 1;
    }

    /* Initialize SDL2 */
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[main] ERROR: Failed to initialize SDL2: %s\n", SDL_GetError());
        return 1;
    }

    /* Allocate App State */
    AppState state;
    app_state_init(&state);
    g_state = &state;

    /* Create Wallpaper Window */
    int screen_w = 0, screen_h = 0;
    WallpaperCtx *wallpaper = wallpaper_create(&screen_w, &screen_h);
    if (!wallpaper) {
        fprintf(stderr, "[main] ERROR: Failed to create wallpaper window.\n");
        SDL_Quit();
        app_state_destroy(&state);
        return 1;
    }
    state.screen_width = screen_w;
    state.screen_height = screen_h;
    g_wallpaper = wallpaper;

    /* Create GTK Control Panel */
    GuiCtx *gui = gui_create(&state, wallpaper);
    if (!gui) {
        fprintf(stderr, "[main] ERROR: Failed to create GUI.\n");
        wallpaper_destroy(wallpaper);
        SDL_Quit();
        app_state_destroy(&state);
        return 1;
    }
    g_gui = gui;

    /* If an explicit video file was passed via CLI, start it */
    if (initial_video[0] != '\0' && access(initial_video, R_OK) == 0) {
        fprintf(stderr, "[main] Starting requested video: %s\n", initial_video);
        gui_load_video_and_scan_folder(gui, initial_video);
    } else {
        wallpaper_hide(wallpaper);
    }

    if (show_gui_window) {
        gui_show(gui);
    }

    /* Main GTK Event Loop */
    gtk_main();

    /* ── Graceful Shutdown ── */
    fprintf(stderr, "[main] Shutting down live wallpaper...\n");
    atomic_store(&state.quit, true);

    gui_stop_render_timer(gui);
    decoder_stop(&state);

    if (state.video_path[0] != '\0') {
        config_save(state.video_path);
    }

    gui_destroy(gui);
    wallpaper_destroy(wallpaper);
    app_state_destroy(&state);

    SDL_Quit();
    fprintf(stderr, "[main] Goodbye.\n");
    return 0;
}
