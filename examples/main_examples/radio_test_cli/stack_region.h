/**
 * @file      stack_region.h
 *
 * @brief     Adapter between the radio_test_cli and the LBM stack region code.
 *
 * Manages the smtc_real_t lifetime and exposes a simplified channel-selection
 * API for the CLI's regulatory test modes.  The actual channel hopping,
 * duty-cycle gating, and channel-mask logic comes from the production stack
 * source files (region_us_915.c, region_eu_868.c, region_as_923.c) compiled
 * unmodified into the CLI binary.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef STACK_REGION_H
#define STACK_REGION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cli_state.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Initialise the stack region for the given CLI region.
 * Must be called before any other stack_region_* function.
 * Returns 0 on success, -1 on error.
 */
int stack_region_init( region_id_t region );

/**
 * Select the next TX channel for the given data rate.
 * @param dr             LoRaWAN data rate index (0-6 for US, 0-5 for EU/JP)
 * @param out_freq_hz    [out] selected TX frequency in Hz
 * @return 0 on success, -1 if no channel available (e.g. duty-cycle exhausted)
 */
int stack_region_get_next_channel( uint8_t dr, uint32_t* out_freq_hz );

/**
 * Mark the most recently returned channel as "used" so it won't be
 * selected again until the full channel set is exhausted (US FHSS).
 */
void stack_region_mask_channel_used( void );

/**
 * Apply a sub-band channel mask (US915 hybrid mode).
 * @param mask_cntl  ChMaskCntl value (5 for bank-level selection)
 * @param ch_mask    16-bit mask (e.g. 1<<N to enable sub-band N)
 * @return 0 on success, -1 on error
 */
int stack_region_apply_channel_mask( uint8_t mask_cntl, uint16_t ch_mask );

/**
 * Check whether a TX at the given frequency / time-on-air is allowed.
 * EU: checks ETSI 1-hour rolling duty cycle.
 * JP: returns true (LBT is handled separately by stack_lbt).
 * US: always returns true.
 */
bool stack_region_is_tx_allowed( uint32_t freq_hz, uint32_t toa_ms );

/**
 * Record a completed TX for duty-cycle accounting (EU only).
 */
void stack_region_record_tx( uint32_t freq_hz, uint32_t toa_ms );

/**
 * Get the frequency of a specific channel index.
 * @param index       channel index (0-71 for US, 0-15 for EU/JP)
 * @param out_freq_hz [out] frequency in Hz
 * @return 0 on success, -1 if index out of range
 */
int stack_region_get_channel_info( uint8_t index, uint32_t* out_freq_hz );

/**
 * Get the total number of TX channels for the active region.
 */
uint8_t stack_region_get_num_channels( void );

/**
 * Get the currently active region.
 */
region_id_t stack_region_get_active( void );

#ifdef __cplusplus
}
#endif

#endif /* STACK_REGION_H */
