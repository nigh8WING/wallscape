cmake_minimum_required(VERSION 3.16)
project(live-wallpaper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -pthread -O2)
endif()

# Find PkgConfig
find_package(PkgConfig REQUIRED)

# Common Required Libraries
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL   REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE    REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)
pkg_check_modules(SDL2       REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(GTK3       REQUIRED IMPORTED_TARGET gtk+-3.0)

# Platform-specific dependencies
if(WIN32)
    # Win32 platform libraries
    set(PLATFORM_LIBS
        user32
        gdi32
        shell32
        ole32
        advapi32
        shlwapi
    )
else()
    # Linux X11 dependencies
    pkg_check_modules(X11      REQUIRED IMPORTED_TARGET x11)
    pkg_check_modules(XFIXES   REQUIRED IMPORTED_TARGET xfixes)
    pkg_check_modules(XEXT     REQUIRED IMPORTED_TARGET xext)
    set(PLATFORM_LIBS
        PkgConfig::X11
        PkgConfig::XFIXES
        PkgConfig::XEXT
        pthread
        m
        ${CMAKE_DL_LIBS}
    )
endif()

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
if(WIN32)
    # Build as Windows GUI application (no persistent console window)
    add_executable(live-wallpaper WIN32 ${SOURCES})
else()
    add_executable(live-wallpaper ${SOURCES})
endif()

target_include_directories(live-wallpaper PRIVATE src)

target_link_libraries(live-wallpaper PRIVATE
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
    PkgConfig::SWSCALE
    PkgConfig::SWRESAMPLE
    PkgConfig::SDL2
    PkgConfig::GTK3
    ${PLATFORM_LIBS}
)

# Installation rules
include(GNUInstallDirs)

install(TARGETS live-wallpaper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(NOT WIN32)
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
else()
    install(FILES assets/live-wallpaper.png
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ==========================================
# CPack Packaging Configuration
# ==========================================
set(CPACK_PACKAGE_NAME "wallscape")
set(CPACK_PACKAGE_VENDOR "WallScape")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WallScape - Live Video & Static Desktop Wallpaper Studio")
set(CPACK_PACKAGE_VERSION "2.7")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_DISPLAY_NAME "WallScape Live Wallpaper")
    set(CPACK_NSIS_PACKAGE_NAME "WallScape")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "WallScape")
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "WallScape Developer <support@wallscape.local>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/nigh8WING/wallscape")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)

