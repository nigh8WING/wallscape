# Changelog

All notable changes to WallScape are documented in this file.

## 2.7
- 🚀 **High-Performance Thumbnail Queue**: Replaced concurrent per-item threads with a dedicated sequential background worker queue, keeping RAM flat (~30MB) and completely eliminating freezes or crashes when opening large folders with 27+ HD/4K videos.
- 💾 **Disk Thumbnail Caching**: Automatically saves generated thumbnails to `~/.cache/live-wallpaper/thumbnails/`, making repeated folder loads and refreshes instantaneous (&lt;1ms) with zero FFmpeg decoding overhead.
- 🔒 **Refresh Busy-Lock & Thread Safety**: Prevents thread storming and crashes when clicking the folder Refresh button repeatedly, providing smooth UI click feedback while safely completing the active scan.

## 2.6
- 🔊 **Live Wallpaper Audio Support**: Full audio decoding and playback for live video wallpapers powered by FFmpeg `libswresample` and SDL2 Audio.
- 🎛️ **Audio Mute/Unmute Controls**: Quick-toggle button in the bottom control bar and system tray menu, with automatic sensitivity handling for videos with or without audio tracks.
- 💾 **Audio Preference Persistence**: Seamlessly remembers your mute/unmute state in `config.txt` across laptop restarts.

## 2.5
- 📁 Instant Folder View on Startup: Synchronized inner page stack state after realization.

## 2.4
- 📁 Instant Folder View on Startup refinement.

## 2.3
- 🔄 Reliable Automatic Restart After Update.

## 2.2
- 🎯 Instant Active Wallpaper Detection, Synchronized Splash Intro, and Silent Background Autostart.

## 2.1
- 📐 Aspect-Preserving Universal Fit & Initial Splash refinements.

## 2.0
- 🎬 Smooth Vector Splash Intro & Startup refinement.

## 1.9
- 🎬 Animated Onboarding Splash Screen foundation.

## 1.8
- 📜 **Release Notes Integration**: Automatic changelog extraction into in-app update dialog and GitHub releases.

## 1.7
- 🚀 Start on Boot & In-Folder Refresh foundation.

## 1.6
- ⚡ **Universal GPU Hardware Acceleration**: Offloads video decoding directly to GPU across Intel & AMD (VA-API), NVIDIA (CUDA / VDPAU), and Vulkan/DRM devices, reducing CPU usage from ~27% down to ~2-5%.
- 🛡️ **Automatic CPU Fallback**: Seamless multi-threaded CPU software fallback if a codec is unsupported by the GPU.
- 🏷️ **Hardware Status Badge**: Active GPU acceleration interface is displayed in the status bar (e.g. `[⚡ vaapi]`).

## 1.5
- 📦 **In-Place Seamless Auto-Update**: Downloads and extracts updates directly into user-space with instant seamless auto-restart (zero root passwords or store redirects).
- 📜 **MIT License**: Added official open-source MIT License.

## 1.4
- 🖼️ **Static Image Wallpapers**: Added support for static image wallpaper collections with GNOME desktop background integration.
- 📁 **Multi-Folder Library**: Support for importing, organizing, and browsing multiple wallpaper folders.
