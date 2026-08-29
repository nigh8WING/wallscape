cmake_minimum_required(VERSION 3.16)
project(live-wallpaper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
add_compile_options(-Wall -Wextra -pthread -O2)

# Find PkgConfig
find_package(PkgConfig REQUIRED)

# Required libraries
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL   REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE  REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SDL2     REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(GTK3     REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(X11      REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XFIXES   REQUIRED IMPORTED_TARGET xfixes)
pkg_check_modules(XEXT     REQUIRED IMPORTED_TARGET xext)

# Sources
set(SOURCES
    src/main.c
    src/common.c
    src/config.c
    src/decoder.c
    src/wallpaper.c
    src/thumbnail.c
    src/static_wallpaper.c
    src/gui.c
    src/updater.c
)

# Executable target
add_executable(live-wallpaper ${SOURCES})

target_include_directories(live-wallpaper PRIVATE src)

target_link_libraries(live-wallpaper PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SDL2
    PkgConfig::GTK3
    PkgConfig::X11
    PkgConfig::XFIXES
    PkgConfig::XEXT
    pthread
    m
    ${CMAKE_DL_LIBS}
)

# Installation rules
include(GNUInstallDirs)

install(TARGETS live-wallpaper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES live-wallpaper.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES assets/live-wallpaper.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME live-wallpaper.svg
)

install(FILES assets/live-wallpaper.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps
    RENAME live-wallpaper.png
)

# ==========================================
# CPack Packaging (.deb installer for Zorin OS)
# ==========================================
set(CPACK_PACKAGE_NAME "wallscape")
set(CPACK_PACKAGE_VENDOR "WallScape")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WallScape - Live Video & Static Desktop Wallpaper Manager for Zorin OS")
set(CPACK_PACKAGE_VERSION "2.3")
set(CPACK_GENERATOR "DEB")

# Debian package metadata
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "WallScape Developer <support@wallscape.local>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/nigh8WING/wallscape")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Automatically calculate shared library dependencies (FFmpeg, GTK3, SDL2, X11)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)

