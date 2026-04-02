# Copyright (c) 2025 Semtech Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear

cmake_minimum_required(VERSION 3.13.1)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(radio_test_cli)

set(CLI_DIR ${ZEPHYR_USP_MODULE_DIR}/examples/main_examples/radio_test_cli)
set(HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/smtc_hal_zephyr)

# Stack region code paths (from USP module)
set(USP_DIR ${ZEPHYR_USP_MODULE_DIR})
set(STACK_REAL_DIR ${USP_DIR}/protocols/lbm_lib/smtc_modem_core/lr1mac/src/smtc_real/src)
set(STACK_LR1MAC_DIR ${USP_DIR}/protocols/lbm_lib/smtc_modem_core/lr1mac/src)
set(STACK_SERVICES_DIR ${USP_DIR}/protocols/lbm_lib/smtc_modem_core/lr1mac/src/services)

target_sources(app PRIVATE
    # Shared CLI sources (same .c files as Linux/baremetal)
    ${CLI_DIR}/main_radio_test.c
    ${CLI_DIR}/cli_parser.c
    ${CLI_DIR}/cli_state.c
    ${CLI_DIR}/linenoise.c
    ${CLI_DIR}/region.c
    ${CLI_DIR}/test_modes.c
    ${CLI_DIR}/per_counter.c
    ${CLI_DIR}/mod_lora.c
    ${CLI_DIR}/stack_lbt.c
    ${CLI_DIR}/stack_region.c
    ${CLI_DIR}/platform_zephyr.c
    # Stack region sources (unmodified, from USP module)
    # NOTE: stack_hal_shim.c is NOT compiled — usp_zephyr's smtc_modem_hal.c
    # provides the same functions (smtc_modem_hal_get_time_in_ms, etc.)
    # NOTE: smtc_duty_cycle.c is NOT compiled — usp_zephyr module builds it
    ${STACK_REAL_DIR}/region_us_915.c
    ${STACK_REAL_DIR}/region_eu_868.c
    ${STACK_REAL_DIR}/region_as_923.c
    ${STACK_LR1MAC_DIR}/lr1mac_utilities.c
    # Zephyr HAL bridge (radio context from DT, DIO IRQ bridge)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/zephyr_hal_bridge.c
    # Zephyr HAL implementations
    ${HAL_DIR}/smtc_hal_mcu.c
    ${HAL_DIR}/smtc_hal_gpio.c
    ${HAL_DIR}/smtc_hal_spi.c
    ${HAL_DIR}/smtc_hal_uart.c
    ${HAL_DIR}/smtc_hal_rtc.c
    ${HAL_DIR}/smtc_hal_watchdog.c
    # NOTE: smtc_hal_led.c NOT compiled — usp_zephyr subsys/usp/ provides it
    ${HAL_DIR}/smtc_hal_rng.c
    ${HAL_DIR}/smtc_hal_trace.c
    ${HAL_DIR}/smtc_hal_lp_timer.c
    ${HAL_DIR}/smtc_hal_button.c
    ${HAL_DIR}/smtc_hal_flash.c
)

# Chip-specific sources — auto-detected from device tree via Kconfig.
# The shield's DT compatible (e.g. "semtech,lr2021") sets CONFIG_SEMTECH_LR20XX
# automatically.  BSP files (ral_*_bsp.c, radio_utilities.c) are NOT compiled
# here — usp_zephyr's drivers/usp/ provides DT-based versions.
if(CONFIG_SEMTECH_LR20XX)
    target_sources(app PRIVATE ${CLI_DIR}/chip_lr20xx.c ${CLI_DIR}/mod_flrc.c)
elseif(CONFIG_SEMTECH_LR11XX)
    target_sources(app PRIVATE ${CLI_DIR}/chip_lr11xx.c)
elseif(CONFIG_SEMTECH_SX126X)
    target_sources(app PRIVATE ${CLI_DIR}/chip_sx126x.c)
else()
    message(FATAL_ERROR "No supported radio detected in device tree. "
        "Use a shield that defines a semtech,lr20xx / lr11xx / sx126x node.")
endif()

# IMPORTANT: HAL_DIR must come BEFORE the subsys/usp include of smtc_hal_linux
# (which adds Linux-specific smtc_hal_gpio_pin_names.h).  target_include_directories
# with BEFORE PUBLIC takes precedence over zephyr_include_directories.
target_include_directories(app BEFORE PUBLIC ${HAL_DIR})

zephyr_include_directories(
    ${CLI_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${USP_DIR}/examples/radio_hal
    ${USP_DIR}/examples
    # Stack region headers
    ${STACK_REAL_DIR}
    ${STACK_LR1MAC_DIR}
    ${STACK_SERVICES_DIR}
    ${USP_DIR}/protocols/lbm_lib/smtc_modem_core/smtc_modem_crypto/smtc_secure_element
    ${USP_DIR}/smtc_rac_lib/radio_planner/src
    ${USP_DIR}/smtc_rac_lib/smtc_ral/src
    ${USP_DIR}/smtc_rac_lib/smtc_ralf/src
)

# Compile definitions for stack region code
target_compile_definitions(app PRIVATE
    REGION_US_915
    REGION_EU_868
    REGION_AS_923
    RP2_103
    MODEM_HAL_DBG_TRACE=0
)

# Radio family + chip variant defines — auto-detected from device tree.
# The family define (LR20XX, LR11XX, SX126X) is already set globally by
# usp_zephyr's modules/usp_drivers/*.cmake.  We add chip-specific variant
# defines that radio_test_cli uses for conditional compilation.
if(CONFIG_DT_HAS_SEMTECH_LR2021_ENABLED)
    target_compile_definitions(app PRIVATE LR2021)
elseif(CONFIG_DT_HAS_SEMTECH_LR2022_ENABLED)
    target_compile_definitions(app PRIVATE LR2022)
elseif(CONFIG_DT_HAS_SEMTECH_LR1110_ENABLED)
    target_compile_definitions(app PRIVATE LR1110)
elseif(CONFIG_DT_HAS_SEMTECH_LR1120_ENABLED)
    target_compile_definitions(app PRIVATE LR1120)
elseif(CONFIG_DT_HAS_SEMTECH_LR1121_ENABLED)
    target_compile_definitions(app PRIVATE LR1121)
elseif(CONFIG_DT_HAS_SEMTECH_SX1261_NEW_ENABLED)
    target_compile_definitions(app PRIVATE SX1261)
elseif(CONFIG_DT_HAS_SEMTECH_SX1262_NEW_ENABLED)
    target_compile_definitions(app PRIVATE SX1262)
elseif(CONFIG_DT_HAS_SEMTECH_SX1268_NEW_ENABLED)
    target_compile_definitions(app PRIVATE SX1268)
endif()

# Float printf is enabled via CONFIG_NEWLIB_LIBC_FLOAT_PRINTF=y in prj.conf
