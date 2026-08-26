/*
 * wallpaper.c — SDL2 wallpaper window with X11 desktop-type hints.
 *
 * This module creates a borderless fullscreen SDL2 window and uses Xlib to
 * set the _NET_WM_WINDOW_TYPE_DESKTOP X11 property.  This tells the window
 * manager (Mutter/GNOME) to place our window at the bottom of the window
 * stack — behind desktop icons but above the actual wallpaper image.
 *
 * ┌───────────────────────────────────────────────────────────────────────┐
 * │  THE X11 DESKTOP WINDOW TYPE TRICK                                   │
 * │                                                                       │
 * │  The Extended Window Manager Hints (EWMH) specification defines a    │
 * │  window type called _NET_WM_WINDOW_TYPE_DESKTOP.  When a window      │
 * │  has this type, compliant window managers (including Mutter/GNOME):   │
 * │                                                                       │
 * │    1. Place it at the bottom of the stacking order                   │
 * │    2. Make it span the full screen                                   │
 * │    3. Keep it below all normal windows and desktop icons             │
 * │    4. Do not show it in the taskbar or task switcher                 │
 * │                                                                       │
 * │  This is the same approach used by:                                  │
 * │    - xwinwrap (video wallpaper for X11)                              │
 * │    - Komorebi (animated wallpaper manager)                           │
 * │    - Variety (wallpaper changer)                                     │
 * │                                                                       │
 * │  On Wayland sessions (Zorin OS 18 default), the SDL window runs     │
 * │  under XWayland.  Mutter honors _NET_WM_WINDOW_TYPE_DESKTOP for     │
 * │  XWayland clients, so the technique works transparently.             │
 * └───────────────────────────────────────────────────────────────────────┘
 *
 * Rendering uses SDL_Renderer with hardware acceleration and vsync.
 * Video frames are uploaded via SDL_UpdateYUVTexture() (YUV420P → IYUV).
 * Scaling uses "cover" mode (fills screen, crops excess) to handle
 * resolution mismatches between the video and the display.
 */

#include "wallpaper.h"
#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

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
 *
 * "Cover" mode scales the video so it completely covers the screen while
 * maintaining aspect ratio.  If the aspect ratios don't match, the excess
 * is cropped (centered).  This is equivalent to CSS background-size:cover.
 *
 * Example:
 *   Screen: 1920×1080 (16:9)
 *   Video:  2560×1440 (16:9) → scales to 1920×1080, dst = (0,0,1920,1080)
 *   Video:  1920×1200 (16:10)→ scales to 1920×1200→ too tall → dst shows
 *                               1920×1080 centered, cropping 60px top+bottom
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
 *
 * This is the core "trick" that makes the SDL window act as the desktop
 * background.  We set three properties:
 *
 *   1. _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP
 *      Tells the WM this is a "desktop" window — it will be placed at
 *      the very bottom of the window stack, behind desktop icons.
 *
 *   2. _NET_WM_STATE includes _NET_WM_STATE_SKIP_TASKBAR
 *      Prevents the window from appearing in the taskbar / dock.
 *
 *   3. _NET_WM_STATE includes _NET_WM_STATE_SKIP_PAGER
 *      Prevents the window from appearing in workspace pager / overview.
 *
 *   4. _NET_WM_STATE includes _NET_WM_STATE_BELOW
 *      Extra insurance: keep this window below others at all times.
 * ──────────────────────────────────────────────────────────────────────────── */
static void set_x11_desktop_hints(SDL_Window *window)
{
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);

    if (!SDL_GetWindowWMInfo(window, &wm_info)) {
        fprintf(stderr, "[wallpaper] WARNING: could not get WM info: %s\n",
                SDL_GetError());
        fprintf(stderr, "[wallpaper] X11 desktop hints not set — "
                "window may appear above desktop icons\n");
        return;
    }

    if (wm_info.subsystem != SDL_SYSWM_X11) {
        fprintf(stderr, "[wallpaper] WARNING: not running on X11 "
                "(subsystem=%d) — desktop hints skipped\n", wm_info.subsystem);
        return;
    }

    Display *dpy = wm_info.info.x11.display;
    Window   xwin = wm_info.info.x11.window;

    /*
     * Property 1: _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP
     *
     * This single property is what makes Mutter treat our window as
     * the desktop background surface.  Without it, our fullscreen window
     * would cover everything — including desktop icons.
     */
    Atom wm_type       = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom wm_type_desk  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(dpy, xwin, wm_type, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&wm_type_desk, 1);

    /*
     * Property 2+3+4: _NET_WM_STATE with skip-taskbar, skip-pager, and below
     */
    Atom wm_state      = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom skip_taskbar  = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skip_pager    = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom state_below   = XInternAtom(dpy, "_NET_WM_STATE_BELOW", False);
    Atom states[3] = { skip_taskbar, skip_pager, state_below };
    XChangeProperty(dpy, xwin, wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)states, 3);

    XFlush(dpy);

    fprintf(stderr, "[wallpaper] X11 desktop hints set successfully\n");
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
        fprintf(stderr, "[wallpaper] SDL_GetCurrentDisplayMode failed: %s\n",
                SDL_GetError());
        free(ctx);
        return NULL;
    }
    ctx->screen_w = dm.w;
    ctx->screen_h = dm.h;

    fprintf(stderr, "[wallpaper] screen resolution: %dx%d\n",
            ctx->screen_w, ctx->screen_h);

    /* Create a borderless fullscreen window.
     *
     * SDL_WINDOW_FULLSCREEN_DESKTOP creates a "fake" fullscreen window that
     * covers the screen without changing the display resolution.  This works
     * better with desktop compositors than true exclusive fullscreen. */
    ctx->window = SDL_CreateWindow(
        "live-wallpaper",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ctx->screen_w, ctx->screen_h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN_DESKTOP |
        SDL_WINDOW_SHOWN);

    if (!ctx->window) {
        fprintf(stderr, "[wallpaper] SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        free(ctx);
        return NULL;
    }

    /* ── Set X11 desktop window type hints ──
     * This is the key step that places our window behind desktop icons.
     * See the detailed comment at set_x11_desktop_hints(). */
    set_x11_desktop_hints(ctx->window);

    /* Create hardware-accelerated renderer with vsync */
    ctx->renderer = SDL_CreateRenderer(ctx->window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ctx->renderer) {
        /* Fallback: try software renderer */
        fprintf(stderr, "[wallpaper] HW renderer failed, trying software: %s\n",
                SDL_GetError());
        ctx->renderer = SDL_CreateRenderer(ctx->window, -1,
            SDL_RENDERER_SOFTWARE);
    }
    if (!ctx->renderer) {
        fprintf(stderr, "[wallpaper] SDL_CreateRenderer failed: %s\n",
                SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        free(ctx);
        return NULL;
    }

    /* Clear to black initially */
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderPresent(ctx->renderer);

    if (out_w) *out_w = ctx->screen_w;
    if (out_h) *out_h = ctx->screen_h;

    fprintf(stderr, "[wallpaper] window created successfully\n");
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
    compute_cover_rect(video_w, video_h, ctx->screen_w, ctx->screen_h,
                       &ctx->dst_rect);

    /* Recreate the texture for the new video dimensions.
     *
     * SDL_PIXELFORMAT_IYUV is SDL's name for YUV420P (planar Y, U, V).
     * SDL_TEXTUREACCESS_STREAMING allows us to update the texture contents
     * every frame via SDL_UpdateYUVTexture(). */
    if (ctx->texture) {
        SDL_DestroyTexture(ctx->texture);
    }
    ctx->texture = SDL_CreateTexture(ctx->renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        video_w, video_h);

    if (!ctx->texture) {
        fprintf(stderr, "[wallpaper] SDL_CreateTexture failed: %s\n",
                SDL_GetError());
    }

    fprintf(stderr, "[wallpaper] texture: %dx%d, dst_rect: (%d,%d,%d,%d)\n",
            video_w, video_h,
            ctx->dst_rect.x, ctx->dst_rect.y,
            ctx->dst_rect.w, ctx->dst_rect.h);
}

void wallpaper_render_frame(WallpaperCtx *ctx, const VideoFrame *frame)
{
    if (!ctx || !ctx->texture || !frame) return;

    /* Upload the YUV420P frame data to the GPU texture.
     *
     * SDL_UpdateYUVTexture() accepts separate Y, U, V plane pointers and
     * their per-row byte pitches (strides).  This maps directly to our
     * VideoFrame layout and to FFmpeg's AVFrame plane format. */
    SDL_UpdateYUVTexture(ctx->texture, NULL,
                         frame->y, frame->y_pitch,
                         frame->u, frame->uv_pitch,
                         frame->v, frame->uv_pitch);

    /* Clear the screen (fills any letterbox/pillarbox areas with black) */
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);

    /* Render the texture scaled to cover the screen with the pre-computed rect */
    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, &ctx->dst_rect);

    /* Present the rendered frame to the display */
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
    }
}

void wallpaper_hide(WallpaperCtx *ctx)
{
    if (ctx && ctx->window) {
        SDL_HideWindow(ctx->window);
    }
}
