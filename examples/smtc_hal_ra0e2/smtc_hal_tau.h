/**
 * @file      smtc_hal_tau.h
 *
 * @brief     TAU timer driver for precise microsecond delays
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef SMTC_HAL_TAU_H
#define SMTC_HAL_TAU_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include <stdint.h>

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC CONSTANTS --------------------------------------------------------
 */

/* Maximum single delay in microseconds (16-bit counter @ 1MHz) */
#define HAL_TAU_MAX_DELAY_US 65000U

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS PROTOTYPES ---------------------------------------------
 */

/**
 * @brief Initialize TAU timer for precise microsecond delays
 */
void hal_tau_init( void );

/**
 * @brief Blocking delay using TAU hardware timer (ISR-safe)
 *
 * Uses TAU channel 0 configured at 1MHz (1us per tick) for precise timing.
 * Polls NVIC pending bit for ISR-safe operation (no interrupt dependency).
 *
 * @param[in] microseconds  Delay duration in microseconds (max 65000)
 */
void hal_tau_delay_us( uint32_t microseconds );

/**
 * @brief TAU interrupt handler (called from fsp_callbacks.c)
 *
 * @note Not used in current polling implementation, kept for FSP compatibility.
 */
void hal_tau_irq_handler( void );

#ifdef __cplusplus
}
#endif

#endif /* SMTC_HAL_TAU_H */
