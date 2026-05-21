cmake_minimum_required(VERSION 3.20)
project(HdRestir LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DEBUG_ENABLED "Enable HdRestir debug macros (asserts, logs, named objects)" ON)
option(METRICS_ENABLED "Enable HdRestir runtime metrics (variance logging, counters)" ON)

set(CMAKE_MACOSX_RPATH ON)

include(CTest)

# ----------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------

find_package(pxr REQUIRED)
find_package(TBB REQUIRED COMPONENTS tbb)
find_package(Microsoft.GSL CONFIG REQUIRED)

find_package(OpenImageDenoise QUIET)
if(OpenImageDenoise_FOUND)
    message(STATUS "[HdRestir] OpenImageDenoise found — OIDN enabled")
else()
    message(STATUS "[HdRestir] OpenImageDenoise not found — OIDN disabled")
endif()

# ----------------------------------------------------------------
# Sources
# ----------------------------------------------------------------

set(PLUGIN_SOURCES
    implicit_surface_scene_index_plugin.cpp
    renderer_plugin.cpp
)

file(GLOB_RECURSE SOURCE_CPP CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
)
list(SORT SOURCE_CPP)

set(SOURCES
    ${PLUGIN_SOURCES}
    ${SOURCE_CPP}
)

# ----------------------------------------------------------------
# Target — MODULE for Hydra plugins
# ----------------------------------------------------------------

add_library(HdRestir MODULE ${SOURCES})

set_source_files_properties(
    renderer_plugin.cpp
    implicit_surface_scene_index_plugin.cpp
    PROPERTIES
    COMPILE_OPTIONS "-Wno-deprecated-declarations"
)

if(DEBUG_ENABLED)
    set(HDRESTIR_DEBUG_SETTING_VALUE 1)
else()
    set(HDRESTIR_DEBUG_SETTING_VALUE 0)
endif()

if(METRICS_ENABLED)
    set(HDRESTIR_METRICS_SETTING_VALUE 1)
else()
    set(HDRESTIR_METRICS_SETTING_VALUE 0)
endif()

target_compile_definitions(HdRestir PRIVATE
    HDRESTIR_DEBUG_SETTING=${HDRESTIR_DEBUG_SETTING_VALUE}
    HDRESTIR_METRICS_SETTING=${HDRESTIR_METRICS_SETTING_VALUE}
)

# USD vcpkg exports without pxr:: namespace.
# Collect source/ and every immediate subdirectory as include roots.
set(_src_root "${CMAKE_CURRENT_SOURCE_DIR}/source")
file(GLOB _source_subdirs LIST_DIRECTORIES true "${_src_root}/*")
set(SOURCE_INCLUDE_DIRS "${_src_root}")
foreach(_dir IN LISTS _source_subdirs)
    if(IS_DIRECTORY "${_dir}")
        list(APPEND SOURCE_INCLUDE_DIRS "${_dir}")
    endif()
endforeach()

# Transitive deps (gf, vt, sdf…) are pulled in automatically.
target_include_directories(HdRestir PRIVATE ${SOURCE_INCLUDE_DIRS})

target_link_libraries(HdRestir PRIVATE
    tf gf vt work
    hd hdsi hio
    sdf sdr
    usdImaging
    Microsoft.GSL::GSL
)

target_compile_definitions(HdRestir PRIVATE
    $<$<CONFIG:Debug>:gsl_CONFIG_CONTRACT_VIOLATION_THROWS=1>
)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

if(OpenImageDenoise_FOUND)
    target_link_libraries(HdRestir PRIVATE OpenImageDenoise::OpenImageDenoise)
    target_compile_definitions(HdRestir PRIVATE HdRestir_HAS_OIDN)
endif()

# ----------------------------------------------------------------
# RPATH — resolved at configure time, handles vcpkg and conda
# ----------------------------------------------------------------

if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_PLUGIN_RPATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
elseif(DEFINED ENV{CONDA_PREFIX})
    set(_PLUGIN_RPATH "$ENV{CONDA_PREFIX}/lib")
else()
    get_target_property(_tbb_loc TBB::tbb LOCATION)
    get_filename_component(_PLUGIN_RPATH "${_tbb_loc}" DIRECTORY)
endif()

message(STATUS "[HdRestir] BUILD_RPATH → ${_PLUGIN_RPATH}")

if(APPLE)
    set(_INSTALL_RPATH "@loader_path;@loader_path/../lib")
elseif(UNIX)
    set(_INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
else()
    set(_INSTALL_RPATH "")
endif()

set_target_properties(HdRestir PROPERTIES
    BUILD_RPATH  "${_PLUGIN_RPATH}"
    INSTALL_RPATH "${_INSTALL_RPATH}"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# ----------------------------------------------------------------
# plugInfo.json.in -> build/resources/plugInfo.json
# PXR_PLUGINPATH_NAME = build/resources
# ----------------------------------------------------------------

set(PLUG_INFO_LIBRARY_PATH "${CMAKE_SHARED_MODULE_PREFIX}HdRestir${CMAKE_SHARED_MODULE_SUFFIX}")
set(PLUG_INFO_RESOURCE_PATH "resources")
set(PLUG_INFO_ROOT "..")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/resources")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.json.in"
    "${CMAKE_BINARY_DIR}/resources/plugInfo.json"
    @ONLY
)
