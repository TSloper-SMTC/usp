/**
 * @file      zephyr_hal_bridge.c
 *
 * @brief     Zephyr HAL bridge for radio_test_cli
 *
 * Bridges the CLI's HAL expectations to Zephyr's device tree model:
 *   - Sets the radio driver context from DT (for chip_lr20xx.c)
 *   - Bridges the usp_zephyr driver's DIO IRQ to the GPIO HAL callback table
 *   - Provides BSP stubs not yet available in usp_zephyr
 *
 * radio_test_cli bypasses RAC and talks directly to the LR20xx driver.
 * It does NOT use SMTC_SW_PLATFORM_INIT() or CONFIG_USP_MAIN_THREAD
 * because there is no RAC engine to run.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "zephyr_hal_bridge.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <stdint.h>

#include <zephyr/usp/lora_lbm_transceiver.h>

#include "chip_interface.h"
#include "smtc_hal_gpio.h"
#include "smtc_hal_gpio_pin_names.h"
#include "modem_pinout.h"

/*
 * --- Compile-time validation ------------------------------------------------
 *
 * modem_pinout_zephyr.h hardcodes pin values (due to include-path conflicts
 * with smtc_hal_linux headers added globally by usp_zephyr).  Verify they
 * match the enum in smtc_hal_gpio_pin_names.h.
 */
_Static_assert( RADIO_NSS == ZEPHYR_PIN_RADIO_NSS, "RADIO_NSS pin mismatch" );
_Static_assert( RADIO_NRST == ZEPHYR_PIN_RADIO_NRST, "RADIO_NRST pin mismatch" );
_Static_assert( RADIO_BUSY_PIN == ZEPHYR_PIN_RADIO_BUSY, "RADIO_BUSY_PIN pin mismatch" );
_Static_assert( RADIO_DIO_MAIN == ZEPHYR_PIN_RADIO_DIO, "RADIO_DIO_MAIN pin mismatch" );
_Static_assert( SMTC_LED_TX == ZEPHYR_PIN_LED_TX, "SMTC_LED_TX pin mismatch" );
_Static_assert( SMTC_LED_RX == ZEPHYR_PIN_LED_RX, "SMTC_LED_RX pin mismatch" );

/*
 * --- DIO IRQ bridge ---------------------------------------------------------
 *
 * main_radio_test.c calls hal_gpio_irq_attach(RADIO_DIO_MAIN, callback) which
 * stores the callback in the GPIO HAL's pin table.  On Zephyr, the usp_zephyr
 * driver owns the DIO interrupt.  We bridge it: the driver calls our wrapper,
 * which fires the callback via hal_gpio_fire_irq().
 */
static void zephyr_dio_irq_wrapper( const struct device* dev )
{
    ( void ) dev;
    hal_gpio_fire_irq( ZEPHYR_PIN_RADIO_DIO );
}

/*
 * --- XOSC trim stub ---------------------------------------------------------
 *
 * usp_zephyr's BSP does not provide ral_lr20xx_bsp_get_xosc_trim() because
 * the DT binding doesn't have XTA/XTB properties yet.  Provide defaults here
 * until the DT binding is extended (see plan TODO U1).
 */
void ral_lr20xx_bsp_get_xosc_trim( const void* context, uint8_t* xta, uint8_t* xtb, uint8_t* wait_time_us )
{
    ( void ) context;
    *xta          = 0x05;  /* default XTA trim */
    *xtb          = 0x05;  /* default XTB trim */
    *wait_time_us = 100;   /* default XOSC stabilization delay */
}

/*
 * --- Radio device from device tree ------------------------------------------
 *
 * The usp_zephyr driver auto-instantiates a struct device for each LR20xx
 * node via DT_FOREACH_STATUS_OKAY.  The lr20xx_hal.c functions cast the
 * context to (const struct device*) and access dev->config / dev->data.
 * We just need to get that device pointer and pass it to chip_set_radio_context().
 */

/* Use the same DT chosen node that all usp_zephyr samples use.
 * The shield overlay defines: zephyr,lorawan-transceiver = &lora_semtech_wio_lr20xx;
 * This makes the bridge radio-agnostic (works with LR2021, LR2022, etc.) */
#define RADIO_NODE DT_CHOSEN( zephyr_lorawan_transceiver )

void zephyr_hal_bridge_init( void )
{
    /* Get the Zephyr device for the radio (instantiated by the driver from DT) */
    const struct device* radio_dev = DEVICE_DT_GET( RADIO_NODE );

    if( !device_is_ready( radio_dev ) )
    {
        printk( "FATAL: radio device not ready\n" );
        k_panic( );
    }

    /* Pass the device pointer as the radio context.
     * lr20xx_hal.c casts this to (const struct device*) and reads
     * dev->config (lr20xx_hal_context_cfg_t) for SPI/GPIO specs. */
    chip_set_radio_context( radio_dev );

    /* Bridge DIO IRQ: attach our wrapper to the usp_zephyr driver's IRQ,
     * then enable it.  When main_radio_test.c calls hal_gpio_irq_attach()
     * the callback is stored in the GPIO HAL pin table.  When the DIO
     * fires, the driver calls zephyr_dio_irq_wrapper() which fires
     * the stored callback via hal_gpio_fire_irq(ZEPHYR_PIN_RADIO_DIO). */
    lora_transceiver_board_attach_interrupt( radio_dev, zephyr_dio_irq_wrapper );
    lora_transceiver_board_enable_interrupt( radio_dev );
}
