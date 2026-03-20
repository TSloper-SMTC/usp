/**
 * @file      stack_hal_shim.c
 *
 * @brief     HAL shim for stack region code — provides the smtc_modem_hal_*
 *            functions that the compiled-in stack sources call at runtime.
 *
 * The radio_test_cli does not link the full LBM modem HAL.  The region
 * source files (region_us_915.c, region_eu_868.c, etc.) call only three
 * HAL entry points at runtime.  This shim delegates them to the existing
 * platform HAL that the CLI already links.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "smtc_modem_hal.h"

#include "smtc_hal_rng.h"
#include "smtc_hal_rtc.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/*
 * --- Random number generation ------------------------------------------------
 */

uint32_t smtc_modem_hal_get_random_nb_in_range( const uint32_t val_1, const uint32_t val_2 )
{
    return hal_rng_get_random_in_range( val_1, val_2 );
}

/*
 * --- Time --------------------------------------------------------------------
 */

uint32_t smtc_modem_hal_get_time_in_ms( void )
{
    return hal_rtc_get_time_ms();
}

/*
 * --- Panic -------------------------------------------------------------------
 */

_Noreturn void smtc_modem_hal_on_panic( uint8_t* func, uint32_t line, const char* fmt, ... )
{
    va_list args;
    va_start( args, fmt );

    fprintf( stderr, "PANIC [%s:%u] ", func, (unsigned) line );
    vfprintf( stderr, fmt, args );
    fprintf( stderr, "\n" );

    va_end( args );
    abort();
}
