# Changelog

All notable changes to WallScape are documented in this file.

## 2.2
- 🎯 **Instant Active Wallpaper Detection**: Synchronous active wallpaper resolution on cold start, ensuring the active card and `✔ Active` badge are immediately visible without delay.
- ⏱️ **Synchronized Splash Intro**: Live wallpaper activation is deferred until the splash animation finishes, activating smoothly right when the main studio appears.
- 🤫 **Silent Background Autostart**: System boot and `--no-gui` launch silently in the system tray without opening the main window.
- 📐 **Universal Aspect-Preserving Scaling**: Strict aspect ratio preservation for both horizontal and vertical videos (e.g. 1080x1920 portrait, 21:9 ultrawide, 16:10, 4:3) — never stretched, distorted, or cropped past screen boundaries.

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
