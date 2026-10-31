# SPDX-License-Identifier: GPL-2.0-or-later
#
# OpenIPC Hi3536DV100: ARMv7-A, ARM EABI softfp. The default toolchain is the
# one installed by research/build.sh vdec; override with
# WBLINK_HI3536DV100_TOOLCHAIN when building from another workspace.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(WBLINK_HI3536DV100_TOOLCHAIN "" CACHE PATH
    "Root of the OpenIPC HiSilicon Hi3536DV100 toolchain")
if(WBLINK_HI3536DV100_TOOLCHAIN STREQUAL "")
    if(NOT "$ENV{WBLINK_HI3536DV100_TOOLCHAIN}" STREQUAL "")
        set(WBLINK_HI3536DV100_TOOLCHAIN "$ENV{WBLINK_HI3536DV100_TOOLCHAIN}")
    else()
        get_filename_component(_wblink_hi3536_default
            "${CMAKE_CURRENT_LIST_DIR}/../../research/toolchain/toolchain.hisilicon-hi3536dv100"
            ABSOLUTE)
        set(WBLINK_HI3536DV100_TOOLCHAIN "${_wblink_hi3536_default}")
    endif()
endif()

set(_wblink_hi3536_prefix
    "${WBLINK_HI3536DV100_TOOLCHAIN}/bin/arm-openipc-linux-gnueabi")
if(NOT EXISTS "${_wblink_hi3536_prefix}-g++")
    message(FATAL_ERROR
        "Hi3536DV100 toolchain not found at '${WBLINK_HI3536DV100_TOOLCHAIN}'. "
        "Install it with research/build.sh vdec or set "
        "-DWBLINK_HI3536DV100_TOOLCHAIN=<toolchain root>.")
endif()

set(CMAKE_C_COMPILER "${_wblink_hi3536_prefix}-gcc")
set(CMAKE_CXX_COMPILER "${_wblink_hi3536_prefix}-g++")
set(CMAKE_STRIP "${_wblink_hi3536_prefix}-strip")

# The bundled OpenIPC sysroot is glibc 2.38, while the Hi3536 firmware has
# glibc 2.36. GNU mode in glibc 2.38 redirects parser calls to C23 symbols.
set(_wblink_glibc_compat "${CMAKE_CURRENT_LIST_DIR}/glibc-2.36-compat.h")
set(CMAKE_CXX_FLAGS_INIT "-include ${_wblink_glibc_compat}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)