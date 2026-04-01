/**
 * @file      smtc_hal_mcu.c
 *
 * @brief     MCU Hardware Abstraction Layer implementation for Zephyr
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

#include "smtc_hal_mcu.h"
#include "smtc_hal_uart.h"
#include "zephyr_hal_bridge.h"

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/reboot.h>

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE MACROS ----------------------------------------------------------
 */

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

/* irq_lock key saved by hal_mcu_disable_irq() for hal_mcu_enable_irq() */
static unsigned int irq_saved_key;

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DECLARATION -------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

void hal_mcu_critical_section_begin( uint32_t* mask )
{
    *mask = irq_lock( );
}

void hal_mcu_critical_section_end( uint32_t* mask )
{
    irq_unlock( *mask );
}

void hal_mcu_disable_irq( void )
{
    irq_saved_key = irq_lock( );
}

void hal_mcu_enable_irq( void )
{
    irq_unlock( irq_saved_key );
}

void hal_mcu_init( void )
{
    /* Zephyr handles most hardware initialization before main().
     * We still need to:
     *   1. Set the radio driver context from device tree and bridge DIO IRQ
     *   2. Initialize UART RX ring buffer and ISR for linenoise console I/O
     */
    zephyr_hal_bridge_init( );
    trace_uart_init( );
}

_Noreturn void hal_mcu_reset( void )
{
    sys_reboot( SYS_REBOOT_COLD );
    /* Should never reach here, but satisfy _Noreturn */
    while( 1 )
    {
    }
}

void hal_mcu_wait_us( const int32_t microseconds )
{
    if( microseconds > 0 )
    {
        k_usleep( microseconds );
    }
}

void hal_mcu_set_sleep_for_ms( const int32_t milliseconds )
{
    if( milliseconds > 0 )
    {
        k_msleep( milliseconds );
    }
}

void hal_mcu_disable_low_power_wait( void )
{
    /* Zephyr PM subsystem manages low power independently. */
}

void hal_mcu_enable_low_power_wait( void )
{
    /* Zephyr PM subsystem manages low power independently. */
}

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DEFINITION --------------------------------------------
 */

/* --- EOF ------------------------------------------------------------------ */
