# SPDX-License-Identifier: BSD-3-Clause-Clear

# This CMake toolchain file describes how to cross-compile for Linux ARM targets
# (e.g., Raspberry Pi, BeagleBone, ARM development boards)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_CROSSCOMPILING 1)

# Detect whichever ARM cross-compiler prefix is installed
find_program(ARM_GCC NAMES arm-none-linux-gnueabihf-gcc arm-linux-gnueabihf-gcc)
if(NOT ARM_GCC)
    message(FATAL_ERROR "No ARM cross-compiler found. Install arm-none-linux-gnueabihf-gcc or arm-linux-gnueabihf-gcc.")
endif()
get_filename_component(ARM_GCC_NAME "${ARM_GCC}" NAME)
string(REPLACE "-gcc" "" CROSS_COMPILE "${ARM_GCC_NAME}")

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

# Compiler flags for ARM Cortex-A series
# These are optimized for ARMv7/ARMv8 targets (32-bit mode)
# Adjust -march and -mfpu flags based on your specific ARM target
set(C_FLAGS_COMMON "\
-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
-fdata-sections -ffunction-sections \
")

set(CMAKE_C_FLAGS_INIT   "${C_FLAGS_COMMON}")
set(CMAKE_CXX_FLAGS_INIT "${C_FLAGS_COMMON}")

# Linker flags to remove unused sections
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")



