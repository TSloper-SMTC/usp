# SPDX-License-Identifier: BSD-3-Clause-Clear

# This CMake toolchain file describes how to cross-compile for Linux ARM64 targets
# (e.g., Raspberry Pi 4/5 64-bit, ARM64 development boards)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_CROSSCOMPILING 1)

# Detect whichever AArch64 cross-compiler prefix is installed
find_program(AARCH64_GCC NAMES aarch64-none-linux-gnu-gcc aarch64-linux-gnu-gcc)
if(NOT AARCH64_GCC)
    message(FATAL_ERROR "No AArch64 cross-compiler found. Install aarch64-none-linux-gnu-gcc or aarch64-linux-gnu-gcc.")
endif()
get_filename_component(AARCH64_GCC_NAME "${AARCH64_GCC}" NAME)
string(REPLACE "-gcc" "" CROSS_COMPILE "${AARCH64_GCC_NAME}")

set(CMAKE_C_COMPILER   ${CROSS_COMPILE}-gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}-g++)
set(CMAKE_ASM_COMPILER ${CROSS_COMPILE}-gcc)
set(CMAKE_AR           ${CROSS_COMPILE}-ar)
set(CMAKE_LINKER       ${CROSS_COMPILE}-ld)
set(CMAKE_NM           ${CROSS_COMPILE}-nm)
set(CMAKE_OBJCOPY      ${CROSS_COMPILE}-objcopy)
set(CMAKE_OBJDUMP      ${CROSS_COMPILE}-objdump)
set(CMAKE_STRIP        ${CROSS_COMPILE}-strip)
set(CMAKE_RANLIB       ${CROSS_COMPILE}-ranlib)

# Where to look for the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Compiler flags for ARM Cortex-A series (64-bit)
set(C_FLAGS_COMMON "\
-march=armv8-a \
-fdata-sections -ffunction-sections \
")

set(CMAKE_C_FLAGS_INIT   "${C_FLAGS_COMMON}")
set(CMAKE_CXX_FLAGS_INIT "${C_FLAGS_COMMON}")

# Linker flags to remove unused sections
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")
