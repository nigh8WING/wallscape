# WallScape — Live & Static Desktop Wallpaper Studio

A high-performance, lightweight, hardware-accelerated dual-mode **Live Video & Static Image** desktop wallpaper studio written in pure **C11**, supporting **Windows 11** and **Linux (Zorin OS / Ubuntu / GNOME)**.

---

## Features

- **🎬 Live Video Wallpapers**: High-performance video background playback (`.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`) positioned directly **behind desktop icons and taskbars**:
  - **Windows 11**: Hooked into `WorkerW` / `Progman` desktop hierarchy.
  - **Linux**: Managed in Mutter/GNOME `META_LAYER_DESKTOP` with `_NET_WM_WINDOW_TYPE_DESKTOP`.
- **⚡ Universal GPU Hardware Acceleration**:
  - **Windows 11**: Direct3D 11 (`D3D11VA`), DirectX (`DXVA2`), NVIDIA (`CUDA`), Intel (`QSV`).
  - **Linux**: Intel & AMD (`VA-API`), NVIDIA (`CUDA` / `VDPAU`), Vulkan, and DRM.
  - Automatic multi-threaded CPU software fallback (drops CPU usage to ~2-5%).
- **📐 Aspect-Preserving Universal Scaling**: Automatically preserves exact aspect ratios for horizontal (16:9, 16:10, 21:9 ultrawide, 4:3) and vertical (1080 × 1920 portrait) videos without distortion or stretching.
- **🎬 Smooth Vector Splash Intro**: 60 FPS Cairo 2D animation with fanning cards, active gradient card, and pulsing play badge that smoothly cross-fades into the workspace.
- **🔄 In-Folder Dynamic Refresh**: Dedicated **Refresh** buttons to instantly detect newly added or removed wallpapers on the fly.
- **🚀 Start on Boot (Autostart)**: 1-click toggle in sidebar footer and tray menu to silently launch WallScape in the background upon login (Windows Registry Run key / Linux XDG Autostart).
- **🖼️ Static Image Wallpapers**: Seamless support for all static images (`.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.svg`, `.gif`):
  - **Windows 11**: Native `SystemParametersInfoW` integration.
  - **Linux**: GNOME `GSettings` integration.
- **🗂️ Multi-Folder Library Management**: Organize wallpapers into albums with folder cards, counters, and quick addition/deletion.
- **📦 In-Place 1-Click Auto-Updater**: Background GitHub Releases checker with 1-click update download and installation.
- **💅 Polished Markdown Release Notes**: In-app formatted "What's New in this update" dialog with bold headers, code styling, and clean bullet points.
- **✔ Active State Badges**: Visual green checkmark badge (`✔ Active`) and glowing border on the currently active wallpaper.
- **💬 Safe Confirmation Modals**: Confirms before applying or turning off wallpapers.
- **Zero Memory Leaks & Zero-CPU Idle**: Strict resource cleanup lifecycle and thread sleep when idle/paused.

---

## Supported Formats

- **Videos**: `.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`
- **Images**: `.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.svg`, `.gif`

---

## 🪟 Windows 11 Installation & Build

### Download Pre-built Installer
Download the latest `wallscape-2.4-win64-setup.exe` or portable `.zip` from [GitHub Releases](https://github.com/nigh8WING/wallscape/releases).

### Build from Source on Windows (MSYS2 / MinGW64)
```bash
# 1. Install MSYS2 & dependencies in MINGW64 environment
pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkg-config mingw-w64-x86_64-gtk3 mingw-w64-x86_64-ffmpeg \
  mingw-w64-x86_64-SDL2 mingw-w64-x86_64-nsis

# 2. Configure and build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3. Create Windows installer & portable ZIP package
cd build
cpack -G "NSIS;ZIP"
```

---

## 🐧 Linux (Zorin OS / Ubuntu / GNOME)

### Quick Start (One-Liner Install & Run)
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

### Create `.deb` Package (Zorin OS / Ubuntu)
```bash
cd build
cpack -G DEB
```

This generates `wallscape-2.7-Linux.deb` in the `build/` directory.

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

### Interactive Studio GUI
```bash
live-wallpaper
# Or on Windows:
live-wallpaper.exe
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

WallScape includes an automated GitHub Actions pipeline (`.github/workflows/ci.yml`) that builds both **Linux `.deb`** and **Windows 11 `.exe` / `.zip`** installers automatically upon push.

### How to Publish a New Version:
1. Update `CPACK_PACKAGE_VERSION` in `CMakeLists.txt` and `WALLSCAPE_CURRENT_VERSION` in `src/updater.h`.
2. Update `CHANGELOG.md` with the new version and highlights.
3. Commit and push:
   ```bash
   git commit -am "feat: release v2.7"
   git push origin windows
   ```
4. GitHub Actions will automatically:
   * Build WallScape on Ubuntu and Windows 11
   * Package `wallscape-2.7-Linux.deb`, `wallscape-2.7-win64.exe`, and `wallscape-2.7-win64.zip`
   * Extract release notes from `CHANGELOG.md`
   * Publish the GitHub Release with Linux & Windows assets attached!

---

## License

MIT License — Free and Open Source.
