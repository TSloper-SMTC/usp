/**
 * @file      smtc_hal_gpio.c
 *
 * @brief     GPIO Hardware Abstraction Layer implementation for Zephyr
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

#include "smtc_hal_gpio.h"

#include <zephyr/drivers/gpio.h>

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

typedef struct
{
    struct gpio_dt_spec   spec;
    bool                  registered;
    struct gpio_callback  cb_data;
    void*                 irq_context;
    void ( *irq_callback )( void* context );
} hal_gpio_zephyr_pin_t;

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */

static hal_gpio_zephyr_pin_t pin_table[ZEPHYR_PIN_COUNT];

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DECLARATION -------------------------------------------
 */

static void gpio_irq_handler( const struct device* port, struct gpio_callback* cb, gpio_port_pins_t pins );

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

void hal_gpio_zephyr_register_pin( hal_gpio_pin_names_t id, const struct gpio_dt_spec* spec )
{
    if( ( int ) id < 0 || ( int ) id >= ZEPHYR_PIN_COUNT )
    {
        return;
    }
    pin_table[id].spec       = *spec;
    pin_table[id].registered = true;
}

void hal_gpio_init_out( const hal_gpio_pin_names_t pin, const uint32_t value )
{
    if( pin == NC )
    {
        return;
    }
    if( ( int ) pin < 0 || ( int ) pin >= ZEPHYR_PIN_COUNT || !pin_table[pin].registered )
    {
        return;
    }
    gpio_pin_configure_dt( &pin_table[pin].spec,
                           value ? GPIO_OUTPUT_INIT_HIGH : GPIO_OUTPUT_INIT_LOW );
}

void hal_gpio_init_in( const hal_gpio_pin_names_t pin, const hal_gpio_pull_mode_t pull_mode,
                       const hal_gpio_irq_mode_t irq_mode, hal_gpio_irq_t* irq )
{
    if( pin == NC )
    {
        return;
    }
    if( ( int ) pin < 0 || ( int ) pin >= ZEPHYR_PIN_COUNT || !pin_table[pin].registered )
    {
        return;
    }

    gpio_flags_t flags = GPIO_INPUT;
    switch( pull_mode )
    {
    case BSP_GPIO_PULL_MODE_UP:
        flags |= GPIO_PULL_UP;
        break;
    case BSP_GPIO_PULL_MODE_DOWN:
        flags |= GPIO_PULL_DOWN;
        break;
    default:
        break;
    }

    gpio_pin_configure_dt( &pin_table[pin].spec, flags );

    if( irq_mode != BSP_GPIO_IRQ_MODE_OFF && irq != NULL )
    {
        hal_gpio_irq_attach( irq );

        gpio_flags_t int_flags = 0;
        switch( irq_mode )
        {
        case BSP_GPIO_IRQ_MODE_RISING:
            int_flags = GPIO_INT_EDGE_RISING;
            break;
        case BSP_GPIO_IRQ_MODE_FALLING:
            int_flags = GPIO_INT_EDGE_FALLING;
            break;
        case BSP_GPIO_IRQ_MODE_RISING_FALLING:
            int_flags = GPIO_INT_EDGE_BOTH;
            break;
        default:
            break;
        }

        gpio_init_callback( &pin_table[pin].cb_data, gpio_irq_handler,
                            BIT( pin_table[pin].spec.pin ) );
        gpio_add_callback( pin_table[pin].spec.port, &pin_table[pin].cb_data );
        gpio_pin_interrupt_configure_dt( &pin_table[pin].spec, int_flags );
    }
}

void hal_gpio_irq_attach( const hal_gpio_irq_t* irq )
{
    if( irq == NULL || irq->pin == NC )
    {
        return;
    }
    if( ( int ) irq->pin < 0 || ( int ) irq->pin >= ZEPHYR_PIN_COUNT )
    {
        return;
    }
    pin_table[irq->pin].irq_callback = irq->callback;
    pin_table[irq->pin].irq_context  = irq->context;
}

void hal_gpio_fire_irq( hal_gpio_pin_names_t pin )
{
    if( ( int ) pin < 0 || ( int ) pin >= ZEPHYR_PIN_COUNT )
    {
        return;
    }
    if( pin_table[pin].irq_callback != NULL )
    {
        pin_table[pin].irq_callback( pin_table[pin].irq_context );
    }
}

void hal_gpio_irq_deatach( const hal_gpio_irq_t* irq )
{
    if( irq == NULL || irq->pin == NC )
    {
        return;
    }
    if( ( int ) irq->pin < 0 || ( int ) irq->pin >= ZEPHYR_PIN_COUNT )
    {
        return;
    }
    pin_table[irq->pin].irq_callback = NULL;
    pin_table[irq->pin].irq_context  = NULL;

    if( pin_table[irq->pin].registered )
    {
        gpio_pin_interrupt_configure_dt( &pin_table[irq->pin].spec, GPIO_INT_DISABLE );
        gpio_remove_callback( pin_table[irq->pin].spec.port, &pin_table[irq->pin].cb_data );
    }
}

void hal_gpio_irq_enable( void )
{
    for( int i = 0; i < ZEPHYR_PIN_COUNT; i++ )
    {
        if( pin_table[i].registered && pin_table[i].irq_callback != NULL )
        {
            /* Re-enable any previously configured interrupt edge */
            gpio_pin_interrupt_configure_dt( &pin_table[i].spec, GPIO_INT_EDGE_TO_ACTIVE );
        }
    }
}

void hal_gpio_irq_disable( void )
{
    for( int i = 0; i < ZEPHYR_PIN_COUNT; i++ )
    {
        if( pin_table[i].registered && pin_table[i].irq_callback != NULL )
        {
            gpio_pin_interrupt_configure_dt( &pin_table[i].spec, GPIO_INT_DISABLE );
        }
    }
}

void hal_gpio_set_value( const hal_gpio_pin_names_t pin, const uint32_t value )
{
    if( pin == NC )
    {
        return;
    }
    if( ( int ) pin < 0 || ( int ) pin >= ZEPHYR_PIN_COUNT || !pin_table[pin].registered )
    {
        return;
    }
    gpio_pin_set_dt( &pin_table[pin].spec, ( int ) value );
}

uint32_t hal_gpio_get_value( const hal_gpio_pin_names_t pin )
{
    if( pin == NC )
    {
        return 0;
    }
    if( ( int ) pin < 0 || ( int ) pin >= ZEPHYR_PIN_COUNT || !pin_table[pin].registered )
    {
        return 0;
    }
    return ( uint32_t ) gpio_pin_get_dt( &pin_table[pin].spec );
}

void hal_gpio_clear_pending_irq( const hal_gpio_pin_names_t pin )
{
    /* Zephyr GPIO driver does not expose a direct "clear pending" API.
     * The interrupt is acknowledged automatically by the driver. */
    ( void ) pin;
}

void hal_gpio_enable_clock( const hal_gpio_pin_names_t pin )
{
    /* Zephyr manages peripheral clocks via device tree / PM subsystem.
     * Nothing to do here. */
    ( void ) pin;
}

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DEFINITION --------------------------------------------
 */

static void gpio_irq_handler( const struct device* port, struct gpio_callback* cb, gpio_port_pins_t pins )
{
    ( void ) port;
    ( void ) pins;

    for( int i = 0; i < ZEPHYR_PIN_COUNT; i++ )
    {
        if( &pin_table[i].cb_data == cb && pin_table[i].irq_callback != NULL )
        {
            pin_table[i].irq_callback( pin_table[i].irq_context );
            break;
        }
    }
}

/* --- EOF ------------------------------------------------------------------ */
