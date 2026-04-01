/**
 * @file      zephyr_hal_bridge.h
 *
 * @brief     Zephyr HAL bridge for radio_test_cli
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */
#ifndef ZEPHYR_HAL_BRIDGE_H
#define ZEPHYR_HAL_BRIDGE_H

/**
 * Initialize the bridge between radio_test_cli's HAL expectations and
 * the Zephyr device tree model.  Called from hal_mcu_init().
 *
 * - Sets the radio driver context from DT (for chip_lr20xx.c)
 * - Bridges the usp_zephyr driver's DIO IRQ to the GPIO HAL callback table
 */
void zephyr_hal_bridge_init( void );

#endif /* ZEPHYR_HAL_BRIDGE_H */
