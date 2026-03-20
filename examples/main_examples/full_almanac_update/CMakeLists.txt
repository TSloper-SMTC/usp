# SPDX-License-Identifier: BSD-3-Clause-Clear
# LR11xx flasher example configuration

cmake_minimum_required(VERSION 3.13)

if(CUSTOM_EXAMPLE_CMAKE_LOAD_OPTIONS)
  set(LBM_GEOLOCATION ON CACHE BOOL "" FORCE)
  message(STATUS "Load options full_almanac_update example")
else()
  set(FULL_ALMANAC_UPDATE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/full_almanac_update/main_full_almanac_update.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/full_almanac_update/app_full_almanac_update.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board/lr1110_board_mb1lxks.c"
  )
  string(REPLACE ";" ";" FULL_ALMANAC_UPDATE_SOURCES_STR "${FULL_ALMANAC_UPDATE_SOURCES}")
  add_example(full_almanac_update "FULL_ALMANAC_UPDATE" "${FULL_ALMANAC_UPDATE_SOURCES_STR}")

  # Include directories
  target_include_directories(full_almanac_update PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/full_almanac_update
      ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/lbm_lib/smtc_modem_core/geolocation_services
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board/
    )

  message(STATUS "Configured full_almanac_update example")
endif()
