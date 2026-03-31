/**
 * @file      lr11xx_hal_linux.c
 *
 * @brief     Hardware Abstraction Layer for LR11xx on Raspberry Pi
 *
 * This implementation provides SPI and GPIO functionality for the LR11xx radio
 * on Raspberry Pi using the Linux SPI and GPIO subsystems.
 *
 * Modeled after lr20xx_hal_linux.c.  Key protocol difference: LR11xx uses a
 * two-phase read with 1 dummy byte (LR20xx uses 2).
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2026. All rights reserved.
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
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION BE
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

#include "lr11xx_hal.h"
#include "smtc_hal_spi.h"
#include "smtc_hal_gpio.h"
#include "modem_pinout.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS -------------------------------------------------------
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

static void lr11xx_hal_wait_on_busy( void )
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

lr11xx_hal_status_t lr11xx_hal_write( const void* context, const uint8_t* command, const uint16_t command_length,
                                      const uint8_t* data, const uint16_t data_length )
{
    lr11xx_hal_wait_on_busy( );

    /* Single SPI transaction: command + data concatenated */
    uint8_t* tx_buf = malloc( command_length + data_length );

    if( tx_buf == NULL )
    {
        return LR11XX_HAL_STATUS_ERROR;
    }

    memcpy( tx_buf, command, command_length );
    if( data != NULL && data_length > 0 )
    {
        memcpy( tx_buf + command_length, data, data_length );
    }

    int ret = hal_spi_transfer( tx_buf, NULL, command_length + data_length );

    free( tx_buf );

    return ( ret >= 0 ) ? LR11XX_HAL_STATUS_OK : LR11XX_HAL_STATUS_ERROR;
}

lr11xx_hal_status_t lr11xx_hal_read( const void* context, const uint8_t* command, const uint16_t command_length,
                                     uint8_t* data, const uint16_t data_length )
{
    lr11xx_hal_wait_on_busy( );

    /* Transaction 1: send command (CS auto-controlled by spidev) */
    int ret = hal_spi_transfer( command, NULL, command_length );
    if( ret < 0 )
    {
        return LR11XX_HAL_STATUS_ERROR;
    }

    if( data_length > 0 )
    {
        lr11xx_hal_wait_on_busy( );

        /* Transaction 2: 1 dummy byte + read data
         * LR11xx uses 1 dummy byte (LR20xx uses 2). */
        uint8_t* tx_buf = malloc( 1 + data_length );
        uint8_t* rx_buf = malloc( 1 + data_length );

        if( tx_buf == NULL || rx_buf == NULL )
        {
            free( tx_buf );
            free( rx_buf );
            return LR11XX_HAL_STATUS_ERROR;
        }

        /* Fill TX buffer: 1 dummy byte + zeros for data portion */
        memset( tx_buf, 0x00, 1 + data_length );

        ret = hal_spi_transfer( tx_buf, rx_buf, 1 + data_length );

        if( ret >= 0 )
        {
            /* Copy received data (skip the 1 dummy response byte) */
            memcpy( data, rx_buf + 1, data_length );
        }

        free( tx_buf );
        free( rx_buf );
    }

    return ( ret >= 0 ) ? LR11XX_HAL_STATUS_OK : LR11XX_HAL_STATUS_ERROR;
}

lr11xx_hal_status_t lr11xx_hal_direct_read( const void* context, uint8_t* data, const uint16_t data_length )
{
    lr11xx_hal_wait_on_busy( );

    /* Direct read with automatic CS control */
    int ret = hal_spi_transfer( NULL, data, data_length );
    return ( ret >= 0 ) ? LR11XX_HAL_STATUS_OK : LR11XX_HAL_STATUS_ERROR;
}

lr11xx_hal_status_t lr11xx_hal_reset( const void* context )
{
    ( void ) context;

    /* Pull reset pin low */
    hal_gpio_set_value( RADIO_NRST, 0 );

    wait_ms( 5 );

    /* Release reset pin (set high) */
    hal_gpio_set_value( RADIO_NRST, 1 );

    /* Wait for LR11xx internal FW to be ready (longer than LR20xx) */
    wait_ms( 200 );

    return LR11XX_HAL_STATUS_OK;
}

lr11xx_hal_status_t lr11xx_hal_wakeup( const void* context )
{
    /* No-op — radio_test_cli never puts the radio to sleep */
    ( void ) context;
    return LR11XX_HAL_STATUS_OK;
}

lr11xx_hal_status_t lr11xx_hal_abort_blocking_cmd( const void* context )
{
    ( void ) context;

    lr11xx_hal_wait_on_busy( );

    /* Send 4 zero bytes to abort any blocking command */
    uint8_t zeros[4] = { 0 };
    int     ret      = hal_spi_transfer( zeros, NULL, 4 );

    lr11xx_hal_wait_on_busy( );

    return ( ret >= 0 ) ? LR11XX_HAL_STATUS_OK : LR11XX_HAL_STATUS_ERROR;
}

/* --- EOF ------------------------------------------------------------------ */
