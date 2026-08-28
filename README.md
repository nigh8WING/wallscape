# WallScape — Live & Static Wallpaper Manager for Zorin OS / GNOME

A lightweight, hardware-accelerated dual-mode **Live Video & Static Image** desktop wallpaper manager written in pure **C11**, designed specifically for **Zorin OS 18 & 17** (Ubuntu 24.04 / 22.04 LTS, GNOME/Mutter, x86_64).

---

## Features

- **🎬 Live Video Wallpapers**: High-performance video background playback (`.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`) positioned directly **behind desktop icons and widgets** (`_NET_WM_WINDOW_TYPE_DESKTOP`).
- **🖼️ Static Image Wallpapers**: Seamless support for all static images (`.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.svg`, `.gif`) natively integrated with GNOME's `GSettings` (Light and Dark mode compatible).
- **🗂️ Dual-Tab Sidebar Navigation**: Switch effortlessly between Live Wallpapers and Static Image galleries.
- **🖼️ Compact 130x75 Thumbnail Grid**: Fast in-memory thumbnail extraction for videos and images with responsive multi-column layout.
- **✔ Active State Badges**: Visual green checkmark badge (`✔ Active`) and green glowing border on the currently active wallpaper.
- **💬 Safe Confirmation Modals**: Confirms before applying or turning off wallpapers.
- **Dual Session Support (Wayland & X11)**: Automatically enables XWayland mode when launched under Wayland (default in Zorin OS 18), allowing desktop hints to function seamlessly under GNOME/Mutter.
- **Seamless Infinite Looping**: High-efficiency in-memory container seeking with zero audio decoding overhead.
- **Aspect-Ratio Cover Scaling**: Fits any aspect ratio (16:9, 16:10, 21:9, 4:3) to the screen, cropping excess automatically without distortion.
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
  libsdl2-dev libgtk-3-dev libx11-dev

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

To generate an installer file that users can double-click to install on Zorin OS:

```bash
cd build
cpack -G DEB
```

This generates `live-wallpaper-1.0.0-Linux.deb` in the `build/` directory.

### 3. System-wide Installation (Direct)

```bash
sudo cmake --install build
```

This installs:
- Executable to `/usr/local/bin/live-wallpaper`
- Desktop entry to `/usr/local/share/applications/live-wallpaper.desktop`
- SVG application icon to `/usr/local/share/icons/hicolor/scalable/apps/live-wallpaper.svg`

### 3. Uninstallation

```bash
sudo rm -f /usr/local/bin/live-wallpaper
sudo rm -f /usr/local/share/applications/live-wallpaper.desktop
sudo rm -f /usr/local/share/icons/hicolor/scalable/apps/live-wallpaper.svg
```

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

## Autostart on Login

To make your live wallpaper launch automatically whenever you log into Zorin OS:

```bash
mkdir -p ~/.config/autostart
cp live-wallpaper.desktop ~/.config/autostart/
```

---

## 🚀 Publishing Releases & Automatic Updates

WallScape has a built-in **Auto-Updater** that checks GitHub Releases for new updates.

### How to Publish a New Version:
1. Update `CPACK_PACKAGE_VERSION` in [`CMakeLists.txt`](file:///home/user/Coding/Live%20Wallpaper%20Software/CMakeLists.txt) and `WALLSCAPE_CURRENT_VERSION` in [`src/updater.h`](file:///home/user/Coding/Live%20Wallpaper%20Software/src/updater.h) (e.g. `1.1.0`).
2. Commit and push a git tag to GitHub:
   ```bash
   git tag v1.1.0
   git push origin v1.1.0
   ```
3. GitHub Actions will automatically:
   * Build WallScape on Ubuntu
   * Package `wallscape-1.1.0-Linux.deb`
   * Create a GitHub Release with the `.deb` attached
4. Existing users running WallScape will automatically receive an update notification prompting them to download and install the new version with one click!

---

## License

MIT License — Feel free to use, modify, and distribute!
