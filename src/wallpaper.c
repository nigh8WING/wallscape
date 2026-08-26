/*
 * wallpaper.c — SDL2 wallpaper surface with correct X11 desktop-layer hints.
 *
 * Stacking & Layering:
 *   - Uses _NET_WM_WINDOW_TYPE_DESKTOP and _NET_WM_STATE_BELOW to place
 *     the video surface in Mutter's desktop layer (META_LAYER_DESKTOP).
 *   - The taskbar, docks, panels, and application windows (META_LAYER_DOCK /
 *     META_LAYER_NORMAL) always remain 100% visible on top.
 *   - Input focus is disabled so the surface never steals keyboard/mouse events.
 *   - WM_CLIENT_LEADER is removed to prevent any client-grouping side effects.
 */

#include "wallpaper.h"
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

struct WallpaperCtx {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;

    int screen_w;
    int screen_h;
    int video_w;
    int video_h;

    SDL_Rect dst_rect;
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Cover-crop destination rectangle
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

    double va = (double)video_w  / (double)video_h;
    double sa = (double)screen_w / (double)screen_h;

    if (va > sa) {
        dst->h = screen_h;
        dst->w = (int)(screen_h * va);
    } else {
        dst->w = screen_w;
        dst->h = (int)(screen_w / va);
    }
    dst->x = (screen_w - dst->w) / 2;
    dst->y = (screen_h - dst->h) / 2;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Set X11 EWMH desktop-layer hints
 * ──────────────────────────────────────────────────────────────────────────── */
static void configure_x11_desktop_hints(Display *dpy, Window xwin)
{
    /* 1. _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP
     *    Tells Mutter to place this in the desktop layer (below panels and windows). */
    {
        Atom type      = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
        Atom type_desk = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
        XChangeProperty(dpy, xwin, type, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&type_desk, 1);
    }

    /* 2. _NET_WM_STATE = SKIP_TASKBAR | SKIP_PAGER | BELOW */
    {
        Atom state        = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom skip_taskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
        Atom skip_pager   = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER",   False);
        Atom below        = XInternAtom(dpy, "_NET_WM_STATE_BELOW",        False);
        Atom states[3]    = { skip_taskbar, skip_pager, below };
        XChangeProperty(dpy, xwin, state, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)states, 3);
    }

    /* 3. _NET_WM_DESKTOP = 0xFFFFFFFF (sticky across all virtual workspaces) */
    {
        Atom net_wm_desktop = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
        unsigned long all_desktops = 0xFFFFFFFF;
        XChangeProperty(dpy, xwin, net_wm_desktop, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&all_desktops, 1);
    }

    /* 4. Disable input focus */
    {
        XWMHints hints;
        hints.flags         = InputHint | StateHint;
        hints.input         = False;
        hints.initial_state = NormalState;
        XSetWMHints(dpy, xwin, &hints);
    }

    /* 5. Remove WM_CLIENT_LEADER to isolate from GUI window group */
    {
        Atom wm_client_leader = XInternAtom(dpy, "WM_CLIENT_LEADER", False);
        XDeleteProperty(dpy, xwin, wm_client_leader);
    }

    /* 6. Set a distinct WM_CLASS */
    {
        XClassHint class_hint;
        class_hint.res_name  = (char *)"live-wallpaper-surface";
        class_hint.res_class = (char *)"LiveWallpaperSurface";
        XSetClassHint(dpy, xwin, &class_hint);
    }

    /* 7. Initial lower */
    XLowerWindow(dpy, xwin);
    XFlush(dpy);
    fprintf(stderr, "[wallpaper] X11 desktop hints configured (_NET_WM_WINDOW_TYPE_DESKTOP).\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

WallpaperCtx *wallpaper_create(int *out_w, int *out_h)
{
    WallpaperCtx *ctx = (WallpaperCtx *)calloc(1, sizeof(WallpaperCtx));
    if (!ctx) return NULL;

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
     * Create a borderless, initially hidden window at (0,0) covering the full screen.
     * We do NOT use SDL_WINDOW_FULLSCREEN_DESKTOP as that would set _NET_WM_STATE_FULLSCREEN.
     */
    ctx->window = SDL_CreateWindow(
        "live-wallpaper-surface",
        0, 0, ctx->screen_w, ctx->screen_h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);

    if (!ctx->window) {
        fprintf(stderr, "[wallpaper] SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(ctx);
        return NULL;
    }

    /* Apply EWMH hints BEFORE showing the window */
    {
        SDL_SysWMinfo wm_info;
        SDL_VERSION(&wm_info.version);
        if (SDL_GetWindowWMInfo(ctx->window, &wm_info) &&
            wm_info.subsystem == SDL_SYSWM_X11) {
            configure_x11_desktop_hints(wm_info.info.x11.display,
                                        wm_info.info.x11.window);
        }
    }

    /* Hardware-accelerated renderer */
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

    compute_cover_rect(video_w, video_h, ctx->screen_w, ctx->screen_h, &ctx->dst_rect);

    if (ctx->texture) {
        SDL_DestroyTexture(ctx->texture);
        ctx->texture = NULL;
    }
    ctx->texture = SDL_CreateTexture(ctx->renderer,
        SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
        video_w, video_h);

    if (!ctx->texture) {
        fprintf(stderr, "[wallpaper] SDL_CreateTexture failed: %s\n", SDL_GetError());
    }
}

void wallpaper_render_frame(WallpaperCtx *ctx, const VideoFrame *frame)
{
    if (!ctx || !frame || !frame->y || !frame->u || !frame->v) return;

    if (!ctx->texture ||
        frame->width  != ctx->video_w ||
        frame->height != ctx->video_h) {
        wallpaper_set_video_size(ctx, frame->width, frame->height);
    }
    if (!ctx->texture) return;

    SDL_UpdateYUVTexture(ctx->texture, NULL,
                         frame->y, frame->y_pitch,
                         frame->u, frame->uv_pitch,
                         frame->v, frame->uv_pitch);

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
        if (event.type == SDL_QUIT) return false;
    }
    return true;
}

void wallpaper_show(WallpaperCtx *ctx)
{
    if (!ctx || !ctx->window) return;

    SDL_ShowWindow(ctx->window);

    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    if (SDL_GetWindowWMInfo(ctx->window, &wm_info) &&
        wm_info.subsystem == SDL_SYSWM_X11) {

        Display *dpy  = wm_info.info.x11.display;
        Window   xwin = wm_info.info.x11.window;

        /* Remove client leader */
        Atom wm_client_leader = XInternAtom(dpy, "WM_CLIENT_LEADER", False);
        XDeleteProperty(dpy, xwin, wm_client_leader);

        /* Lower window to the bottom of the desktop layer */
        XLowerWindow(dpy, xwin);

        XWindowChanges wc;
        wc.stack_mode = Below;
        XConfigureWindow(dpy, xwin, CWStackMode, &wc);

        XFlush(dpy);
        fprintf(stderr, "[wallpaper] window shown in desktop layer (below taskbar).\n");
    }
}

void wallpaper_hide(WallpaperCtx *ctx)
{
    if (ctx && ctx->window) {
        SDL_HideWindow(ctx->window);
    }
}
