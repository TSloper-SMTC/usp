# SPDX-License-Identifier: BSD-3-Clause-Clear
# LR11xx wifi region detection example configuration

cmake_minimum_required(VERSION 3.13)
if(CUSTOM_EXAMPLE_CMAKE_LOAD_OPTIONS)
  set(LBM_GEOLOCATION ON CACHE BOOL "" FORCE)
  message(STATUS "Load options wifi_region_detection example")
else()
  set(WIFI_REGION_DETECTION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/wifi_region_detection/main_wifi_region_detection.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/wifi_region_detection/wifi_region_finder.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board/lr1110_board_mb1lxks.c"
  )
  string(REPLACE ";" ";" WIFI_REGION_DETECTION_SOURCES_STR "${WIFI_REGION_DETECTION_SOURCES}")
  add_example(wifi_region_detection "WIFI_REGION_DETECTION" "${WIFI_REGION_DETECTION_SOURCES_STR}")

  # Include directories
  target_include_directories(wifi_region_detection PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/wifi_region_detection
      ${CMAKE_CURRENT_SOURCE_DIR}/../protocols/lbm_lib/smtc_modem_core/geolocation_services
      ${CMAKE_CURRENT_SOURCE_DIR}/main_examples/geoloc_example/lr11xx_board/
    )

  message(STATUS "Configured wifi_region_detection example")
endif()