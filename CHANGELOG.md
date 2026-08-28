# Changelog

All notable changes to WallScape are documented in this file.

## 2.1
- 📐 **Aspect-Preserving Universal Fit**: Automatic aspect ratio preservation for both horizontal and vertical videos (e.g. 1080x1920 portrait, 21:9 ultrawide, 16:10, 4:3) — never stretched, distorted, or cropped past screen boundaries.
- 🎬 **Smooth Vector Splash Intro**: Elegant vector card deck animation on startup that automatically cross-fades into the app without extra button prompts.
- 🔄 **In-Folder Refresh**: Dedicated Refresh buttons to discover new video and image wallpapers instantly on the fly.
- 🚀 **Start on Boot**: One-click autostart option in sidebar and tray menu to resume wallpapers on laptop login.
- 💅 **Polished Update Dialog**: Clean native bold formatting, spaced bullet points, and modern card container styling.

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
