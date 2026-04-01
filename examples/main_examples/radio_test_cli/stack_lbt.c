/**
 * @file      stack_lbt.c
 *
 * @brief     Listen Before Talk for Japan (AS923) regulatory testing
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "stack_lbt.h"
#include "platform.h"
#include <stdio.h>

/** RSSI stabilisation wait before sampling (ms) */
#define LBT_SETTLE_MS 2

/** Polling interval during sniff window (us) */
#define LBT_POLL_INTERVAL_US 500

stack_lbt_result_t stack_lbt_check( const chip_driver_t* chip, uint32_t freq_hz, int16_t* rssi_out )
{
    int     rc;
    int16_t peak_rssi = -128;

    /* Put radio into continuous RX — chip must already be configured for the
     * correct modulation / bandwidth before calling this function. */
    rc = chip->start_rx( 0 );
    if( rc != 0 )
    {
        return LBT_ERROR;
    }

    /* Wait for RSSI to stabilise */
    platform_sleep_us( LBT_SETTLE_MS * 1000 );

    /* Poll instantaneous RSSI for the sniff window */
    uint32_t elapsed_us = 0;
    uint32_t target_us  = ( uint32_t ) STACK_LBT_SNIFF_DURATION_MS * 1000;

    while( elapsed_us < target_us )
    {
        int16_t rssi = -128;
        rc = chip->get_rssi_inst( &rssi );
        if( rc != 0 )
        {
            chip->stop();
            return LBT_ERROR;
        }

        if( rssi > peak_rssi )
        {
            peak_rssi = rssi;
        }

        if( peak_rssi >= STACK_LBT_THRESHOLD_DBM )
        {
            /* Channel busy — early exit */
            chip->stop();
            if( rssi_out != NULL )
            {
                *rssi_out = peak_rssi;
            }
            return LBT_CHANNEL_BUSY;
        }

        platform_sleep_us( LBT_POLL_INTERVAL_US );
        elapsed_us += LBT_POLL_INTERVAL_US;
    }

    chip->stop();

    if( rssi_out != NULL )
    {
        *rssi_out = peak_rssi;
    }

    return LBT_CHANNEL_FREE;
}
