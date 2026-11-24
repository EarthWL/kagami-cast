# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(KagamiCast VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Optimised build unless the caller asks for something else
# (an existing build dir with an empty CMAKE_BUILD_TYPE is switched to Release too).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Product name shown to users; build-all.sh passes the value from product.conf.
set(APP_DISPLAY_NAME "Kagami Cast" CACHE STRING "Product name shown to users (ASCII)")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Version and name reach the code through a generated header (no quoted -D values,
# which windres is known to mangle when they also land on the .rc command line).
configure_file(app_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/app_config.h @ONLY)

set(APP_SOURCES
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

# Icon + version information embedded in the .exe
if(WIN32)
    enable_language(RC)
    configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

# WIN32 -> GUI subsystem (no console window pops up)
add_executable(KagamiCast WIN32 ${APP_SOURCES})

target_include_directories(KagamiCast PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(KagamiCast PRIVATE Qt6::Widgets)
if(WIN32)
  target_link_libraries(KagamiCast PRIVATE dxgi)   # OBS mode: map OBS's adapter LUID to an index
endif()

# No machine-specific paths are compiled in: uxplay.exe and the GStreamer runtime
# are found next to KagamiCast.exe (override with UXPLAY_EXE for development).
