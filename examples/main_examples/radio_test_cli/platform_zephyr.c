/**
 * @file      platform_zephyr.c
 *
 * @brief     Platform abstraction — Zephyr RTOS implementation
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "platform.h"

#include <zephyr/kernel.h>

void platform_sleep_us( uint32_t us )
{
    k_usleep( ( int32_t ) us );
}

uint32_t platform_time_ms( void )
{
    return ( uint32_t ) k_uptime_get( );
}
