/**
 * @file      stack_lbt.h
 *
 * @brief     Listen Before Talk for Japan (AS923) regulatory testing
 *
 * Performs an RSSI-based channel-clear assessment using the chip driver
 * before each TX, as required by ARIB STD-T108.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef STACK_LBT_H
#define STACK_LBT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "chip_interface.h"
#include <stdint.h>
#include <stdbool.h>

/** LBT parameters from LoRaWAN AS923 regional specification */
#define STACK_LBT_SNIFF_DURATION_MS  5
#define STACK_LBT_THRESHOLD_DBM      ( -80 )
#define STACK_LBT_BW_HZ             200000

typedef enum
{
    LBT_CHANNEL_FREE = 0,
    LBT_CHANNEL_BUSY,
    LBT_ERROR,
} stack_lbt_result_t;

/**
 * Perform a Listen Before Talk check on the given frequency.
 *
 * Puts the radio into continuous RX, waits for RSSI stabilisation,
 * then polls instantaneous RSSI for STACK_LBT_SNIFF_DURATION_MS.
 * Returns LBT_CHANNEL_FREE if all samples are below threshold,
 * LBT_CHANNEL_BUSY if any sample exceeds it, or LBT_ERROR on failure.
 *
 * @param chip     chip driver (used for start_rx / stop / RSSI read)
 * @param freq_hz  frequency to sniff (Hz)
 * @param rssi_out [out] peak RSSI observed (dBm), may be NULL
 */
stack_lbt_result_t stack_lbt_check( const chip_driver_t* chip, uint32_t freq_hz, int16_t* rssi_out );

#ifdef __cplusplus
}
#endif

#endif /* STACK_LBT_H */
