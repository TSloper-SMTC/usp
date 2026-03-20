/**
 * @file      smtc_hal_irq.h
 *
 * @brief     IRQ Management for RA0E2 Low Power Mode
 *
 * Provides functions for clearing pending interrupts before entering
 * sleep mode to prevent immediate wakeup from spurious IRQs.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef SMTC_HAL_IRQ_H
#define SMTC_HAL_IRQ_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS --------------------------------------------------------
 */

/**
 * @brief Clear all pending interrupts before WFI
 *
 * Clears:
 * - NMISR flags (required per RA0E2 Section 11.8)
 * - All NVIC pending IRQs
 * - SysTick pending exception
 * - TML hardware interrupt flags
 */
void hal_irq_clear_all_pending( void );

/**
 * @brief Clear pending interrupts except specified wakeup sources
 *
 * @param preserve_tml0  Don't clear TML0/TML1 (wakeup/LP timer)
 * @param preserve_rtc   Don't clear RTC alarm
 * @param preserve_radio Don't clear radio DIO (ICU_IRQ5)
 */
void hal_irq_clear_all_pending_except( bool preserve_tml0, bool preserve_rtc, bool preserve_radio );

#ifdef __cplusplus
}
#endif

#endif /* SMTC_HAL_IRQ_H */
