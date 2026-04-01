/**
 * @file      platform_baremetal.c
 *
 * @brief     Platform abstraction — baremetal MCU implementation
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "platform.h"

#include "smtc_hal_mcu.h"
#include "smtc_hal_rtc.h"

void platform_sleep_us( uint32_t us )
{
    hal_mcu_wait_us( ( int32_t ) us );
}

uint32_t platform_time_ms( void )
{
    return hal_rtc_get_time_ms( );
}
