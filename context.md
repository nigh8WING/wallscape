# Project Context: WallScape

## 1. Overview & Purpose
**WallScape** is a high-performance, lightweight, hardware-accelerated desktop wallpaper manager and gallery studio written in pure **C (C11)**.

- **Primary Target**: Zorin OS 18 & Zorin OS 17 (Ubuntu 24.04 / 22.04 LTS, GNOME 46 / Mutter, x86_64).
- **Core Functionality**:
  - **Dual-Mode Left Sidebar**:
    - **🎬 Live Wallpapers Tab**: High-performance video wallpapers (`.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`) playing behind desktop icons with SDL2 hardware acceleration and infinite looping.
    - **🖼️ Static Wallpapers Tab**: High-resolution static image gallery (`.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.svg`, `.gif`) natively integrated with GNOME's `GSettings`.
  - **Compact 130x75 Thumbnail Grid**: Responsive card grid (`GtkFlowBox`) with video frame extraction and image preview scaling.
  - **Empty-State UI**: Centered placeholder graphic and instructions when no folder has been imported.
  - **Active State Indicator**: Overlay green checkmark badge (`✔ Active`) with an active green glowing border.
  - **Interactive Confirmation Dialogs**: Asks for confirmation before turning ON or turning OFF wallpapers.
  - **Zero Memory Leaks & Zero-CPU Idle Mode**: Strict resource lifecycle management and condition-variable sleeping when idle/paused.
  - **Polished Card-Stack SVG Branding**: Sleek dark vector logo with layered cards, gradient wallpaper, and play badge in `assets/live-wallpaper.svg`.
  - **Persistent State**: Automatically remembers and restores both live video and static image folders across sessions in `~/.config/live-wallpaper/config.txt`.
  - **One-Click Native Debian Packaging (.deb)**: Integrated CPack Debian generator (`wallscape-1.0.0-Linux.deb`) for double-click installation via Zorin OS App Center.
  - **Automatic In-App Updates**: Background updater querying GitHub Releases API with 1-click update download and installation.
  - **Automated CI/CD Workflows**: GitHub Actions pipelines for automated compilation, packaging, and GitHub Release asset creation on Git tag push.
- **Cost**: 100% Free & Open Source, utilizing standard Ubuntu repository packages.

---

## 2. Platform Compatibility & Desktop Layering

### Zorin OS 18 / GNOME 46 Wayland & Desktop Icons NG (DING)
- **Challenge**: 
  - Zorin OS 18 uses GNOME 46 Wayland with the `zorin-desktop-icons` (DING) extension managing desktop icons.
  - Desktop icons need to be fully visible and interactive on top of the live video wallpaper.
  - GUI window minimize actions must never minimize other unrelated application windows.
- **Solution**:
  1. **Hybrid Display Architecture**:
     - GTK3 control panel runs on **native Wayland** (`GDK_BACKEND=wayland`). Its window management actions (`xdg_toplevel` minimize/restore) are isolated to itself.
     - SDL2 wallpaper surface runs via **XWayland** (`SDL_VIDEODRIVER=x11`), allowing bottom-layer desktop surface stacking.
  2. **Layering & Surface Management**:
     - `_NET_WM_WINDOW_TYPE_DESKTOP` + `_NET_WM_STATE_BELOW` positions the wallpaper in Mutter's `META_LAYER_DESKTOP`, strictly beneath taskbar panels, docks (`META_LAYER_DOCK`), and normal application windows.
     - `override_redirect` is disabled to prevent covering the taskbar and panel.
     - `XLowerWindow` is called strictly once upon showing to ensure stable stacking.
     - `WM_CLIENT_LEADER` is cleared from the SDL window to prevent window-grouping side effects.

---

## 3. Architecture & Data Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       Main Thread (GTK3 - Wayland)                          │
│  ┌─────────────────────────┬─────────────────────────────────────────────┐  │
│  │      Left Sidebar       │       GtkStack Studio Pages                 │  │
│  │   [Logo] WallScape      │  1. Live Wallpaper Grid (130x75 tiles)      │  │
│  │   [🎬 Live Wallpapers]  │  2. Static Wallpaper Grid (130x75 tiles)    │  │
│  │   [🖼️ Static Wallpapers]│  - Empty-State Placeholders                 │  │
│  │   --------------------  │  - Active Badges (✔ Active)                 │  │
│  │   v1.0.0                │  - Confirmation Modals: Turn ON / OFF       │  │
│  │   [Check for Updates]   │  - Update Notification Modal                │  │
│  └─────────────────────────┴─────────────────────────────────────────────┘  │
│                              │                                              │
│         g_timeout_add() Render Timer (~60 FPS / 16ms)                       │
│                              │                                              │
│  ┌───────────────────────────▼───────────────────────────────────────────┐  │
│  │             SDL2 Wallpaper Surface (wallpaper.c - XWayland)           │  │
│  │  - _NET_WM_WINDOW_TYPE_DESKTOP & _NET_WM_STATE_BELOW                  │  │
│  │  - _NET_WM_DESKTOP = 0xFFFFFFFF (sticky across all workspaces)       │  │
│  │  - Input disabled (XWMHints.input = False)                            │  │
│  │  - Texture: Streaming SDL_PIXELFORMAT_IYUV (YUV420P)                  │  │
│  │  - Scaling: Aspect-ratio cover-crop (no distortion)                   │  │
│  │  - Managed in META_LAYER_DESKTOP (below taskbar and normal windows)   │  │
│  └───────────────────────────▲───────────────────────────────────────────┘  │
└──────────────────────────────┼──────────────────────────────────────────────┘
                               │ pop frame (non-blocking)
┌──────────────────────────────┴──────────────────────────────────────────────┐
│                    Decoder Background Thread (decoder.c)                     │
│  avformat_open_input() → avcodec_send_packet() → avcodec_receive_frame()    │
│  → sws_scale() [YUV420P] → Thread-Safe FrameQueue (Ring Buffer)             │
│  - Seamless end-of-file seeking (infinite loop)                             │
│  - Pthread condition-variable sleep when paused (0% CPU)                    │
└─────────────────────────────────────────────────────────────────────────────┘
                               ▲
┌──────────────────────────────┴──────────────────────────────────────────────┐
│                    Updater Background Thread (updater.c)                     │
│  Checks GitHub API: GET /repos/nigh8WING/wallscape/releases/latest            │
│  - Parses tag_name, compare semver, downloads .deb & launches installer      │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Module Map

| Module | Source Files | Responsibilities |
|---|---|---|
| **Entry & Lifecycle** | `src/main.c` | Display environment setup, signal handling, CLI parsing (`--no-gui`, `-v`, `-h`) |
| **GUI Control Panel** | `src/gui.c`, `src/gui.h` | Dual-tab sidebar, thumbnail grids, empty states, confirmation modals, updater UI |
| **Auto-Updater** | `src/updater.c`, `src/updater.h` | Background GitHub Releases checker, semver compare, async `.deb` download & launch |
| **Wallpaper Surface** | `src/wallpaper.c`, `src/wallpaper.h` | SDL2 window, X11 desktop hints, XFixes click-through, hardware rendering |
| **Static Background** | `src/static_wallpaper.c`, `.h` | GNOME GSettings (`picture-uri`, `picture-uri-dark`) integration |
| **Video Decoder** | `src/decoder.c`, `src/decoder.h` | Multi-threaded FFmpeg 6.1 decoding, frame pacing, infinite loop |
| **Thumbnail Engine** | `src/thumbnail.c`, `src/thumbnail.h` | Video frame seeking/extraction + static image scaling (130x75) |
| **Configuration** | `src/config.c`, `src/config.h` | Persistent key=value storage (`video_path`, `live_folder`, `static_folder`) |
| **Common Data** | `src/common.c`, `src/common.h` | `VideoFrame`, `FrameQueue`, `AppState` |
| **Branding Asset** | `assets/live-wallpaper.svg` | Layered card stack vector SVG application logo |
| **CI/CD Pipelines** | `.github/workflows/ci.yml`, `release.yml` | Automated build testing and GitHub Release `.deb` publishing |

---

## 5. Build, Package & Run

```bash
# 1. Build locally
cmake -B build -S .
cmake --build build

# 2. Package as a .deb installer for Zorin OS
cd build
cpack -G DEB

# 3. Run
./build/live-wallpaper
```

---

## 6. Release & Distribution Workflow

```bash
# Bump version in CMakeLists.txt and src/updater.h (e.g., 1.1.0)
git tag v1.1.0
git push origin v1.1.0
```
- GitHub Actions automatically compiles, builds `wallscape-1.1.0-Linux.deb`, and creates a GitHub Release with assets attached.
- WallScape clients automatically detect the update on next launch and prompt the user with a 1-click update.
