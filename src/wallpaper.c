/*
 * wallpaper.c — SDL2 wallpaper window with X11 desktop-type hints.
 *
 * This module creates a borderless window positioned at (0, 0) spanning the entire
 * display, and uses Xlib to:
 *   1. Remove any fullscreen states that would raise the window above panels/taskbars.
 *   2. Set _NET_WM_WINDOW_TYPE to _NET_WM_WINDOW_TYPE_DESKTOP.
 *   3. Set _NET_WM_STATE to SKIP_TASKBAR, SKIP_PAGER, and BELOW.
 *   4. Set override_redirect = True and call XLowerWindow() so the window sits
 *      at the bottom of the X11 window stack behind desktop icons and taskbars.
 */

#include "wallpaper.h"
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * WallpaperCtx — internal state for the wallpaper window.
 * ──────────────────────────────────────────────────────────────────────────── */
struct WallpaperCtx {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;

    int screen_w;
    int screen_h;
    int video_w;
    int video_h;

    /* Pre-computed destination rect for cover-crop scaling */
    SDL_Rect dst_rect;
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Compute the "cover" destination rectangle.
 * ──────────────────────────────────────────────────────────────────────────── */
static void compute_cover_rect(int video_w, int video_h,
                                int screen_w, int screen_h,
                                SDL_Rect *dst)
{
    if (video_w <= 0 || video_h <= 0) {
        dst->x = 0; dst->y = 0;
        dst->w = screen_w; dst->h = screen_h;
        return;
    }

    double video_aspect  = (double)video_w / (double)video_h;
    double screen_aspect = (double)screen_w / (double)screen_h;

    if (video_aspect > screen_aspect) {
        /* Video is wider → scale to match height, crop sides */
        dst->h = screen_h;
        dst->w = (int)(screen_h * video_aspect);
    } else {
        /* Video is taller (or same) → scale to match width, crop top/bottom */
        dst->w = screen_w;
        dst->h = (int)(screen_w / video_aspect);
    }

    /* Center the scaled video on screen (negative offsets = cropping) */
    dst->x = (screen_w - dst->w) / 2;
    dst->y = (screen_h - dst->h) / 2;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Set X11 window properties for desktop-type behavior.
 * ──────────────────────────────────────────────────────────────────────────── */
static void configure_x11_desktop_surface(SDL_Window *window)
{
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);

    if (!SDL_GetWindowWMInfo(window, &wm_info)) {
        fprintf(stderr, "[wallpaper] WARNING: could not get WM info: %s\n", SDL_GetError());
        return;
    }

    if (wm_info.subsystem != SDL_SYSWM_X11) {
        fprintf(stderr, "[wallpaper] WARNING: not running on X11 (subsystem=%d)\n", wm_info.subsystem);
        return;
    }

    Display *dpy  = wm_info.info.x11.display;
    Window   xwin = wm_info.info.x11.window;

    /*
     * 1. Set _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP
     */
    Atom wm_type      = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom wm_type_desk = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(dpy, xwin, wm_type, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&wm_type_desk, 1);

    /*
     * 2. Set _NET_WM_STATE = SKIP_TASKBAR, SKIP_PAGER, BELOW
     */
    Atom wm_state     = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom skip_taskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skip_pager   = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom state_below  = XInternAtom(dpy, "_NET_WM_STATE_BELOW", False);
    Atom states[3] = { skip_taskbar, skip_pager, state_below };
    XChangeProperty(dpy, xwin, wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)states, 3);

    /*
     * 3. Set WM Hints: do not take keyboard/input focus
     */
    XWMHints hints;
    hints.flags = InputHint | StateHint;
    hints.input = False;
    hints.initial_state = NormalState;
    XSetWMHints(dpy, xwin, &hints);

    /*
     * 4. Send XLowerWindow to position this window behind desktop icons
     */
    XLowerWindow(dpy, xwin);
    XFlush(dpy);

    fprintf(stderr, "[wallpaper] X11 desktop hints configured and window lowered.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

WallpaperCtx *wallpaper_create(int *out_w, int *out_h)
{
    WallpaperCtx *ctx = (WallpaperCtx *)calloc(1, sizeof(WallpaperCtx));
    if (!ctx) return NULL;

    /* Get the primary display resolution */
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0) {
        fprintf(stderr, "[wallpaper] SDL_GetCurrentDisplayMode failed: %s\n", SDL_GetError());
        free(ctx);
        return NULL;
    }
    ctx->screen_w = dm.w;
    ctx->screen_h = dm.h;

    fprintf(stderr, "[wallpaper] screen resolution: %dx%d\n", ctx->screen_w, ctx->screen_h);

    /*
     * Create a borderless window spanning the full screen.
     * NOTE: We DO NOT use SDL_WINDOW_FULLSCREEN_DESKTOP because that flag sets
     * _NET_WM_STATE_FULLSCREEN, which forces Mutter to place the window on top
     * of the taskbar/panels and desktop icons. Instead, a borderless window
     * at (0, 0) with size (screen_w, screen_h) is used.
     */
    ctx->window = SDL_CreateWindow(
        "live-wallpaper-surface",
        0, 0,
        ctx->screen_w, ctx->screen_h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);

    if (!ctx->window) {
        fprintf(stderr, "[wallpaper] SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(ctx);
        return NULL;
    }

    /* Configure desktop hints before showing */
    configure_x11_desktop_surface(ctx->window);

    /* Create hardware-accelerated renderer with vsync */
    ctx->renderer = SDL_CreateRenderer(ctx->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ctx->renderer) {
        fprintf(stderr, "[wallpaper] HW renderer failed, trying software: %s\n", SDL_GetError());
        ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ctx->renderer) {
        fprintf(stderr, "[wallpaper] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        free(ctx);
        return NULL;
    }

    /* Clear to black */
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderPresent(ctx->renderer);

    if (out_w) *out_w = ctx->screen_w;
    if (out_h) *out_h = ctx->screen_h;

    return ctx;
}

void wallpaper_destroy(WallpaperCtx *ctx)
{
    if (!ctx) return;
    if (ctx->texture)  SDL_DestroyTexture(ctx->texture);
    if (ctx->renderer) SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window)   SDL_DestroyWindow(ctx->window);
    free(ctx);
    fprintf(stderr, "[wallpaper] destroyed\n");
}

void wallpaper_set_video_size(WallpaperCtx *ctx, int video_w, int video_h)
{
    if (!ctx) return;

    ctx->video_w = video_w;
    ctx->video_h = video_h;

    /* Recompute the cover-crop destination rect */
    compute_cover_rect(video_w, video_h, ctx->screen_w, ctx->screen_h, &ctx->dst_rect);

    if (ctx->texture) {
        SDL_DestroyTexture(ctx->texture);
    }
    ctx->texture = SDL_CreateTexture(ctx->renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        video_w, video_h);

    if (!ctx->texture) {
        fprintf(stderr, "[wallpaper] SDL_CreateTexture failed: %s\n", SDL_GetError());
    }
}

void wallpaper_render_frame(WallpaperCtx *ctx, const VideoFrame *frame)
{
    if (!ctx || !frame || !frame->y || !frame->u || !frame->v) return;

    /* Automatically recreate texture if frame dimensions changed */
    if (!ctx->texture || frame->width != ctx->video_w || frame->height != ctx->video_h) {
        wallpaper_set_video_size(ctx, frame->width, frame->height);
    }
    if (!ctx->texture) return;

    /* Upload frame */
    SDL_UpdateYUVTexture(ctx->texture, NULL,
                         frame->y, frame->y_pitch,
                         frame->u, frame->uv_pitch,
                         frame->v, frame->uv_pitch);

    /* Render */
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, &ctx->dst_rect);
    SDL_RenderPresent(ctx->renderer);
}

bool wallpaper_process_events(WallpaperCtx *ctx)
{
    (void)ctx;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return false;
        }
    }
    return true;
}

void wallpaper_show(WallpaperCtx *ctx)
{
    if (ctx && ctx->window) {
        SDL_ShowWindow(ctx->window);

        /* Ensure the window is pushed to the bottom layer */
        SDL_SysWMinfo wm_info;
        SDL_VERSION(&wm_info.version);
        if (SDL_GetWindowWMInfo(ctx->window, &wm_info) && wm_info.subsystem == SDL_SYSWM_X11) {
            XLowerWindow(wm_info.info.x11.display, wm_info.info.x11.window);
            XFlush(wm_info.info.x11.display);
        }
    }
}

void wallpaper_hide(WallpaperCtx *ctx)
{
    if (ctx && ctx->window) {
        SDL_HideWindow(ctx->window);
    }
}
