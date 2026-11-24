# SPDX-License-Identifier: GPL-3.0-or-later
# Kagami Cast for OBS — based on obs-plugintemplate (see LICENSE-obs-plugintemplate
# for the cmake/ helper files taken from it).
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp src/kagami-source.cpp src/kagami-source.hpp src/kagami-ipc.cpp src/kagami-ipc.hpp
)

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE d3d11 dxgi)
  # /DEBUG (set by the template) would embed the absolute PDB path, i.e. the
  # builder's user name, in the DLL; keep only the file name.
  target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "/PDBALTPATH:%_PDB%")
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
