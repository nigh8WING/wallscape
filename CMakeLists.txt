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
