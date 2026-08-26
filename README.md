# Live Wallpaper for Zorin OS (17 & 18) / GNOME

A lightweight, hardware-accelerated live video desktop wallpaper manager written in pure **C11**, designed specifically for **Zorin OS 17 & 18** (Ubuntu 22.04 / 24.04 LTS, GNOME/Mutter, x86_64).

---

## Features

- **100% Free & Open Source**: Built using only standard Ubuntu packages (`FFmpeg`, `SDL2`, `GTK3`, `Xlib`).
- **Seamless Infinite Looping**: Uses container seeking with zero audio overhead and no frame-stutter.
- **Behind Desktop Icons**: Uses the X11 `_NET_WM_WINDOW_TYPE_DESKTOP` window manager hint, placing video beneath desktop icons and desktop widgets (identical to *xwinwrap* and *Komorebi*).
- **Dual Session Support (X11 & Wayland)**: Automatically forces XWayland mode when launched under Wayland (the default in Zorin OS 18), allowing desktop hints to function seamlessly under GNOME/Mutter.
- **Aspect-Ratio Cover Scaling**: Fits any aspect ratio video (16:9, 16:10, 21:9, 4:3) to the screen, cropping excess automatically without distortion.
- **Multi-threaded Architecture**: Video decoding and scaling runs on a dedicated background thread; rendering is timer-driven (~60 FPS) on the main thread via GTK.
- **GTK3 Control Panel**: Interactive GUI with a file chooser dialog, pause/resume toggle, stop button, and clean exit.
- **Persistent State & Autostart**: Saves the last-used video in `~/.config/live-wallpaper/config.txt` and supports login autostart via standard `.desktop` entries.

---

## Supported Formats

- `.mp4` (H.264, HEVC/H.265, AV1)
- `.mkv` (Matroska)
- `.webm` (VP8, VP9, AV1)
- `.avi`, `.mov`

---

## Prerequisites (Build Dependencies)

All dependencies are completely free and available in the default Ubuntu / Zorin OS repositories:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libsdl2-dev libgtk-3-dev libx11-dev
```

---

## Build & Install

### 1. Build from Source

```bash
cd "/home/user/Coding/Live Wallpaper Software"
cmake -B build
cmake --build build
```

### 2. System-wide Installation (Optional)

```bash
sudo cmake --install build
```

This installs the `live-wallpaper` binary to `/usr/local/bin` and the desktop file to `/usr/local/share/applications`.

---

## Running

### Standard Mode (Opens GTK Control Panel)
```bash
./build/live-wallpaper
```

### Direct Video Path
```bash
./build/live-wallpaper /path/to/my_wallpaper.mp4
```

### Background / Headless Mode (No GUI Window)
```bash
./build/live-wallpaper --no-gui
```

---

## Setting Up Autostart on Login

To make your live wallpaper launch automatically whenever you log into Zorin OS:

```bash
mkdir -p ~/.config/autostart
cp live-wallpaper.desktop ~/.config/autostart/
```

On login, `live-wallpaper` will read the last played video from `~/.config/live-wallpaper/config.txt` and resume playback behind your desktop icons.

---

## Architecture

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
                            │ pop frame
                 ┌──────────┴──────────┐
                 │ FrameQueue (Ring 3) │
                 └──────────▲──────────┘
                            │ push frame
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
