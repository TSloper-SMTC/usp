/**
 * @file      platform.h
 *
 * @brief     Platform abstraction for timing primitives
 *
 * Each target provides its own implementation:
 *   - platform_linux.c     (POSIX)
 *   - platform_baremetal.c (HAL-based)
 *   - platform_zephyr.c    (Zephyr kernel)
 *
 * The build system selects the correct .c file; no #ifdef needed here.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

/** Blocking delay in microseconds.  Yields the CPU on RTOS targets. */
void platform_sleep_us( uint32_t us );

/** Monotonic time in milliseconds (for seeding, rough timing). */
uint32_t platform_time_ms( void );

#endif /* PLATFORM_H */
