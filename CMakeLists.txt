cmake_minimum_required(VERSION 3.28) # 3.28+ needed for C++23 module dependency scanning (HdRestirGpu / LightRHI)
project(HdRestir LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ----------------------------------------------------------------
# Default build type — RelWithDebInfo, not Debug.
#
# OpenUSD is built in Release. A pure Debug build would fail to link
# because the matching debug USD dylibs do not exist. RelWithDebInfo
# links against those Release dylibs while still producing full debug
# symbols and enabling our own debug assertions and logging
# (DEBUG_ENABLED below). Never change this default to Debug without
# first building OpenUSD in debug mode.
# ----------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo"
        CACHE STRING "Build type: Debug / RelWithDebInfo / Release / MinSizeRel" FORCE)
endif()

# DEBUG_ENABLED is ON for development build types (Debug and
# RelWithDebInfo) and OFF for Release / MinSizeRel.
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(_debug_default ON)
else()
    set(_debug_default OFF)
endif()
option(DEBUG_ENABLED  "Enable HdRestir debug macros (asserts, logs, named objects)" ${_debug_default})
option(METRICS_ENABLED "Enable HdRestir runtime metrics (variance logging, counters)" ${_debug_default})

# GPU-accelerated ReSTIR passes via the LightRHI submodule (external/rhi).
# LightRHI is built from C++23 modules, which require a compiler with
# clang-scan-deps. Fail at configure time when the requested GPU toolchain is
# incomplete; silently changing build features hides cross-environment errors.
option(GPU_ENABLED "Enable GPU-accelerated ReSTIR passes via LightRHI" ON)
if(GPU_ENABLED)
    get_filename_component(_cxx_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(_clang_scan_deps
        NAMES clang-scan-deps
        HINTS "${_cxx_compiler_dir}"
        NO_CACHE)

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "[HdRestir] GPU requires upstream Clang with clang-scan-deps; "
                            "${CMAKE_CXX_COMPILER_ID} is active")
    elseif(NOT _clang_scan_deps)
        message(FATAL_ERROR "[HdRestir] GPU requires clang-scan-deps next to the compiler or on PATH")
    elseif(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/rhi/CMakeLists.txt")
        message(FATAL_ERROR "[HdRestir] GPU requires the external/rhi submodule")
    else()
        set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${_clang_scan_deps}" CACHE FILEPATH "Path to clang-scan-deps")
    endif()
endif()

set(CMAKE_MACOSX_RPATH ON)

# On Windows, <windows.h> (pulled in transitively by USD/Arch) defines min/max
# macros that break std::min/std::max, so define NOMINMAX (and trim windows.h)
# ourselves.
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(CTest)

# ----------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------

# OpenUSD — the install location is never assumed. workflow.py resolves it
# (searching the environment) and pins it via $HDRESTIR_USD_INSTALL; we honour
# that first. Otherwise we fall back to the local from-source build under
# external/usd-install, and finally to whatever find_package(pxr) discovers via
# CMAKE_PREFIX_PATH / a PATH-derived prefix. pxrConfig.cmake also defines the
# TBB::tbb target USD links against, so we do NOT find TBB separately.
set(_usd_local "${CMAKE_CURRENT_SOURCE_DIR}/external/usd-install")
if(DEFINED ENV{HDRESTIR_USD_INSTALL} AND EXISTS "$ENV{HDRESTIR_USD_INSTALL}/pxrConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "$ENV{HDRESTIR_USD_INSTALL}")
elseif(EXISTS "${_usd_local}/pxrConfig.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${_usd_local}")
endif()
# USD's imaging targets (garch/hdSt/…) reference OpenGL::GL in their link
# interface, so find it explicitly.
find_package(OpenGL REQUIRED)
find_package(pxr CONFIG REQUIRED)

# Microsoft GSL — header-only; fetched directly.
include(FetchContent)
FetchContent_Declare(Microsoft.GSL
    GIT_REPOSITORY https://github.com/microsoft/GSL.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE)
set(GSL_INSTALL OFF CACHE BOOL "" FORCE)
set(GSL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Microsoft.GSL)

find_package(OpenImageDenoise QUIET)
if(OpenImageDenoise_FOUND)
    message(STATUS "[HdRestir] OpenImageDenoise found — OIDN enabled")
else()
    message(STATUS "[HdRestir] OpenImageDenoise not found — OIDN disabled")
endif()

# LightRHI (external/rhi) — only pulled in when the compiler can build its
# C++23 modules (see GPU_ENABLED guard above).
if(GPU_ENABLED)
    set(LIGHT_RHI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LIGHT_RHI_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/rhi)
    include("${CMAKE_CURRENT_SOURCE_DIR}/external/rhi/cmake/Shaders.cmake")

    if(NOT LIGHT_RHI_SLANG_AVAILABLE)
        message(FATAL_ERROR "[HdRestir] GPU requires slangc. Put slangc on PATH, set SLANG_DIR, or set SLANGC.")
    endif()
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

# source/gpu_functions/*.cpp may import LightRHI's C++23 modules. LightRHI's
# module ABI requires -fno-rtti on any TU that imports it, which would otherwise
# leak (as a PUBLIC usage requirement) onto every HdRestir source and break
# USD/boost-python's typeid/RTTI usage. Build those implementation files into a
# PRIVATE-linked static library below, which absorbs that requirement instead
# of re-exporting it. RenderPass code stays under source/gpu_passes in the main
# HdRestir target and talks to gpu_functions only through plain C++ headers.
file(GLOB_RECURSE GPU_FUNCTION_CPP CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/gpu_functions/*.cpp"
)
list(SORT GPU_FUNCTION_CPP)
list(REMOVE_ITEM SOURCE_CPP ${GPU_FUNCTION_CPP})

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

target_compile_definitions(HdRestir PRIVATE
    $<$<BOOL:${DEBUG_ENABLED}>:DEBUG_ENABLED=1>
    $<$<NOT:$<BOOL:${DEBUG_ENABLED}>>:DEBUG_ENABLED=0>
    $<$<BOOL:${METRICS_ENABLED}>:METRICS_ENABLED=1>
    $<$<NOT:$<BOOL:${METRICS_ENABLED}>>:METRICS_ENABLED=0>
    $<$<BOOL:${GPU_ENABLED}>:GPU_ENABLED=1>
    $<$<NOT:$<BOOL:${GPU_ENABLED}>>:GPU_ENABLED=0>
)

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

if(GPU_ENABLED)
    add_library(HdRestirGpu STATIC ${GPU_FUNCTION_CPP})
    target_compile_features(HdRestirGpu PRIVATE cxx_std_23)
    # Must match LightRHI's own CXX_EXTENSIONS OFF (module ABI/config consistency).
    set_target_properties(HdRestirGpu PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(HdRestirGpu PRIVATE ${SOURCE_INCLUDE_DIRS})
    target_compile_definitions(HdRestirGpu PRIVATE
        GPU_ENABLED=1
        $<$<BOOL:${METRICS_ENABLED}>:METRICS_ENABLED=1>
        $<$<NOT:$<BOOL:${METRICS_ENABLED}>>:METRICS_ENABLED=0>
    )
    # PRIVATE: absorbs LightRHI's PUBLIC -fno-rtti (module ABI requirement)
    # here instead of propagating it to HdRestir's other translation units.
    target_link_libraries(HdRestirGpu PRIVATE LightRHI::LightRHI LightRHI::Backend Microsoft.GSL::GSL gf)

    set(GPU_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_shaders")
    light_rhi_compile_shaders(
        TARGET          HdRestirGpu
        SHADERS_JSON    "${CMAKE_CURRENT_SOURCE_DIR}/source/gpu_functions/shaders/shaders_registry.generated.json"
        OUTPUT_DIR      "${GPU_SHADER_DIR}"
        BACKEND_DIR_VAR _GPU_SHADER_BACKEND
        EXTENSION_VAR   _GPU_SHADER_EXT
        FORMAT_VAR      _GPU_SHADER_FORMAT)

    target_compile_definitions(HdRestirGpu PRIVATE
        GPU_SHADER_DIR="${GPU_SHADER_DIR}"
        GPU_SHADER_BACKEND_DIR="${_GPU_SHADER_BACKEND}"
        GPU_SHADER_EXTENSION="${_GPU_SHADER_EXT}"
        GPU_SHADER_FORMAT=${_GPU_SHADER_FORMAT}
    )

    target_link_libraries(HdRestir PRIVATE HdRestirGpu)
endif()

target_compile_definitions(HdRestir PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:gsl_CONFIG_CONTRACT_VIOLATION_THROWS=1>
)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

if(OpenImageDenoise_FOUND)
    target_link_libraries(HdRestir PRIVATE OpenImageDenoise::OpenImageDenoise)
    target_compile_definitions(HdRestir PRIVATE HdRestir_HAS_OIDN)
endif()

# ----------------------------------------------------------------
# RPATH — resolved at configure time from wherever USD was found
# ----------------------------------------------------------------

# pxr_DIR is set by find_package(pxr CONFIG) to the dir holding pxrConfig.cmake
# — i.e. the install prefix wherever it was found, whether that is the local
# external/usd-install build, a system-wide install, or a conda environment —
# so the RPATH tracks the USD we actually linked against, not a hard-coded
# location.
if(pxr_DIR AND EXISTS "${pxr_DIR}/lib")
    set(_PLUGIN_RPATH "${pxr_DIR}/lib")
elseif(EXISTS "${_usd_local}/lib")
    set(_PLUGIN_RPATH "${_usd_local}/lib")
elseif(DEFINED ENV{CONDA_PREFIX})
    set(_PLUGIN_RPATH "$ENV{CONDA_PREFIX}/lib")
elseif(TARGET TBB::tbb)
    get_target_property(_tbb_loc TBB::tbb LOCATION)
    get_filename_component(_PLUGIN_RPATH "${_tbb_loc}" DIRECTORY)
endif()

if(GPU_ENABLED)
    list(APPEND _PLUGIN_RPATH "$<TARGET_FILE_DIR:LightRHIBackend>")
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
