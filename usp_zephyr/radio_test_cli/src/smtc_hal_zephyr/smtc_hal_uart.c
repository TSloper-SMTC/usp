/**
 * @file      smtc_hal_uart.c
 *
 * @brief     UART Hardware Abstraction Layer implementation for Zephyr
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
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

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include "smtc_hal_uart.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE MACROS ----------------------------------------------------------
 */

#define UART_RX_BUF_SIZE 256

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE CONSTANTS -------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE TYPES -----------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */

static const struct device* console_dev;
static uint8_t              rx_ring_buf[UART_RX_BUF_SIZE];
static volatile uint16_t    rx_head;
static volatile uint16_t    rx_tail;

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DECLARATION -------------------------------------------
 */

static void uart_isr_callback( const struct device* dev, void* user_data );

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

void trace_uart_init( void )
{
    console_dev = DEVICE_DT_GET( DT_CHOSEN( zephyr_console ) );
    if( !device_is_ready( console_dev ) )
    {
        console_dev = NULL;
        return;
    }

    rx_head = 0;
    rx_tail = 0;

    uart_irq_callback_set( console_dev, uart_isr_callback );
    uart_irq_rx_enable( console_dev );
}

void trace_uart_deinit( void )
{
    if( console_dev != NULL )
    {
        uart_irq_rx_disable( console_dev );
    }
}

void trace_uart_tx( uint8_t* buff, uint8_t len )
{
    if( console_dev == NULL )
    {
        return;
    }
    for( uint8_t i = 0; i < len; i++ )
    {
        uart_poll_out( console_dev, buff[i] );
    }
}

bool trace_uart_rx_available( void )
{
    return ( rx_head != rx_tail );
}

int trace_uart_rx_getchar( void )
{
    if( rx_head == rx_tail )
    {
        return -1;
    }
    uint8_t ch = rx_ring_buf[rx_tail];
    rx_tail    = ( rx_tail + 1 ) % UART_RX_BUF_SIZE;
    return ( int ) ch;
}

/* hw_modem_* functions are not used by radio_test_cli -- empty stubs */

void hw_modem_uart_init( void )
{
}

void hw_modem_uart_deinit( void )
{
}

void hw_modem_uart_dma_start_rx( uint8_t* buff, uint16_t size )
{
    ( void ) buff;
    ( void ) size;
}

void hw_modem_uart_dma_stop_rx( void )
{
}

void hw_modem_uart_tx( uint8_t* buff, uint8_t len )
{
    ( void ) buff;
    ( void ) len;
}

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DEFINITION --------------------------------------------
 */

static void uart_isr_callback( const struct device* dev, void* user_data )
{
    ( void ) user_data;

    if( !uart_irq_update( dev ) )
    {
        return;
    }

    while( uart_irq_rx_ready( dev ) )
    {
        uint8_t  byte;
        int      ret = uart_fifo_read( dev, &byte, 1 );
        if( ret <= 0 )
        {
            break;
        }
        uint16_t next_head = ( rx_head + 1 ) % UART_RX_BUF_SIZE;
        if( next_head != rx_tail )
        {
            rx_ring_buf[rx_head] = byte;
            rx_head              = next_head;
        }
        /* else: ring buffer full, drop byte */
    }
}

/* --- EOF ------------------------------------------------------------------ */
