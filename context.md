# Project Context: Live Wallpaper Software

## 1. Overview & Purpose
**Live Wallpaper Software** is a high-performance, lightweight, hardware-accelerated desktop video wallpaper daemon and gallery studio written in pure **C (C11)**.

- **Primary Target**: Zorin OS 18 & Zorin OS 17 (Ubuntu 24.04 / 22.04 LTS, GNOME 46 / Mutter, x86_64).
- **Core Functionality**:
  - Displays local video files (`.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`) as the active desktop background.
  - Sits directly **behind desktop icons and widgets** using window manager desktop-layer hinting (`_NET_WM_WINDOW_TYPE_DESKTOP`).
  - Seamless infinite looping without audio decoding overhead.
  - **Modern GTK3 Gallery Studio**: Visual card grid (`GtkFlowBox`) with video thumbnails extracted via FFmpeg and GdkPixbuf.
  - **Active State Indicator**: Overlay green checkmark badge (`✔ Active`) with an active green glowing border.
  - **Interactive Confirmation Dialogs**: Asks for confirmation before turning ON or turning OFF wallpaper playback.
  - **Simplified Workflow**: Direct folder selection (`📁 Select Folder...`) with one-click wallpaper activation.
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
│  │           Live Wallpaper Studio GUI (gui.c)           │  │
│  │   [📁 Select Folder...]  [⏸ Pause]  [⏹ Turn Off]       │  │
│  │   Thumbnail Card Grid (GtkFlowBox + GtkOverlay)       │  │
│  │   Active Indicator (✔ Checkmark Badge + Green Glow)   │  │
│  │   Confirmation Modals: Turn ON / Turn OFF             │  │
│  └───────────────────────────────────────────────────────┘  │
│                              │                              │
│         g_timeout_add() Render Timer (~60 FPS / 16ms)       │
│                              │                              │
│  ┌───────────────────────────▼───────────────────────────┐  │
│  │         SDL2 Wallpaper Surface (wallpaper.c)          │  │
│  │  - Window: Borderless at (0, 0), no fullscreen flag   │  │
│  │  - Xlib Hints: _NET_WM_WINDOW_TYPE_DESKTOP            │  │
│  │    _NET_WM_STATE_SKIP_TASKBAR, SKIP_PAGER, BELOW      │  │
│  │  - XSetWMHints: InputHint = False (clicks pass to WM) │  │
│  │  - Texture: Streaming SDL_PIXELFORMAT_IYUV (YUV420P)  │  │
│  │  - Scaling: Aspect-ratio cover-crop (no distortion)   │  │
│  │  - Auto-resizing: Dynamic texture recreation on-the-fly│ │
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

---

## 4. Tech Stack & Dependencies

All dependencies are standard C libraries available in Ubuntu / Zorin repositories:

| Library | Package Name | Purpose |
|---------|--------------|---------|
| **C11 Compiler** | `build-essential` | ISO C11 standard support (`<stdatomic.h>`, `<stdbool.h>`, `<threads.h>`/pthreads) |
| **CMake** | `cmake` (3.16+) | Cross-platform build system |
| **FFmpeg 6.1+** | `libavformat-dev`, `libavcodec-dev`, `libavutil-dev`, `libswscale-dev` | Media container demuxing, video stream decoding, pixel scaling, thumbnail extraction |
| **SDL2** | `libsdl2-dev` | Borderless window creation, hardware-accelerated 2D renderer, YUV streaming textures |
| **GTK+ 3.0** | `libgtk-3-dev` | Control panel window, GtkFlowBox thumbnail grid, custom CSS provider, GLib event loop timer |
| **GdkPixbuf** | Part of `libgtk-3-dev` | Thumbnail image scaling and rendering |
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
    ├── common.h / common.c     # Core definitions: VideoFrame, FrameQueue, AppState, atomic flags
    ├── config.h / config.c     # Key=value config persistence (~/.config/live-wallpaper/config.txt)
    ├── decoder.h / decoder.c   # FFmpeg demuxing, decoding, YUV420P conversion, PTS pacing, EOF loop
    ├── wallpaper.h / wallpaper.c # SDL2 window creation, X11 EWMH desktop hints, dynamic texture resize
    ├── thumbnail.h / thumbnail.c # Fast video thumbnail frame extraction and GdkPixbuf generation
    ├── gui.h / gui.c           # GTK3 thumbnail grid gallery, confirmation modals, CSS styling, render loop
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
# Default mode (opens thumbnail grid studio + resumes saved wallpaper)
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
// Borderless window without SDL_WINDOW_FULLSCREEN_DESKTOP to avoid META_LAYER_FULLSCREEN override
ctx->window = SDL_CreateWindow("live-wallpaper-surface", 0, 0, ctx->screen_w, ctx->screen_h,
                               SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);

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

// 3. Disable input focus so mouse events pass directly to desktop icons
XWMHints hints;
hints.flags = InputHint | StateHint;
hints.input = False;
hints.initial_state = NormalState;
XSetWMHints(dpy, xwin, &hints);

XLowerWindow(dpy, xwin);
XFlush(dpy);
```

### B. Fast Thumbnail Generation (`thumbnail.c`)
```c
GdkPixbuf *thumbnail_generate(const char *video_path, int target_w, int target_h) {
    // 1. Open input container and find video stream
    // 2. Seek slightly into video (5%) to avoid black opening frames
    // 3. Decode first valid video frame
    // 4. Convert format to RGB24 with sws_scale()
    // 5. Wrap into GdkPixbuf and return
}
```

### C. Confirmation Modals & In-Place UI Updates (`gui.c`)
- Clicking an inactive card prompts: *"Apply Live Wallpaper?"* → Confirms and sets background.
- Clicking the currently active card prompts: *"Turn Off Live Wallpaper?"* → Confirms and restores desktop.
- List items and card visual classes are updated in-place without destroying GTK widgets during event dispatch.

---

## 8. State of the Project & Verification

- **Code Status**: Complete, fully implemented, and tested.
- **Build Quality**: Verified with GCC using `-std=c11 -Wall -Wextra -pthread -O2` with **0 compiler warnings and 0 errors**.
- **Clean Shutdown**: `SIGINT` / `SIGTERM` signals and GTK window close events cleanly unblock the frame queue, join decoder threads, release FFmpeg / SDL resources, and persist state.
