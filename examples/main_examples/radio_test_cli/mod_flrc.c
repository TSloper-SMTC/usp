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

    cfg->modulation    = MODULATION_FLRC;
    cfg->flrc_br_kbps  = fc->default_br_kbps;
    cfg->flrc_cr       = fc->default_cr;
    cfg->flrc_bt       = fc->default_bt;
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

    printf( "ERROR: Unknown FLRC parameter '%s'\n", name );
    return -1;
}

static bool flrc_owns_param( const char* name )
{
    return strcasecmp( name, "br" ) == 0 ||
           strcasecmp( name, "cr" ) == 0 ||
           strcasecmp( name, "bt" ) == 0;
}

static void flrc_print_status( const radio_config_t* cfg )
{
    printf( "  Bitrate:    %u kbps\n", cfg->flrc_br_kbps );
    printf( "  CR:         %s\n", cli_state_flrc_cr_str( cfg->flrc_cr ) );
    printf( "  BT:         %s\n", cli_state_flrc_bt_str( cfg->flrc_bt ) );
}

static void flrc_print_help( void )
{
    printf( "\nFLRC parameters:\n" );
    printf( "  br <kbps>                  Bitrate\n" );
    printf( "  cr <1/2|2/3|3/4|none>      Coding rate\n" );
    printf( "  bt <off|bt0.5|bt1>         Pulse shape filter\n" );
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

    return false;
}

static const char* const flrc_param_names[] = {
    "br", "cr", "bt", NULL
};

static const char* const cr_completions[]     = { "1/2", "2/3", "3/4", "none", NULL };
static const char* const bt_completions[]     = { "off", "bt0.5", "bt1", NULL };
static const char* const br_completions[]     = { "260", "325", "520", "650", "1040", "1300", "2080", "2600", NULL };

static const char* const* flrc_get_completions( const char* param_name,
                                                 const radio_config_t* cfg )
{
    ( void ) cfg;
    if( strcasecmp( param_name, "cr" ) == 0 )
        return cr_completions;
    if( strcasecmp( param_name, "bt" ) == 0 )
        return bt_completions;
    if( strcasecmp( param_name, "br" ) == 0 )
        return br_completions;
    return NULL;
}

static const char* flrc_get_hint( const char* param_name, const radio_config_t* cfg )
{
    ( void ) cfg;
    if( strcasecmp( param_name, "br" ) == 0 )
        return "<260|325|520|650|1040|1300|2080|2600>";
    if( strcasecmp( param_name, "cr" ) == 0 )
        return "<1/2|2/3|3/4|none>";
    if( strcasecmp( param_name, "bt" ) == 0 )
        return "<off|bt0.5|bt1>";
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
