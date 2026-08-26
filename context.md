# Project Context: Live Wallpaper Software

## 1. Overview & Purpose
**Live Wallpaper Software** is a high-performance, lightweight, hardware-accelerated desktop video wallpaper daemon and control panel written in pure **C (C11)**.

- **Primary Target**: Zorin OS 18 & Zorin OS 17 (Ubuntu 24.04 / 22.04 LTS, GNOME 46 / Mutter, x86_64).
- **Core Functionality**:
  - Displays local video files (`.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`) as the active desktop background.
  - Sits directly **behind desktop icons and widgets** using window manager desktop-layer hinting.
  - Seamless infinite looping without audio decoding overhead.
  - Interactive GTK3 control panel for selecting videos, pausing/resuming, stopping, and quitting.
  - Persistent last-played video storage (`~/.config/live-wallpaper/config.txt`) and session autostart (`.desktop` entry).
- **Cost**: 100% Free & Open Source, utilizing only standard Ubuntu repository packages.

---

## 2. Platform Compatibility & Architectural Solutions

### Zorin OS 18 / GNOME 46 Wayland Default
- **Challenge**: Zorin OS 18 defaults to Wayland. Wayland compositors (Mutter) prohibit client windows from arbitrarily setting global window layers or types (`_NET_WM_WINDOW_TYPE_DESKTOP` does not exist in standard Wayland protocols, and Mutter does not support `wlr-layer-shell`).
- **Solution**: The application dynamically inspects the environment (`$WAYLAND_DISPLAY`, `$XDG_SESSION_TYPE`). If a Wayland session is detected, it automatically exports:
  ```bash
  SDL_VIDEODRIVER=x11
  GDK_BACKEND=x11
  ```
  This forces SDL2 and GTK3 into **XWayland** mode. GNOME Mutter's XWayland implementation honors legacy EWMH hints (`_NET_WM_WINDOW_TYPE_DESKTOP`), positioning the wallpaper surface behind desktop icons across both native X11 and Wayland sessions.

---

## 3. Architecture & Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    Main Thread (GTK3)                       │
│  ┌───────────────────────────────────────────────────────┐  │
│  │           Control Panel GUI (gui.c)                   │  │
│  │   [Choose Video]  [Pause/Resume]  [Stop]  [Quit]      │  │
│  │   Status Label: Shows active video path               │  │
│  └───────────────────────────────────────────────────────┘  │
│                              │                              │
│         g_timeout_add() Render Timer (~60 FPS / 16ms)       │
│                              │                              │
│  ┌───────────────────────────▼───────────────────────────┐  │
│  │         SDL2 Wallpaper Surface (wallpaper.c)          │  │
│  │  - Window: SDL_WINDOW_FULLSCREEN_DESKTOP              │  │
│  │  - Xlib Hints: _NET_WM_WINDOW_TYPE_DESKTOP            │  │
│  │    _NET_WM_STATE_SKIP_TASKBAR, SKIP_PAGER, BELOW      │  │
│  │  - Texture: Streaming SDL_PIXELFORMAT_IYUV (YUV420P)  │  │
│  │  - Scaling: Aspect-ratio cover-crop (no distortion)   │  │
│  └───────────────────────────▲───────────────────────────┘  │
└──────────────────────────────┼──────────────────────────────┘
                               │ pop frame (non-blocking)
                    ┌──────────┴──────────┐
                    │ FrameQueue (Ring 3) │
                    │ Mutex + Condvars    │
                    └──────────▲──────────┘
                               │ push frame (blocking/backpressured)
┌──────────────────────────────┼──────────────────────────────┐
│  ┌───────────────────────────┴───────────────────────────┐  │
│  │            FFmpeg Decoder Thread (decoder.c)          │  │
│  │  - libavformat: Demux container (Audio streams ignored)│ │
│  │  - libavcodec: Multi-threaded hardware/software decode │ │
│  │  - libswscale: Format conversion -> YUV420P           │  │
│  │  - Frame Pacing: PTS-based delay via nanosleep()      │  │
│  │  - Infinite Loop: On EOF -> av_seek_frame(0) + flush  │  │
│  └───────────────────────────────────────────────────────┘  │
│                   Dedicated pthread                         │
└─────────────────────────────────────────────────────────────┘
```

### Key Concurrency Design:
1. **Thread Separation**: Video decoding runs on a dedicated `pthread` (`decoder.c`); UI and SDL rendering run on the GTK main thread (`gui.c` + `wallpaper.c`).
2. **Event Loop Safety**: SDL2 requires video/render calls on the main thread. Rather than running competing event loops, GTK's `g_timeout_add(16, ...)` drives frame extraction and SDL texture presentation.
3. **Bounded Ring Queue**: `FrameQueue` buffers up to 3 decoded `VideoFrame` structs. Push blocks when full (backpressure on decoder); pop is non-blocking (re-renders previous frame if queue temporarily empties).

---

## 4. Tech Stack & Dependencies

All dependencies are standard C libraries available in Ubuntu / Zorin repositories:

| Library | Package Name | Purpose |
|---------|--------------|---------|
| **C11 Compiler** | `build-essential` | ISO C11 standard support (`<stdatomic.h>`, `<stdbool.h>`, `<threads.h>`/pthreads) |
| **CMake** | `cmake` (3.16+) | Cross-platform build system |
| **FFmpeg 6.1+** | `libavformat-dev`, `libavcodec-dev`, `libavutil-dev`, `libswscale-dev` | Media container demuxing, video stream decoding, pixel scaling |
| **SDL2** | `libsdl2-dev` | Fullscreen borderless window creation, hardware-accelerated 2D renderer, YUV streaming textures |
| **GTK+ 3.0** | `libgtk-3-dev` | Control panel window, file chooser dialog, GLib event loop timer |
| **Xlib** | `libx11-dev` | Setting X11 Extended Window Manager Hints (EWMH) on the SDL window |

---

## 5. File Structure & Responsibilities

```
/home/user/Coding/Live Wallpaper Software/
├── CMakeLists.txt              # Build configuration & dependency linking via PkgConfig
├── live-wallpaper.desktop      # Desktop entry for application menu & autostart
├── README.md                   # Full user documentation, build guide, autostart setup
├── context.md                  # Handoff context & technical reference (this file)
└── src/
    ├── common.h                # Core definitions: VideoFrame, FrameQueue, AppState, atomic flags
    ├── common.c                # Thread-safe ring buffer queue & state memory lifecycle
    ├── config.h / config.c     # Key=value config persistence (~/.config/live-wallpaper/config.txt)
    ├── decoder.h / decoder.c   # FFmpeg demuxing, decoding, YUV420P conversion, PTS pacing, EOF loop
    ├── wallpaper.h / wallpaper.c # SDL2 window creation, X11 EWMH desktop hints, cover scaling
    ├── gui.h / gui.c           # GTK3 control panel, GtkFileChooserDialog, 60 FPS render callback
    └── main.c                  # Wayland/X11 environment setup, CLI args, signal handling, orchestration
```

---

## 6. Build, Install & Run Instructions

### Install Build Dependencies
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libsdl2-dev libgtk-3-dev libx11-dev
```

### Compile
```bash
cd "/home/user/Coding/Live Wallpaper Software"
cmake -B build
cmake --build build
```

### Run
```bash
# Default mode (opens control panel + starts saved wallpaper)
./build/live-wallpaper

# Pass a specific video directly
./build/live-wallpaper /path/to/video.mp4

# Run headless/background (no GUI window, wallpaper only)
./build/live-wallpaper --no-gui
```

### Autostart Setup
```bash
mkdir -p ~/.config/autostart
cp live-wallpaper.desktop ~/.config/autostart/
```

---

## 7. Key Code Snippets & Mechanics

### A. The X11 Desktop Window Trick (`wallpaper.c`)
```c
// Retrieves X11 display and window handle from SDL2
SDL_SysWMinfo wm_info;
SDL_VERSION(&wm_info.version);
SDL_GetWindowWMInfo(window, &wm_info);
Display *dpy = wm_info.info.x11.display;
Window   xwin = wm_info.info.x11.window;

// 1. _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DESKTOP (places behind desktop icons)
Atom wm_type      = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
Atom wm_type_desk = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
XChangeProperty(dpy, xwin, wm_type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&wm_type_desk, 1);

// 2. _NET_WM_STATE = SKIP_TASKBAR, SKIP_PAGER, BELOW
Atom wm_state     = XInternAtom(dpy, "_NET_WM_STATE", False);
Atom states[3]    = {
    XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False),
    XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False),
    XInternAtom(dpy, "_NET_WM_STATE_BELOW", False)
};
XChangeProperty(dpy, xwin, wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)states, 3);
XFlush(dpy);
```

### B. Seamless Infinite Looping (`decoder.c`)
```c
if (ret == AVERROR_EOF || avio_feof(d.fmt_ctx->pb)) {
    // 1. Flush remaining buffered frames from codec
    avcodec_send_packet(d.codec_ctx, NULL);
    while (avcodec_receive_frame(d.codec_ctx, d.frame_raw) == 0) {
        // scale and push...
    }
    // 2. Seek format context back to beginning (timestamp 0)
    av_seek_frame(d.fmt_ctx, d.video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
    // 3. Flush codec internal state
    avcodec_flush_buffers(d.codec_ctx);
    // 4. Continue decoding loop seamlessly
    continue;
}
```

### C. Aspect-Ratio Cover Scaling (`wallpaper.c`)
Computes centered destination rect with cropping to fill the entire display without letterboxing distortion:
```c
double video_aspect  = (double)video_w / (double)video_h;
double screen_aspect = (double)screen_w / (double)screen_h;

if (video_aspect > screen_aspect) {
    dst->h = screen_h;
    dst->w = (int)(screen_h * video_aspect);
} else {
    dst->w = screen_w;
    dst->h = (int)(screen_w / video_aspect);
}
dst->x = (screen_w - dst->w) / 2;
dst->y = (screen_h - dst->h) / 2;
```

---

## 8. State of the Project & Verification

- **Code Status**: Complete and fully implemented.
- **Build Quality**: Verified with GCC using `-std=c11 -Wall -Wextra -pthread -O2` with **0 compiler warnings**.
- **Clean Shutdown**: `SIGINT` / `SIGTERM` signals and GTK window close events cleanly unblock the frame queue, join decoder threads, release FFmpeg / SDL resources, and persist state.

---

## 9. Potential Future Extensions (Optional Ideas)

1. **Multi-Monitor Support**: Extend `wallpaper_create()` to create separate SDL windows / renderers across all detected `SDL_GetNumVideoDisplays()`.
2. **AppIndicator / Status Icon**: Integrate `libappindicator3-dev` or `GtkStatusIcon` for a minimized system tray menu.
3. **Hardware Video Acceleration**: Add `AV_HWDEVICE_TYPE_VAAPI` / `AV_HWDEVICE_TYPE_CUDA` decoder configuration in `decoder.c` for ultra-low CPU 4K@60 decoding.
4. **Pause on Fullscreen Window**: Use X11 event sniffing (`_NET_ACTIVE_WINDOW`) to automatically pause video playback when games or fullscreen applications are in focus.
