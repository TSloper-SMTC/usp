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
    cfg->invert_iq       = false;
    cfg->ldro            = -1; /* auto */

    /* FLRC packet param defaults */
    cfg->flrc_preamble     = FLRC_PREAMBLE_32;
    cfg->flrc_sw_len       = FLRC_SW_LEN_4;
    cfg->flrc_tx_sw        = 1; /* use syncword 1 */
    cfg->flrc_rx_sw        = 1; /* match syncword 1 */
    cfg->flrc_header_fixed = false; /* variable length */
    cfg->flrc_crc          = FLRC_CRC_2;
    cfg->flrc_syncword[0]  = 0xED;
    cfg->flrc_syncword[1]  = 0x59;
    cfg->flrc_syncword[2]  = 0x23;
    cfg->flrc_syncword[3]  = 0x98;

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
#if defined( LR20XX ) || defined( LR11XX )
    case CODING_RATE_LI_4_5:
        return "LI 4/5";
    case CODING_RATE_LI_4_6:
        return "LI 4/6";
    case CODING_RATE_LI_4_8:
        return "LI 4/8";
#endif
#if defined( LR20XX )
    case CODING_RATE_LI_CONV_4_6:
        return "LI-Conv 4/6";
    case CODING_RATE_LI_CONV_4_8:
        return "LI-Conv 4/8";
#endif
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

const char* cli_state_flrc_preamble_str( flrc_preamble_t p )
{
    switch( p )
    {
    case FLRC_PREAMBLE_4:   return "4";
    case FLRC_PREAMBLE_8:   return "8";
    case FLRC_PREAMBLE_12:  return "12";
    case FLRC_PREAMBLE_16:  return "16";
    case FLRC_PREAMBLE_20:  return "20";
    case FLRC_PREAMBLE_24:  return "24";
    case FLRC_PREAMBLE_28:  return "28";
    case FLRC_PREAMBLE_32:  return "32";
    default:                return "?";
    }
}

const char* cli_state_flrc_sw_len_str( flrc_sw_len_t sw )
{
    switch( sw )
    {
    case FLRC_SW_LEN_OFF: return "off";
    case FLRC_SW_LEN_2:   return "2 bytes";
    case FLRC_SW_LEN_4:   return "4 bytes";
    default:              return "?";
    }
}

const char* cli_state_flrc_crc_str( flrc_crc_t crc )
{
    switch( crc )
    {
    case FLRC_CRC_OFF: return "off";
    case FLRC_CRC_2:   return "2 bytes";
    case FLRC_CRC_3:   return "3 bytes";
    case FLRC_CRC_4:   return "4 bytes";
    default:           return "?";
    }
}

const char* cli_state_flrc_rx_sw_str( uint8_t rx_sw )
{
    switch( rx_sw )
    {
    case 0:  return "off";
    case 1:  return "sw1";
    case 2:  return "sw2";
    case 3:  return "sw1|2";
    case 4:  return "sw3";
    case 5:  return "sw1|3";
    case 6:  return "sw2|3";
    case 7:  return "sw1|2|3";
    default: return "?";
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
#if defined( LR20XX )
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
        {
            const char* tc_mode;
            switch( cfg->temp_comp_mode )
            {
            case 1:  tc_mode = "relative"; break;
            case 2:  tc_mode = "absolute"; break;
            default: tc_mode = "off"; break;
            }
            printf( "  Temp comp:  %s%s\n", tc_mode,
                    cfg->temp_comp_ntc ? " + NTC" : "" );
        }
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
        printf( "  PER config: count=%u  interval=%ums  payload=%u bytes\n",
                (unsigned) cfg->per_count, (unsigned) cfg->per_interval_ms, (unsigned) cfg->per_payload_size );
}
