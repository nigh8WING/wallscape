# Project Context: WallScape

## 1. Overview & Purpose
**WallScape** is a high-performance, lightweight, hardware-accelerated desktop wallpaper manager and gallery studio written in pure **C (C11)**.

- **Primary Target**: Zorin OS 18 & Zorin OS 17 (Ubuntu 24.04 / 22.04 LTS, GNOME 46 / Mutter, x86_64).
- **Core Functionality**:
  - **Dual-Mode Left Sidebar**:
    - **🎬 Live Wallpapers Tab**: High-performance video wallpapers (`.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`) playing behind desktop icons with SDL2 hardware acceleration and infinite looping.
    - **🖼️ Static Wallpapers Tab**: High-resolution static image gallery (`.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.svg`, `.gif`) natively integrated with GNOME's `GSettings`.
  - **Multi-Folder Library Management & Navigation**:
    - **Root Album View**: Folder cards for each imported folder showing folder name, item counter, active badge indicator (`✔ Active`), and a delete (`✕`) button to remove folders from the library without touching disk files.
    - **Inside-Folder View**: Detailed wallpaper card grid for the selected folder with an `← Folders` back button in the header.
    - **`+ Add Folder…` Card & Header Button**: Quick folder importation for both video and static image collections.
  - **Single-Instance Application Lifecycle (`GtkApplication`)**:
    - Runs as a persistent single instance (`com.nigh8wing.wallscape`) with `g_application_hold()`.
    - **Window Close (`X`)**: Hides the control panel window while wallpaper playback continues uninterrupted in the background.
    - **Re-Launch Wakeup**: Launching WallScape from the terminal or application menu automatically wakes and unhides the running primary instance with all controls and active badges intact.
    - **Explicit Quit**: Terminating via the **"Quit"** button or tray popup **"Quit WallScape"** gracefully stops playback, releases resources, and exits the process.
  - **Seamless Live ↔ Static Transitions & Real-Time Sync**:
    - Switching from live to static wallpaper stops the decoder thread, hides the SDL2 surface, and applies the static background.
    - Listens to GNOME `org.gnome.desktop.background` (`picture-uri` & `picture-uri-dark`) changes in real time.
  - **Compact Responsive Thumbnail Grid**: Card grid (`GtkFlowBox`) with non-blocking asynchronous thumbnail extraction (`g_idle_add`) and aspect-ratio preservation (letterboxing/pillarboxing for portrait videos).
  - **System Tray & Action Center Integration**: Background execution via `GtkStatusIcon` with right-click menu (Show/Hide, Turn Off, Quit) and taskbar suppression (`skip_taskbar_hint`).
  - **Dynamic FPS-Matched Render Loop**: Automatically synchronizes GTK timer tick intervals to video stream frame rate (e.g. ~41ms for 24fps, ~16ms for 60fps) to eliminate wasted CPU cycles.
  - **Multi-Desktop Sticky Rendering**: Runtime EWMH `_NET_WM_STATE_STICKY` + `_NET_WM_DESKTOP = 0xFFFFFFFF` ClientMessage signaling so live wallpapers persist across all virtual workspaces (Super+W / workspace switching).
  - **Seamless Video Switching**: Independent `decoder_quit` thread lifecycle state to allow instant video switching without affecting overall application runtime.
  - **Empty-State UI**: Centered placeholder graphic and instructions when no folder has been imported.
  - **Active State Indicator**: Overlay green checkmark badge (`✔ Active`) with an active green glowing border.
  - **Interactive Confirmation Dialogs**: Asks for confirmation before turning ON or turning OFF wallpapers.
  - **Zero Memory Leaks & Zero-CPU Idle Mode**: Strict resource lifecycle management and condition-variable sleeping when idle/paused.
  - **Polished Card-Stack SVG Branding**: Sleek dark vector logo with layered cards, gradient wallpaper, and play badge in `assets/live-wallpaper.svg`.
  - **Persistent State**: Automatically remembers and restores multi-folder lists (`live_folders=`, `static_folders=`) and active wallpapers across sessions in `~/.config/live-wallpaper/config`.
  - **One-Click Native Debian Packaging (.deb)**: Integrated CPack Debian generator (`wallscape-1.1.0-Linux.deb`) for double-click installation via Zorin OS App Center.
  - **Automatic In-App Updates**: Background updater querying GitHub Releases API with 1-click update download and installation.
  - **Automated Commit-Triggered CI/CD**: GitHub Actions pipeline that automatically detects version bumps in `CMakeLists.txt`, builds `.deb` packages, creates Git tags, and publishes GitHub Releases on push to `main`.
- **Cost**: 100% Free & Open Source, utilizing standard Ubuntu repository packages.

---

## 2. Platform Compatibility & Desktop Layering

### Zorin OS 18 / GNOME 46 Wayland & Desktop Icons NG (DING)
- **Challenge**: 
  - Zorin OS 18 uses GNOME 46 Wayland with the `zorin-desktop-icons` (DING) extension managing desktop icons.
  - Desktop icons need to be fully visible and interactive on top of the live video wallpaper.
  - GUI window minimize actions must never minimize other unrelated application windows.
  - Wallpaper must remain stacked underneath windows across all virtual workspace switches.
- **Solution**:
  1. **Hybrid Display Architecture**:
     - GTK3 control panel runs on **native Wayland** (`GDK_BACKEND=wayland`). Its window management actions (`xdg_toplevel` minimize/restore) are isolated to itself.
     - SDL2 wallpaper surface runs via **XWayland** (`SDL_VIDEODRIVER=x11`), allowing bottom-layer desktop surface stacking.
  2. **Layering, Multi-Desktop & Surface Management**:
     - `_NET_WM_WINDOW_TYPE_DESKTOP` + `_NET_WM_STATE_BELOW` positions the wallpaper in Mutter's `META_LAYER_DESKTOP`, strictly beneath taskbar panels, docks (`META_LAYER_DOCK`), and normal application windows.
     - `_NET_WM_STATE_STICKY` + `_NET_WM_STATE_SKIP_TASKBAR` + `_NET_WM_STATE_SKIP_PAGER` sent via EWMH `ClientMessage` to the root window upon map ensuring Mutter applies sticky state dynamically across all virtual workspaces.
     - `override_redirect` is disabled to prevent covering the taskbar and panel.
     - `XLowerWindow` is called upon showing to ensure stable stacking at the bottom of the layer.
     - `WM_CLIENT_LEADER` is cleared from the SDL window to prevent window-grouping side effects.

---

## 3. Architecture & Data Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                 Main Thread (GtkApplication - Wayland)                      │
│  ┌─────────────────────────┬─────────────────────────────────────────────┐  │
│  │      Left Sidebar       │       GtkStack Studio Pages                 │  │
│  │   [Logo] WallScape      │  1. Live Wallpapers Tab:                    │  │
│  │   [🎬 Live Wallpapers]  │     - Root Folders View (Folder Cards)      │  │
│  │   [🖼️ Static Wallpapers]│     - Inside-Folder Grid (130x75 Video Cards│  │
│  │   --------------------  │  2. Static Wallpapers Tab:                  │  │
│  │   v1.1.0                │     - Root Folders View (Folder Cards)      │  │
│  │   [Check for Updates]   │     - Inside-Folder Grid (130x75 Image Cards│  │
│  │                         │  - Navigation: [← Folders] & [+ Add Folder] │  │
│  │                         │  - Active Badges (✔ Active)                 │  │
│  │                         │  - Confirmation Modals: Turn ON / OFF       │  │
│  │                         │  - Update Notification Modal                │  │
│  └─────────────────────────┴─────────────────────────────────────────────┘  │
│                                │                                            │
│         [System Tray / Action Center (GtkStatusIcon)]                       │
│         [Dynamic FPS Timer: 1000/fps ms (16ms @ 60fps, 41ms @ 24fps)]       │
│         [GNOME GSettings Desktop Sync: picture-uri, picture-uri-dark]       │
│                                │                                            │
│  ┌─────────────────────────────▼─────────────────────────────────────────┐  │
│  │             SDL2 Wallpaper Surface (wallpaper.c - XWayland)           │  │
│  │  - _NET_WM_WINDOW_TYPE_DESKTOP & _NET_WM_STATE_BELOW                  │  │
│  │  - _NET_WM_STATE_STICKY & _NET_WM_DESKTOP = 0xFFFFFFFF (All Desktops) │  │
│  │  - Input disabled (XWMHints.input = False)                            │  │
│  │  - Texture: Streaming SDL_PIXELFORMAT_IYUV (YUV420P)                  │  │
│  │  - Scaling: Aspect-ratio cover-crop (no distortion)                   │  │
│  │  - Managed in META_LAYER_DESKTOP (below taskbar and normal windows)   │  │
│  └─────────────────────────────▲─────────────────────────────────────────┘  │
└────────────────────────────────┼────────────────────────────────────────────┘
                                 │ pop frame (non-blocking)
┌────────────────────────────────┴────────────────────────────────────────────┐
│                    Decoder Background Thread (decoder.c)                     │
│  avformat_open_input() → avcodec_send_packet() → avcodec_receive_frame()    │
│  → sws_scale() [YUV420P, 32-byte stride] → Thread-Safe FrameQueue (8 slots) │
│  - Seamless end-of-file seeking (infinite loop)                             │
│  - Independent atomic `decoder_quit` flag for seamless video switching      │
│  - Pthread condition-variable sleep when paused (0% CPU)                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                 ▲
┌────────────────────────────────┴────────────────────────────────────────────┐
│                    Updater Background Thread (updater.c)                     │
│  Checks GitHub API: GET /repos/nigh8WING/wallscape/releases/latest          │
│  - Parses tag_name, compare semver, downloads .deb & launches installer      │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Module Map

| Module | Source Files | Responsibilities |
|---|---|---|
| **Entry & Lifecycle** | `src/main.c` | Single-instance `GtkApplication` (`com.nigh8wing.wallscape`), background hold, command-line parsing (`--no-gui`, `-v`, `-h`), window wake/present |
| **GUI Control Panel** | `src/gui.c`, `src/gui.h` | Multi-folder navigation, folder album views, thumbnail grids, back buttons, confirmation modals, updater UI, System Tray icon (`GtkStatusIcon`), FPS-adaptive render timer, GSettings sync |
| **Auto-Updater** | `src/updater.c`, `src/updater.h` | Background GitHub Releases checker, semver compare, async `.deb` download & launch |
| **Wallpaper Surface** | `src/wallpaper.c`, `src/wallpaper.h` | SDL2 window, X11 EWMH desktop/sticky ClientMessage hints, hardware rendering |
| **Static Background** | `src/static_wallpaper.c`, `.h` | GNOME GSettings (`picture-uri`, `picture-uri-dark`) integration, format validation |
| **Video Decoder** | `src/decoder.c`, `src/decoder.h` | Multi-threaded FFmpeg 6.1 decoding, frame pacing, infinite loop, isolated thread lifecycle (`decoder_quit`) |
| **Thumbnail Engine** | `src/thumbnail.c`, `src/thumbnail.h` | Aspect-ratio preserving video frame seeking/extraction (letterbox/pillarbox) + static image scaling (130x75) |
| **Configuration** | `src/config.c`, `src/config.h` | Persistent key=value storage (`video_path`, `static_path`, `live_folders`, `static_folders`) |
| **Common Data** | `src/common.c`, `src/common.h` | `VideoFrame`, `FrameQueue` (8-frame ring buffer), `AppState` (atomic synchronization) |
| **Branding Asset** | `assets/live-wallpaper.svg` | Layered card stack vector SVG application logo |
| **CI/CD Pipelines** | `.github/workflows/ci.yml`, `release.yml` | Automated build, packaging (.deb), auto-tagging, and GitHub Release creation on push |

---

## 5. Build, Package & Run

```bash
# 1. Build locally
cmake -B build -S .
cmake --build build -- -j$(nproc)

# 2. Package as a .deb installer for Zorin OS
cd build
cpack -G DEB

# 3. Run
./build/live-wallpaper
```

---

## 6. Release & Distribution Workflow

### Mandatory Versioning Rule for New Features & Bug Fixes
Every update pushed to `main` that introduces **new features** or **bug fixes** must increment the version number in both [`CMakeLists.txt`](file:///home/user/Coding/Live%20Wallpaper%20Software/CMakeLists.txt) (`set(CPACK_PACKAGE_VERSION "X.Y.Z")`) and [`src/updater.h`](file:///home/user/Coding/Live%20Wallpaper%20Software/src/updater.h) (`#define WALLSCAPE_CURRENT_VERSION "X.Y.Z"`).

#### Versioning Standard (`MAJOR.MINOR.PATCH`):
- **PATCH Bump (`1.2.0` ➜ `1.2.1`)**: Bug fixes, stability improvements, UI responsiveness fixes, performance optimizations.
- **MINOR Bump (`1.2.0` ➜ `1.3.0`)**: New features, new capabilities (e.g. multi-folder management, new settings, new format support).
- **MAJOR Bump (`1.0.0` ➜ `2.0.0`)**: Major architectural redesigns or breaking changes.

### Automated Release Lifecycle:
1. Bump version in `CMakeLists.txt` and `src/updater.h`.
2. Commit and push to `main`:
   ```bash
   git commit -am "feat/fix: describe changes and bump version to vX.Y.Z"
   git push origin main
   ```
3. GitHub Actions (`.github/workflows/ci.yml`):
   - Automatically detects the new version tag `vX.Y.Z`.
   - Compiles WallScape with `-O2` optimizations.
   - Builds the Debian package `wallscape-X.Y.Z-Linux.deb` with CPack.
   - Automatically creates the Git tag `vX.Y.Z`.
   - Publishes the new GitHub Release with generated release notes and the `.deb` asset attached.
4. Installed WallScape clients automatically detect the new release on startup or via the "Check for Updates" button and prompt the user with a 1-click upgrade.

---

## 7. Package Management & Uninstallation

```bash
# Remove package
sudo apt remove wallscape
# or
sudo dpkg -r wallscape

# Purge configurations
sudo apt purge wallscape
```

