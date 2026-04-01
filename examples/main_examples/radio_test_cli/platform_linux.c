/**
 * @file      platform_linux.c
 *
 * @brief     Platform abstraction — Linux (POSIX) implementation
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "platform.h"

#include <unistd.h>
#include <time.h>

void platform_sleep_us( uint32_t us )
{
    usleep( us );
}

uint32_t platform_time_ms( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ( uint32_t )( ts.tv_sec * 1000 + ts.tv_nsec / 1000000 );
}
