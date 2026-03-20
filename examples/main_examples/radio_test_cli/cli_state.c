/**
 * @file      cli_state.c
 *
 * @brief     Radio configuration state and status display
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "cli_state.h"
#include "modulation.h"
#include <stdio.h>
#include <string.h>

void cli_state_init( radio_config_t* cfg )
{
    memset( cfg, 0, sizeof( *cfg ) );
    cfg->region      = REGION_NONE;
    cfg->modulation  = MODULATION_NONE;
    cfg->active_mode = MODE_IDLE;
    cfg->freq_mhz   = 0.0f;
    cfg->power_dbm   = 0;
    cfg->agc_gain    = AGC_GAIN_AUTO;
    cfg->rx_boost_lf = -1; /* auto (0) */
    cfg->rx_boost_hf = -1; /* auto (4) */
    cfg->bw_khz      = 0;
    cfg->sf          = 0;
    cfg->cr          = CODING_RATE_4_5;
    cfg->preamble    = 8;
    cfg->syncword    = 0;
    cfg->header_implicit = false;
    cfg->crc_on          = true;
    cfg->ldro            = -1; /* auto */

    /* PER defaults */
    cfg->per_count        = 100;
    cfg->per_interval_ms  = 500;
    cfg->per_payload_size = 0; /* 0 = auto (maximize airtime within interval) */

    /* xosc_xta/xtb/wait_us left as 0; seeded from BSP by main after chip init */
}

const char* cli_state_region_str( region_id_t region )
{
    switch( region )
    {
    case REGION_US:
        return "US902-928";
    case REGION_EU:
        return "EU863-870";
    case REGION_JP:
        return "JP AS923-1";
    case REGION_WW2G4:
        return "WW2G4 (2.4 GHz)";
    default:
        return "(not set)";
    }
}

const char* cli_state_modulation_str( modulation_id_t mod )
{
    switch( mod )
    {
    case MODULATION_LORA:
        return "LoRa";
    case MODULATION_GFSK:
        return "GFSK";
    case MODULATION_FLRC:
        return "FLRC";
    default:
        return "(not set)";
    }
}

const char* cli_state_mode_str( active_mode_t mode )
{
    switch( mode )
    {
    case MODE_IDLE:
        return "idle";
    case MODE_CW:
        return "CW";
    case MODE_MODULATED:
        return "modulated";
    case MODE_RX:
        return "RX";
    case MODE_FHSS:
        return "FHSS";
    case MODE_DTS:
        return "DTS";
    case MODE_HYBRID:
        return "hybrid";
    case MODE_EU_TEST:
        return "eu-test";
    case MODE_JP_TEST:
        return "jp-test";
    case MODE_PER_TX:
        return "PER TX";
    case MODE_PER_RX:
        return "PER RX";
    default:
        return "unknown";
    }
}

const char* cli_state_cr_str( coding_rate_t cr )
{
    switch( cr )
    {
    case CODING_RATE_4_5:
        return "4/5";
    case CODING_RATE_4_6:
        return "4/6";
    case CODING_RATE_4_7:
        return "4/7";
    case CODING_RATE_4_8:
        return "4/8";
    default:
        return "?";
    }
}

const char* cli_state_flrc_cr_str( flrc_cr_t cr )
{
    switch( cr )
    {
    case FLRC_CR_1_2:
        return "1/2";
    case FLRC_CR_3_4:
        return "3/4";
    case FLRC_CR_2_3:
        return "2/3";
    case FLRC_CR_NONE:
        return "none";
    default:
        return "?";
    }
}

const char* cli_state_flrc_bt_str( flrc_bt_t bt )
{
    switch( bt )
    {
    case FLRC_BT_OFF:
        return "off";
    case FLRC_BT_0_5:
        return "bt0.5";
    case FLRC_BT_1:
        return "bt1";
    default:
        return "?";
    }
}

const char* cli_state_agc_gain_str( agc_gain_t gain )
{
    switch( gain )
    {
    case AGC_GAIN_AUTO:
        return "auto";
    case AGC_GAIN_G1:
        return "g1";
    case AGC_GAIN_G2:
        return "g2";
    case AGC_GAIN_G3:
        return "g3";
    case AGC_GAIN_G4:
        return "g4";
    case AGC_GAIN_G5:
        return "g5";
    case AGC_GAIN_G6:
        return "g6";
    case AGC_GAIN_G7:
        return "g7";
    case AGC_GAIN_G8:
        return "g8";
    case AGC_GAIN_G9:
        return "g9";
    case AGC_GAIN_G10:
        return "g10";
    case AGC_GAIN_G11:
        return "g11";
    case AGC_GAIN_G12:
        return "g12";
    case AGC_GAIN_G13:
        return "g13";
    default:
        return "?";
    }
}

void cli_state_print_status( const radio_config_t* cfg )
{
    printf( "  Region:     %s\n", cli_state_region_str( cfg->region ) );
    printf( "  Modulation: %s\n", cli_state_modulation_str( cfg->modulation ) );

    if( cfg->region != REGION_NONE )
    {
        printf( "  Frequency:  %.3f MHz\n", ( double ) cfg->freq_mhz );
        printf( "  Power:      %+d dBm\n", cfg->power_dbm );
#ifndef SX126X
        printf( "  AGC gain:   %s\n", cli_state_agc_gain_str( cfg->agc_gain ) );
        /* Mark whichever path the current frequency selects */
        const char* lf_tag = ( cfg->freq_mhz > 0.0f && cfg->freq_mhz < 1500.0f ) ? "  [active]" : "";
        const char* hf_tag = ( cfg->freq_mhz >= 1500.0f )                         ? "  [active]" : "";
        if( cfg->rx_boost_lf < 0 )
            printf( "  Boost LF:   auto (0)%s\n", lf_tag );
        else
            printf( "  Boost LF:   %d%s\n", cfg->rx_boost_lf, lf_tag );
        if( cfg->rx_boost_hf < 0 )
            printf( "  Boost HF:   auto (4)%s\n", hf_tag );
        else
            printf( "  Boost HF:   %d%s\n", cfg->rx_boost_hf, hf_tag );
#endif
    }

    const modulation_module_t* mod = modulation_get_by_id( cfg->modulation );
    if( mod != NULL )
    {
        mod->print_status( cfg );
    }

    printf( "  Mode:       %s", cli_state_mode_str( cfg->active_mode ) );
    if( cfg->active_mode != MODE_IDLE )
    {
        printf( " (active)" );
    }
    printf( "\n" );

    if( cfg->mod_pld_size > 0 )
    {
        printf( "  Mod TX pld: %u bytes\n", cfg->mod_pld_size );
    }
    else
    {
        printf( "  Mod TX pld: auto\n" );
    }

    /* PER config line */
    if( cfg->per_payload_size == 0 )
        printf( "  PER config: count=%u  interval=%ums  payload=auto\n",
                (unsigned) cfg->per_count, (unsigned) cfg->per_interval_ms );
    else
        printf( "  PER config: count=%u  interval=%ums  payload=%uB\n",
                (unsigned) cfg->per_count, (unsigned) cfg->per_interval_ms, (unsigned) cfg->per_payload_size );
}
