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
#include "updater.h"

/* Global state pointer for signal handlers and app lifecycle */
static AppState        g_state;
static WallpaperCtx   *g_wallpaper = NULL;
static GuiCtx         *g_gui       = NULL;
static GtkApplication *g_app       = NULL;

/* ─────────────────────────────────────────────────────────────────────────────
 * Clean Signal Handling (SIGINT, SIGTERM)
 * ──────────────────────────────────────────────────────────────────────────── */
static void signal_handler(int sig)
{
    (void)sig;
    atomic_store(&g_state.quit, true);
    if (g_app) {
        g_application_quit(G_APPLICATION(g_app));
    } else if (gtk_main_level() > 0) {
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Application Lifecycle (GtkApplication - Single Instance)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void app_startup(GApplication *app, gpointer user_data)
{
    (void)user_data;

    setup_display_environment();
    setup_signals();

    /* Initialize SDL2 */
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "[main] ERROR: Failed to initialize SDL2: %s\n", SDL_GetError());
        g_application_quit(app);
        return;
    }

    app_state_init(&g_state);

    /* Create Wallpaper Window */
    int screen_w = 0, screen_h = 0;
    g_wallpaper = wallpaper_create(&screen_w, &screen_h);
    if (!g_wallpaper) {
        fprintf(stderr, "[main] ERROR: Failed to create wallpaper window.\n");
        SDL_Quit();
        app_state_destroy(&g_state);
        g_application_quit(app);
        return;
    }
    g_state.screen_width = screen_w;
    g_state.screen_height = screen_h;
    wallpaper_hide(g_wallpaper);

    /* Create GTK Control Panel */
    g_gui = gui_create(&g_state, g_wallpaper);
    if (!g_gui) {
        fprintf(stderr, "[main] ERROR: Failed to create GUI.\n");
        wallpaper_destroy(g_wallpaper);
        SDL_Quit();
        app_state_destroy(&g_state);
        g_application_quit(app);
        return;
    }

    /* Keep the background instance alive even when GUI window is closed/hidden */
    g_application_hold(app);
}

static int app_command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data)
{
    (void)app;
    (void)user_data;

    int argc = 0;
    char **argv = g_application_command_line_get_arguments(cmdline, &argc);

    bool show_gui = true;
    char initial_video[LW_MAX_PATH] = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            g_application_command_line_print(cmdline,
                "WallScape — Live Wallpaper Manager for Zorin OS / GNOME (X11 & Wayland)\n\n"
                "Usage:\n"
                "  wallscape [options] [video_file]\n\n"
                "Options:\n"
                "  -h, --help       Show this help message\n"
                "  -v, --version    Show version information\n"
                "  --no-gui         Start wallpaper in background without showing control panel\n\n"
                "Supported video formats: .mp4, .mkv, .webm, .avi, .mov\n");
            g_strfreev(argv);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            g_application_command_line_print(cmdline, "WallScape version " WALLSCAPE_CURRENT_VERSION " (C11, FFmpeg, SDL2, GTK3)\n");
            g_strfreev(argv);
            return 0;
        } else if (strcmp(argv[i], "--no-gui") == 0) {
            show_gui = false;
        } else if (argv[i][0] != '-') {
            snprintf(initial_video, sizeof(initial_video), "%s", argv[i]);
        }
    }

    if (g_gui) {
        if (!show_gui) {
            gui_set_headless(g_gui, true);
        }
        if (initial_video[0] != '\0' && access(initial_video, R_OK) == 0) {
            fprintf(stderr, "[main] Starting requested video: %s\n", initial_video);
            gui_load_video_and_scan_folder(g_gui, initial_video);
        }
        if (show_gui) {
            gui_show(g_gui);
        }
    }

    g_strfreev(argv);
    return 0;
}

static void app_shutdown(GApplication *app, gpointer user_data)
{
    (void)app;
    (void)user_data;

    fprintf(stderr, "[main] Shutting down live wallpaper...\n");
    atomic_store(&g_state.quit, true);

    if (g_gui) {
        gui_stop_render_timer(g_gui);
    }
    decoder_stop(&g_state);

    if (atomic_load(&g_state.playing) && g_state.video_path[0] != '\0') {
        config_save(g_state.video_path);
    }

    if (g_gui) {
        gui_destroy(g_gui);
        g_gui = NULL;
    }
    if (g_wallpaper) {
        wallpaper_destroy(g_wallpaper);
        g_wallpaper = NULL;
    }
    app_state_destroy(&g_state);

    SDL_Quit();
    fprintf(stderr, "[main] Goodbye.\n");
}

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

int main(int argc, char *argv[])
{
    /* Pre-check help & version before GUI/display initialization */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("WallScape version " WALLSCAPE_CURRENT_VERSION " (C11, FFmpeg, SDL2, GTK3)\n");
            return 0;
        }
    }

    GtkApplication *app = gtk_application_new("com.nigh8wing.wallscape",
                                              G_APPLICATION_HANDLES_COMMAND_LINE);
    g_app = app;

    g_signal_connect(app, "startup",      G_CALLBACK(app_startup), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(app_command_line), NULL);
    g_signal_connect(app, "shutdown",     G_CALLBACK(app_shutdown), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
