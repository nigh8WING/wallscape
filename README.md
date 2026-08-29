# WallScape — Live & Static Desktop Wallpaper Studio

A lightweight, hardware-accelerated dual-mode **Live Video & Static Image** desktop wallpaper studio written in pure **C11**, designed specifically for **Zorin OS 18 & 17** (Ubuntu 24.04 / 22.04 LTS, GNOME 46 / Mutter, x86_64).

---

## Features

- **🎬 Live Video Wallpapers**: High-performance video background playback (`.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`) positioned directly **behind desktop icons and widgets** (`_NET_WM_WINDOW_TYPE_DESKTOP`).
- **⚡ Universal GPU Hardware Acceleration**: Offloads video decoding directly to GPU (Intel & AMD VA-API, NVIDIA CUDA / VDPAU, Vulkan/DRM), dropping CPU usage to ~2-5% with automatic multi-threaded CPU fallback.
- **📐 Aspect-Preserving Universal Scaling**: Automatically preserves exact aspect ratios for horizontal (16:9, 16:10, 21:9 ultrawide, 4:3) and vertical (1080 × 1920 portrait) videos without distortion, artificial stretching, or exceeding screen bounds.
- **🎬 Smooth Vector Splash Intro**: Custom 60 FPS Cairo 2D animation with fanning cards, active gradient card, and pulsing play badge that smoothly cross-fades directly into the application workspace.
- **🔄 In-Folder Dynamic Refresh**: Dedicated **Refresh** buttons to instantly detect newly added or removed wallpapers on the fly without restarting.
- **🚀 Start on Boot (Autostart)**: 1-click toggle in sidebar footer and tray menu to silently launch WallScape in the background and resume active wallpapers on laptop boot.
- **🖼️ Static Image Wallpapers**: Seamless support for all static images (`.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.svg`, `.gif`) natively integrated with GNOME's `GSettings` (Light & Dark mode compatible).
- **🗂️ Multi-Folder Library Management**: Organize wallpapers into albums with folder cards, counters, and quick addition/deletion.
- **📦 In-Place 1-Click Auto-Updater**: Background GitHub Releases checker with 1-click update download, extraction, and instant restart directly into user-space (`~/.local/share/wallscape/`).
- **💅 Polished Markdown Release Notes**: In-app formatted "What's New in this update" dialog with bold headers, code styling, and clean bullet points.
- **✔ Active State Badges**: Visual green checkmark badge (`✔ Active`) and glowing border on the currently active wallpaper.
- **💬 Safe Confirmation Modals**: Confirms before applying or turning off wallpapers.
- **Dual Session Support (Wayland & X11)**: Runs natively on Wayland with an XWayland desktop-layer surface, fully compatible with GNOME Shell and Zorin Desktop.
- **Seamless Infinite Looping**: High-efficiency in-memory container seeking with zero audio decoding overhead.
- **Zero Memory Leaks & Zero-CPU Idle**: Strict resource cleanup lifecycle and thread sleep when idle/paused.

---

## Supported Formats

- **Videos**: `.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`
- **Images**: `.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.svg`, `.gif`

---

## Quick Start (One-Liner Install & Run)

```bash
# 1. Install dependencies
sudo apt update && sudo apt install -y build-essential cmake pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libsdl2-dev libgtk-3-dev libx11-dev libva-dev libvdpau-dev

# 2. Clone and build
git clone https://github.com/nigh8WING/wallscape.git
cd wallscape
cmake -B build && cmake --build build

# 3. Launch
./build/live-wallpaper
```

---

## Build & Install

### 1. Build Locally

```bash
cmake -B build
cmake --build build
```

The compiled binary will be located at `./build/live-wallpaper`.

### 2. Create Double-Click `.deb` Installer (Recommended for Zorin OS)

```bash
cd build
cpack -G DEB
```

This generates `wallscape-2.3-Linux.deb` in the `build/` directory.

### 3. System-wide Installation (Direct)

```bash
sudo cmake --install build
```

This installs:
- Executable to `/usr/local/bin/live-wallpaper`
- Desktop entry to `/usr/local/share/applications/live-wallpaper.desktop`
- SVG application icon to `/usr/local/share/icons/hicolor/scalable/apps/live-wallpaper.svg`

---

## Usage

### Interactive Mode (Studio GUI)
```bash
live-wallpaper
# Or if running locally from build directory:
./build/live-wallpaper
```

### Direct Video Path
```bash
live-wallpaper /path/to/my_wallpaper.mp4
```

### Headless / Background Mode (No GUI Window)
```bash
live-wallpaper --no-gui
```

---

## 🚀 Publishing Releases & Automatic Updates

WallScape has a built-in **Auto-Updater** that checks GitHub Releases for new updates.

### How to Publish a New Version:
1. Update `CPACK_PACKAGE_VERSION` in `CMakeLists.txt` and `WALLSCAPE_CURRENT_VERSION` in `src/updater.h`.
2. Update `CHANGELOG.md` with the new version and highlights.
3. Commit and push to `main`:
   ```bash
   git commit -am "feat: release v2.3"
   git push origin main
   ```
4. GitHub Actions will automatically:
   * Build WallScape on Ubuntu
   * Package `wallscape-2.3-Linux.deb`
   * Extract release notes from `CHANGELOG.md`
   * Create the tag `v2.3` and publish the GitHub Release with the `.deb` attached
5. Existing users running WallScape will automatically receive an update notification prompting them to update with 1 click!

---

## License

MIT License — Feel free to use, modify, and distribute!
