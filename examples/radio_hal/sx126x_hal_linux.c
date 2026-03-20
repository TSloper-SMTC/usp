/**
 * @file      sx126x_hal_linux.c
 *
 * @brief     Hardware Abstraction Layer for SX126x on Raspberry Pi
 *
 * This implementation provides SPI and GPIO functionality for the SX126x radio
 * on Raspberry Pi using the Linux SPI and GPIO subsystems.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sx126x_hal.h"
#include "smtc_hal_spi.h"
#include "smtc_hal_gpio.h"
#include "modem_pinout.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE MACROS
 * -----------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE TYPES
 * ------------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES
 * ------------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS
 * --------------------------------------------------------
 */

static void wait_us( unsigned long delay_us )
{
    struct timespec dly;
    struct timespec rem;

    dly.tv_sec  = delay_us / 1000000;
    dly.tv_nsec = ( ( long ) delay_us % 1000000 ) * 1000;

    if( ( dly.tv_sec > 0 ) || ( ( dly.tv_sec == 0 ) && ( dly.tv_nsec > 100000 ) ) )
    {
        clock_nanosleep( CLOCK_MONOTONIC, 0, &dly, &rem );
    }
}

static void wait_ms( unsigned long delay_ms )
{
    wait_us( delay_ms * 1000 );
}

static void sx126x_hal_wait_on_busy( void )
{
    while( hal_gpio_get_value( RADIO_BUSY_PIN ) == 1 )
    {
        wait_us( 10 );
    }
}

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS --------------------------------------------------------
 */

sx126x_hal_status_t sx126x_hal_write( const void* context, const uint8_t* command, const uint16_t command_length,
                                      const uint8_t* data, const uint16_t data_length )
{
    sx126x_hal_wait_on_busy( );

    uint8_t* tx_buf = malloc( command_length + data_length );

    if( tx_buf == NULL )
    {
        return SX126X_HAL_STATUS_ERROR;
    }

    memcpy( tx_buf, command, command_length );
    if( data != NULL && data_length > 0 )
    {
        memcpy( tx_buf + command_length, data, data_length );
    }

    int ret = hal_spi_transfer( tx_buf, NULL, command_length + data_length );

    free( tx_buf );

    return ( ret >= 0 ) ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

sx126x_hal_status_t sx126x_hal_read( const void* context, const uint8_t* command, const uint16_t command_length,
                                     uint8_t* data, const uint16_t data_length )
{
    sx126x_hal_wait_on_busy( );

    uint8_t* tx_buf = malloc( command_length + data_length );
    uint8_t* rx_buf = malloc( command_length + data_length );

    if( tx_buf == NULL || rx_buf == NULL )
    {
        free( tx_buf );
        free( rx_buf );
        return SX126X_HAL_STATUS_ERROR;
    }

    memcpy( tx_buf, command, command_length );
    memset( tx_buf + command_length, 0x00, data_length );

    int ret = hal_spi_transfer( tx_buf, rx_buf, command_length + data_length );

    if( ret >= 0 )
    {
        memcpy( data, rx_buf + command_length, data_length );
    }

    free( tx_buf );
    free( rx_buf );

    return ( ret >= 0 ) ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

sx126x_hal_status_t sx126x_hal_reset( const void* context )
{
    ( void ) context;

    hal_gpio_set_value( RADIO_NRST, 0 );

    wait_ms( 1 );

    hal_gpio_set_value( RADIO_NRST, 1 );

    return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_wakeup( const void* context )
{
    // TODO: need to control NSS pin to wake up the radio
    return SX126X_HAL_STATUS_OK;
}

/*
 * -----------------------------------------------------------------------------
 * --- INITIALIZATION FUNCTIONS
 * -------------------------------------------------
 */

/* --- EOF ------------------------------------------------------------------ */
