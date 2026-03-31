# SPDX-License-Identifier: BSD-3-Clause-Clear

cmake_minimum_required(VERSION 3.25)

# Check if BOARD is defined
if(NOT DEFINED BOARD OR BOARD STREQUAL "")
    message(FATAL_ERROR "You have to define a -DBOARD=NUCLEO_L476, NUCLEO_L073, LINUX, LINUX_ARM, or LINUX_ARM64")
endif()

message(STATUS "BOARD is set to: '${BOARD}'")

if("${BOARD}" STREQUAL "NUCLEO_L073")
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/smtc_hal_l0_LL/cmake_stm32l0_toolchain.cmake)
elseif("${BOARD}" STREQUAL "NUCLEO_L476")
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/smtc_hal_l4/cmake_stm32l4_toolchain.cmake)
elseif("${BOARD}" STREQUAL "LINUX")
    # Native Linux (x86_64, x86, etc.) - no cross-compilation
    # Suitable for virtual radio (udp_pf) testing on development machines
    message(STATUS "Native Linux build (no cross-compilation)")
elseif("${BOARD}" STREQUAL "LINUX_ARM")
    # Cross-compilation for ARM Linux targets (Raspberry Pi, etc.) - 32-bit
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/smtc_hal_linux/cmake_linux_arm_toolchain.cmake)
    message(STATUS "ARM Linux cross-compilation enabled (32-bit)")
elseif("${BOARD}" STREQUAL "LINUX_ARM64")
    # Cross-compilation for ARM64 Linux targets (Raspberry Pi 4/5 64-bit, etc.)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/smtc_hal_linux/cmake_linux_arm64_toolchain.cmake)
    message(STATUS "ARM64 Linux cross-compilation enabled")
elseif("${BOARD}" STREQUAL "FPB_RA0E2")
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/smtc_hal_ra0e2/cmake_ra0e2_toolchain.cmake)
else()
    message(FATAL_ERROR "Unknown BOARD: '${BOARD}'. Valid values: NUCLEO_L476, NUCLEO_L073, LINUX, LINUX_ARM, LINUX_ARM64, FPB_RA0E2")
endif()
set_property(CACHE BOARD PROPERTY STRINGS "NUCLEO_L476;NUCLEO_L073;LINUX;LINUX_ARM;LINUX_ARM64;FPB_RA0E2")

if("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64")
    project(examples
        DESCRIPTION "LoRa Basics Modem examples"
        LANGUAGES C
    )
else()
    project(examples
        DESCRIPTION "LoRa Basics Modem examples"
        LANGUAGES C ASM
    )
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_LINK_LIBRARIES_ONLY_TARGETS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Board-specific compile definitions
if("${BOARD}" STREQUAL "FPB_RA0E2")
    add_compile_definitions(FPB_RA0E2)
endif()

# Set default build type
set(default_build_type "MinSizeRel")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Disable fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Enable ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Generate a compile_commands.json for IDEs / clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

################################################################################
# Project Options
################################################################################

# Hw Modem default options
option(ENABLE_FULL_LBM_FEATURES "Enable all LBM features" OFF)

if(NOT DEFINED APP)
    # No APP specified: enable all features for hw_modem compatibility
    set(ENABLE_FULL_LBM_FEATURES ON)
elseif(DEFINED APP)
    string(TOUPPER ${APP} APP_UPPER)
    if(APP_UPPER STREQUAL HW_MODEM)
        # APP=HW_MODEM: enable all features
        set(ENABLE_FULL_LBM_FEATURES ON CACHE BOOL "" FORCE)
        set(LBM_GEOLOCATION ON CACHE BOOL "")
        set(LBM_ALMANAC OFF CACHE BOOL "")
    endif()
    if(APP_UPPER STREQUAL GEOLOCATION)
        # APP=GEOLOCATION: enable all features
        set(ENABLE_FULL_LBM_FEATURES  ON CACHE BOOL "" FORCE)
        set(LBM_GEOLOCATION ON CACHE BOOL "")
        set(LBM_ALMANAC OFF CACHE BOOL "")
    endif()
endif()


if(ENABLE_FULL_LBM_FEATURES)
    set(LBM_CMAKE_CONFIG_AUTO ON CACHE BOOL "")
    # Set LBM build options
    set(LBM_ALC_SYNC ON CACHE BOOL "")
    set(LBM_CLASS_B ON CACHE BOOL "")
    set(LBM_CLASS_C ON CACHE BOOL "")
    set(LBM_MULTICAST ON CACHE BOOL "")
    set(LBM_CSMA ON CACHE BOOL "")

    set(LBM_RELAY_RX ON CACHE BOOL "")
    set(LBM_RELAY_TX ON CACHE BOOL "")

    set(LBM_STREAM ON CACHE BOOL "")
    set(LBM_DEVICE_MANAGEMENT ON CACHE BOOL "")
    set(LBM_LFU ON CACHE BOOL "")

    set(LBM_FUOTA ON CACHE BOOL "")
    set(LBM_FUOTA_MPA ON CACHE BOOL "")

    set(LBM_STORE_AND_FORWARD ON CACHE BOOL "")
endif()

# Radio Planner Options
set(RP_MARGIN_DELAY "8" CACHE STRING "Radio Planner margin delay in ms")
message(STATUS "RP_MARGIN_DELAY: ${RP_MARGIN_DELAY}")

# LR20XX LoRa Plus Expansion Board vs LR20XX shield : IRQ is not mapped to the same PIN
if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
    option(LEGACY_EVK_LR20XX "Enable LR20XX shield" OFF)
    message(STATUS "Legacy (mbed) LR20XX shield: ${LEGACY_EVK_LR20XX}")
endif()

# Activation of CRC over SPI on LR11XX
if(${RAC_RADIO} MATCHES "^(lr1110|lr1120|lr1121)$")
    option(USE_LR11XX_CRC_SPI "Enable " OFF)
endif()

# Detect custom CMakeLists.txt for the selected APP
if(DEFINED APP AND NOT "${APP}" STREQUAL "")
    string(TOLOWER ${APP} APP_LOWER)
    set(EXAMPLE_CMAKE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/${APP_LOWER}/CMakeLists.txt")
    if(NOT EXISTS ${EXAMPLE_CMAKE_FILE})
        set(EXAMPLE_CMAKE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/${APP_LOWER}_example/CMakeLists.txt")
    endif()

    if(EXISTS ${EXAMPLE_CMAKE_FILE})
        message(STATUS "Loading example options: ${EXAMPLE_CMAKE_FILE}")
        set(CUSTOM_EXAMPLE_CMAKE_LOAD_OPTIONS TRUE)
        include(${EXAMPLE_CMAKE_FILE})
    else()
        set(CUSTOM_EXAMPLE_CMAKE_LOAD_OPTIONS FALSE)
        message(WARNING "No specific CMakeLists.txt found for APP=${APP}; skipping app-specific options")
    endif()
endif()

################################################################################
# Logging Options
option(RAC_LOG_ENABLE "Enable logging system" ON)

# RAC logging controls
option(RAC_CORE_LOG_ERROR_ENABLE "Enable RAC Core ERROR logs" ON)
option(RAC_CORE_LOG_CONFIG_ENABLE "Enable RAC Core CONFIG logs" ON)
option(RAC_CORE_LOG_INFO_ENABLE "Enable RAC Core INFO logs" OFF)
option(RAC_CORE_LOG_WARN_ENABLE "Enable RAC Core WARN logs" OFF)
option(RAC_CORE_LOG_DEBUG_ENABLE "Enable RAC Core DEBUG logs" OFF)
option(RAC_CORE_LOG_API_ENABLE "Enable RAC Core API logs" OFF)
option(RAC_CORE_LOG_RADIO_ENABLE "Enable RAC Core RADIO logs" OFF)

# LoRa logging controls
option(RAC_LORA_LOG_TX_ENABLE "Enable RAC LoRa TX logs" ON)
option(RAC_LORA_LOG_RX_ENABLE "Enable RAC LoRa RX logs" ON)
option(RAC_LORA_LOG_CONFIG_ENABLE "Enable RAC LoRa CONFIG logs" ON)
option(RAC_LORA_LOG_ERROR_ENABLE "Enable RAC LoRa ERROR logs" ON)
option(RAC_LORA_LOG_INFO_ENABLE "Enable RAC LoRa INFO logs" OFF)
option(RAC_LORA_LOG_WARN_ENABLE "Enable RAC LoRa WARN logs" OFF)
option(RAC_LORA_LOG_DEBUG_ENABLE "Enable RAC LoRa DEBUG logs" OFF)

# FLRC Burst debug controls
option(RAC_FLRC_BURST_DEBUG_ENABLE "Enable RAC FLRC Burst debug code" OFF)

# Predefined logging profiles
set(RAC_LOG_PROFILE "DEFAULT" CACHE STRING "logging profile")
set_property(CACHE RAC_LOG_PROFILE PROPERTY STRINGS "MINIMAL;DEFAULT;VERBOSE;DEBUG;ALL")

# Apply logging profile settings
if(RAC_LOG_PROFILE STREQUAL "MINIMAL")
    message(STATUS "Logging Profile: MINIMAL (only ERROR logs)")
    set(RAC_CORE_LOG_ERROR_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_ERROR_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_CORE_LOG_CONFIG_ENABLE OFF CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_CONFIG_ENABLE OFF CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_TX_ENABLE OFF CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_RX_ENABLE OFF CACHE BOOL "" FORCE)
elseif(RAC_LOG_PROFILE STREQUAL "VERBOSE")
    message(STATUS "Logging Profile: VERBOSE (ERROR, CONFIG, TX, RX, INFO)")
    set(RAC_CORE_LOG_INFO_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_INFO_ENABLE ON CACHE BOOL "" FORCE)
elseif(RAC_LOG_PROFILE STREQUAL "DEBUG")
    message(STATUS "Logging Profile: DEBUG (ERROR, CONFIG, TX, RX, INFO, WARN, DEBUG)")
    set(RAC_CORE_LOG_INFO_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_CORE_LOG_WARN_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_CORE_LOG_DEBUG_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_INFO_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_WARN_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_DEBUG_ENABLE ON CACHE BOOL "" FORCE)
elseif(RAC_LOG_PROFILE STREQUAL "ALL")
    message(STATUS "Logging Profile: ALL (enable all log types)")
    set(RAC_CORE_LOG_INFO_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_CORE_LOG_WARN_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_CORE_LOG_DEBUG_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_CORE_LOG_API_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_CORE_LOG_RADIO_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_INFO_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_WARN_ENABLE ON CACHE BOOL "" FORCE)
    set(RAC_LORA_LOG_DEBUG_ENABLE ON CACHE BOOL "" FORCE)
else()
    message(STATUS "Logging Profile: DEFAULT (ERROR, CONFIG, TX, RX)")
endif()

################################################################################
# First build the HAL that might set useful variables

message(STATUS "SMTC_HAL_DIR: ${SMTC_HAL_DIR}")
message(STATUS "CMAKE_CURRENT_SOURCE_DIR:${CMAKE_CURRENT_SOURCE_DIR}")

# Set target radio
set(sx126x_radios sx1261 sx1262 sx1268)
set(lr11xx_radios lr1110 lr1120 lr1121)
set(lr20xx_radios lr2021 lr2022)
set(udp_pf_radios udp_pf)
set(known_radios ${sx126x_radios} ${lr11xx_radios} ${lr20xx_radios} ${udp_pf_radios})

set(RAC_RADIO "" CACHE STRING "The radio to target")
set_property(CACHE RAC_RADIO PROPERTY STRINGS ${known_radios})
if(NOT DEFINED RAC_RADIO OR RAC_RADIO STREQUAL "")
    message(FATAL_ERROR "You must select the radio with -DRAC_RADIO=")
endif()
if(NOT RAC_RADIO IN_LIST known_radios)
    message(FATAL_ERROR "Unknown radio ${RAC_RADIO}")
endif()

# Set target radio family
if(RAC_RADIO IN_LIST sx126x_radios)
    set(RADIO_FAMILY sx126x)
elseif(RAC_RADIO IN_LIST lr11xx_radios)
    set(RADIO_FAMILY lr11xx)
elseif(RAC_RADIO IN_LIST lr20xx_radios)
    set(RADIO_FAMILY lr20xx)
elseif(RAC_RADIO IN_LIST udp_pf_radios)
    set(RADIO_FAMILY udp_pf)
endif()

# Set target board
string(TOUPPER ${BOARD} BOARD_UPPER)
string(TOUPPER ${RADIO_FAMILY} RADIO_FAMILY_UPPER)

# Define SMTC_HAL_DIR
if("${BOARD}" STREQUAL "NUCLEO_L073")
    set(SMTC_HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/smtc_hal_l0_LL)
elseif("${BOARD}" STREQUAL "NUCLEO_L476")
    set(SMTC_HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/smtc_hal_l4)
elseif("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64")
    set(SMTC_HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/smtc_hal_linux)
elseif("${BOARD}" STREQUAL "FPB_RA0E2")
    set(SMTC_HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/smtc_hal_ra0e2)
    set(MCU_DRIVERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers)
endif()

message(STATUS "SMTC_HAL_DIR set to: ${SMTC_HAL_DIR}")

message(STATUS "Selected radio ${RAC_RADIO} in family ${RADIO_FAMILY}\n")
if(NOT DEFINED LBM_RADIO)
    set(LBM_RADIO ${RAC_RADIO})
endif()
if(NOT LBM_RADIO STREQUAL ${RAC_RADIO})
    message(FATAL_ERROR "LBM_RADIO (${LBM_RADIO}) must match RAC_RADIO (${RAC_RADIO})")
endif()

add_subdirectory(${SMTC_HAL_DIR})
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib smtc_rac_lib)

# Apply logging configuration to smtc_rac target
if(TARGET smtc_rac)
    # Add LINUX_PLATFORM definition for ral.h when building for Linux
    if("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64")
        if(TARGET smtc_ral_${RADIO_FAMILY})
            target_compile_definitions(smtc_ral_${RADIO_FAMILY} PUBLIC LINUX_PLATFORM)
        endif()
    endif()

    # Apply Radio Planner options
    target_compile_definitions(smtc_rac PRIVATE RP_MARGIN_DELAY=${RP_MARGIN_DELAY})

    # Apply RAC logging options
    if(RAC_CORE_LOG_ERROR_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_CORE_LOG_ERROR_ENABLE=1)
    endif()
    if(RAC_CORE_LOG_CONFIG_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_CORE_LOG_CONFIG_ENABLE=1)
    endif()
    if(RAC_CORE_LOG_INFO_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_CORE_LOG_INFO_ENABLE=0)
    endif()
    if(RAC_CORE_LOG_WARN_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_CORE_LOG_WARN_ENABLE=0)
    endif()
    if(RAC_CORE_LOG_DEBUG_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_CORE_LOG_DEBUG_ENABLE=0)
    endif()
    if(RAC_CORE_LOG_API_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_CORE_LOG_API_ENABLE=0)
    endif()
    if(RAC_CORE_LOG_RADIO_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_CORE_LOG_RADIO_ENABLE=0)
    endif()

    # Apply RAC LoRa logging options to the same target (both are in smtc_rac)
    if(RAC_LORA_LOG_TX_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_LORA_LOG_TX_ENABLE=1)
    endif()
    if(RAC_LORA_LOG_RX_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_LORA_LOG_RX_ENABLE=1)
    endif()
    if(RAC_LORA_LOG_CONFIG_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_LORA_LOG_CONFIG_ENABLE=0)
    endif()
    if(RAC_LORA_LOG_ERROR_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_LORA_LOG_ERROR_ENABLE=1)
    endif()
    if(RAC_LORA_LOG_INFO_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_LORA_LOG_INFO_ENABLE=0)
    endif()
    if(RAC_LORA_LOG_WARN_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_LORA_LOG_WARN_ENABLE=0)
    endif()
    if(RAC_LORA_LOG_DEBUG_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_LORA_LOG_DEBUG_ENABLE=0)
    endif()

    # Apply RAC FLRC Burst debug option
    if(RAC_FLRC_BURST_DEBUG_ENABLE)
        target_compile_definitions(smtc_rac PRIVATE RAC_FLRC_BURST_DEBUG_ENABLE=1)
        message(STATUS "  - FLRC Burst debug: ENABLED")
    endif()

    message(STATUS "Applied logging configuration to smtc_rac target")
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/smtc_modem_hal)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/radio_hal)
if(${RAC_RADIO} MATCHES "lr1110")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC LR1110)
endif()
if(${RAC_RADIO} MATCHES "lr1120")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC LR1120)
endif()
if(${RAC_RADIO} MATCHES "lr1121")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC LR1121)
endif()
if(${RAC_RADIO} MATCHES "^(lr1110|lr1120|lr1121)$")
    target_compile_definitions(smtc_hal PUBLIC LR11XX_TRANSCEIVER)
    target_compile_definitions(radio_hal PUBLIC LR11XX_TRANSCEIVER)
    # TODO: remove following one once it's removed from smtc_modem_api.h
    target_compile_definitions(smtc_modem_hal_implem PUBLIC LR11XX_TRANSCEIVER)
    if(USE_LR11XX_CRC_SPI)
        target_compile_definitions(radio_hal PRIVATE USE_LR11XX_CRC_OVER_SPI=1)
    endif()
endif()
if(${RAC_RADIO} MATCHES "lr2021")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC LR2021)
    target_compile_definitions(smtc_hal PUBLIC LR2021)
    target_compile_definitions(radio_hal PUBLIC LR2021)
endif()
if(${RAC_RADIO} MATCHES "lr2022")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC LR2022)
    target_compile_definitions(smtc_hal PUBLIC LR2022)
    target_compile_definitions(radio_hal PUBLIC LR2022)
endif()
if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC LR20XX)
    target_compile_definitions(smtc_hal PUBLIC LR20XX)
    target_compile_definitions(radio_hal PUBLIC LR20XX)
    if(LEGACY_EVK_LR20XX)
        target_compile_definitions(radio_hal PRIVATE LEGACY_EVK_LR20XX)
        target_compile_definitions(smtc_modem_hal_implem PRIVATE LEGACY_EVK_LR20XX)
        target_compile_definitions(smtc_hal PRIVATE LEGACY_EVK_LR20XX)
    endif()
endif()
if(${RAC_RADIO} MATCHES "sx1261")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC SX1261)
    target_compile_definitions(smtc_hal PUBLIC SX1261)
    target_compile_definitions(radio_hal PUBLIC SX1261)
endif()
if(${RAC_RADIO} MATCHES "sx1262")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC SX1262)
    target_compile_definitions(smtc_hal PUBLIC SX1262)
    target_compile_definitions(radio_hal PUBLIC SX1262)
endif()
if(${RAC_RADIO} MATCHES "sx1268")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC SX1268)
    target_compile_definitions(smtc_hal PUBLIC SX1268)
    target_compile_definitions(radio_hal PUBLIC SX1268)
endif()
if(${RAC_RADIO} MATCHES "^(sx1261|sx1262|sx1268)$")
    target_compile_definitions(smtc_modem_hal_implem PUBLIC SX126X)
    target_compile_definitions(smtc_hal PUBLIC SX126X)
    target_compile_definitions(radio_hal PUBLIC SX126X)
endif()

if(LBM_GEOLOCATION)
    target_compile_definitions(smtc_modem_hal_implem PUBLIC ADD_APP_GEOLOCATION)
endif()

if(LBM_RELAY_TX)
    target_compile_definitions(smtc_modem_hal_implem PUBLIC USE_RELAY_TX)
endif()

if(LBM_RELAY_RX)
    target_compile_definitions(smtc_modem_hal_implem PUBLIC USE_RELAY_RX)
endif()

if(LBM_FUOTA)
    target_compile_definitions(smtc_hal PUBLIC USE_FUOTA USE_FLASH_READ_MODIFY_WRITE)
    target_compile_definitions(smtc_modem_hal_implem PUBLIC USE_FUOTA USE_FLASH_READ_MODIFY_WRITE)
endif()

if(LBM_STORE_AND_FORWARD)
    target_compile_definitions(smtc_modem_hal_implem PUBLIC ADD_SMTC_STORE_AND_FORWARD)
endif()

if(APP_DEBUG)
    target_compile_definitions(smtc_hal PRIVATE HW_DEBUG_PROBE=1)
endif()

################################################################################
# Original example target (keep for compatibility)

add_executable(example main.c)

target_sources(example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/main_lctt_certif.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/main_periodical_uplink.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/multiprotocol_example/main_multiprotocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/main_hw_modem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/hw_modem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/cmd_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/git_version.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/pb_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/pb_decode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/pb_encode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/smtc_rac_context.pb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/helpers/rac_context_converter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo/main_ranging_demo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo/app_ranging_hopping.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ping_pong_example/main_ping_pong.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ping_pong_example/app_ping_pong.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ping_pong_example/app_periodic_uplink.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_example/main_per.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_example/app_per.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example/main_per_flrc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example/app_per_flrc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example/main_per_fsk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example/app_per_fsk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/lrfhss_example/main_lrfhss_example.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/lrfhss_example/app_lrfhss_example.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/spectral_scan_example/main_spectral_scan.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/spectral_scan_example/app_spectral_scan.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/main_rf_certification.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_rf_certification.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_notification.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/main_flrc_burst.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_rx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_tx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/tx_cw_example/main_tx_cw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/tx_cw_example/app_tx_cw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/cad_example/main_cad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/cad_example/app_cad.c
)

  if(LBM_GEOLOCATION)
    target_sources(example PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/full_almanac_update/main_full_almanac_update.c
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/full_almanac_update/app_full_almanac_update.c
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/wifi_region_detection/main_wifi_region_detection.c
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/wifi_region_detection/wifi_region_finder.c
    )
  endif()

  if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
    target_sources(example PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/direct_driver_access_example/main_direct_driver_access.c
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/direct_driver_access_example/app_direct_driver_access.c
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/immediate_radio_access_example/main_immediate_radio_access.c
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/immediate_radio_access_example/immediate_radio_access.c
    )
endif()

target_include_directories(example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples
    ${CMAKE_CURRENT_SOURCE_DIR}/examples
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/nanopb
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/spectral_scan_example
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/full_almanac_update
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/wifi_region_detection
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/lbm_lib/smtc_modem_core/geolocation_services
    ${CMAKE_CURRENT_SOURCE_DIR}/smtc_sw_platform
    ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_rac_api
    ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_modem_hal
    ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/radio_planner/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_ral/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_ralf/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/radio_drivers
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../protocols/lbm_lib lbm_lib)
target_compile_definitions(lora_basics_modem PRIVATE USE_SMTC_RAC)
target_link_libraries(example PUBLIC
    smtc_modem_hal_implem
    smtc_rac
    LoRaBasicsModemApi
    lora_basics_modem
    smtc_hal
    radio_hal
)

# CAD configuration (used by both 'example' and 'cad' targets)
set(TYPES_OF_CAD "CAD_ONLY" "CAD_RX" "CAD_LBT")
set(TYPE_OF_CAD "CAD_LBT" CACHE STRING "Action after CAD is finished (${TYPES_OF_CAD})")
if(NOT TYPE_OF_CAD IN_LIST TYPES_OF_CAD)
    message(FATAL_ERROR "Invalid TYPE_OF_CAD: ${TYPE_OF_CAD} (possible values: ${TYPES_OF_CAD})")
endif()

# Compile options for original target
if(DEFINED APP)
    string(TOUPPER ${APP} APP_UPPER)
    target_compile_definitions(example PRIVATE
        MAKEFILE_APP=${APP_UPPER}
        RP_MARGIN_DELAY=${RP_MARGIN_DELAY}
    )

    if(${RAC_RADIO} MATCHES "lr2021")
        target_compile_definitions(example PUBLIC LR2021)
    endif()
    if(${RAC_RADIO} MATCHES "lr2022")
        target_compile_definitions(example PUBLIC LR2022)
    endif()
    if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
        target_compile_definitions(example PUBLIC LR20XX)
        if(LEGACY_EVK_LR20XX)
            target_compile_definitions(example PRIVATE LEGACY_EVK_LR20XX)
        endif()
    endif()
    if(${RAC_RADIO} MATCHES "sx1261")
        target_compile_definitions(example PUBLIC SX1261)
    endif()
    if(${RAC_RADIO} MATCHES "sx1262")
        target_compile_definitions(example PUBLIC SX1262)
    endif()
    if(${RAC_RADIO} MATCHES "sx1268")
        target_compile_definitions(example PUBLIC SX1268)
    endif()
    if(${RAC_RADIO} MATCHES "^(sx1261|sx1262|sx1268)$")
        target_compile_definitions(example PUBLIC SX126X)
    endif()

    set(APPS_WITH_OPTIONAL_PERIODIC_UPLINK RTTOF PING_PONG)
    if(APP_UPPER IN_LIST APPS_WITH_OPTIONAL_PERIODIC_UPLINK)
        option(PERIODIC_UPLINK "Enable the periodic uplink" ON)
        if(PERIODIC_UPLINK)
            target_compile_definitions(example PRIVATE ENABLE_PERIODIC_UPLINK)
        endif()
    endif()

    set(RANGING_DEVICE_MODE "" CACHE STRING "RTTOF mode of the device (MANAGER or SUBORDINATE)")
    if(NOT RANGING_DEVICE_MODE)
        if(APP_UPPER MATCHES "RTTOF")
            message(WARNING "RANGING_DEVICE_MODE not defined, using default value")
        endif()
        target_compile_definitions(example PRIVATE RANGING_DEVICE_MODE=RANGING_DEVICE_MODE_MANAGER)
    else()
        if(NOT APP_UPPER MATCHES "RTTOF")
            message(WARNING "unused variable: RANGING_DEVICE_MODE")
        endif()
        target_compile_definitions(example PRIVATE RANGING_DEVICE_MODE=RANGING_DEVICE_MODE_${RANGING_DEVICE_MODE})
    endif()

    set(ROLE "" CACHE STRING "Role of the device (TRANSMITTER or RECEIVER)")
    if(NOT ROLE)
        if(APP_UPPER MATCHES "PER")
            message(WARNING "ROLE not defined, using default value")
        endif()
        target_compile_definitions(example PRIVATE ROLE=TRANSMITTER)
    else()
        if(NOT APP_UPPER MATCHES "PER")
            message(WARNING "unused variable: ROLE")
        endif()
        target_compile_definitions(example PRIVATE ROLE=${ROLE})
    endif()
    # Add HAL def for hw_modem
    if(APP_UPPER MATCHES "HW_MODEM")
        target_compile_definitions(smtc_hal PUBLIC HW_MODEM_ENABLED)
    endif()

    # Add TYPE_OF_CAD definition for CAD application
    if(APP_UPPER MATCHES "CAD")
        target_compile_definitions(example PRIVATE TYPE_OF_CAD=RAL_LORA_${TYPE_OF_CAD})
    endif()

    if(APP_UPPER MATCHES "GEOLOCATION")
        target_compile_definitions(radio_hal PUBLIC ENABLE_MODEM_GNSS_FEATURE)
        target_compile_definitions(smtc_hal PUBLIC ENABLE_MODEM_GNSS_FEATURE)
        target_sources(example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/main_geolocation.c)
    endif()

    target_sources(example PRIVATE
        main.c
        $<TARGET_OBJECTS:smtc_hal>
    )

    add_custom_command(
        TARGET example POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:example>" "$<TARGET_FILE:example>.hex"
        COMMENT "Creating hex..."
    )

    add_custom_command(
        TARGET example POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary -S "$<TARGET_FILE:example>" "$<TARGET_FILE:example>.bin"
        COMMENT "Creating bin..."
    )

    add_custom_target(flash
        DEPENDS example
        COMMAND st-flash --connect-under-reset write $<TARGET_FILE:example>.bin 0x8000000
        USES_TERMINAL
    )

    add_custom_target(flash_copy
        DEPENDS example
        COMMAND cp $<TARGET_FILE:example>.bin /media/$ENV{USER}/NODE_L476RG/.
        USES_TERMINAL
    )
endif()

################################################################################
# Function to create example targets

function(add_example TARGET_NAME APP_DEFINE EXAMPLE_SOURCES)
    # Create executable
    add_executable(${TARGET_NAME} ${EXAMPLE_SOURCES})

    # Set compile definitions
    string(TOUPPER ${APP_DEFINE} APP_UPPER)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        MAKEFILE_APP=${APP_UPPER}
        RP_MARGIN_DELAY=${RP_MARGIN_DELAY}
    )
    if(${RAC_RADIO} MATCHES "lr2021")
        target_compile_definitions(${TARGET_NAME} PUBLIC LR2021)
    endif()
    if(${RAC_RADIO} MATCHES "lr2022")
        target_compile_definitions(${TARGET_NAME} PUBLIC LR2022)
    endif()
    if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
        target_compile_definitions(${TARGET_NAME} PUBLIC LR20XX)
        if(LEGACY_EVK_LR20XX)
            target_compile_definitions(${TARGET_NAME} PRIVATE LEGACY_EVK_LR20XX)
        endif()
    endif()
    if(${RAC_RADIO} MATCHES "sx1261")
        target_compile_definitions(${TARGET_NAME} PUBLIC SX1261)
    endif()
    if(${RAC_RADIO} MATCHES "sx1262")
        target_compile_definitions(${TARGET_NAME} PUBLIC SX1262)
    endif()
    if(${RAC_RADIO} MATCHES "sx1268")
        target_compile_definitions(${TARGET_NAME} PUBLIC SX1268)
    endif()
    if(${RAC_RADIO} MATCHES "^(sx1261|sx1262|sx1268)$")
        target_compile_definitions(${TARGET_NAME} PUBLIC SX126X)
    endif()

    # For FPB_RA0E2: Add warmstart hook directly (ensures it overrides weak default)
    if("${BOARD}" STREQUAL "FPB_RA0E2")
        target_sources(${TARGET_NAME} PRIVATE
            ${SMTC_HAL_DIR}/hal_warmstart.c
        )
    endif()

    # Add common sources
    target_sources(${TARGET_NAME} PRIVATE
        main.c
        $<TARGET_OBJECTS:smtc_hal>
    )

    # Add includes
    target_include_directories(${TARGET_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/main_examples
        ${CMAKE_CURRENT_SOURCE_DIR}/examples
        ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo
        ${CMAKE_CURRENT_SOURCE_DIR}/smtc_sw_platform
        ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_rac_api
        ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_modem_hal
        ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/radio_planner/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_ral/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/smtc_ralf/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../smtc_rac_lib/radio_drivers
        ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/network_discovery/network_discovery_api
        ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/network_discovery/network_discovery_core
    )

    # For FPB_RA0E2: Add FSP include directories
    if("${BOARD}" STREQUAL "FPB_RA0E2")
        target_include_directories(${TARGET_NAME} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/cmsis/Core/Include
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_ra0e2/ra_gen
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_ra0e2/ra_cfg/fsp_cfg
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_ra0e2/ra_cfg/fsp_cfg/bsp
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_ra0e2/cmsis/Device/RENESAS/Include
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_ra0e2/bsp/mcu/ra0e2
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_fsp_common/bsp/mcu/all
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_fsp_common/fsp/inc
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_fsp_common/fsp/inc/api
            ${CMAKE_CURRENT_SOURCE_DIR}/mcu_drivers/renesas_fsp_common/fsp/inc/instances
        )

        # Suppress pedantic warnings for FSP vendor code
        target_compile_options(${TARGET_NAME} PRIVATE -Wno-pedantic)
    endif()

    # Link libraries
    target_link_libraries(${TARGET_NAME} PUBLIC
        smtc_modem_hal_implem
        smtc_rac
        LoRaBasicsModemApi
        lora_basics_modem
        smtc_hal
        radio_hal
    )

    # Generate hex file
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:${TARGET_NAME}>" "${CMAKE_BINARY_DIR}/${TARGET_NAME}.hex"
        COMMENT "Creating ${TARGET_NAME}.hex..."
    )

    # Generate bin file
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary -S "$<TARGET_FILE:${TARGET_NAME}>" "${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin"
        COMMENT "Creating ${TARGET_NAME}.bin..."
    )

    # Add flash target
    add_custom_target(flash_${TARGET_NAME}
        DEPENDS ${TARGET_NAME}
        COMMAND st-flash --connect-under-reset write "${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin" 0x8000000
        USES_TERMINAL
    )
endfunction()

################################################################################
# Define all examples

if(${RAC_RADIO} MATCHES "^(lr1120|lr1110)$")
    add_example(geolocation "GEOLOCATION" "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/main_geolocation.c")
    target_include_directories(geolocation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example
        ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board
    )
endif()

# HW_modem example
string(CONCAT HW_MODEM_SOURCES_STR
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/hw_modem.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/git_version.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/cmd_parser.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/pb_common.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/pb_decode.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/pb_encode.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated/smtc_rac_context.pb.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/helpers/rac_context_converter.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_tx.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_rx.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_utils.c\;"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/main_hw_modem.c")

add_example(hw_modem "HW_MODEM" ${HW_MODEM_SOURCES_STR})

if(LBM_CRYPTO MATCHES "^LR11XX")
    target_compile_definitions(hw_modem PRIVATE USE_LR11XX_CE)
endif()

target_include_directories(hw_modem PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/lbm_lib/smtc_modem_core/geolocation_services
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/nanopb
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/hw_modem/serialization/helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example
)

# RTToF (Ranging) Examples
add_example(rttof_manager "RTTOF"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo/main_ranging_demo.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo/app_ranging_hopping.c"
)
target_compile_definitions(rttof_manager PRIVATE
    RANGING_DEVICE_MODE=RANGING_DEVICE_MODE_MANAGER
    ENABLE_PERIODIC_UPLINK
)

add_example(rttof_subordinate "RTTOF"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo/main_ranging_demo.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo/app_ranging_hopping.c"
)
target_compile_definitions(rttof_subordinate PRIVATE
    RANGING_DEVICE_MODE=RANGING_DEVICE_MODE_SUBORDINATE
    ENABLE_PERIODIC_UPLINK
)

# Ping Pong Example
add_example(ping_pong "PING_PONG"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ping_pong_example/main_ping_pong.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ping_pong_example/app_ping_pong.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ping_pong_example/app_periodic_uplink.c"
)
target_compile_definitions(ping_pong PRIVATE ENABLE_PERIODIC_UPLINK)

# PER (Packet Error Rate) Examples
add_example(per_tx "PER"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_example/main_per.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_example/app_per.c"
)
target_compile_definitions(per_tx PRIVATE ROLE=2) # TRANSMITTER

add_example(per_rx "PER"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_example/main_per.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_example/app_per.c"
)
target_compile_definitions(per_rx PRIVATE ROLE=1) # RECEIVER

# FSK PER (Packet Error Rate) Examples
add_example(per_fsk_tx "PER_FSK"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example/main_per_fsk.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example/app_per_fsk.c"
)
target_compile_definitions(per_fsk_tx PRIVATE ROLE=2) # TRANSMITTER
target_include_directories(per_fsk_tx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example
)

add_example(per_fsk_rx "PER_FSK"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example/main_per_fsk.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example/app_per_fsk.c"
)
target_compile_definitions(per_fsk_rx PRIVATE ROLE=1) # RECEIVER
target_include_directories(per_fsk_rx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_fsk_example
)

# FLRC PER (Packet Error Rate) Examples
add_example(per_flrc_tx "PER_FLRC"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example/main_per_flrc.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example/app_per_flrc.c"
)
target_compile_definitions(per_flrc_tx PRIVATE ROLE=2) # TRANSMITTER
target_include_directories(per_flrc_tx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example
)

add_example(per_flrc_rx "PER_FLRC"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example/main_per_flrc.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example/app_per_flrc.c"
)
target_compile_definitions(per_flrc_rx PRIVATE ROLE=1) # RECEIVER
target_include_directories(per_flrc_rx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/packet_error_rate_flrc_example
)

# LR-FHSS Example
add_example(lrfhss_tx "LR_FHSS"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/lrfhss_example/main_lrfhss_example.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/lrfhss_example/app_lrfhss_example.c"
)
target_include_directories(lrfhss_tx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/lrfhss_example
)

# Generate lrfhss.bin file (alias for lrfhss_tx.bin)
add_custom_command(
    TARGET lrfhss_tx POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/lrfhss_tx.bin" "${CMAKE_BINARY_DIR}/lrfhss.bin"
    COMMENT "Creating lrfhss.bin..."
)

# Spectral Scan Example
add_example(spectral_scan "SPECTRAL_SCAN"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/spectral_scan_example/main_spectral_scan.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/spectral_scan_example/app_spectral_scan.c"
)
target_include_directories(spectral_scan PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/spectral_scan_example
)

# RF Certification Examples (one for each region)
set(NOTIFICATION_MODE "NOTIFICATIONS_OFF" CACHE STRING "Notification mode (ON or OFF)")
set(APP_MODE "APP_MODE_CERTIFICATION" CACHE STRING "App mode (CERTIFICATION or NOTIFICATION)")

# ETSI Region
add_example(rf_certification_etsi "RF_CERTIFICATION"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/main_rf_certification.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_rf_certification.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_notification.c"
)
target_include_directories(rf_certification_etsi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example
)
target_compile_definitions(rf_certification_etsi PRIVATE RF_CERT_REGION=RF_CERT_REGION_ETSI)
target_compile_definitions(rf_certification_etsi PRIVATE NOTIFICATION_MODE=${NOTIFICATION_MODE})
target_compile_definitions(rf_certification_etsi PRIVATE APP_MODE=${APP_MODE})

# ARIB Region
add_example(rf_certification_arib "RF_CERTIFICATION"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/main_rf_certification.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_rf_certification.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_notification.c"
)
target_include_directories(rf_certification_arib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example
)
target_compile_definitions(rf_certification_arib PRIVATE RF_CERT_REGION=RF_CERT_REGION_ARIB)
target_compile_definitions(rf_certification_arib PRIVATE NOTIFICATION_MODE=${NOTIFICATION_MODE})
target_compile_definitions(rf_certification_arib PRIVATE APP_MODE=${APP_MODE})

# FCC Region
add_example(rf_certification_fcc "RF_CERTIFICATION"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/main_rf_certification.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_rf_certification.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example/app_notification.c"
)
target_include_directories(rf_certification_fcc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/rf_certification_example
)
target_compile_definitions(rf_certification_fcc PRIVATE RF_CERT_REGION=RF_CERT_REGION_FCC)
target_compile_definitions(rf_certification_fcc PRIVATE NOTIFICATION_MODE=${NOTIFICATION_MODE})
target_compile_definitions(rf_certification_fcc PRIVATE APP_MODE=${APP_MODE})

# Periodical Uplink Example
add_example(periodical_uplink "PERIODICAL_UPLINK"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/main_periodical_uplink.c"
)
target_compile_definitions(periodical_uplink PRIVATE ENABLE_PERIODIC_UPLINK)

# Multiprotocol Example
add_example(multiprotocol "MULTIPROTOCOL"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/multiprotocol_example/main_multiprotocol.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo/app_ranging_hopping.c"
)
target_include_directories(multiprotocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/ranging_demo
)


# FLRC Burst Example
add_example(flrc_burst_tx "FLRC_BURST_EXAMPLE"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/main_flrc_burst.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_tx.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_utils.c"
)
target_compile_definitions(flrc_burst_tx PRIVATE ROLE=2) # TRANSMITTER
target_include_directories(flrc_burst_tx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example
)

add_example(flrc_burst_rx "FLRC_BURST_EXAMPLE"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/main_flrc_burst.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_rx.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example/flrc_burst_utils.c"
)
target_compile_definitions(flrc_burst_rx PRIVATE ROLE=1) # RECEIVER
target_include_directories(flrc_burst_rx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/flrc_burst_example
)

# Hardware Modem Test Example

# LCTT Certification Example
add_example(lctt_certif "LCTT_CERTIF"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/main_lctt_certif.c"
)

# TX CW (Continuous Wave) Example
add_example(tx_cw "TX_CW"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/tx_cw_example/main_tx_cw.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/tx_cw_example/app_tx_cw.c"
)
target_include_directories(tx_cw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/tx_cw_example
)
option(INFINITE_PREAMBLE "Enable INFINITE_PREAMBLE" OFF)

if(INFINITE_PREAMBLE)
    target_compile_definitions(tx_cw PRIVATE INFINITE_PREAMBLE)
    message(STATUS "tx_cw sample - INFINITE PREAMBLE ENABLED")
else()
    message(STATUS "tx_cw sample - CONTINUOUS WAVE ENABLED")
endif()

# Direct Driver Access Example
if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
add_example(direct_driver_access "DIRECT_DRIVER_ACCESS"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/direct_driver_access_example/main_direct_driver_access.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/direct_driver_access_example/app_direct_driver_access.c"
)
target_include_directories(direct_driver_access PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/direct_driver_access_example
)

# Immediate Radio Access Example
add_example(immediate_radio_access "IMMEDIATE_RADIO_ACCESS"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/immediate_radio_access_example/main_immediate_radio_access.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/immediate_radio_access_example/immediate_radio_access.c"
)
target_include_directories(immediate_radio_access PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/immediate_radio_access_example
)
endif()

# Porting Tests
add_example(porting_tests "PORTING_TESTS"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/main_porting_tests.c"
)

# CAD (Channel Activity Detection) Example
if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
    add_example(cad "CAD"
        "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/cad_example/main_cad.c;${CMAKE_CURRENT_SOURCE_DIR}/main_examples/cad_example/app_cad.c"
    )
    target_include_directories(cad PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/cad_example
    )
    target_compile_definitions(cad PRIVATE TYPE_OF_CAD=RAL_LORA_${TYPE_OF_CAD})
endif()

# Add LR11xx flasher and full_almanac_update
if(EXISTS ${EXAMPLE_CMAKE_FILE})
    unset(CUSTOM_EXAMPLE_CMAKE_LOAD_OPTIONS)
    message(STATUS "Loading example configuration: ${EXAMPLE_CMAKE_FILE}")
    include(${EXAMPLE_CMAKE_FILE})
else()
  unset(CUSTOM_EXAMPLE_CMAKE_LOAD_OPTIONS)
  include("${CMAKE_CURRENT_SOURCE_DIR}/main_examples/full_almanac_update/CMakeLists.txt")
  include("${CMAKE_CURRENT_SOURCE_DIR}/main_examples/wifi_region_detection/CMakeLists.txt")
endif()

if(LBM_GEOLOCATION)
    if(APP_UPPER STREQUAL HW_MODEM)
      target_sources(hw_modem PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board/lr1110_board_mb1lxks.c"
      )
    endif()
      if(APP_UPPER STREQUAL GEOLOCATION)
      target_sources(geolocation PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board/lr1110_board_mb1lxks.c"
      )
    endif()
    target_sources(example PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board/lr1110_board_mb1lxks.c"
    )
endif()

################################################################################
# Radio Test CLI — standalone binary (no RAC/RAL/LBM dependencies)
# Build on Linux platforms or NUCLEO_L476 with a real radio
if(("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64" OR "${BOARD}" STREQUAL "NUCLEO_L476" OR "${BOARD}" STREQUAL "NUCLEO_L073" OR "${BOARD}" STREQUAL "FPB_RA0E2") AND NOT "${RAC_RADIO}" STREQUAL "udp_pf")
    set(RADIO_TEST_CLI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/radio_test_cli)

    # Stack region code paths (compiled directly, NOT a library)
    set(SDK_ROOT_DIR ${CMAKE_SOURCE_DIR}/..)
    set(STACK_REAL_DIR ${SDK_ROOT_DIR}/protocols/lbm_lib/smtc_modem_core/lr1mac/src/smtc_real/src)
    set(STACK_LR1MAC_DIR ${SDK_ROOT_DIR}/protocols/lbm_lib/smtc_modem_core/lr1mac/src)
    set(STACK_SERVICES_DIR ${SDK_ROOT_DIR}/protocols/lbm_lib/smtc_modem_core/lr1mac/src/services)

    add_executable(radio_test_cli
        ${RADIO_TEST_CLI_DIR}/main_radio_test.c
        ${RADIO_TEST_CLI_DIR}/cli_parser.c
        ${RADIO_TEST_CLI_DIR}/cli_state.c
        ${RADIO_TEST_CLI_DIR}/region.c
        ${RADIO_TEST_CLI_DIR}/mod_lora.c
        ${RADIO_TEST_CLI_DIR}/test_modes.c
        ${RADIO_TEST_CLI_DIR}/per_counter.c
        ${RADIO_TEST_CLI_DIR}/linenoise.c
        # Stack region adapter + HAL shim + LBT
        ${RADIO_TEST_CLI_DIR}/stack_region.c
        ${RADIO_TEST_CLI_DIR}/stack_hal_shim.c
        ${RADIO_TEST_CLI_DIR}/stack_lbt.c
        # Actual stack sources (unmodified)
        ${STACK_REAL_DIR}/region_us_915.c
        ${STACK_REAL_DIR}/region_eu_868.c
        ${STACK_REAL_DIR}/region_as_923.c
        ${STACK_SERVICES_DIR}/smtc_duty_cycle.c
        ${STACK_LR1MAC_DIR}/lr1mac_utilities.c
        $<TARGET_OBJECTS:smtc_hal>
    )

    # Chip-specific backend + HAL glue
    if(RADIO_FAMILY STREQUAL "lr20xx")
        if("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64")
            set(LR20XX_HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/lr20xx_hal_linux.c)
        else()
            set(LR20XX_HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/lr20xx_hal.c)
        endif()
        target_sources(radio_test_cli PRIVATE
            ${RADIO_TEST_CLI_DIR}/chip_lr20xx.c
            ${RADIO_TEST_CLI_DIR}/mod_flrc.c
            ${LR20XX_HAL_SRC}
            ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/ral_lr20xx_bsp.c
            ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/radio_utilities.c
        )
        target_link_libraries(radio_test_cli PRIVATE
            lr20xx_driver
            lr20xx_driver_lora
            lr20xx_driver_flrc
        )
    elseif(RADIO_FAMILY STREQUAL "lr11xx")
        if("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64")
            set(LR11XX_HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/lr11xx_hal_linux.c)
        else()
            set(LR11XX_HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/lr11xx_hal.c)
        endif()
        target_sources(radio_test_cli PRIVATE
            ${RADIO_TEST_CLI_DIR}/chip_lr11xx.c
            ${LR11XX_HAL_SRC}
            ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/ral_lr11xx_bsp.c
            ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/radio_utilities.c
        )
        target_link_libraries(radio_test_cli PRIVATE
            lr11xx_driver
        )
    elseif(RADIO_FAMILY STREQUAL "sx126x")
        if("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64")
            set(SX126X_HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/sx126x_hal_linux.c)
        else()
            set(SX126X_HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/sx126x_hal.c)
        endif()
        target_sources(radio_test_cli PRIVATE
            ${RADIO_TEST_CLI_DIR}/chip_sx126x.c
            ${SX126X_HAL_SRC}
            ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/ral_sx126x_bsp.c
            ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal/radio_utilities.c
        )
        target_link_libraries(radio_test_cli PRIVATE
            sx126x_driver
        )
    endif()

    # Chip and family defines
    if(${RAC_RADIO} MATCHES "lr2021")
        target_compile_definitions(radio_test_cli PRIVATE LR2021)
    endif()
    if(${RAC_RADIO} MATCHES "lr2022")
        target_compile_definitions(radio_test_cli PRIVATE LR2022)
    endif()
    if(${RAC_RADIO} MATCHES "^(lr2021|lr2022)$")
        target_compile_definitions(radio_test_cli PRIVATE LR20XX)
        if(LEGACY_EVK_LR20XX)
            target_compile_definitions(radio_test_cli PRIVATE LEGACY_EVK_LR20XX)
        endif()
    endif()
    if(${RAC_RADIO} MATCHES "sx1261")
        target_compile_definitions(radio_test_cli PRIVATE SX1261)
    endif()
    if(${RAC_RADIO} MATCHES "sx1262")
        target_compile_definitions(radio_test_cli PRIVATE SX1262)
    endif()
    if(${RAC_RADIO} MATCHES "sx1268")
        target_compile_definitions(radio_test_cli PRIVATE SX1268)
    endif()
    if(${RAC_RADIO} MATCHES "^(sx1261|sx1262|sx1268)$")
        target_compile_definitions(radio_test_cli PRIVATE SX126X)
    endif()
    if(${RAC_RADIO} MATCHES "lr1110")
        target_compile_definitions(radio_test_cli PRIVATE LR1110)
    endif()
    if(${RAC_RADIO} MATCHES "lr1120")
        target_compile_definitions(radio_test_cli PRIVATE LR1120)
    endif()
    if(${RAC_RADIO} MATCHES "lr1121")
        target_compile_definitions(radio_test_cli PRIVATE LR1121)
    endif()
    if(${RAC_RADIO} MATCHES "^(lr1110|lr1120|lr1121)$")
        target_compile_definitions(radio_test_cli PRIVATE LR11XX)
    endif()

    # Stack region compile definitions
    target_compile_definitions(radio_test_cli PRIVATE
        REGION_US_915
        REGION_EU_868
        REGION_AS_923
        RP2_103
        MODEM_HAL_DBG_TRACE=0
    )

    target_include_directories(radio_test_cli PRIVATE
        ${RADIO_TEST_CLI_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/radio_hal
        # Stack region headers
        ${STACK_REAL_DIR}
        ${STACK_LR1MAC_DIR}
        ${STACK_SERVICES_DIR}
        ${SDK_ROOT_DIR}/smtc_rac_lib/smtc_ral/src
        ${SDK_ROOT_DIR}/smtc_rac_lib/smtc_ralf/src
        ${SDK_ROOT_DIR}/smtc_rac_lib/smtc_modem_hal
        ${SDK_ROOT_DIR}/smtc_rac_lib/radio_planner/src
        ${SDK_ROOT_DIR}/protocols/lbm_lib/smtc_modem_core/smtc_modem_crypto/smtc_secure_element
    )

    # Inherit smtc_hal transitive include directories (for smtc_hal_*.h, modem_pinout.h)
    target_link_libraries(radio_test_cli PRIVATE smtc_hal)

    # FPB_RA0E2: warmstart hook applies FSP pin configuration (IOPORT_CFG_IRQ_ENABLE etc.)
    if("${BOARD}" STREQUAL "FPB_RA0E2")
        target_sources(radio_test_cli PRIVATE
            ${SMTC_HAL_DIR}/hal_warmstart.c
        )
    endif()

    # Linux system libraries
    if("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64")
        find_package(Threads REQUIRED)
        set_target_properties(radio_test_cli PROPERTIES LINK_LIBRARIES_ONLY_TARGETS OFF)
        target_link_libraries(radio_test_cli PRIVATE Threads::Threads -lrt)
    else()
        # MCU build: enable float printf support (newlib-nano omits it by default)
        target_link_options(radio_test_cli PRIVATE -u _printf_float)
        # MCU build: generate .hex and .bin
        add_custom_command(
            TARGET radio_test_cli POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O ihex "$<TARGET_FILE:radio_test_cli>" "${CMAKE_BINARY_DIR}/radio_test_cli.hex"
            COMMENT "Creating radio_test_cli.hex...")
        add_custom_command(
            TARGET radio_test_cli POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary -S "$<TARGET_FILE:radio_test_cli>" "${CMAKE_BINARY_DIR}/radio_test_cli.bin"
            COMMENT "Creating radio_test_cli.bin...")
    endif()

    message(STATUS "  - radio_test_cli   : Interactive radio test CLI (with stack region code)")
else()
    if(NOT ("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64" OR "${BOARD}" STREQUAL "NUCLEO_L476" OR "${BOARD}" STREQUAL "NUCLEO_L073" OR "${BOARD}" STREQUAL "FPB_RA0E2"))
        message(STATUS "radio_test_cli: SKIPPED (only LINUX, LINUX_ARM, LINUX_ARM64, NUCLEO_L476, NUCLEO_L073, and FPB_RA0E2 builds are currently supported)")
    else()
        message(STATUS "radio_test_cli: SKIPPED (requires RAC_RADIO != udp_pf)")
    endif()
endif()

################################################################################
# Create "all_examples" target to build everything
set(ALL_EXAMPLES rttof_manager rttof_subordinate ping_pong per_tx per_rx per_fsk_tx per_fsk_rx per_flrc_tx per_flrc_rx lrfhss_tx spectral_scan periodical_uplink lctt_certif multiprotocol rf_certification_etsi rf_certification_arib rf_certification_fcc tx_cw porting_tests)
if(RAC_RADIO IN_LIST lr20xx_radios)
    list(APPEND ALL_EXAMPLES
      direct_driver_access cad immediate_radio_access flrc_burst_tx flrc_burst_rx
  )
endif()
# Add radio_test_cli only when conditions are met
if(("${BOARD}" STREQUAL "LINUX" OR "${BOARD}" STREQUAL "LINUX_ARM" OR "${BOARD}" STREQUAL "LINUX_ARM64" OR "${BOARD}" STREQUAL "NUCLEO_L476" OR "${BOARD}" STREQUAL "NUCLEO_L073" OR "${BOARD}" STREQUAL "FPB_RA0E2") AND NOT "${RAC_RADIO}" STREQUAL "udp_pf")
    list(APPEND ALL_EXAMPLES radio_test_cli)
endif()

add_custom_target(all_examples
    DEPENDS ${ALL_EXAMPLES}
    COMMENT "Building all examples..."
)

################################################################################
# Display information

message(STATUS "")
message(STATUS "Available examples:")
message(STATUS "  - hw_modem         : Hardware modem")
message(STATUS "  - rttof_manager    : ranging (RTToF) manager")
message(STATUS "  - rttof_subordinate: ranging (RTToF) subordinate")
message(STATUS "  - ping_pong        : ping-pong communication example")
message(STATUS "  - per_tx           : packet error rate - LoRa (transmitter)")
message(STATUS "  - per_rx           : packet error rate - LoRa (receiver)")
message(STATUS "  - per_fsk_tx       : packet error rate - FSK (transmitter)")
message(STATUS "  - per_fsk_rx       : packet error rate - FSK (receiver)")
message(STATUS "  - per_flrc_tx      : packet error rate - FLRC (transmitter)")
message(STATUS "  - per_flrc_rx      : packet error rate - FLRC (receiver)")
message(STATUS "  - lrfhss_tx        : LR-FHSS transmission example")
message(STATUS "  - spectral_scan    : spectral scan analysis example")
message(STATUS "  - periodical_uplink      : Periodical uplink transmission example")
message(STATUS "  - multiprotocol          : Multiprotocol example")
message(STATUS "  - porting_tests          : Porting_tests example")
message(STATUS "  - lctt_certif            : LCTT certification example")
message(STATUS "  - geolocation            : geolocation example")
message(STATUS "  - flrc_burst_tx   : FLRC burst exchange (transmitter)")
message(STATUS "  - flrc_burst_rx   : FLRC burst exchange (receiver)")
message(STATUS "  - radio_test_cli         : Interactive radio test CLI (LINUX/LINUX_ARM/LINUX_ARM64 only, no udp_pf)")
message(STATUS "  - full_almanac_update    : full almanac update example")
message(STATUS "  - wifi_region_detection  : WiFi region detection example")
message(STATUS "  - rf_certification_etsi : RF certification continuous LoRa TX example (ETSI region)")
message(STATUS "  - rf_certification_arib : RF certification continuous LoRa TX example (ARIB region)")
message(STATUS "  - rf_certification_fcc  : RF certification continuous LoRa TX example (FCC region)")
message(STATUS "  - tx_cw            : continuous transmission (TX CW) example")
message(STATUS "  - direct_driver_access : direct radio driver access example")
message(STATUS "  - immediate_radio_access : immediate radio access example")
message(STATUS "  - cad              : CAD (Channel Activity Detection) example")
message(STATUS "")
message(STATUS "Build targets:")
message(STATUS "  - ninja -C build all_examples    : Build all examples")
message(STATUS "  - ninja -C build <example_name>  : Build specific example")
message(STATUS "  - ninja -C build flash_<example> : Flash specific example to device")
message(STATUS "")
message(STATUS "Generated files will be in ${CMAKE_BINARY_DIR}/:")
message(STATUS "  - <example_name>.bin")
message(STATUS "  - <example_name>.hex")
message(STATUS "")
message(STATUS "Logging Configuration:")
message(STATUS "  Profile: ${RAC_LOG_PROFILE}")
message(STATUS "  Core - ERROR:${RAC_CORE_LOG_ERROR_ENABLE} CONFIG:${RAC_CORE_LOG_CONFIG_ENABLE} INFO:${RAC_CORE_LOG_INFO_ENABLE} DEBUG:${RAC_CORE_LOG_DEBUG_ENABLE}")
message(STATUS "  LoRa - TX:${RAC_LORA_LOG_TX_ENABLE} RX:${RAC_LORA_LOG_RX_ENABLE} ERROR:${RAC_LORA_LOG_ERROR_ENABLE} CONFIG:${RAC_LORA_LOG_CONFIG_ENABLE}")
message(STATUS "")
message(STATUS "To change logging profile: -DRAC_LOG_PROFILE=<MINIMAL|DEFAULT|VERBOSE|DEBUG|ALL>")
message(STATUS "To enable specific logs: -DRAC_CORE_LOG_INFO_ENABLE=ON -DRAC_LORA_LOG_DEBUG_ENABLE=ON")
message(STATUS "")
