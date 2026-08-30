# Changelog

All notable changes to WallScape are documented in this file.

## 2.4
- 🪟 **Official Windows 11 Edition**: Native Windows 11 live video & static wallpaper support.
- 🎬 **Windows Desktop WorkerW Hooking**: Seamless video background rendering behind desktop icons and taskbar via Progman/WorkerW.
- ⚡ **Direct3D 11 Hardware Acceleration**: Added D3D11VA, DXVA2, CUDA, and Intel QSV decoding on Windows (CPU < 3%).
- 🖼️ **Windows Static Wallpaper Integration**: Native `SystemParametersInfoW` wallpaper control.
- 🚀 **Windows Registry Autostart**: 1-click Start on Boot via Windows Run Registry.
- 📦 **Windows NSIS Installer & Portable ZIP**: Built-in Windows installer generation with desktop shortcuts and uninstaller.

## 2.3
- 🔄 **Reliable Automatic Restart After Update**: Resolved the single-instance D-Bus lock race condition during in-app updates, allowing WallScape to automatically and cleanly relaunch the updated version immediately after installation.

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
