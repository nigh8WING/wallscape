# Live Wallpaper for Zorin OS (18 & 17) / GNOME

A lightweight, hardware-accelerated live video desktop wallpaper manager written in pure **C11**, designed specifically for **Zorin OS 18 & 17** (Ubuntu 24.04 / 22.04 LTS, GNOME/Mutter, x86_64).

---

## Features

- **100% Free & Open Source**: Built using standard system libraries (`FFmpeg`, `SDL2`, `GTK3`, `Xlib`).
- **Behind Desktop Icons**: Places video beneath desktop icons and widgets using window manager desktop-layer hinting (`_NET_WM_WINDOW_TYPE_DESKTOP`).
- **Dual Session Support (Wayland & X11)**: Automatically forces XWayland mode when launched under Wayland (default in Zorin OS 18), allowing desktop hints to function seamlessly under GNOME/Mutter.
- **Seamless Infinite Looping**: High-efficiency in-memory container seeking with zero audio overhead.
- **Aspect-Ratio Cover Scaling**: Fits any video aspect ratio (16:9, 16:10, 21:9, 4:3) to the screen, cropping excess automatically without distortion.
- **Multi-threaded Architecture**: Video decoding and scaling runs on a dedicated background thread; rendering is timer-driven (~60 FPS) on the main GTK loop.
- **GTK3 Control Panel**: Interactive GUI with a file chooser dialog, pause/resume toggle, stop button, and clean exit.
- **Persistent State & Autostart**: Automatically remembers the last-used video in `~/.config/live-wallpaper/config.txt` and supports login autostart.

---

## Supported Formats

- `.mp4` (H.264, HEVC/H.265, AV1)
- `.mkv` (Matroska)
- `.webm` (VP8, VP9, AV1)
- `.avi`, `.mov`

---

## Quick Start (One-Liner Install & Run)

```bash
# 1. Install dependencies
sudo apt update && sudo apt install -y build-essential cmake pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libsdl2-dev libgtk-3-dev libx11-dev

# 2. Clone and build
git clone https://github.com/nigh8WING/zorin18-livewallpaper.git
cd zorin18-livewallpaper
cmake -B build && cmake --build build

# 3. Launch
./build/live-wallpaper
```

---

## Prerequisites (Dependencies)

All required packages are available directly in Ubuntu / Zorin OS repositories:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libsdl2-dev libgtk-3-dev libx11-dev
```

---

## Build & Install

### 1. Build Locally

```bash
cmake -B build
cmake --build build
```

The compiled binary will be located at `./build/live-wallpaper`.

### 2. System-wide Installation (Optional)

```bash
sudo cmake --install build
```

This installs:
- Executable to `/usr/local/bin/live-wallpaper`
- Desktop entry to `/usr/local/share/applications/live-wallpaper.desktop`

### 3. Uninstallation

If you ever wish to remove the system-wide installation:

```bash
sudo rm -f /usr/local/bin/live-wallpaper
sudo rm -f /usr/local/share/applications/live-wallpaper.desktop
```

---

## Usage

### Interactive Mode (Control Panel)
```bash
live-wallpaper
# Or if running locally from build directory:
./build/live-wallpaper
```

### Direct Video Path
```bash
live-wallpaper /path/to/my_wallpaper.mp4
```

### Headless / Background Mode (No Control Panel Window)
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

On login, `live-wallpaper` will read the last played video from `~/.config/live-wallpaper/config.txt` and resume playback behind your desktop icons.

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                   Main Thread (GTK3)                     │
│  ┌────────────────────────────────────────────────────┐  │
│  │           Control Panel GUI (gui.c)                │  │
│  │   [Choose Video]  [Pause/Resume]  [Stop]  [Quit]   │  │
│  └────────────────────────────────────────────────────┘  │
│                           │                              │
│       g_timeout_add (60 FPS Render Callback)             │
│                           │                              │
│  ┌────────────────────────▼───────────────────────────┐  │
│  │        SDL2 Wallpaper Window (wallpaper.c)         │  │
│  │  - Window Type: _NET_WM_WINDOW_TYPE_DESKTOP        │  │
│  │  - Window State: SKIP_TASKBAR, SKIP_PAGER, BELOW   │  │
│  │  - Hardware-Accelerated SDL_Renderer (V-Sync)      │  │
│  │  - SDL_UpdateYUVTexture() + Aspect Cover Cropping  │  │
│  └────────────────────────▲───────────────────────────┘  │
└───────────────────────────┼──────────────────────────────┘
                            │ pop frame (non-blocking)
                 ┌──────────┴──────────┐
                 │ FrameQueue (Ring 3) │
                 └──────────▲──────────┘
                            │ push frame (blocking)
┌───────────────────────────┼──────────────────────────────┐
│  ┌────────────────────────┴───────────────────────────┐  │
│  │         FFmpeg Decoder Thread (decoder.c)          │  │
│  │  - libavformat: Demux container (Audio bypassed)   │  │
│  │  - libavcodec: Multi-threaded hardware decode      │  │
│  │  - libswscale: Convert pix_fmt to YUV420P          │  │
│  │  - Seamless EOF seek to start (Infinite loop)      │  │
│  └────────────────────────────────────────────────────┘  │
│               Background Decoder Thread                  │
└──────────────────────────────────────────────────────────┘
```

---

## License

MIT License — Feel free to use, modify, and distribute!
