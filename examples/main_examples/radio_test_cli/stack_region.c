/**
 * @file      stack_region.c
 *
 * @brief     Adapter between the radio_test_cli and the LBM stack region code.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "stack_region.h"
#include "smtc_real_defs.h"
#include "region_us_915.h"
#include "region_eu_868.h"
#include "region_as_923.h"
#include "smtc_duty_cycle.h"
#include "lr1mac_utilities.h"

#include <stdio.h>
#include <string.h>

/*
 * --- Private state -----------------------------------------------------------
 */

static smtc_real_t  s_real;
static region_id_t  s_active_region = REGION_NONE;
static bool         s_initialised   = false;

/*
 * --- Public functions --------------------------------------------------------
 */

int stack_region_init( region_id_t region )
{
    memset( &s_real, 0, sizeof( s_real ) );
    s_active_region = REGION_NONE;
    s_initialised   = false;

    switch( region )
    {
    case REGION_US:
        s_real.region_type = SMTC_REAL_REGION_US_915;
        region_us_915_init( &s_real );
        region_us_915_config( &s_real );
        /* Force all 72 channels active, post-join full state */
        region_us_915_enable_all_channels_with_valid_freq( &s_real );
        /* Skip post-join progressive channel activation */
        s_real.region.us915.first_ch_mask_received = 3; /* ch_mask_after_join_full */
        break;

    case REGION_EU:
        s_real.region_type = SMTC_REAL_REGION_EU_868;
        region_eu_868_init( &s_real );
        region_eu_868_config( &s_real );
        /* Initialise and enable the duty-cycle engine */
        smtc_duty_cycle_init();
        for( uint8_t i = 0; i < s_real.real_const.const_dtc_number_of_band; i++ )
        {
            smtc_duty_cycle_config(
                s_real.real_const.const_dtc_number_of_band,
                i,
                s_real.real_const.const_dtc_by_band[i],
                s_real.real_const.const_dtc_frequency_range_by_band[i * 2],
                s_real.real_const.const_dtc_frequency_range_by_band[i * 2 + 1] );
        }
        smtc_duty_cycle_enable_set( SMTC_DTC_ENABLED );
        break;

    case REGION_JP:
        s_real.region_type = SMTC_REAL_REGION_AS_923;
        region_as_923_init( &s_real, 1 ); /* Group 1 for Japan */
        region_as_923_config( &s_real );
        break;

    default:
        fprintf( stderr, "stack_region_init: unsupported region\n" );
        return -1;
    }

    s_active_region = region;
    s_initialised   = true;
    return 0;
}

int stack_region_get_next_channel( uint8_t dr, uint32_t* out_freq_hz )
{
    if( !s_initialised )
    {
        return -1;
    }

    uint32_t         tx_freq   = 0;
    uint32_t         rx1_freq  = 0;
    uint8_t          active_nb = 0;
    status_lorawan_t rc;

    switch( s_active_region )
    {
    case REGION_US:
        rc = region_us_915_get_next_channel( &s_real, dr, &tx_freq, &rx1_freq, &active_nb );
        break;
    case REGION_EU:
        rc = region_eu_868_get_next_channel( &s_real, dr, &tx_freq, &rx1_freq, &active_nb );
        break;
    case REGION_JP:
        rc = region_as_923_get_next_channel( &s_real, dr, &tx_freq, &rx1_freq, &active_nb );
        break;
    default:
        return -1;
    }

    if( rc != OKLORAWAN )
    {
        return -1;
    }

    *out_freq_hz = tx_freq;
    return 0;
}

void stack_region_mask_channel_used( void )
{
    if( !s_initialised )
    {
        return;
    }

    if( s_active_region == REGION_US )
    {
        region_us_915_mask_channel_used_for_tx( &s_real );
    }
    /* EU/JP: channel exhaustion is not applicable — channels are reusable. */
}

int stack_region_apply_channel_mask( uint8_t mask_cntl, uint16_t ch_mask )
{
    if( !s_initialised || s_active_region != REGION_US )
    {
        return -1;
    }

    status_channel_t rc = region_us_915_build_channel_mask( &s_real, mask_cntl, ch_mask );
    if( rc != OKCHANNEL )
    {
        return -1;
    }

    region_us_915_set_channel_mask( &s_real );
    return 0;
}

bool stack_region_is_tx_allowed( uint32_t freq_hz, uint32_t toa_ms )
{
    if( !s_initialised )
    {
        return false;
    }

    if( s_active_region == REGION_EU )
    {
        smtc_duty_cycle_update();
        return smtc_duty_cycle_is_channel_free( freq_hz );
    }

    /* US and JP: always allowed at this layer.
     * JP LBT is handled separately in stack_lbt. */
    ( void ) toa_ms;
    return true;
}

void stack_region_record_tx( uint32_t freq_hz, uint32_t toa_ms )
{
    if( !s_initialised )
    {
        return;
    }

    if( s_active_region == REGION_EU )
    {
        smtc_duty_cycle_sum( freq_hz, toa_ms );
    }
}

int stack_region_get_channel_info( uint8_t index, uint32_t* out_freq_hz )
{
    if( !s_initialised )
    {
        return -1;
    }

    switch( s_active_region )
    {
    case REGION_US:
        if( index >= 72 )
        {
            return -1;
        }
        *out_freq_hz = region_us_915_get_tx_frequency_channel( &s_real, index );
        break;

    case REGION_EU:
    case REGION_JP:
        if( index >= s_real.real_const.const_number_of_tx_channel )
        {
            return -1;
        }
        /* EU/JP store frequencies in per-channel arrays pointed to by ctx */
        if( s_real.real_ctx.tx_frequency_channel_ctx == NULL )
        {
            return -1;
        }
        *out_freq_hz = s_real.real_ctx.tx_frequency_channel_ctx[index];
        break;

    default:
        return -1;
    }

    return 0;
}

uint8_t stack_region_get_num_channels( void )
{
    if( !s_initialised )
    {
        return 0;
    }
    return s_real.real_const.const_number_of_tx_channel;
}

region_id_t stack_region_get_active( void )
{
    return s_active_region;
}
