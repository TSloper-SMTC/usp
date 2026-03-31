/**
 * @file      mod_flrc.c
 *
 * @brief     FLRC modulation module — parameters, defaults, validation
 *
 * FLRC (Fast Long Range Communication) is a proprietary high-speed modulation
 * available on the LR2021 chip only (not LR2022 or other variants).
 * The CLI gates FLRC at runtime via chip->supports_modulation().
 *
 * Supported regions: US902-928 and WW2G4 (2.4 GHz).
 * EU and JP may be added later.
 *
 * Parameters:
 *   br      <kbps>             Bitrate: 260, 325, 520, 650, 1040, 1300, 2080, 2600
 *   cr      <1/2|3/4|2/3|none> Coding rate
 *   bt      <off|bt0.5|bt1>    Pulse shape (BT filter)
 *
 * Default: 1300 kbps, CR 3/4, BT 0.5 (good spectral containment at 1.3 Mbps)
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "modulation.h"
#include "region.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * --- Per-region FLRC constraints ---
 */

typedef struct
{
    region_id_t region;

    /* Valid bitrate/bandwidth options (kbps, terminated by 0) */
    uint16_t br_options[9]; /* up to 8 values + terminator */

    /* Defaults */
    uint16_t  default_br_kbps;
    flrc_cr_t default_cr;
    flrc_bt_t default_bt;
} flrc_region_constraints_t;

static const flrc_region_constraints_t flrc_constraints[] = {
    {
        .region     = REGION_US,
        .br_options = { 260, 325, 520, 650, 1040, 1300, 2080, 2600, 0 },
        .default_br_kbps = 1300,
        .default_cr      = FLRC_CR_3_4,
        .default_bt      = FLRC_BT_0_5,
    },
    {
        .region     = REGION_WW2G4,
        .br_options = { 260, 325, 520, 650, 1040, 1300, 2080, 2600, 0 },
        .default_br_kbps = 1300,
        .default_cr      = FLRC_CR_3_4,
        .default_bt      = FLRC_BT_0_5,
    },
};

static const int num_flrc_constraints = sizeof( flrc_constraints ) / sizeof( flrc_constraints[0] );

static const flrc_region_constraints_t* flrc_get_constraints( region_id_t region )
{
    for( int i = 0; i < num_flrc_constraints; i++ )
    {
        if( flrc_constraints[i].region == region )
        {
            return &flrc_constraints[i];
        }
    }
    return NULL;
}

/*
 * --- Module callbacks ---
 */

static void flrc_load_defaults( radio_config_t* cfg, region_id_t region )
{
    const flrc_region_constraints_t* fc = flrc_get_constraints( region );
    if( fc == NULL )
    {
        printf( "ERROR: FLRC is not supported for this region. "
                "Supported regions: us, 2g4\n" );
        return; /* leave cfg->modulation unchanged — signals failure to caller */
    }

    cfg->modulation        = MODULATION_FLRC;
    cfg->flrc_br_kbps      = fc->default_br_kbps;
    cfg->flrc_cr           = fc->default_cr;
    cfg->flrc_bt           = fc->default_bt;
    cfg->flrc_preamble     = FLRC_PREAMBLE_32;
    cfg->flrc_sw_len       = FLRC_SW_LEN_4;
    cfg->flrc_tx_sw        = 1;
    cfg->flrc_rx_sw        = 1;
    cfg->flrc_header_fixed = false;
    cfg->flrc_crc          = FLRC_CRC_2;
    cfg->flrc_syncword[0]  = 0xED;
    cfg->flrc_syncword[1]  = 0x59;
    cfg->flrc_syncword[2]  = 0x23;
    cfg->flrc_syncword[3]  = 0x98;
}

static int flrc_set_param( radio_config_t* cfg, const region_def_t* region,
                            const char* name, const char* value )
{
    if( value == NULL || *value == '\0' )
    {
        /* Show valid options instead of a generic error.
         * We can't call flrc_print_param_help() here (defined later),
         * so print inline context-aware help. */
        const flrc_region_constraints_t* fc2 = flrc_get_constraints( cfg->region );
        if( strcasecmp( name, "br" ) == 0 )
        {
            printf( "br — FLRC bitrate (kbps).  Current: %u kbps\n", cfg->flrc_br_kbps );
            if( fc2 != NULL )
            {
                printf( "  Valid:" );
                for( int i = 0; fc2->br_options[i] != 0; i++ )
                {
                    printf( " %u", fc2->br_options[i] );
                }
                printf( "\n" );
            }
        }
        else if( strcasecmp( name, "cr" ) == 0 )
        {
            printf( "cr — FLRC coding rate.  Current: %s\n", cli_state_flrc_cr_str( cfg->flrc_cr ) );
            printf( "  Valid: 1/2, 2/3, 3/4, none\n" );
        }
        else if( strcasecmp( name, "bt" ) == 0 )
        {
            printf( "bt — FLRC pulse shape.  Current: %s\n", cli_state_flrc_bt_str( cfg->flrc_bt ) );
            printf( "  Valid: off, bt0.5, bt1\n" );
        }
        else if( strcasecmp( name, "preamble" ) == 0 )
        {
            printf( "preamble — FLRC preamble length (bits).  Current: %s bits\n",
                    cli_state_flrc_preamble_str( cfg->flrc_preamble ) );
            printf( "  Valid: 4, 8, 12, 16, 20, 24, 28, 32\n" );
        }
        else if( strcasecmp( name, "sw_len" ) == 0 )
        {
            printf( "sw_len — FLRC syncword length.  Current: %s\n",
                    cli_state_flrc_sw_len_str( cfg->flrc_sw_len ) );
            printf( "  Valid: off, 2, 4 (bytes)\n" );
        }
        else if( strcasecmp( name, "tx_sw" ) == 0 )
        {
            printf( "tx_sw — FLRC TX syncword index.  Current: %s\n",
                    cfg->flrc_tx_sw == 0 ? "off" : ( cfg->flrc_tx_sw == 1 ? "sw1" :
                    ( cfg->flrc_tx_sw == 2 ? "sw2" : "sw3" ) ) );
            printf( "  Valid: off, 1, 2, 3\n" );
        }
        else if( strcasecmp( name, "rx_sw" ) == 0 )
        {
            printf( "rx_sw — FLRC RX syncword match.  Current: %s\n",
                    cli_state_flrc_rx_sw_str( cfg->flrc_rx_sw ) );
            printf( "  Valid: off, 1-7 (bitmask: 1=sw1 2=sw2 4=sw3)\n" );
        }
        else if( strcasecmp( name, "header" ) == 0 )
        {
            printf( "header — FLRC header type.  Current: %s\n",
                    cfg->flrc_header_fixed ? "fixed" : "variable" );
            printf( "  Valid: variable, fixed\n" );
        }
        else if( strcasecmp( name, "crc" ) == 0 )
        {
            printf( "crc — FLRC CRC type.  Current: %s\n",
                    cli_state_flrc_crc_str( cfg->flrc_crc ) );
            printf( "  Valid: off, 2, 3, 4 (bytes)\n" );
        }
        else if( strcasecmp( name, "syncword" ) == 0 )
        {
            printf( "syncword — FLRC syncword bytes (register 1).  Current: 0x%02X%02X%02X%02X\n",
                    cfg->flrc_syncword[0], cfg->flrc_syncword[1],
                    cfg->flrc_syncword[2], cfg->flrc_syncword[3] );
            printf( "  Valid: 1-8 hex digits (zero-padded to 4 bytes)\n" );
        }
        else
        {
            printf( "ERROR: Missing value for '%s'\n", name );
        }
        return -1;
    }

    if( cfg->region == REGION_NONE )
    {
        printf( "ERROR: Region not set.\n" );
        return -1;
    }

    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    const flrc_region_constraints_t* fc = flrc_get_constraints( cfg->region );
    if( fc == NULL )
    {
        printf( "ERROR: No FLRC constraints for current region\n" );
        return -1;
    }

    /* --- br --- */
    if( strcasecmp( name, "br" ) == 0 )
    {
        char*    endptr;
        uint16_t br = ( uint16_t ) strtoul( value, &endptr, 10 );
        if( *endptr != '\0' )
        {
            printf( "ERROR: Invalid bitrate value (use kbps integer, e.g. 1300)\n" );
            return -1;
        }

        /* Validate against the allowed list */
        bool found = false;
        for( int i = 0; fc->br_options[i] != 0; i++ )
        {
            if( fc->br_options[i] == br )
            {
                found = true;
                break;
            }
        }
        if( !found )
        {
            printf( "ERROR: %u kbps not valid for %s. Valid: 260, 325, 520, 650, 1040, 1300, 2080, 2600\n",
                    br, region->short_name );
            return -1;
        }

        cfg->flrc_br_kbps = br;
        printf( "FLRC bitrate set: %u kbps\n", br );
        return 0;
    }

    /* --- cr --- */
    if( strcasecmp( name, "cr" ) == 0 )
    {
        if( strcmp( value, "1/2" ) == 0 )
        {
            cfg->flrc_cr = FLRC_CR_1_2;
        }
        else if( strcmp( value, "3/4" ) == 0 )
        {
            cfg->flrc_cr = FLRC_CR_3_4;
        }
        else if( strcmp( value, "2/3" ) == 0 )
        {
            cfg->flrc_cr = FLRC_CR_2_3;
        }
        else if( strcasecmp( value, "none" ) == 0 )
        {
            cfg->flrc_cr = FLRC_CR_NONE;
        }
        else
        {
            printf( "ERROR: Invalid FLRC coding rate. Valid: 1/2, 2/3, 3/4, none\n" );
            return -1;
        }

        printf( "FLRC CR set: %s\n", cli_state_flrc_cr_str( cfg->flrc_cr ) );
        return 0;
    }

    /* --- bt --- */
    if( strcasecmp( name, "bt" ) == 0 )
    {
        if( strcasecmp( value, "off" ) == 0 )
        {
            cfg->flrc_bt = FLRC_BT_OFF;
        }
        else if( strcmp( value, "bt0.5" ) == 0 || strcmp( value, "0.5" ) == 0 )
        {
            cfg->flrc_bt = FLRC_BT_0_5;
        }
        else if( strcmp( value, "bt1" ) == 0 || strcmp( value, "1" ) == 0 )
        {
            cfg->flrc_bt = FLRC_BT_1;
        }
        else
        {
            printf( "ERROR: Invalid pulse shape. Valid: off, bt0.5, bt1\n" );
            return -1;
        }

        printf( "FLRC BT set: %s\n", cli_state_flrc_bt_str( cfg->flrc_bt ) );
        return 0;
    }

    /* --- preamble --- */
    if( strcasecmp( name, "preamble" ) == 0 )
    {
        char*   endptr;
        uint8_t bits = ( uint8_t ) strtoul( value, &endptr, 10 );
        if( *endptr != '\0' )
        {
            printf( "ERROR: Invalid preamble value\n" );
            return -1;
        }
        switch( bits )
        {
        case 4:   cfg->flrc_preamble = FLRC_PREAMBLE_4;  break;
        case 8:   cfg->flrc_preamble = FLRC_PREAMBLE_8;  break;
        case 12:  cfg->flrc_preamble = FLRC_PREAMBLE_12; break;
        case 16:  cfg->flrc_preamble = FLRC_PREAMBLE_16; break;
        case 20:  cfg->flrc_preamble = FLRC_PREAMBLE_20; break;
        case 24:  cfg->flrc_preamble = FLRC_PREAMBLE_24; break;
        case 28:  cfg->flrc_preamble = FLRC_PREAMBLE_28; break;
        case 32:  cfg->flrc_preamble = FLRC_PREAMBLE_32; break;
        default:
            printf( "ERROR: Invalid preamble. Valid: 4, 8, 12, 16, 20, 24, 28, 32 (bits)\n" );
            return -1;
        }
        printf( "FLRC preamble set: %s bits\n", cli_state_flrc_preamble_str( cfg->flrc_preamble ) );
        return 0;
    }

    /* --- sw_len --- */
    if( strcasecmp( name, "sw_len" ) == 0 )
    {
        if( strcasecmp( value, "off" ) == 0 )
            cfg->flrc_sw_len = FLRC_SW_LEN_OFF;
        else if( strcmp( value, "2" ) == 0 )
            cfg->flrc_sw_len = FLRC_SW_LEN_2;
        else if( strcmp( value, "4" ) == 0 )
            cfg->flrc_sw_len = FLRC_SW_LEN_4;
        else
        {
            printf( "ERROR: Invalid syncword length. Valid: off, 2, 4 (bytes)\n" );
            return -1;
        }
        printf( "FLRC syncword length set: %s\n", cli_state_flrc_sw_len_str( cfg->flrc_sw_len ) );
        return 0;
    }

    /* --- tx_sw --- */
    if( strcasecmp( name, "tx_sw" ) == 0 )
    {
        if( strcasecmp( value, "off" ) == 0 || strcasecmp( value, "none" ) == 0 || strcmp( value, "0" ) == 0 )
            cfg->flrc_tx_sw = 0;
        else if( strcmp( value, "1" ) == 0 )
            cfg->flrc_tx_sw = 1;
        else if( strcmp( value, "2" ) == 0 )
            cfg->flrc_tx_sw = 2;
        else if( strcmp( value, "3" ) == 0 )
            cfg->flrc_tx_sw = 3;
        else
        {
            printf( "ERROR: Invalid TX syncword. Valid: off, 1, 2, 3\n" );
            return -1;
        }
        printf( "FLRC TX syncword set: %s\n", cfg->flrc_tx_sw == 0 ? "off" : ( cfg->flrc_tx_sw == 1 ? "sw1" :
                ( cfg->flrc_tx_sw == 2 ? "sw2" : "sw3" ) ) );
        return 0;
    }

    /* --- rx_sw --- */
    if( strcasecmp( name, "rx_sw" ) == 0 )
    {
        if( strcasecmp( value, "off" ) == 0 || strcmp( value, "0" ) == 0 )
            cfg->flrc_rx_sw = 0;
        else
        {
            char*   endptr;
            uint8_t v = ( uint8_t ) strtoul( value, &endptr, 10 );
            if( *endptr != '\0' || v > 7 )
            {
                printf( "ERROR: Invalid RX syncword match. Valid: off, 1-7\n" );
                printf( "  1=sw1  2=sw2  3=sw1|2  4=sw3  5=sw1|3  6=sw2|3  7=sw1|2|3\n" );
                return -1;
            }
            cfg->flrc_rx_sw = v;
        }
        printf( "FLRC RX syncword match set: %s\n", cli_state_flrc_rx_sw_str( cfg->flrc_rx_sw ) );
        return 0;
    }

    /* --- header --- */
    if( strcasecmp( name, "header" ) == 0 )
    {
        if( strcasecmp( value, "variable" ) == 0 )
            cfg->flrc_header_fixed = false;
        else if( strcasecmp( value, "fixed" ) == 0 )
            cfg->flrc_header_fixed = true;
        else
        {
            printf( "ERROR: Invalid header type. Valid: variable, fixed\n" );
            return -1;
        }
        printf( "FLRC header set: %s\n", cfg->flrc_header_fixed ? "fixed" : "variable" );
        return 0;
    }

    /* --- crc --- */
    if( strcasecmp( name, "crc" ) == 0 )
    {
        if( strcasecmp( value, "off" ) == 0 )
            cfg->flrc_crc = FLRC_CRC_OFF;
        else if( strcmp( value, "2" ) == 0 )
            cfg->flrc_crc = FLRC_CRC_2;
        else if( strcmp( value, "3" ) == 0 )
            cfg->flrc_crc = FLRC_CRC_3;
        else if( strcmp( value, "4" ) == 0 )
            cfg->flrc_crc = FLRC_CRC_4;
        else
        {
            printf( "ERROR: Invalid CRC type. Valid: off, 2, 3, 4 (bytes)\n" );
            return -1;
        }
        printf( "FLRC CRC set: %s\n", cli_state_flrc_crc_str( cfg->flrc_crc ) );
        return 0;
    }

    /* --- syncword --- */
    if( strcasecmp( name, "syncword" ) == 0 )
    {
        /* Parse 1-8 hex digits, zero-pad to 4 bytes from the left */
        const char* p = value;
        if( p[0] == '0' && ( p[1] == 'x' || p[1] == 'X' ) )
        {
            p += 2;
        }
        size_t len = strlen( p );
        if( len == 0 || len > 8 )
        {
            printf( "ERROR: Syncword must be 1-8 hex digits (e.g. ED592398)\n" );
            return -1;
        }
        char* endptr;
        uint32_t val = ( uint32_t ) strtoul( p, &endptr, 16 );
        if( *endptr != '\0' )
        {
            printf( "ERROR: Invalid hex value '%s'\n", value );
            return -1;
        }
        cfg->flrc_syncword[0] = ( uint8_t ) ( ( val >> 24 ) & 0xFF );
        cfg->flrc_syncword[1] = ( uint8_t ) ( ( val >> 16 ) & 0xFF );
        cfg->flrc_syncword[2] = ( uint8_t ) ( ( val >>  8 ) & 0xFF );
        cfg->flrc_syncword[3] = ( uint8_t ) ( val & 0xFF );
        printf( "FLRC syncword set: 0x%02X%02X%02X%02X\n",
                cfg->flrc_syncword[0], cfg->flrc_syncword[1],
                cfg->flrc_syncword[2], cfg->flrc_syncword[3] );
        return 0;
    }

    printf( "ERROR: Unknown FLRC parameter '%s'\n", name );
    return -1;
}

static bool flrc_owns_param( const char* name )
{
    return strcasecmp( name, "br" ) == 0 ||
           strcasecmp( name, "cr" ) == 0 ||
           strcasecmp( name, "bt" ) == 0 ||
           strcasecmp( name, "preamble" ) == 0 ||
           strcasecmp( name, "sw_len" ) == 0 ||
           strcasecmp( name, "tx_sw" ) == 0 ||
           strcasecmp( name, "rx_sw" ) == 0 ||
           strcasecmp( name, "header" ) == 0 ||
           strcasecmp( name, "crc" ) == 0 ||
           strcasecmp( name, "syncword" ) == 0;
}

static void flrc_print_status( const radio_config_t* cfg )
{
    printf( "  Bitrate:    %u kbps\n", cfg->flrc_br_kbps );
    printf( "  CR:         %s\n", cli_state_flrc_cr_str( cfg->flrc_cr ) );
    printf( "  BT:         %s\n", cli_state_flrc_bt_str( cfg->flrc_bt ) );
    printf( "  Preamble:   %s bits\n", cli_state_flrc_preamble_str( cfg->flrc_preamble ) );
    printf( "  SW length:  %s\n", cli_state_flrc_sw_len_str( cfg->flrc_sw_len ) );
    printf( "  TX SW:      %s\n", cfg->flrc_tx_sw == 0 ? "off" : ( cfg->flrc_tx_sw == 1 ? "sw1" :
                                  ( cfg->flrc_tx_sw == 2 ? "sw2" : "sw3" ) ) );
    printf( "  RX SW:      %s\n", cli_state_flrc_rx_sw_str( cfg->flrc_rx_sw ) );
    printf( "  Header:     %s\n", cfg->flrc_header_fixed ? "fixed" : "variable" );
    printf( "  CRC:        %s\n", cli_state_flrc_crc_str( cfg->flrc_crc ) );
    printf( "  Syncword:   0x%02X%02X%02X%02X\n",
            cfg->flrc_syncword[0], cfg->flrc_syncword[1],
            cfg->flrc_syncword[2], cfg->flrc_syncword[3] );
}

static void flrc_print_help( void )
{
    printf( "\nFLRC parameters:\n" );
    printf( "  br <kbps>                  Bitrate\n" );
    printf( "  cr <1/2|2/3|3/4|none>      Coding rate\n" );
    printf( "  bt <off|bt0.5|bt1>         Pulse shape filter\n" );
    printf( "  preamble <bits>            Preamble length (4-32, step 4)\n" );
    printf( "  sw_len <off|2|4>           Syncword length (bytes)\n" );
    printf( "  tx_sw <off|1|2|3>          TX syncword index\n" );
    printf( "  rx_sw <off|1-7>            RX syncword match\n" );
    printf( "  header <variable|fixed>    Header type\n" );
    printf( "  crc <off|2|3|4>            CRC length (bytes)\n" );
    printf( "  syncword <hex>             Syncword bytes (e.g. ED592398)\n" );
}

static bool flrc_print_param_help( const char* param_name, const radio_config_t* cfg )
{
    if( strcasecmp( param_name, "br" ) == 0 )
    {
        printf( "br — FLRC bitrate (kbps)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
        {
            printf( "  Current: %u kbps\n", cfg->flrc_br_kbps );
        }
        printf( "  Valid: 260, 325, 520, 650, 1040, 1300, 2080, 2600\n" );
        printf( "  Example: br 1300\n" );
        return true;
    }

    if( strcasecmp( param_name, "cr" ) == 0 )
    {
        printf( "cr — FLRC coding rate\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
        {
            printf( "  Current: %s\n", cli_state_flrc_cr_str( cfg->flrc_cr ) );
        }
        printf( "  Valid: 1/2, 2/3, 3/4, none\n" );
        printf( "    none = no forward error correction (highest throughput)\n" );
        printf( "  Example: cr 3/4\n" );
        return true;
    }

    if( strcasecmp( param_name, "bt" ) == 0 )
    {
        printf( "bt — FLRC pulse shape (Gaussian BT filter)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
        {
            printf( "  Current: %s\n", cli_state_flrc_bt_str( cfg->flrc_bt ) );
        }
        printf( "  Valid: off, bt0.5, bt1\n" );
        printf( "    bt0.5 = good spectral containment (recommended)\n" );
        printf( "    bt1   = moderate filtering\n" );
        printf( "    off   = no filtering (widest bandwidth)\n" );
        printf( "  Example: bt bt0.5\n" );
        return true;
    }

    if( strcasecmp( param_name, "preamble" ) == 0 )
    {
        printf( "preamble — FLRC preamble length (bits)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
            printf( "  Current: %s bits\n", cli_state_flrc_preamble_str( cfg->flrc_preamble ) );
        printf( "  Valid: 4, 8, 12, 16, 20, 24, 28, 32\n" );
        printf( "  Example: preamble 32\n" );
        return true;
    }

    if( strcasecmp( param_name, "sw_len" ) == 0 )
    {
        printf( "sw_len — FLRC syncword length (bytes)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
            printf( "  Current: %s\n", cli_state_flrc_sw_len_str( cfg->flrc_sw_len ) );
        printf( "  Valid: off, 2, 4\n" );
        printf( "  Example: sw_len 4\n" );
        return true;
    }

    if( strcasecmp( param_name, "tx_sw" ) == 0 )
    {
        printf( "tx_sw — TX syncword index (which syncword register to transmit)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
            printf( "  Current: %s\n", cfg->flrc_tx_sw == 0 ? "off" : ( cfg->flrc_tx_sw == 1 ? "sw1" :
                    ( cfg->flrc_tx_sw == 2 ? "sw2" : "sw3" ) ) );
        printf( "  Valid: off, 1, 2, 3\n" );
        printf( "  Example: tx_sw 1\n" );
        return true;
    }

    if( strcasecmp( param_name, "rx_sw" ) == 0 )
    {
        printf( "rx_sw — RX syncword match filter (which syncwords to accept)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
            printf( "  Current: %s\n", cli_state_flrc_rx_sw_str( cfg->flrc_rx_sw ) );
        printf( "  Valid: off, 1-7 (bitmask: 1=sw1 2=sw2 4=sw3, e.g. 3=sw1|sw2)\n" );
        printf( "  Example: rx_sw 1\n" );
        return true;
    }

    if( strcasecmp( param_name, "header" ) == 0 )
    {
        printf( "header — FLRC packet length mode\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
            printf( "  Current: %s\n", cfg->flrc_header_fixed ? "fixed" : "variable" );
        printf( "  Valid: variable, fixed\n" );
        printf( "    variable = length in header (default)\n" );
        printf( "    fixed    = both sides must agree on payload length\n" );
        printf( "  Example: header variable\n" );
        return true;
    }

    if( strcasecmp( param_name, "crc" ) == 0 )
    {
        printf( "crc — FLRC CRC length (bytes)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
            printf( "  Current: %s\n", cli_state_flrc_crc_str( cfg->flrc_crc ) );
        printf( "  Valid: off, 2, 3, 4\n" );
        printf( "  Example: crc 2\n" );
        return true;
    }

    if( strcasecmp( param_name, "syncword" ) == 0 )
    {
        printf( "syncword — FLRC syncword bytes (register 1, 4 bytes max)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_FLRC )
            printf( "  Current: 0x%02X%02X%02X%02X\n",
                    cfg->flrc_syncword[0], cfg->flrc_syncword[1],
                    cfg->flrc_syncword[2], cfg->flrc_syncword[3] );
        printf( "  Enter 1-8 hex digits, zero-padded from the left to 4 bytes.\n" );
        printf( "  Both units must use the same syncword for FLRC communication.\n" );
        printf( "  Example: syncword ED592398\n" );
        return true;
    }

    return false;
}

static const char* const flrc_param_names[] = {
    "br", "cr", "bt", "preamble", "sw_len", "tx_sw", "rx_sw", "header", "crc", "syncword", NULL
};

static const char* const cr_completions[]       = { "1/2", "2/3", "3/4", "none", NULL };
static const char* const bt_completions[]       = { "off", "bt0.5", "bt1", NULL };
static const char* const br_completions[]       = { "260", "325", "520", "650", "1040", "1300", "2080", "2600", NULL };
static const char* const preamble_completions[] = { "4", "8", "12", "16", "20", "24", "28", "32", NULL };
static const char* const sw_len_completions[]   = { "off", "2", "4", NULL };
static const char* const tx_sw_completions[]    = { "off", "1", "2", "3", NULL };
static const char* const rx_sw_completions[]    = { "off", "1", "2", "3", "4", "5", "6", "7", NULL };
static const char* const header_completions[]   = { "variable", "fixed", NULL };
static const char* const crc_completions[]      = { "off", "2", "3", "4", NULL };

static const char* const* flrc_get_completions( const char* param_name,
                                                 const radio_config_t* cfg )
{
    ( void ) cfg;
    if( strcasecmp( param_name, "cr" ) == 0 )       return cr_completions;
    if( strcasecmp( param_name, "bt" ) == 0 )       return bt_completions;
    if( strcasecmp( param_name, "br" ) == 0 )       return br_completions;
    if( strcasecmp( param_name, "preamble" ) == 0 ) return preamble_completions;
    if( strcasecmp( param_name, "sw_len" ) == 0 )   return sw_len_completions;
    if( strcasecmp( param_name, "tx_sw" ) == 0 )    return tx_sw_completions;
    if( strcasecmp( param_name, "rx_sw" ) == 0 )    return rx_sw_completions;
    if( strcasecmp( param_name, "header" ) == 0 )   return header_completions;
    if( strcasecmp( param_name, "crc" ) == 0 )      return crc_completions;
    return NULL;
}

static const char* flrc_get_hint( const char* param_name, const radio_config_t* cfg )
{
    ( void ) cfg;
    if( strcasecmp( param_name, "br" ) == 0 )       return "<260|325|520|650|1040|1300|2080|2600>";
    if( strcasecmp( param_name, "cr" ) == 0 )       return "<1/2|2/3|3/4|none>";
    if( strcasecmp( param_name, "bt" ) == 0 )       return "<off|bt0.5|bt1>";
    if( strcasecmp( param_name, "preamble" ) == 0 ) return "<4|8|12|16|20|24|28|32>";
    if( strcasecmp( param_name, "sw_len" ) == 0 )   return "<off|2|4>";
    if( strcasecmp( param_name, "tx_sw" ) == 0 )    return "<off|1|2|3>";
    if( strcasecmp( param_name, "rx_sw" ) == 0 )    return "<off|1-7>";
    if( strcasecmp( param_name, "header" ) == 0 )   return "<variable|fixed>";
    if( strcasecmp( param_name, "crc" ) == 0 )      return "<off|2|3|4>";
    if( strcasecmp( param_name, "syncword" ) == 0 ) return "<hex, e.g. ED592398>";
    return NULL;
}

/*
 * --- Module instance ---
 */

const modulation_module_t mod_flrc = {
    .name             = "flrc",
    .id               = MODULATION_FLRC,
    .load_defaults    = flrc_load_defaults,
    .set_param        = flrc_set_param,
    .owns_param       = flrc_owns_param,
    .print_status     = flrc_print_status,
    .print_help       = flrc_print_help,
    .print_param_help = flrc_print_param_help,
    .param_names      = flrc_param_names,
    .get_completions  = flrc_get_completions,
    .get_hint         = flrc_get_hint,
};
