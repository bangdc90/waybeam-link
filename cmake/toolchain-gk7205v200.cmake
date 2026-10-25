# SPDX-License-Identifier: GPL-2.0-or-later
#
# OpenIPC GK7205V200: ARMv7-A, musl soft-float ABI. The default toolchain is
# the pinned research checkout beside waybeam-link; override with
# WBLINK_GK7205V200_TOOLCHAIN when building from another workspace.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(WBLINK_GK7205V200_TOOLCHAIN "" CACHE PATH
    "Root of the OpenIPC Goke GK7205V200 toolchain")
if(WBLINK_GK7205V200_TOOLCHAIN STREQUAL "")
    if(NOT "$ENV{WBLINK_GK7205V200_TOOLCHAIN}" STREQUAL "")
        set(WBLINK_GK7205V200_TOOLCHAIN "$ENV{WBLINK_GK7205V200_TOOLCHAIN}")
    else()
        get_filename_component(_wblink_gk7205_default
            "${CMAKE_CURRENT_LIST_DIR}/../../research/toolchain/toolchain.goke-gk7205v200"
            ABSOLUTE)
        set(WBLINK_GK7205V200_TOOLCHAIN "${_wblink_gk7205_default}")
    endif()
endif()

set(_wblink_gk7205_prefix
    "${WBLINK_GK7205V200_TOOLCHAIN}/bin/arm-openipc-linux-musleabi")
if(NOT EXISTS "${_wblink_gk7205_prefix}-g++")
    message(FATAL_ERROR
        "GK7205V200 toolchain not found at '${WBLINK_GK7205V200_TOOLCHAIN}'. "
        "Build it with research/build.sh venc-goke or set "
        "-DWBLINK_GK7205V200_TOOLCHAIN=<toolchain root>.")
endif()

set(CMAKE_C_COMPILER "${_wblink_gk7205_prefix}-gcc")
set(CMAKE_CXX_COMPILER "${_wblink_gk7205_prefix}-g++")
set(CMAKE_STRIP "${_wblink_gk7205_prefix}-strip")

# Keep the cross compiler's bundled musl/sysroot selection intact. Static C++
# runtime linking avoids depending on an OpenIPC rootfs package outside the
# staged waybeam-link bundle.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)