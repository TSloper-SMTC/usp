/**
 * @file      mod_lora.c
 *
 * @brief     LoRa modulation module — parameters, defaults, validation
 *
 * Per-region LoRa constraints (BW options, SF ranges, defaults) live here
 * instead of in region.c, so the region layer stays modulation-agnostic.
 *
 * Default values are derived from LoRaWAN Regional Parameters specification:
 *   - US: DR0 = SF10/125kHz, ch1 = 902.3 MHz
 *   - EU: DR0 = SF12/125kHz, ch1 = 868.1 MHz
 *   - JP: DR2 = SF10/125kHz, ch1 = 923.2 MHz
 *   - WW2G4: DR0 = SF12/812kHz, default freq = 2423.0 MHz
 *     BW is 812 kHz (LR20XX_RADIO_LORA_BW_812), the chip's closest match
 *     to the WW-2G4 spec's 800 kHz channel bandwidth.
 *     Sync word 0x21 is the WW-2G4 public network value (not 0x34).
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "modulation.h"
#include "region.h"

/* LBM region defs — authoritative source for LoRaWAN sync word constants */
#include "region_us_915_defs.h"
#include "region_eu_868_defs.h"
#include "region_as_923_defs.h"
#include "region_ww_2g4_defs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * --- Per-region LoRa constraints ---
 */

typedef struct
{
    region_id_t region;

    /* Bandwidth options (terminated by 0) */
    uint16_t bw_options[4];

    /* SF range per bandwidth index: [0]=bw_options[0], [1]=bw_options[1], ... */
    uint8_t sf_min[4];
    uint8_t sf_max[4];

    /* Defaults */
    uint16_t      default_bw_khz;
    uint8_t       default_sf;
    coding_rate_t default_cr;
    uint16_t      default_preamble;
    uint8_t       default_syncword;
} lora_region_constraints_t;

static const lora_region_constraints_t lora_constraints[] = {
    {
        .region     = REGION_US,
        .bw_options = { 125, 250, 500, 0 },
        .sf_min     = { 5, 5, 5, 0 },
        .sf_max     = { 10, 12, 12, 0 },
        .default_bw_khz    = 125,
        .default_sf         = 10,
        .default_cr         = CODING_RATE_4_5,
        .default_preamble   = 8,
        .default_syncword   = SYNC_WORD_PUBLIC_US_915,
    },
    {
        .region     = REGION_EU,
        .bw_options = { 125, 250, 0, 0 },
        .sf_min     = { 7, 7, 0, 0 },
        .sf_max     = { 12, 12, 0, 0 },
        .default_bw_khz    = 125,
        .default_sf         = 12,
        .default_cr         = CODING_RATE_4_5,
        .default_preamble   = 8,
        .default_syncword   = SYNC_WORD_PUBLIC_EU_868,
    },
    {
        .region     = REGION_JP,
        .bw_options = { 125, 250, 0, 0 },
        .sf_min     = { 7, 7, 0, 0 },
        .sf_max     = { 12, 12, 0, 0 },
        .default_bw_khz    = 125,
        .default_sf         = 10,
        .default_cr         = CODING_RATE_4_5,
        .default_preamble   = 8,
        .default_syncword   = SYNC_WORD_PUBLIC_AS_923,
    },
#ifndef SX126X
    {
        /* WW-2G4: 800 kHz BW (BW_812 is the chip's closest enum).
         * Full SF5-SF12 supported.  SYNC_WORD_PUBLIC_WW_2G4 = 0x21 (not 0x34). */
        .region     = REGION_WW2G4,
        .bw_options = { 812, 0, 0, 0 },
        .sf_min     = { 5, 0, 0, 0 },
        .sf_max     = { 12, 0, 0, 0 },
        .default_bw_khz    = 812,
        .default_sf         = 12,
        .default_cr         = CODING_RATE_4_5,
        .default_preamble   = 8,
        .default_syncword   = SYNC_WORD_PUBLIC_WW_2G4,
    },
#endif /* SX126X */
};

static const int num_lora_constraints = sizeof( lora_constraints ) / sizeof( lora_constraints[0] );

static const lora_region_constraints_t* lora_get_constraints( region_id_t region )
{
    for( int i = 0; i < num_lora_constraints; i++ )
    {
        if( lora_constraints[i].region == region )
        {
            return &lora_constraints[i];
        }
    }
    return NULL;
}

/*
 * --- BW / SF validation (moved from region.c) ---
 */

static const char* lora_validate_bw( const lora_region_constraints_t* lc,
                                      const region_def_t* region,
                                      uint16_t bw_khz )
{
    static char errbuf[128];
    for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
    {
        if( lc->bw_options[i] == bw_khz )
        {
            return NULL;
        }
    }
    snprintf( errbuf, sizeof( errbuf ), "%u kHz not valid for %s",
              bw_khz, region->short_name );
    return errbuf;
}

static const char* lora_validate_sf( const lora_region_constraints_t* lc,
                                      const region_def_t* region,
                                      uint16_t bw_khz, uint8_t sf )
{
    static char errbuf[128];

    int bw_idx = -1;
    for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
    {
        if( lc->bw_options[i] == bw_khz )
        {
            bw_idx = i;
            break;
        }
    }

    if( bw_idx < 0 )
    {
        snprintf( errbuf, sizeof( errbuf ), "BW %u kHz not valid for %s",
                  bw_khz, region->short_name );
        return errbuf;
    }

    if( sf < lc->sf_min[bw_idx] || sf > lc->sf_max[bw_idx] )
    {
        snprintf( errbuf, sizeof( errbuf ),
                  "SF%u not valid for %s at %u kHz BW (valid: SF%u-SF%u)",
                  sf, region->short_name, bw_khz,
                  lc->sf_min[bw_idx], lc->sf_max[bw_idx] );
        return errbuf;
    }
    return NULL;
}

/* Forward declarations */
static bool lora_print_param_help( const char* param_name, const radio_config_t* cfg );
static const char* lora_bw_hint( const radio_config_t* cfg );
static const char* lora_sf_hint( const radio_config_t* cfg );
static const char* const* lora_bw_completions( const radio_config_t* cfg );

/*
 * --- Module callbacks ---
 */

static void lora_load_defaults( radio_config_t* cfg, region_id_t region )
{
    const lora_region_constraints_t* lc = lora_get_constraints( region );
    if( lc == NULL )
    {
        return;
    }

    cfg->modulation = MODULATION_LORA;
    cfg->bw_khz     = lc->default_bw_khz;
    cfg->sf          = lc->default_sf;
    cfg->cr          = lc->default_cr;
    cfg->preamble    = lc->default_preamble;
    cfg->syncword    = lc->default_syncword;
    cfg->header_implicit = false;
    cfg->crc_on          = true;
}

static int lora_set_param( radio_config_t* cfg, const region_def_t* region,
                            const char* name, const char* value )
{
    if( value == NULL || *value == '\0' )
    {
        /* Show valid options instead of a generic error */
        lora_print_param_help( name, cfg );
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

    const lora_region_constraints_t* lc = lora_get_constraints( cfg->region );
    if( lc == NULL )
    {
        printf( "ERROR: No LoRa constraints for current region\n" );
        return -1;
    }

    /* --- bw --- */
    if( strcasecmp( name, "bw" ) == 0 )
    {
        char*         endptr;
        unsigned long bw_raw = strtoul( value, &endptr, 10 );
        if( *endptr != '\0' || bw_raw > 65535 )
        {
            printf( "ERROR: Invalid bandwidth value\n" );
            return -1;
        }
        uint16_t bw = ( uint16_t ) bw_raw;

        const char* err = lora_validate_bw( lc, region, bw );
        if( err )
        {
            printf( "ERROR: %s\n", err );
            return -1;
        }

        /* Warn if current SF may not be valid at new BW */
        err = lora_validate_sf( lc, region, bw, cfg->sf );
        if( err )
        {
            printf( "WARNING: Current SF%u may not be valid at %u kHz. Use 'sf' to adjust.\n",
                    cfg->sf, bw );
        }

        cfg->bw_khz = bw;
        printf( "BW set: %u kHz\n", bw );
        return 0;
    }

    /* --- sf --- */
    if( strcasecmp( name, "sf" ) == 0 )
    {
        if( cfg->modulation == MODULATION_NONE )
        {
            printf( "ERROR: Modulation not set.\n" );
            return -1;
        }

        char*   endptr;
        uint8_t sf = ( uint8_t ) strtoul( value, &endptr, 10 );
        if( *endptr != '\0' )
        {
            printf( "ERROR: Invalid SF value\n" );
            return -1;
        }

        const char* err = lora_validate_sf( lc, region, cfg->bw_khz, sf );
        if( err )
        {
            printf( "ERROR: %s\n", err );
            return -1;
        }

        cfg->sf = sf;
        printf( "SF set: %u\n", sf );
        return 0;
    }

    /* --- cr --- */
    if( strcasecmp( name, "cr" ) == 0 )
    {
        if( strcmp( value, "4/5" ) == 0 )
        {
            cfg->cr = CODING_RATE_4_5;
        }
        else if( strcmp( value, "4/6" ) == 0 )
        {
            cfg->cr = CODING_RATE_4_6;
        }
        else if( strcmp( value, "4/7" ) == 0 )
        {
            cfg->cr = CODING_RATE_4_7;
        }
        else if( strcmp( value, "4/8" ) == 0 )
        {
            cfg->cr = CODING_RATE_4_8;
        }
        else
        {
            printf( "ERROR: Invalid coding rate. Valid: 4/5, 4/6, 4/7, 4/8\n" );
            return -1;
        }

        printf( "CR set: %s\n", cli_state_cr_str( cfg->cr ) );
        return 0;
    }

    /* --- preamble --- */
    if( strcasecmp( name, "preamble" ) == 0 )
    {
        char*         endptr;
        unsigned long preamble_raw = strtoul( value, &endptr, 10 );
        if( *endptr != '\0' || preamble_raw < 4 || preamble_raw > 65535 )
        {
            printf( "ERROR: Invalid preamble length (4-65535)\n" );
            return -1;
        }
        uint16_t preamble = ( uint16_t ) preamble_raw;

        cfg->preamble = preamble;
        printf( "Preamble set: %u symbols\n", preamble );
        return 0;
    }

    /* --- syncword --- */
    if( strcasecmp( name, "syncword" ) == 0 )
    {
        char*         endptr;
        unsigned long sw_raw = strtoul( value, &endptr, 16 );
        if( *endptr != '\0' || sw_raw > 0xFF )
        {
            printf( "ERROR: Invalid sync word (use hex, e.g. 0x12 or 0x34)\n" );
            return -1;
        }
        uint8_t sw = ( uint8_t ) sw_raw;

        cfg->syncword = sw;
        printf( "Sync word set: 0x%02X\n", sw );
        return 0;
    }

    /* --- header --- */
    if( strcasecmp( name, "header" ) == 0 )
    {
        if( strcasecmp( value, "implicit" ) == 0 )
            cfg->header_implicit = true;
        else if( strcasecmp( value, "explicit" ) == 0 )
            cfg->header_implicit = false;
        else
        {
            printf( "ERROR: Invalid header type. Valid: implicit, explicit\n" );
            return -1;
        }
        printf( "Header: %s\n", cfg->header_implicit ? "implicit" : "explicit" );
        return 0;
    }

    /* --- crc --- */
    if( strcasecmp( name, "crc" ) == 0 )
    {
        if( strcasecmp( value, "on" ) == 0 )
            cfg->crc_on = true;
        else if( strcasecmp( value, "off" ) == 0 )
            cfg->crc_on = false;
        else
        {
            printf( "ERROR: Invalid CRC setting. Valid: on, off\n" );
            return -1;
        }
        printf( "CRC: %s\n", cfg->crc_on ? "on" : "off" );
        return 0;
    }

#ifdef SX126X
    /* --- ldro --- */
    if( strcasecmp( name, "ldro" ) == 0 )
    {
        if( strcasecmp( value, "auto" ) == 0 )
            cfg->ldro = -1;
        else if( strcasecmp( value, "on" ) == 0 )
            cfg->ldro = 1;
        else if( strcasecmp( value, "off" ) == 0 )
            cfg->ldro = 0;
        else
        {
            printf( "ERROR: Invalid LDRO setting. Valid: auto, on, off\n" );
            return -1;
        }
        printf( "LDRO: %s\n", cfg->ldro < 0 ? "auto" : ( cfg->ldro ? "on" : "off" ) );
        return 0;
    }
#endif

    printf( "ERROR: Unknown LoRa parameter '%s'\n", name );
    return -1;
}

static bool lora_owns_param( const char* name )
{
    return strcasecmp( name, "bw" ) == 0 ||
           strcasecmp( name, "sf" ) == 0 ||
           strcasecmp( name, "cr" ) == 0 ||
           strcasecmp( name, "preamble" ) == 0 ||
           strcasecmp( name, "syncword" ) == 0 ||
           strcasecmp( name, "header" ) == 0 ||
           strcasecmp( name, "crc" ) == 0 ||
#ifdef SX126X
           strcasecmp( name, "ldro" ) == 0 ||
#endif
           false;
}

static void lora_print_status( const radio_config_t* cfg )
{
    printf( "  BW:         %u kHz\n", cfg->bw_khz );
    printf( "  SF:         %u\n", cfg->sf );
    printf( "  CR:         %s\n", cli_state_cr_str( cfg->cr ) );
    printf( "  Preamble:   %u symbols\n", cfg->preamble );
    printf( "  Sync word:  0x%02X\n", cfg->syncword );
    printf( "  Header:     %s\n", cfg->header_implicit ? "implicit" : "explicit" );
    printf( "  CRC:        %s\n", cfg->crc_on ? "on" : "off" );
#ifdef SX126X
    printf( "  LDRO:       %s\n", cfg->ldro < 0 ? "auto" : ( cfg->ldro ? "on" : "off" ) );
#endif
}

static void lora_print_help( void )
{
    printf( "\nLoRa parameters:\n" );
    printf( "  bw <kHz>                   Bandwidth\n" );
    printf( "  sf <N>                     Spreading factor\n" );
    printf( "  cr <4/5|4/6|4/7|4/8>       Coding rate\n" );
    printf( "  preamble <symbols>         Preamble length (4-65535)\n" );
    printf( "  syncword <hex>             Sync word (e.g. 0x12)\n" );
    printf( "  header <implicit|explicit> Header type\n" );
    printf( "  crc <on|off>               CRC\n" );
#ifdef SX126X
    printf( "  ldro <auto|on|off>         Low data rate optimizer\n" );
#endif
}

/*
 * --- Context-aware BW/SF hint helpers ---
 */

/** Build a BW hint string filtered by current region, e.g. "<125|250|500>". */
static const char* lora_bw_hint( const radio_config_t* cfg )
{
    static char buf[48];
    const lora_region_constraints_t* lc = ( cfg != NULL ) ? lora_get_constraints( cfg->region ) : NULL;
    if( lc == NULL )
    {
        /* No region set — show all possible BWs for this chip */
#ifdef SX126X
        return "<125|250|500>";
#else
        return "<125|250|500|812>";
#endif
    }

    buf[0] = '<';
    buf[1] = '\0';
    for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
    {
        if( i > 0 )
        {
            strncat( buf, "|", sizeof( buf ) - strlen( buf ) - 1 );
        }
        char tmp[8];
        snprintf( tmp, sizeof( tmp ), "%u", lc->bw_options[i] );
        strncat( buf, tmp, sizeof( buf ) - strlen( buf ) - 1 );
    }
    strncat( buf, ">", sizeof( buf ) - strlen( buf ) - 1 );
    return buf;
}

/** Build an SF hint string filtered by current region+BW, e.g. "<SF5-SF10>". */
static const char* lora_sf_hint( const radio_config_t* cfg )
{
    static char buf[32];
    const lora_region_constraints_t* lc = ( cfg != NULL ) ? lora_get_constraints( cfg->region ) : NULL;
    if( lc == NULL )
    {
        return "<5-12>";
    }

    /* Find the BW index for current bandwidth */
    int bw_idx = -1;
    for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
    {
        if( lc->bw_options[i] == cfg->bw_khz )
        {
            bw_idx = i;
            break;
        }
    }
    if( bw_idx < 0 )
    {
        /* BW not yet set — show overall min/max across all BWs */
        uint8_t sf_lo = lc->sf_min[0], sf_hi = lc->sf_max[0];
        for( int i = 1; i < 4 && lc->bw_options[i] != 0; i++ )
        {
            if( lc->sf_min[i] < sf_lo ) sf_lo = lc->sf_min[i];
            if( lc->sf_max[i] > sf_hi ) sf_hi = lc->sf_max[i];
        }
        snprintf( buf, sizeof( buf ), "<SF%u-SF%u>", sf_lo, sf_hi );
        return buf;
    }
    snprintf( buf, sizeof( buf ), "<SF%u-SF%u>", lc->sf_min[bw_idx], lc->sf_max[bw_idx] );
    return buf;
}

/** Build a BW completions list filtered by current region. */
static const char* const* lora_bw_completions( const radio_config_t* cfg )
{
    static const char* bw_strs[5]; /* max 4 + NULL */
    static char bw_bufs[4][8];
    const lora_region_constraints_t* lc = ( cfg != NULL ) ? lora_get_constraints( cfg->region ) : NULL;
    if( lc == NULL )
    {
        return NULL;
    }
    int j = 0;
    for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
    {
        snprintf( bw_bufs[j], sizeof( bw_bufs[j] ), "%u", lc->bw_options[i] );
        bw_strs[j] = bw_bufs[j];
        j++;
    }
    bw_strs[j] = NULL;
    return bw_strs;
}

/*
 * --- Detailed per-parameter help ---
 */

static bool lora_print_param_help( const char* param_name, const radio_config_t* cfg )
{
    const lora_region_constraints_t* lc = ( cfg != NULL ) ? lora_get_constraints( cfg->region ) : NULL;
    const region_def_t* region = ( cfg != NULL ) ? region_get_by_id( cfg->region ) : NULL;

    if( strcasecmp( param_name, "bw" ) == 0 )
    {
        printf( "bw — LoRa bandwidth (kHz)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: %u kHz\n", cfg->bw_khz );
        }
        if( lc != NULL && region != NULL )
        {
            printf( "  Valid for %s:", region->short_name );
            for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
            {
                printf( "  %u (SF%u-%u)", lc->bw_options[i],
                        lc->sf_min[i], lc->sf_max[i] );
            }
            printf( "\n" );
        }
        else
        {
            printf( "  Set region first to see valid bandwidths.\n" );
        }
        printf( "  Example: bw 125\n" );
        return true;
    }

    if( strcasecmp( param_name, "sf" ) == 0 )
    {
        printf( "sf — LoRa spreading factor\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: SF%u\n", cfg->sf );
        }
        if( lc != NULL && region != NULL )
        {
            /* Find the BW index, defaulting to first if BW not yet set */
            int bw_idx = 0;
            bool bw_found = false;
            for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
            {
                if( lc->bw_options[i] == cfg->bw_khz )
                {
                    bw_idx = i;
                    bw_found = true;
                    break;
                }
            }
            if( bw_found )
            {
                printf( "  Valid for %s at %u kHz: SF%u-SF%u\n",
                        region->short_name, cfg->bw_khz,
                        lc->sf_min[bw_idx], lc->sf_max[bw_idx] );
            }
            else
            {
                /* BW not set — show SF ranges for all BWs */
                printf( "  Valid for %s:\n", region->short_name );
                for( int i = 0; i < 4 && lc->bw_options[i] != 0; i++ )
                {
                    printf( "    %u kHz: SF%u-SF%u\n", lc->bw_options[i],
                            lc->sf_min[i], lc->sf_max[i] );
                }
            }
        }
        else
        {
            printf( "  Set region first to see valid SF range.\n" );
        }
        printf( "  Example: sf 7\n" );
        return true;
    }

    if( strcasecmp( param_name, "cr" ) == 0 )
    {
        printf( "cr — LoRa coding rate\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: %s\n", cli_state_cr_str( cfg->cr ) );
        }
        printf( "  Valid: 4/5, 4/6, 4/7, 4/8\n" );
        printf( "  Example: cr 4/5\n" );
        return true;
    }

    if( strcasecmp( param_name, "preamble" ) == 0 )
    {
        printf( "preamble — LoRa preamble length (symbols)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: %u symbols\n", cfg->preamble );
        }
        printf( "  Valid: 4-65535\n" );
        printf( "  Example: preamble 8\n" );
        return true;
    }

    if( strcasecmp( param_name, "syncword" ) == 0 )
    {
        printf( "syncword — LoRa sync word (8-bit hex)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: 0x%02X\n", cfg->syncword );
        }
        printf( "  Any hex value 0x00-0xFF. Common values:\n" );
        printf( "    0x12 = LoRaWAN public network (EU868, AS923)\n" );
        printf( "    0x34 = LoRaWAN private / US915\n" );
        printf( "  Example: syncword 0x34\n" );
        return true;
    }

    if( strcasecmp( param_name, "header" ) == 0 )
    {
        printf( "header — LoRa header type\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: %s\n", cfg->header_implicit ? "implicit" : "explicit" );
        }
        printf( "  Valid: implicit, explicit\n" );
        printf( "  Example: header explicit\n" );
        return true;
    }

    if( strcasecmp( param_name, "crc" ) == 0 )
    {
        printf( "crc — LoRa CRC\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: %s\n", cfg->crc_on ? "on" : "off" );
        }
        printf( "  Valid: on, off\n" );
        printf( "  Example: crc on\n" );
        return true;
    }

#ifdef SX126X
    if( strcasecmp( param_name, "ldro" ) == 0 )
    {
        printf( "ldro — Low data rate optimizer (SX126x only)\n" );
        if( cfg != NULL && cfg->modulation == MODULATION_LORA )
        {
            printf( "  Current: %s\n", cfg->ldro < 0 ? "auto" : ( cfg->ldro ? "on" : "off" ) );
        }
        printf( "  Valid: auto, on, off\n" );
        printf( "    auto = enabled automatically for long symbols (low SF + narrow BW)\n" );
        printf( "  Example: ldro auto\n" );
        return true;
    }
#endif

    return false;
}

#ifdef SX126X
static const char* const lora_param_names[] = {
    "bw", "sf", "cr", "preamble", "syncword", "header", "crc", "ldro", NULL
};
#else
static const char* const lora_param_names[] = {
    "bw", "sf", "cr", "preamble", "syncword", "header", "crc", NULL
};
#endif

static const char* const cr_completions[]     = { "4/5", "4/6", "4/7", "4/8", NULL };
static const char* const header_completions[] = { "implicit", "explicit", NULL };
static const char* const crc_completions[]    = { "on", "off", NULL };
static const char* const ldro_completions[]   = { "auto", "on", "off", NULL };

static const char* const* lora_get_completions( const char* param_name,
                                                 const radio_config_t* cfg )
{
    if( strcasecmp( param_name, "bw" ) == 0 )
    {
        return lora_bw_completions( cfg );
    }
    if( strcasecmp( param_name, "cr" ) == 0 )
    {
        return cr_completions;
    }
    if( strcasecmp( param_name, "header" ) == 0 )
    {
        return header_completions;
    }
    if( strcasecmp( param_name, "crc" ) == 0 )
    {
        return crc_completions;
    }
    if( strcasecmp( param_name, "ldro" ) == 0 )
    {
        return ldro_completions;
    }
    return NULL;
}

static const char* lora_get_hint( const char* param_name, const radio_config_t* cfg )
{
    if( strcasecmp( param_name, "bw" ) == 0 )
        return lora_bw_hint( cfg );
    if( strcasecmp( param_name, "sf" ) == 0 )
        return lora_sf_hint( cfg );
    if( strcasecmp( param_name, "cr" ) == 0 )
        return "<4/5|4/6|4/7|4/8>";
    if( strcasecmp( param_name, "preamble" ) == 0 )
        return "<4-65535>";
    if( strcasecmp( param_name, "syncword" ) == 0 )
        return "<hex, e.g. 0x12>";
    if( strcasecmp( param_name, "header" ) == 0 )
        return "<implicit|explicit>";
    if( strcasecmp( param_name, "crc" ) == 0 )
        return "<on|off>";
#ifdef SX126X
    if( strcasecmp( param_name, "ldro" ) == 0 )
        return "<auto|on|off>";
#endif
    return NULL;
}

/*
 * --- Module instance ---
 */

static const modulation_module_t mod_lora = {
    .name             = "lora",
    .id               = MODULATION_LORA,
    .load_defaults    = lora_load_defaults,
    .set_param        = lora_set_param,
    .owns_param       = lora_owns_param,
    .print_status     = lora_print_status,
    .print_help       = lora_print_help,
    .print_param_help = lora_print_param_help,
    .param_names      = lora_param_names,
    .get_completions  = lora_get_completions,
    .get_hint         = lora_get_hint,
};

/*
 * --- Registry ---
 */

#ifndef SX126X
/* Defined in mod_flrc.c */
extern const modulation_module_t mod_flrc;
static const modulation_module_t* const all_modules[] = { &mod_lora, &mod_flrc, NULL };
#else
static const modulation_module_t* const all_modules[] = { &mod_lora, NULL };
#endif

const modulation_module_t* modulation_lookup( const char* name )
{
    for( int i = 0; all_modules[i] != NULL; i++ )
    {
        if( strcasecmp( name, all_modules[i]->name ) == 0 )
        {
            return all_modules[i];
        }
    }
    return NULL;
}

const modulation_module_t* modulation_get_by_id( modulation_id_t id )
{
    for( int i = 0; all_modules[i] != NULL; i++ )
    {
        if( all_modules[i]->id == id )
        {
            return all_modules[i];
        }
    }
    return NULL;
}

const modulation_module_t* const* modulation_get_all( void )
{
    return all_modules;
}
