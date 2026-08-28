# Changelog

All notable changes to WallScape are documented in this file.

## 1.9
- 🎬 **Animated Onboarding Splash Screen**: Dynamic 60 FPS vector card-deck animation introducing WallScape on startup with seamless crossfade transition.
- 💅 **Polished Update Dialog**: Formatted release notes with native bold markup, clean bullet points, and modern card styling.
- 🚀 **Start on Boot**: One-click autostart option in sidebar and tray menu to resume wallpapers on laptop login.
- 🔄 **In-Folder Refresh**: Instantly scan and detect new wallpaper files in any directory without restarting.

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
