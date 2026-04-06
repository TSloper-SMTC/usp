/**
 * @file      cli_parser.c
 *
 * @brief     Command tokenizer, dispatch table, help system, tab completion
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "version.h"
#include "cli_parser.h"
#include "region.h"
#include "modulation.h"
#include "test_modes.h"
#include "stack_region.h"
#include "linenoise.h"
#include "per_counter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#ifdef __linux__
#include <time.h>
#endif
#include "platform.h"

/*
 * --- Token helpers ---
 */

#define MAX_TOKENS 16

typedef struct
{
    int         count;
    const char* tokens[MAX_TOKENS];
} tokenized_t;

static tokenized_t tokenize( char* line )
{
    tokenized_t result = { .count = 0 };
    char*       tok    = strtok( line, " \t" );
    while( tok != NULL && result.count < MAX_TOKENS )
    {
        result.tokens[result.count++] = tok;
        tok                           = strtok( NULL, " \t" );
    }
    return result;
}

/*
 * --- Inline override parsing ---
 * Scans tokens for --freq <val>, --power <val>, etc.
 * Returns a temporary config copy with overrides applied (or error).
 */
static int parse_inline_overrides( const tokenized_t* tokens, int start_idx, radio_config_t* tmp_cfg,
                                    const region_def_t* region )
{
    for( int i = start_idx; i < tokens->count; i++ )
    {
        if( strcmp( tokens->tokens[i], "--freq" ) == 0 && i + 1 < tokens->count )
        {
            char* endptr;
            float freq = strtof( tokens->tokens[++i], &endptr );
            if( *endptr != '\0' )
            {
                printf( "ERROR: Invalid value for --freq\n" );
                return -1;
            }
            const char* err = region_validate_freq( region, freq );
            if( err )
            {
                printf( "ERROR: %s\n", err );
                return -1;
            }
            tmp_cfg->freq_mhz = freq;
        }
        else if( strcmp( tokens->tokens[i], "--power" ) == 0 && i + 1 < tokens->count )
        {
            char* endptr;
            long  power_raw = strtol( tokens->tokens[++i], &endptr, 10 );
            if( *endptr != '\0' )
            {
                printf( "ERROR: Invalid value for --power\n" );
                return -1;
            }
            int power = ( int ) power_raw;
            const char* err = region_validate_power( region, power );
            if( err )
            {
                printf( "ERROR: %s\n", err );
                return -1;
            }
            tmp_cfg->power_dbm = power;
        }
        else if( strcmp( tokens->tokens[i], "--mask" ) == 0 && i + 1 < tokens->count )
        {
            char* endptr;
            long  mask = strtol( tokens->tokens[++i], &endptr, 10 );
            if( *endptr != '\0' )
            {
                printf( "ERROR: Invalid value for --mask\n" );
                return -1;
            }
            if( mask < 0 || mask > 7 )
            {
                printf( "ERROR: Mask must be 0-7\n" );
                return -1;
            }
            tmp_cfg->hybrid_mask = ( uint8_t ) mask;
        }
        else if( strcmp( tokens->tokens[i], "--dr" ) == 0 && i + 1 < tokens->count )
        {
            char* endptr;
            long  dr = strtol( tokens->tokens[++i], &endptr, 10 );
            if( *endptr != '\0' )
            {
                printf( "ERROR: Invalid value for --dr\n" );
                return -1;
            }
            if( dr < 0 || dr > 6 )
            {
                printf( "ERROR: DR must be 0-6 (US915 uplink)\n" );
                return -1;
            }
            tmp_cfg->reg_dr = ( uint8_t ) dr;
        }
        else if( strcmp( tokens->tokens[i], "--count" ) == 0 && i + 1 < tokens->count )
        {
            char* endptr;
            long  cnt = strtol( tokens->tokens[++i], &endptr, 10 );
            if( *endptr != '\0' )
            {
                printf( "ERROR: Invalid value for --count\n" );
                return -1;
            }
            if( cnt < 0 )
            {
                printf( "ERROR: Count must be >= 0\n" );
                return -1;
            }
            tmp_cfg->reg_count = ( uint32_t ) cnt;
        }
        else if( strcmp( tokens->tokens[i], "--delay" ) == 0 && i + 1 < tokens->count )
        {
            char* endptr;
            long  d = strtol( tokens->tokens[++i], &endptr, 10 );
            if( *endptr != '\0' )
            {
                printf( "ERROR: Invalid value for --delay\n" );
                return -1;
            }
            if( d < 0 )
            {
                printf( "ERROR: Delay must be >= 0\n" );
                return -1;
            }
            tmp_cfg->reg_delay_ms = ( uint32_t ) d;
        }
        else if( tokens->tokens[i][0] == '-' && tokens->tokens[i][1] == '-' )
        {
            printf( "ERROR: Unknown override '%s'\n", tokens->tokens[i] );
            return -1;
        }
    }
    return 0;
}

/*
 * --- Command: region ---
 */
static int cmd_region( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( tokens->count < 2 )
    {
#ifdef SX126X
        printf( "ERROR: Usage: region <us>\n" );
#else
        printf( "ERROR: Usage: region <us|2g4>\n" );
#endif
        return -1;
    }

    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    const region_def_t* region = region_lookup( tokens->tokens[1] );
    if( region == NULL )
    {
#ifdef SX126X
        printf( "ERROR: Unknown region '%s'. Valid: us\n", tokens->tokens[1] );
#else
        printf( "ERROR: Unknown region '%s'. Valid: us, 2g4\n", tokens->tokens[1] );
#endif
        return -1;
    }

    /* Check runtime chip support (e.g. LR1110 does not support WW2G4) */
    if( chip->supports_region != NULL && !chip->supports_region( region->id ) )
    {
        printf( "ERROR: Region '%s' is not supported by this chip\n", tokens->tokens[1] );
        return -1;
    }

    region_load_defaults( cfg, region );
    chip->refresh_pa_config( cfg );

    printf( "Region set: %s\n", region->name );
    printf( "  Defaults: freq=%.1f MHz, power=%+d dBm\n",
            ( double ) cfg->freq_mhz, cfg->power_dbm );
    return 0;
}

/*
 * --- Command: modulation ---
 */
static int cmd_modulation( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: modulation <" );
        const modulation_module_t* const* all = modulation_get_all();
        bool first = true;
        for( int i = 0; all[i] != NULL; i++ )
        {
            if( chip->supports_modulation != NULL && !chip->supports_modulation( all[i]->id ) )
                continue;
            printf( "%s%s", first ? "" : "|", all[i]->name );
            first = false;
        }
        printf( "|help>\n" );
        return -1;
    }

    if( cfg->region == REGION_NONE )
    {
        /* Build chip-filtered region list for the error message */
        char region_list[32] = "";
        const char* all_regions[] = { "us", "2g4", NULL };  /* EU/JP hidden */
        bool first = true;
        for( int i = 0; all_regions[i] != NULL; i++ )
        {
            if( chip->supports_region != NULL )
            {
                const region_def_t* r = region_lookup( all_regions[i] );
                if( r == NULL || !chip->supports_region( r->id ) )
                    continue;
            }
            if( !first ) strncat( region_list, "|", sizeof( region_list ) - 1 );
            strncat( region_list, all_regions[i], sizeof( region_list ) - 1 );
            first = false;
        }
        printf( "ERROR: Region not set. Use 'region <%s>' first.\n", region_list );
        return -1;
    }

    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    if( strcasecmp( tokens->tokens[1], "help" ) == 0 )
    {
        printf( "Available modulations for %s:\n", chip->chip_name );
        const modulation_module_t* const* all = modulation_get_all();
        for( int i = 0; all[i] != NULL; i++ )
        {
            if( chip->supports_modulation != NULL && !chip->supports_modulation( all[i]->id ) )
                continue;
            printf( "  %s\n", all[i]->name );
        }
        return 0;
    }

    const modulation_module_t* mod = modulation_lookup( tokens->tokens[1] );
    if( mod == NULL )
    {
        printf( "ERROR: Unknown modulation '%s'. Use 'modulation help' to see available options.\n",
                tokens->tokens[1] );
        return -1;
    }

    /* Check chip capability at runtime (e.g. FLRC requires LR2021) */
    if( chip->supports_modulation != NULL && !chip->supports_modulation( mod->id ) )
    {
        printf( "ERROR: '%s' is not supported by this chip.\n", mod->name );
        return -1;
    }

    modulation_id_t prev_mod = cfg->modulation;
    mod->load_defaults( cfg, cfg->region );
    if( cfg->modulation != mod->id )
    {
        /* load_defaults returned without setting modulation (region not supported).
         * The module already printed an error. Restore previous modulation. */
        cfg->modulation = prev_mod;
        return -1;
    }

    printf( "Modulation set: %s\n", cli_state_modulation_str( mod->id ) );
    mod->print_status( cfg );
    return 0;
}

/*
 * --- Command: freq ---
 */
static int cmd_freq( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) chip;
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: freq <MHz>\n" );
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

    char*       endptr;
    float       freq = strtof( tokens->tokens[1], &endptr );
    if( *endptr != '\0' )
    {
        printf( "ERROR: Invalid frequency value\n" );
        return -1;
    }

    const region_def_t* region = region_get_by_id( cfg->region );
    const char*         err    = region_validate_freq( region, freq );
    if( err )
    {
        printf( "ERROR: %s\n", err );
        return -1;
    }

    cfg->freq_mhz = freq;
    printf( "Frequency set: %.3f MHz\n", ( double ) freq );
    return 0;
}

/*
 * --- Command: power ---
 */
static int cmd_power( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: power <dBm>\n" );
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

    char* endptr;
    int   power = ( int ) strtol( tokens->tokens[1], &endptr, 10 );
    if( *endptr != '\0' )
    {
        printf( "ERROR: Invalid power value\n" );
        return -1;
    }

    const region_def_t* region = region_get_by_id( cfg->region );
    const char*         err    = region_validate_power( region, power );
    if( err )
    {
        printf( "ERROR: %s\n", err );
        return -1;
    }

    cfg->power_dbm = power;
    chip->reset_pa_overrides();
    chip->refresh_pa_config( cfg );
    printf( "Power set: %+d dBm\n", power );
    return 0;
}

#if defined( LR20XX )
/*
 * --- Command: agc ---
 */
static int cmd_agc( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) chip;
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: agc <auto|g1-g13>\n" );
        return -1;
    }
    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    const char* value = tokens->tokens[1];

    if( strcasecmp( value, "auto" ) == 0 )
    {
        cfg->agc_gain = AGC_GAIN_AUTO;
    }
    else if( value[0] == 'g' || value[0] == 'G' )
    {
        char* endptr;
        long  step = strtol( value + 1, &endptr, 10 );
        if( *endptr != '\0' || step < 1 || step > 13 )
        {
            printf( "ERROR: Invalid gain step. Valid: auto, g1-g13\n" );
            return -1;
        }
        cfg->agc_gain = ( agc_gain_t ) step;
    }
    else
    {
        /* Allow bare numeric input (1-13) */
        char* endptr;
        long  step = strtol( value, &endptr, 10 );
        if( *endptr != '\0' || step < 1 || step > 13 )
        {
            printf( "ERROR: Invalid gain step. Valid: auto, g1-g13\n" );
            return -1;
        }
        cfg->agc_gain = ( agc_gain_t ) step;
    }

    printf( "AGC gain set: %s\n", cli_state_agc_gain_str( cfg->agc_gain ) );
    return 0;
}

/*
 * --- Command: boost-lf / boost-hf ---
 */
static int parse_boost_value( const char* value, int8_t* result )
{
    if( strcasecmp( value, "auto" ) == 0 )
    {
        *result = -1;
        return 0;
    }
    char* endptr;
    long  val = strtol( value, &endptr, 10 );
    if( *endptr != '\0' || val < 0 || val > 7 )
        return -1;
    *result = ( int8_t ) val;
    return 0;
}

static int cmd_boost_lf( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) chip;
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: boost-lf <auto|0-7>\n" );
        return -1;
    }
    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }
    if( parse_boost_value( tokens->tokens[1], &cfg->rx_boost_lf ) != 0 )
    {
        printf( "ERROR: Invalid boost level. Valid: auto, 0-7\n" );
        return -1;
    }
    if( cfg->rx_boost_lf < 0 )
        printf( "RX boost LF: auto (0)\n" );
    else
        printf( "RX boost LF: %d\n", cfg->rx_boost_lf );
    return 0;
}

static int cmd_boost_hf( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) chip;
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: boost-hf <auto|0-7>\n" );
        return -1;
    }
    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }
    if( parse_boost_value( tokens->tokens[1], &cfg->rx_boost_hf ) != 0 )
    {
        printf( "ERROR: Invalid boost level. Valid: auto, 0-7\n" );
        return -1;
    }
    if( cfg->rx_boost_hf < 0 )
        printf( "RX boost HF: auto (4)\n" );
    else
        printf( "RX boost HF: %d\n", cfg->rx_boost_hf );
    return 0;
}
#endif /* LR20XX */

/*
 * --- Command: start ---
 */
static int cmd_start( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: start <cw|modulated|rx|fhss|dts|hybrid|per-tx|per-rx>\n" );  /* EU/JP hidden */
        return -1;
    }

    if( cfg->region == REGION_NONE )
    {
        printf( "ERROR: Region not set.\n" );
        return -1;
    }
    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first with 'stop'.\n" );
        return -1;
    }

    /* Map mode name to enum */
    active_mode_t mode;
    const char*   mode_str = tokens->tokens[1];

    if( strcasecmp( mode_str, "cw" ) == 0 )
    {
        mode = MODE_CW;
    }
    else if( strcasecmp( mode_str, "modulated" ) == 0 )
    {
        mode = MODE_MODULATED;
    }
    else if( strcasecmp( mode_str, "rx" ) == 0 )
    {
        mode = MODE_RX;
    }
    else if( strcasecmp( mode_str, "fhss" ) == 0 )
    {
        mode = MODE_FHSS;
    }
    else if( strcasecmp( mode_str, "dts" ) == 0 )
    {
        mode = MODE_DTS;
    }
    else if( strcasecmp( mode_str, "hybrid" ) == 0 )
    {
        mode = MODE_HYBRID;
    }
    else if( strcasecmp( mode_str, "eu-test" ) == 0 || strcasecmp( mode_str, "jp-test" ) == 0 )
    {
        /* EU/JP hidden */
        printf( "ERROR: EU and JP regions are not available in this preview release\n" );
        return -1;
    }
    else if( strcasecmp( mode_str, "per-tx" ) == 0 )
    {
        mode = MODE_PER_TX;
    }
    else if( strcasecmp( mode_str, "per-rx" ) == 0 )
    {
        mode = MODE_PER_RX;
    }
    else
    {
        printf( "ERROR: Unknown mode '%s'. Type 'help start' for available modes.\n", mode_str );
        return -1;
    }

    /* CW only needs freq/power — no modulation required.
     * All other modes need modulation parameters configured. */
    if( mode != MODE_CW && cfg->modulation == MODULATION_NONE )
    {
        printf( "ERROR: Modulation not set. Use 'modulation lora' first.\n" );
        return -1;
    }

    /* Check region-gated modes */
    const region_def_t* region = region_get_by_id( cfg->region );
    if( mode == MODE_FHSS && !region->fhss_available )
    {
        printf( "ERROR: FHSS not available in %s region\n", region->short_name );
        return -1;
    }
    if( mode == MODE_DTS && !region->dts_available )
    {
        printf( "ERROR: DTS not available in %s region\n", region->short_name );
        return -1;
    }
    if( mode == MODE_HYBRID )
    {
        if( !region->hybrid_available )
        {
            printf( "ERROR: Hybrid not available in %s region\n", region->short_name );
            return -1;
        }
        /* Check --mask is present */
        bool has_mask = false;
        for( int i = 2; i < tokens->count; i++ )
        {
            if( strcmp( tokens->tokens[i], "--mask" ) == 0 )
            {
                has_mask = true;
                break;
            }
        }
        if( !has_mask )
        {
            printf( "ERROR: --mask required for hybrid mode\n" );
            return -1;
        }
    }
    if( mode == MODE_EU_TEST && cfg->region != REGION_EU )
    {
        printf( "ERROR: eu-test requires EU region\n" );
        return -1;
    }
    if( mode == MODE_JP_TEST && cfg->region != REGION_JP )
    {
        printf( "ERROR: jp-test requires JP region\n" );
        return -1;
    }

    /* Apply inline overrides to a temporary copy so they don't persist */
    radio_config_t tmp_cfg = *cfg;
    if( parse_inline_overrides( tokens, 2, &tmp_cfg, region ) != 0 )
    {
        return -1;
    }

    int rc = test_mode_start( &tmp_cfg, chip, mode );
    if( rc == 0 )
    {
        /* Tick-based modes (PER, regulatory) set active_mode to the
         * requested mode; the main loop's tick dispatch will reset it
         * to IDLE when complete.  Fire-and-forget modes (CW, modulated,
         * RX) also leave it set.  Propagate either way. */
        cfg->active_mode = tmp_cfg.active_mode;
    }
    return rc;
}

/*
 * --- Command: stop ---
 */
static int cmd_stop( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) tokens;
    return test_mode_stop( cfg, chip );
}

/*
 * --- Command: status ---
 */
static int cmd_status( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) tokens;
    ( void ) chip;
    cli_state_print_status( cfg );
    return 0;
}

/*
 * --- Command: pa ---
 */
static int cmd_pa( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: pa <show|reset|param> [value]\n" );
        return -1;
    }

    if( strcasecmp( tokens->tokens[1], "show" ) == 0 )
    {
        chip->refresh_pa_config( cfg );
        printf( "PA parameters for %s (TX power: %+d dBm):\n", chip->chip_name, cfg->power_dbm );
        printf( "  %-16s  %-5s  %-7s  %s\n", "Parameter", "Value", "Range", "Description" );
        printf( "  %-16s  %-5s  %-7s  %s\n", "---------", "-----", "-----", "-----------" );
        for( int i = 0; i < chip->pa_param_count; i++ )
        {
            int val = 0;
            chip->get_pa_param( chip->pa_params[i].name, &val );
            char range[16];
            if( chip->pa_params[i].read_only )
            {
                snprintf( range, sizeof( range ), "(r/o)" );
            }
            else
            {
                snprintf( range, sizeof( range ), "%d-%d",
                          chip->pa_params[i].min, chip->pa_params[i].max );
            }
            printf( "  %-16s  %-5d  %-7s  %s\n",
                    chip->pa_params[i].name, val, range,
                    chip->pa_params[i].description );
        }
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "reset" ) == 0 )
    {
        chip->reset_pa_overrides();
        chip->refresh_pa_config( cfg );
        printf( "PA overrides cleared — all params restored to BSP table values\n" );
        return 0;
    }

    /* pa <param> <value> */
    if( tokens->count < 3 )
    {
        printf( "ERROR: Usage: pa <show|reset|param> [value]\n" );
        return -1;
    }

    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    const char* param_name = tokens->tokens[1];
    char*       endptr;
    int         value = ( int ) strtol( tokens->tokens[2], &endptr, 10 );
    if( *endptr != '\0' )
    {
        printf( "ERROR: Invalid value\n" );
        return -1;
    }

    int rc = chip->set_pa_param( param_name, value );
    if( rc == -2 )
    {
        printf( "ERROR: Unknown PA parameter '%s'. Use 'pa show'.\n", param_name );
        return -1;
    }
    if( rc == -4 )
    {
        printf( "ERROR: '%s' is read-only\n", param_name );
        return -1;
    }
    if( rc == -3 )
    {
        /* Error already printed by chip backend (e.g. discrete value validation) */
        return -1;
    }
    if( rc == -1 )
    {
        /* Find the param to report range */
        for( int i = 0; i < chip->pa_param_count; i++ )
        {
            if( strcasecmp( param_name, chip->pa_params[i].name ) == 0 )
            {
                printf( "ERROR: %s range is %d-%d\n", param_name,
                        chip->pa_params[i].min, chip->pa_params[i].max );
                return -1;
            }
        }
        printf( "ERROR: Value out of range\n" );
        return -1;
    }

    printf( "%s set: %d\n", param_name, value );
    return 0;
}

/*
 * --- Command: show ---
 */
static int cmd_show( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) chip;
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: show <channels>\n" );
        return -1;
    }

    if( strcasecmp( tokens->tokens[1], "channels" ) == 0 )
    {
        if( cfg->region == REGION_NONE )
        {
            printf( "ERROR: Region not set.\n" );
            return -1;
        }

        /* Initialise stack region if not already done */
        if( stack_region_get_active() != cfg->region )
        {
            int rc = stack_region_init( cfg->region );
            if( rc != 0 )
            {
                printf( "ERROR: Failed to initialise stack region\n" );
                return -1;
            }
        }

        uint8_t num_ch = stack_region_get_num_channels();
        printf( "%s channel plan (%u channels):\n", cli_state_region_str( cfg->region ), num_ch );
        printf( "  %-6s  %-12s\n", "Index", "Frequency" );
        printf( "  %-6s  %-12s\n", "-----", "---------" );
        for( uint8_t i = 0; i < num_ch; i++ )
        {
            uint32_t freq_hz = 0;
            if( stack_region_get_channel_info( i, &freq_hz ) == 0 && freq_hz > 0 )
            {
                printf( "  %-6u  %.3f MHz\n", i, ( double ) freq_hz / 1e6 );
            }
        }
        return 0;
    }

    printf( "ERROR: Unknown show subcommand '%s'. Valid: channels\n", tokens->tokens[1] );
    return -1;
}

/*
 * --- Command: delay (Linux only — used in script/pipe mode) ---
 */
#ifdef __linux__
static int cmd_delay( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: delay <ms>\n" );
        return -1;
    }

    char* endptr;
    long  ms = strtol( tokens->tokens[1], &endptr, 10 );
    if( *endptr != '\0' || ms < 0 )
    {
        printf( "ERROR: Invalid delay value\n" );
        return -1;
    }

    /* Tick any active mode while waiting so TX/RX continues during the delay */
    struct timespec start;
    clock_gettime( CLOCK_MONOTONIC, &start );
    uint64_t end_us = ( uint64_t ) start.tv_sec * 1000000 + ( uint64_t ) start.tv_nsec / 1000
                      + ( uint64_t ) ms * 1000;

    bool was_active = ( cfg->active_mode != MODE_IDLE );

    for( ;; )
    {
        if( cfg->active_mode != MODE_IDLE )
        {
            test_mode_tick( cfg, chip );
        }
        else if( was_active )
        {
            /* Mode was stopped (e.g. Ctrl+C) — exit delay early */
            break;
        }

        struct timespec now;
        clock_gettime( CLOCK_MONOTONIC, &now );
        uint64_t now_us = ( uint64_t ) now.tv_sec * 1000000 + ( uint64_t ) now.tv_nsec / 1000;
        if( now_us >= end_us )
        {
            break;
        }
        platform_sleep_us( 500 );
    }
    return 0;
}
#endif /* __linux__ */

#ifndef __linux__
/*
 * --- Command: term ---
 */
static int cmd_term( const tokenized_t* tokens, radio_config_t* cfg )
{
    if( tokens->count < 2 )
    {
        printf( "Terminal: %s (type 'term %s' to switch)\n",
                cfg->term_ansi ? "ansi" : "plain",
                cfg->term_ansi ? "plain" : "ansi" );
        return 0;
    }
    if( strcasecmp( tokens->tokens[1], "ansi" ) == 0 )
    {
        cfg->term_ansi = true;
        printf( "Terminal: ansi (arrow keys, history, tab completion enabled)\n" );
        return 0;
    }
    if( strcasecmp( tokens->tokens[1], "plain" ) == 0 )
    {
        cfg->term_ansi = false;
        printf( "Terminal: plain (type 'term ansi' for full editing)\n" );
        return 0;
    }
    printf( "ERROR: Usage: term <ansi|plain>\n" );
    return -1;
}
#endif /* __linux__ */

/*
 * --- Command: per ---
 */
static int cmd_per( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) chip;
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: per <count|interval|payload|stats|reset>\n" );
        return -1;
    }

    /* --- per stats / per reset: available any time, no active-mode gate --- */

    if( strcasecmp( tokens->tokens[1], "stats" ) == 0 )
    {
        const per_counter_t* last = per_counter_get_last();
        if( last == NULL || !last->has_data )
        {
            printf( "No PER test has been run yet.\n" );
        }
        else
        {
            printf( "Last PER test result (%s):\n", last->is_tx_side ? "tx" : "rx" );
            per_counter_print( last );
        }
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "reset" ) == 0 )
    {
        per_counter_save_last( NULL );
        printf( "PER counters cleared.\n" );
        return 0;
    }

    /* --- Config subcommands: blocked while a mode is active --- */

    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    if( tokens->count < 3 )
    {
        printf( "ERROR: Usage: per %s <value>\n", tokens->tokens[1] );
        return -1;
    }

    char* endptr;

    if( strcasecmp( tokens->tokens[1], "count" ) == 0 )
    {
        long val = strtol( tokens->tokens[2], &endptr, 10 );
        if( *endptr != '\0' || val < 0 )
        {
            printf( "ERROR: count must be >= 0\n" );
            return -1;
        }
        cfg->per_count = ( uint32_t ) val;
        printf( "PER count set: %u%s\n", (unsigned) cfg->per_count, cfg->per_count == 0 ? " (infinite)" : "" );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "interval" ) == 0 )
    {
        long val = strtol( tokens->tokens[2], &endptr, 10 );
        if( *endptr != '\0' || val <= 0 )
        {
            printf( "ERROR: interval must be > 0 ms\n" );
            return -1;
        }
        cfg->per_interval_ms = ( uint32_t ) val;
        printf( "PER interval set: %u ms\n", (unsigned) cfg->per_interval_ms );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "payload" ) == 0 )
    {
        if( strcasecmp( tokens->tokens[2], "auto" ) == 0 )
        {
            cfg->per_payload_size = 0;
            printf( "PER payload set: auto\n" );
            return 0;
        }
        long val = strtol( tokens->tokens[2], &endptr, 10 );
        if( *endptr != '\0' || val < 6 || val > 255 )
        {
            printf( "ERROR: payload must be 6-255 bytes or 'auto'\n" );
            return -1;
        }
        cfg->per_payload_size = ( uint8_t ) val;
        printf( "PER payload set: %u bytes\n", cfg->per_payload_size );
        return 0;
    }

    printf( "ERROR: Unknown per subcommand '%s'\n"
            "  Valid: count, interval, payload, stats, reset\n",
            tokens->tokens[1] );
    return -1;
}

#if defined( LR20XX ) || defined( SX126X )
/*
 * --- Command: xosc ---
 *
 * xosc show             Show current XTA/XTB values (and wait on LR20xx)
 * xosc xta <0-47>       Set XTA capacitor trim
 * xosc xtb <0-47>       Set XTB capacitor trim
 * xosc wait <0-255>     Set stabilization delay (µs) — LR20xx only
 */
static void xosc_print( const radio_config_t* cfg, const chip_driver_t* chip )
{
    double xta_pf = chip->xosc_xta_min_pf + cfg->xosc_xta * 0.47;
    double xtb_pf = chip->xosc_xtb_min_pf + cfg->xosc_xtb * 0.47;
    printf( "XOSC trim:\n" );
    printf( "  xta:  0x%02X (%u) -> %.1f pF\n", cfg->xosc_xta, cfg->xosc_xta, xta_pf );
    printf( "  xtb:  0x%02X (%u) -> %.1f pF\n", cfg->xosc_xtb, cfg->xosc_xtb, xtb_pf );
    if( chip->xosc_has_wait )
    {
        printf( "  wait: %u us\n", cfg->xosc_wait_us );
    }
}

static int cmd_xosc( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( chip->apply_xosc_trim == NULL )
    {
        printf( "ERROR: XOSC trim not supported on %s\n", chip->chip_name );
        return -1;
    }

    /* xosc show — print current values */
    if( tokens->count < 2 )
    {
        if( chip->xosc_has_wait )
            printf( "ERROR: Usage: xosc <show|default|xta|xtb|wait>\n" );
        else
            printf( "ERROR: Usage: xosc <show|default|xta|xtb>\n" );
        return -1;
    }
    if( strcasecmp( tokens->tokens[1], "show" ) == 0 )
    {
        xosc_print( cfg, chip );
        return 0;
    }

    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    if( strcasecmp( tokens->tokens[1], "default" ) == 0 )
    {
        chip->get_xosc_defaults( &cfg->xosc_xta, &cfg->xosc_xtb, &cfg->xosc_wait_us );
        if( chip->apply_xosc_trim( cfg->xosc_xta, cfg->xosc_xtb, cfg->xosc_wait_us ) != 0 )
        {
            printf( "ERROR: apply_xosc_trim failed\n" );
            return -1;
        }
        printf( "XOSC trim restored to defaults:\n" );
        xosc_print( cfg, chip );
        return 0;
    }

    if( tokens->count < 3 )
    {
        printf( "ERROR: Usage: xosc <xta|xtb|wait> <value>\n" );
        return -1;
    }

    char* endptr;
    long  val = strtol( tokens->tokens[2], &endptr, 0 ); /* accept 0x hex prefix */
    if( *endptr != '\0' )
    {
        printf( "ERROR: Invalid value '%s'\n", tokens->tokens[2] );
        return -1;
    }

    if( strcasecmp( tokens->tokens[1], "xta" ) == 0 )
    {
        if( val < 0 || val > 47 )
        {
            printf( "ERROR: xta must be 0-47\n" );
            return -1;
        }
        cfg->xosc_xta = ( uint8_t ) val;
        if( chip->apply_xosc_trim( cfg->xosc_xta, cfg->xosc_xtb, cfg->xosc_wait_us ) != 0 )
        {
            printf( "ERROR: apply_xosc_trim failed\n" );
            return -1;
        }
        double pf = chip->xosc_xta_min_pf + cfg->xosc_xta * 0.47;
        printf( "XOSC xta set: 0x%02X (%u) -> %.1f pF\n", cfg->xosc_xta, cfg->xosc_xta, pf );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "xtb" ) == 0 )
    {
        if( val < 0 || val > 47 )
        {
            printf( "ERROR: xtb must be 0-47\n" );
            return -1;
        }
        cfg->xosc_xtb = ( uint8_t ) val;
        if( chip->apply_xosc_trim( cfg->xosc_xta, cfg->xosc_xtb, cfg->xosc_wait_us ) != 0 )
        {
            printf( "ERROR: apply_xosc_trim failed\n" );
            return -1;
        }
        double pf = chip->xosc_xtb_min_pf + cfg->xosc_xtb * 0.47;
        printf( "XOSC xtb set: 0x%02X (%u) -> %.1f pF\n", cfg->xosc_xtb, cfg->xosc_xtb, pf );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "wait" ) == 0 )
    {
        if( !chip->xosc_has_wait )
        {
            printf( "ERROR: xosc wait not applicable on %s (stabilisation is hardware-managed)\n",
                    chip->chip_name );
            return -1;
        }
        if( val < 0 || val > 255 )
        {
            printf( "ERROR: wait must be 0-255\n" );
            return -1;
        }
        cfg->xosc_wait_us = ( uint8_t ) val;
        if( chip->apply_xosc_trim( cfg->xosc_xta, cfg->xosc_xtb, cfg->xosc_wait_us ) != 0 )
        {
            printf( "ERROR: apply_xosc_trim failed\n" );
            return -1;
        }
        printf( "XOSC wait set: %u us\n", cfg->xosc_wait_us );
        return 0;
    }

    if( chip->xosc_has_wait )
        printf( "ERROR: Unknown xosc subcommand '%s'. Valid: show, default, xta, xtb, wait\n",
                tokens->tokens[1] );
    else
        printf( "ERROR: Unknown xosc subcommand '%s'. Valid: show, default, xta, xtb\n",
                tokens->tokens[1] );
    return -1;
}
#endif /* LR20XX || SX126X */

#if defined( LR20XX )
/*
 * --- Command: temp ---
 */

static const char* temp_source_str( uint8_t src )
{
    switch( src )
    {
    case 0:  return "VBE";
    case 1:  return "XOSC";
    case 2:  return "NTC";
    default: return "unknown";
    }
}

static int cmd_temp( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) cfg;

    if( chip->get_temp == NULL )
    {
        printf( "ERROR: Temperature sensor not supported on %s\n", chip->chip_name );
        return -1;
    }

    uint8_t source = 0; /* default: VBE */
    if( tokens->count >= 2 )
    {
        if( strcasecmp( tokens->tokens[1], "vbe" ) == 0 )
            source = 0;
        else if( strcasecmp( tokens->tokens[1], "xosc" ) == 0 )
            source = 1;
        else if( strcasecmp( tokens->tokens[1], "ntc" ) == 0 )
            source = 2;
        else
        {
            printf( "ERROR: Usage: temp [vbe|xosc|ntc]\n" );
            return -1;
        }
    }

    float temp_c;
    if( chip->get_temp( source, &temp_c ) != 0 )
    {
        return -1; /* error already printed by driver */
    }

    printf( "Temperature (%s): %.1f °C\n", temp_source_str( source ), temp_c );
    return 0;
}

/*
 * --- Command: tempcomp ---
 */
static int cmd_tempcomp( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( chip->set_temp_comp == NULL )
    {
        printf( "ERROR: Temperature compensation not supported on %s\n", chip->chip_name );
        return -1;
    }

    /* No argument: show current state */
    if( tokens->count < 2 )
    {
        if( chip->is_tcxo )
        {
            printf( "Temperature compensation: off (TCXO — not applicable)\n" );
        }
        else
        {
            const char* mode_str;
            switch( cfg->temp_comp_mode )
            {
            case 1:  mode_str = "relative"; break;
            case 2:  mode_str = cfg->temp_comp_ntc ? "absolute + NTC" : "absolute"; break;
            default: mode_str = "off"; break;
            }
            printf( "Temperature compensation: %s\n", mode_str );
        }
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "off" ) == 0 )
    {
        if( chip->set_temp_comp( 0, false ) != 0 )
            return -1;
        cfg->temp_comp_mode = 0;
        cfg->temp_comp_ntc  = false;
        printf( "Temperature compensation: off\n" );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "relative" ) == 0 )
    {
        if( chip->set_temp_comp( 1, false ) != 0 )
            return -1;
        cfg->temp_comp_mode = 1;
        cfg->temp_comp_ntc  = false;
        printf( "Temperature compensation: relative\n" );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "absolute" ) == 0 )
    {
        if( chip->set_temp_comp( 2, true ) != 0 )
            return -1;
        cfg->temp_comp_mode = 2;
        cfg->temp_comp_ntc  = true;
        printf( "Temperature compensation: absolute + NTC\n" );
        return 0;
    }

    printf( "ERROR: Usage: tempcomp <off|relative|absolute>\n" );
    return -1;
}

/*
 * --- Command: ntc ---
 */
static int cmd_ntc( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    if( chip->set_ntc_params == NULL )
    {
        printf( "ERROR: NTC configuration not supported on %s\n", chip->chip_name );
        return -1;
    }

    /* No argument: show current config */
    if( tokens->count < 2 )
    {
        if( chip->is_tcxo )
        {
            printf( "NTC configuration: not applicable (TCXO board)\n" );
        }
        else if( cfg->ntc_ratio == 0 && cfg->ntc_beta == 0 )
        {
            printf( "NTC config: not set (use 'ntc ratio/beta/delay' to configure)\n" );
        }
        else
        {
            printf( "NTC config:\n" );
            printf( "  R-ratio: %.2f (R_bias / R_NTC_25C)\n", cfg->ntc_ratio / 512.0 );
            printf( "  Beta:    %u K\n", cfg->ntc_beta * 2 );
            printf( "  Delay:   %u\n", cfg->ntc_delay );
        }
        return 0;
    }

    if( tokens->count < 3 )
    {
        printf( "ERROR: Usage: ntc <ratio|beta|delay> <value>\n" );
        return -1;
    }

    if( strcasecmp( tokens->tokens[1], "ratio" ) == 0 )
    {
        char* endptr;
        float ratio = strtof( tokens->tokens[2], &endptr );
        if( *endptr != '\0' || ratio < 0.0f || ratio > 127.0f )
        {
            printf( "ERROR: ratio must be 0.0-127.0 (R_bias / R_NTC_25C)\n" );
            return -1;
        }
        cfg->ntc_ratio = ( uint16_t )( ratio * 512.0f + 0.5f );
        if( chip->set_ntc_params( cfg->ntc_ratio, cfg->ntc_beta, cfg->ntc_delay ) != 0 )
            return -1;
        printf( "NTC R-ratio: %.2f (reg: %u)\n", ratio, cfg->ntc_ratio );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "beta" ) == 0 )
    {
        char* endptr;
        long val = strtol( tokens->tokens[2], &endptr, 0 );
        if( *endptr != '\0' || val < 0 || val > 131070 )
        {
            printf( "ERROR: beta must be 0-131070 (Kelvin)\n" );
            return -1;
        }
        cfg->ntc_beta = ( uint16_t )( val / 2 );
        if( chip->set_ntc_params( cfg->ntc_ratio, cfg->ntc_beta, cfg->ntc_delay ) != 0 )
            return -1;
        printf( "NTC beta: %ld K (reg: %u)\n", val, cfg->ntc_beta );
        return 0;
    }

    if( strcasecmp( tokens->tokens[1], "delay" ) == 0 )
    {
        char* endptr;
        long val = strtol( tokens->tokens[2], &endptr, 0 );
        if( *endptr != '\0' || val < 0 || val > 255 )
        {
            printf( "ERROR: delay must be 0-255\n" );
            return -1;
        }
        cfg->ntc_delay = ( uint8_t ) val;
        if( chip->set_ntc_params( cfg->ntc_ratio, cfg->ntc_beta, cfg->ntc_delay ) != 0 )
            return -1;
        printf( "NTC delay: %u\n", cfg->ntc_delay );
        return 0;
    }

    printf( "ERROR: Unknown ntc subcommand '%s'. Valid: ratio, beta, delay\n", tokens->tokens[1] );
    return -1;
}
#endif /* LR20XX */

/*
 * --- Command: pld ---
 */
static int cmd_pld( const tokenized_t* tokens, radio_config_t* cfg, const chip_driver_t* chip )
{
    ( void ) chip;
    if( tokens->count < 2 )
    {
        printf( "ERROR: Usage: pld <1-255|auto>\n" );
        return -1;
    }
    if( cfg->active_mode != MODE_IDLE )
    {
        printf( "ERROR: Mode active. Stop first.\n" );
        return -1;
    }

    if( strcasecmp( tokens->tokens[1], "auto" ) == 0 )
    {
        cfg->mod_pld_size = 0;
        printf( "Modulated TX payload: auto (max for dwell time)\n" );
        return 0;
    }

    char* endptr;
    long  val = strtol( tokens->tokens[1], &endptr, 10 );
    if( *endptr != '\0' || val < 1 || val > 255 )
    {
        printf( "ERROR: Payload must be 1-255 bytes, or 'auto'\n" );
        return -1;
    }
    cfg->mod_pld_size = ( uint16_t ) val;
    printf( "Modulated TX payload: %u bytes\n", cfg->mod_pld_size );
    return 0;
}

/*
 * --- Help system ---
 */

static void print_help( const char* topic, const radio_config_t* cfg, const chip_driver_t* chip )
{
    if( topic == NULL )
    {
        printf( "Configuration:\n" );
#ifdef SX126X
        printf( "  region <us>                Set region (loads defaults)\n" );
#else
        printf( "  region <us|2g4>            Set region (loads defaults)\n" );
#endif
#if defined( LR20XX )
        printf( "  modulation <type>          Set modulation (lora, flrc)\n" );
#else
        printf( "  modulation <type>          Set modulation (lora)\n" );
#endif
        printf( "  freq <MHz>                 Frequency\n" );
        printf( "  power <dBm>                TX power\n" );
#if defined( LR20XX )
        printf( "  agc <auto|g1-g13>          AGC gain (auto or fixed step)\n" );
        printf( "  boost-lf <auto|0-7>        RX boost level for LF path (auto=0)\n" );
        printf( "  boost-hf <auto|0-7>        RX boost level for HF path (auto=4)\n" );
#endif
        printf( "  pld <1-255|auto>           Payload size (auto=max airtime)\n" );
        printf( "  status                     Show current config\n" );

        /* Modulation-specific parameter help */
        const modulation_module_t* mod = modulation_get_by_id( cfg->modulation );
        if( mod != NULL )
        {
            mod->print_help();
        }
        else
        {
            /* Show all modulation params so user knows what's available */
            const modulation_module_t* const* all = modulation_get_all();
            for( int i = 0; all[i] != NULL; i++ )
            {
                all[i]->print_help();
            }
        }

        printf( "\nPA config:\n" );
        printf( "  pa show                    Show PA parameters\n" );
        printf( "  pa <param> <value>         Set PA parameter (use 'pa show' to list)\n" );
#if defined( LR20XX ) || defined( SX126X )
        printf( "\nXOSC trim:\n" );
        printf( "  xosc show                  Show XTA/XTB values\n" );
        printf( "  xosc default               Restore factory defaults\n" );
        printf( "  xosc xta <0-47>            Set XTA capacitor trim\n" );
        printf( "  xosc xtb <0-47>            Set XTB capacitor trim\n" );
#if defined( LR20XX )
        printf( "  xosc wait <0-255>          Set stabilization delay (us)\n" );
#endif
#endif
#if defined( LR20XX )
        printf( "\nTemperature:\n" );
        printf( "  temp [vbe|xosc|ntc]        Read chip temperature sensor\n" );
        printf( "  tempcomp <off|rel|abs>     Set XTAL temperature compensation mode\n" );
        printf( "  ntc ratio <value>          NTC R_bias/R_NTC_25C ratio (e.g., 10.0)\n" );
        printf( "  ntc beta <kelvin>          NTC beta coefficient (e.g., 3380)\n" );
        printf( "  ntc delay <0-255>          NTC time delay coefficient\n" );
#endif

        printf( "\nTest modes:\n" );
        printf( "  start <mode>               Start a test mode\n" );
        printf( "  stop                       Stop active mode\n" );

        printf( "\nPER test:\n" );
        printf( "  start per-tx               Start PER transmitter\n" );
        printf( "  start per-rx               Start PER receiver\n" );
        printf( "  per count <N>              Packets to send (0=infinite)\n" );
        printf( "  per interval <ms>          Inter-packet gap\n" );
        printf( "  per payload <6-255|auto>   Payload size (auto=max airtime)\n" );
        printf( "  per stats                  Show last test result\n" );
        printf( "  per reset                  Clear last test result\n" );

        printf( "\nUtility:\n" );
        printf( "  show channels              Display region LoRaWAN channel plan\n" );
#ifdef __linux__
        printf( "  delay <ms>                 Pause (for scripts)\n" );
#else
        printf( "  term ansi                  Enable ANSI terminal (arrow keys, history, tab)\n" );
        printf( "  term plain                 Disable ANSI terminal (basic echo mode)\n" );
#endif
        printf( "  version                    Show CLI version\n" );
        printf( "  help [command]             Show help (try: help start, help bw, etc.)\n" );
#ifdef __linux__
        printf( "  exit / quit                Exit\n" );
#endif
        return;
    }

    /* --- Per-command detailed help --- */

    if( strcasecmp( topic, "start" ) == 0 )
    {
        printf( "Test modes:\n" );
        printf( "  Low-level RF (all regions):\n" );
        printf( "    start cw                  Unmodulated carrier\n" );
        printf( "    start modulated           Continuous modulated signal\n" );
        printf( "    start rx                  Receive mode\n" );
        printf( "\n" );
        printf( "  Regulatory (US only):\n" );
        printf( "    start fhss                64-ch frequency hopping\n" );
        printf( "    start dts                 Digital transmission system\n" );
        printf( "    start hybrid --mask N     8-ch hopping (mask 0-7)\n" );
        printf( "\n" );
        printf( "  PER test (all regions):\n" );
        printf( "    start per-tx              PER test transmitter\n" );
        printf( "    start per-rx              PER test receiver\n" );
        printf( "\n" );
        printf( "  Inline overrides: start cw --freq 905.0 --power 20\n" );
        printf( "  Regulatory opts:  start fhss --dr 0 --count 72 --delay 100\n" );
        printf( "    --freq <MHz>   Override frequency\n" );
        printf( "    --power <dBm>  Override TX power\n" );
        printf( "    --mask <0-7>   Channel mask (required for hybrid)\n" );
        printf( "    --dr <0-6>     Data rate (regulatory modes)\n" );
        printf( "    --count <N>    Packet count (regulatory modes, 0=unlimited)\n" );
        printf( "    --delay <ms>   Inter-packet delay (regulatory modes)\n" );

        if( cfg->region != REGION_NONE )
        {
            const region_def_t* region = region_get_by_id( cfg->region );
            if( !region->fhss_available )
            {
                printf( "  Note: fhss/dts/hybrid not available in %s\n", region->short_name );
            }
        }
        return;
    }

    if( strcasecmp( topic, "region" ) == 0 )
    {
        printf( "region — set radio region (loads frequency, power, and modulation defaults)\n" );
        if( cfg->region != REGION_NONE )
        {
            const region_def_t* region = region_get_by_id( cfg->region );
            printf( "  Current: %s\n", region->name );
        }
        printf( "  Available regions for %s:\n", chip->chip_name );
        const char* all_regions[] = { "us", "2g4", NULL };
        for( int i = 0; all_regions[i] != NULL; i++ )
        {
            const region_def_t* r = region_lookup( all_regions[i] );
            if( r == NULL )
                continue;
            if( chip->supports_region != NULL && !chip->supports_region( r->id ) )
                continue;
            printf( "    %-6s  %s  (%.1f-%.1f MHz, %+d to %+d dBm)\n",
                    r->short_name, r->name,
                    ( double ) r->freq_min_mhz, ( double ) r->freq_max_mhz,
                    r->power_min_dbm, r->power_max_dbm );
        }
        printf( "  Example: region us\n" );
        return;
    }

    if( strcasecmp( topic, "modulation" ) == 0 )
    {
        printf( "modulation — set radio modulation type\n" );
        if( cfg->modulation != MODULATION_NONE )
        {
            printf( "  Current: %s\n", cli_state_modulation_str( cfg->modulation ) );
        }
        printf( "  Available modulations for %s:\n", chip->chip_name );
        const modulation_module_t* const* all = modulation_get_all();
        for( int i = 0; all[i] != NULL; i++ )
        {
            if( chip->supports_modulation != NULL && !chip->supports_modulation( all[i]->id ) )
                continue;
            printf( "    %s\n", all[i]->name );
        }
        printf( "  Example: modulation lora\n" );
        return;
    }

    if( strcasecmp( topic, "freq" ) == 0 )
    {
        printf( "freq — transmit/receive frequency (MHz)\n" );
        if( cfg->region != REGION_NONE )
        {
            const region_def_t* region = region_get_by_id( cfg->region );
            printf( "  Current: %.1f MHz\n", ( double ) cfg->freq_mhz );
            printf( "  Valid for %s: %.1f-%.1f MHz\n",
                    region->short_name, ( double ) region->freq_min_mhz, ( double ) region->freq_max_mhz );
        }
        else
        {
            printf( "  Set region first to see valid frequency range.\n" );
        }
        printf( "  Example: freq 902.3\n" );
        return;
    }

    if( strcasecmp( topic, "power" ) == 0 )
    {
        printf( "power — TX output power (dBm)\n" );
        if( cfg->region != REGION_NONE )
        {
            const region_def_t* region = region_get_by_id( cfg->region );
            printf( "  Current: %+d dBm\n", cfg->power_dbm );
            printf( "  Valid for %s: %+d to %+d dBm\n",
                    region->short_name, region->power_min_dbm, region->power_max_dbm );
        }
        else
        {
            printf( "  Set region first to see valid power range.\n" );
        }
        if( cfg->region != REGION_NONE )
        {
            const region_def_t* r = region_get_by_id( cfg->region );
            printf( "  Example: power %d\n", r->power_max_dbm );
        }
        else
        {
            printf( "  Example: power 10\n" );
        }
        return;
    }

#if defined( LR20XX )
    if( strcasecmp( topic, "agc" ) == 0 )
    {
        printf( "agc — AGC gain control (LR20xx only)\n" );
        printf( "  Current: %s\n", cli_state_agc_gain_str( cfg->agc_gain ) );
        printf( "  Valid: auto, g1-g13\n" );
        printf( "    auto  = automatic gain control\n" );
        printf( "    g1    = maximum gain (highest sensitivity)\n" );
        printf( "    g13   = minimum gain\n" );
        printf( "  Example: agc auto\n" );
        return;
    }
    if( strcasecmp( topic, "boost-lf" ) == 0 )
    {
        printf( "boost-lf — RX boost level for the LF path (LR20xx only)\n" );
        if( cfg->rx_boost_lf < 0 )
            printf( "  Current: auto (0)\n" );
        else
            printf( "  Current: %d\n", cfg->rx_boost_lf );
        printf( "  Valid: auto, 0-7\n" );
        printf( "    auto = use recommended default (0 for LF)\n" );
        printf( "    7    = maximum boost (matches datasheet sensitivity spec condition)\n" );
        printf( "  Note: boost only affects gain steps G12 and G13. With agc auto\n" );
        printf( "    (default), the AGC selects G12/G13 on weak signals, so boost\n" );
        printf( "    is active when it matters most. With agc fixed to g1-g11,\n" );
        printf( "    boost has no effect.\n" );
        printf( "  Example: boost-lf auto\n" );
        printf( "  Example: boost-lf 7\n" );
        return;
    }
    if( strcasecmp( topic, "boost-hf" ) == 0 )
    {
        printf( "boost-hf — RX boost level for the HF path (LR20xx only)\n" );
        if( cfg->rx_boost_hf < 0 )
            printf( "  Current: auto (4)\n" );
        else
            printf( "  Current: %d\n", cfg->rx_boost_hf );
        printf( "  Valid: auto, 0-7\n" );
        printf( "    auto = use recommended default (4 for HF)\n" );
        printf( "    7    = maximum boost (matches datasheet sensitivity spec condition)\n" );
        printf( "  Note: boost only affects gain steps G12 and G13. With agc auto\n" );
        printf( "    (default), the AGC selects G12/G13 on weak signals, so boost\n" );
        printf( "    is active when it matters most. With agc fixed to g1-g11,\n" );
        printf( "    boost has no effect.\n" );
        printf( "  Example: boost-hf auto\n" );
        printf( "  Example: boost-hf 7\n" );
        return;
    }
#endif

    if( strcasecmp( topic, "pld" ) == 0 )
    {
        printf( "pld — modulated TX payload size (bytes)\n" );
        if( cfg->mod_pld_size == 0 )
        {
            printf( "  Current: auto (max for dwell time)\n" );
        }
        else
        {
            printf( "  Current: %u bytes\n", cfg->mod_pld_size );
        }
        printf( "  Valid: 1-255, or 'auto'\n" );
        printf( "    auto = compute maximum payload that fits within dwell time\n" );
        printf( "  Example: pld 32\n" );
        return;
    }

    if( strcasecmp( topic, "pa" ) == 0 )
    {
        printf( "pa — power amplifier configuration\n" );
        printf( "  Subcommands:\n" );
        printf( "    pa show                  Show PA parameters, values, and ranges\n" );
        printf( "    pa reset                 Clear all overrides, restore BSP table values\n" );
        printf( "    pa <param> <value>       Override a single PA parameter\n" );
        printf( "\n" );
        printf( "  Overrides are per-parameter: setting one param does NOT affect others.\n" );
        printf( "  Overrides are cleared by: pa reset, power <N>, or PA path change (LF/HF).\n" );
        printf( "\n" );
        /* Show the PA table inline */
        chip->refresh_pa_config( ( radio_config_t* ) cfg );
        printf( "  Parameters for %s (TX power: %+d dBm):\n", chip->chip_name, cfg->power_dbm );
        printf( "  %-16s  %-5s  %-7s  %s\n", "Parameter", "Value", "Range", "Description" );
        printf( "  %-16s  %-5s  %-7s  %s\n", "---------", "-----", "-----", "-----------" );
        for( int i = 0; i < chip->pa_param_count; i++ )
        {
            int val = 0;
            chip->get_pa_param( chip->pa_params[i].name, &val );
            char range[16];
            if( chip->pa_params[i].read_only )
            {
                snprintf( range, sizeof( range ), "(r/o)" );
            }
            else
            {
                snprintf( range, sizeof( range ), "%d-%d",
                          chip->pa_params[i].min, chip->pa_params[i].max );
            }
            printf( "  %-16s  %-5d  %-7s  %s\n",
                    chip->pa_params[i].name, val, range,
                    chip->pa_params[i].description );
        }
        printf( "\n  Example: pa %s %d\n",
                chip->pa_params[0].name, chip->pa_params[0].min );
        return;
    }

    if( strcasecmp( topic, "per" ) == 0 )
    {
        printf( "per — Packet Error Rate test configuration\n" );
        printf( "  Current settings:\n" );
        printf( "    count:    %u%s\n", (unsigned) cfg->per_count, cfg->per_count == 0 ? " (infinite)" : "" );
        printf( "    interval: %u ms\n", (unsigned) cfg->per_interval_ms );
        if( cfg->per_payload_size == 0 )
        {
            printf( "    payload:  auto (max for dwell time)\n" );
        }
        else
        {
            printf( "    payload:  %u bytes\n", cfg->per_payload_size );
        }
        printf( "\n" );
        printf( "  Subcommands:\n" );
        printf( "    per count <N>            Packets to send (0=infinite)\n" );
        printf( "    per interval <ms>        Inter-packet gap in milliseconds\n" );
        printf( "    per payload <6-255|auto> Payload size in bytes\n" );
        printf( "    per stats                Show last test result\n" );
        printf( "    per reset                Clear last test result\n" );
        printf( "\n" );
        printf( "  Usage: configure with per commands, then 'start per-tx' / 'start per-rx'\n" );
        return;
    }

#if defined( LR20XX ) || defined( SX126X )
    if( strcasecmp( topic, "xosc" ) == 0 )
    {
        printf( "xosc — crystal oscillator capacitor trim\n" );
        printf( "  Current settings:\n" );
        printf( "    xta:  0x%02X (%u) -> %.1f pF\n",
                cfg->xosc_xta, cfg->xosc_xta, chip->xosc_xta_min_pf + cfg->xosc_xta * 0.47 );
        printf( "    xtb:  0x%02X (%u) -> %.1f pF\n",
                cfg->xosc_xtb, cfg->xosc_xtb,
                chip->xosc_xtb_min_pf + cfg->xosc_xtb * 0.47 );
        if( chip->xosc_has_wait )
            printf( "    wait: %u us\n", cfg->xosc_wait_us );
        printf( "\n" );
        printf( "  Subcommands:\n" );
        printf( "    xosc show                Show current XTA/XTB values\n" );
        if( chip->xosc_has_wait )
            printf( "    xosc default             Restore factory defaults (0x14/0x14/150 us)\n" );
        else
            printf( "    xosc default             Restore factory defaults (0x12/0x12)\n" );
        printf( "    xosc xta <0-47>          Set XTA capacitor trim\n" );
        printf( "    xosc xtb <0-47>          Set XTB capacitor trim\n" );
        if( chip->xosc_has_wait )
            printf( "    xosc wait <0-255>        Set stabilization delay (us)\n" );
        printf( "\n" );
        printf( "  Capacitance formula:\n" );
        printf( "    xta_pf = %.1f + xta * 0.47\n", chip->xosc_xta_min_pf );
        printf( "    xtb_pf = %.1f + xtb * 0.47\n", chip->xosc_xtb_min_pf );
        printf( "  Trim range: 0 (%.1f/%.1f pF) to 47 (%.1f/%.1f pF)\n",
                chip->xosc_xta_min_pf, chip->xosc_xtb_min_pf,
                chip->xosc_xta_min_pf + 47 * 0.47, chip->xosc_xtb_min_pf + 47 * 0.47 );
        printf( "  Accepts decimal or hex (0x) values.\n" );
        printf( "\n" );
        printf( "  Example: xosc xta 0x0A\n" );
        printf( "  Example: xosc xtb 10\n" );
#if defined( LR20XX )
        printf( "  Example: xosc wait 200\n" );
#endif
        return;
    }
#endif

#if defined( LR20XX )
    if( strcasecmp( topic, "temp" ) == 0 )
    {
        printf( "temp — read chip temperature sensor\n" );
        printf( "  Sources:\n" );
        printf( "    temp                     Read from VBE (default)\n" );
        printf( "    temp vbe                 Built-in junction temperature\n" );
        printf( "    temp xosc                Junction temperature near XOSC\n" );
        printf( "    temp ntc                 External NTC thermistor (XTAL boards only)\n" );
        printf( "\n" );
        printf( "  Returns temperature in °C. Works while idle or during TX/RX.\n" );
        printf( "  NTC source fails on TCXO boards.\n" );
        return;
    }

    if( strcasecmp( topic, "tempcomp" ) == 0 )
    {
        printf( "tempcomp — XTAL temperature compensation during TX\n" );
        printf( "  Modes:\n" );
        printf( "    tempcomp                 Show current mode\n" );
        printf( "    tempcomp off             Disable compensation\n" );
        printf( "    tempcomp relative        Relative mode (VBE delta-T tracking)\n" );
        printf( "    tempcomp absolute        Absolute mode (uses NTC sensor)\n" );
        printf( "\n" );
        printf( "  XTAL boards only — fails if TCXO is configured.\n" );
        printf( "  For absolute mode, configure NTC parameters first with 'ntc'.\n" );
        return;
    }

    if( strcasecmp( topic, "ntc" ) == 0 )
    {
        printf( "ntc — configure external NTC thermistor parameters\n" );
        printf( "  Subcommands:\n" );
        printf( "    ntc                      Show current NTC config\n" );
        printf( "    ntc ratio <ratio>       R_bias / R_NTC_25C (e.g., 1.0, 10.0)\n" );
        printf( "    ntc beta <kelvin>        Beta coefficient in Kelvin (e.g., 3380, 4250)\n" );
        printf( "    ntc delay <value>        First-order time delay (0-255)\n" );
        printf( "\n" );
        printf( "  XTAL boards only — not applicable on TCXO boards.\n" );
        printf( "  Example: 10k NTC with 100k bias, B=3380K:\n" );
        printf( "    ntc ratio 10.0\n" );
        printf( "    ntc beta 3380\n" );
        return;
    }
#endif

    if( strcasecmp( topic, "show" ) == 0 )
    {
        printf( "show — display information\n" );
        printf( "  Subcommands:\n" );
        printf( "    show channels            Display region LoRaWAN channel plan\n" );
        return;
    }

    if( strcasecmp( topic, "delay" ) == 0 )
    {
        printf( "delay — pause execution (for scripts)\n" );
        printf( "  Valid: any positive integer (milliseconds)\n" );
        printf( "  Example: delay 1000\n" );
        return;
    }

    /* Check if a modulation module owns this parameter */
    const modulation_module_t* active_mod = modulation_get_by_id( cfg->modulation );
    if( active_mod != NULL && active_mod->print_param_help != NULL )
    {
        if( active_mod->print_param_help( topic, cfg ) )
        {
            return;
        }
    }
    /* Fall back: check all modules (e.g. user asks "help br" but FLRC isn't active) */
    const modulation_module_t* const* all = modulation_get_all();
    for( int i = 0; all[i] != NULL; i++ )
    {
        if( all[i] != active_mod && all[i]->print_param_help != NULL )
        {
            if( all[i]->print_param_help( topic, cfg ) )
            {
                return;
            }
        }
    }

    printf( "Unknown help topic '%s'. Available topics:\n", topic );
    printf( "  region, modulation, freq, power, pld, pa, per, start, show, delay\n" );
#if defined( LR20XX )
    printf( "  agc, boost-lf, boost-hf, xosc, temp, tempcomp, ntc\n" );
#endif
    if( active_mod != NULL && active_mod->param_names != NULL )
    {
        printf( "  %s params:", active_mod->name );
        for( int i = 0; active_mod->param_names[i] != NULL; i++ )
        {
            printf( " %s", active_mod->param_names[i] );
        }
        printf( "\n" );
    }
}

/*
 * --- Command dispatch ---
 */

int cli_execute( const char* line, radio_config_t* cfg, const chip_driver_t* chip )
{
    /* Make a mutable copy for tokenization */
    char buf[512];
    strncpy( buf, line, sizeof( buf ) - 1 );
    buf[sizeof( buf ) - 1] = '\0';

    tokenized_t tokens = tokenize( buf );
    if( tokens.count == 0 )
    {
        return 0; /* empty line */
    }
    printf( "\n" );

    const char* cmd = tokens.tokens[0];

    /* Exit commands */
#ifdef __linux__
    if( strcasecmp( cmd, "exit" ) == 0 || strcasecmp( cmd, "quit" ) == 0 )
    {
        return 1;
    }
#endif

    /* Version */
    if( strcasecmp( cmd, "version" ) == 0 )
    {
        printf( "Radio Test CLI %s\n", RADIO_TEST_CLI_VERSION );
        return 0;
    }

    /* Help */
    if( strcasecmp( cmd, "help" ) == 0 )
    {
        print_help( tokens.count > 1 ? tokens.tokens[1] : NULL, cfg, chip );
        return 0;
    }

    /* Standard commands */
    if( strcasecmp( cmd, "region" ) == 0 )
        return cmd_region( &tokens, cfg, chip );
    if( strcasecmp( cmd, "modulation" ) == 0 )
        return cmd_modulation( &tokens, cfg, chip );
    if( strcasecmp( cmd, "freq" ) == 0 )
        return cmd_freq( &tokens, cfg, chip );
    if( strcasecmp( cmd, "power" ) == 0 )
        return cmd_power( &tokens, cfg, chip );
#if defined( LR20XX )
    if( strcasecmp( cmd, "agc" ) == 0 )
        return cmd_agc( &tokens, cfg, chip );
    if( strcasecmp( cmd, "boost-lf" ) == 0 )
        return cmd_boost_lf( &tokens, cfg, chip );
    if( strcasecmp( cmd, "boost-hf" ) == 0 )
        return cmd_boost_hf( &tokens, cfg, chip );
#endif
    if( strcasecmp( cmd, "status" ) == 0 )
        return cmd_status( &tokens, cfg, chip );
    if( strcasecmp( cmd, "start" ) == 0 )
        return cmd_start( &tokens, cfg, chip );
    if( strcasecmp( cmd, "stop" ) == 0 )
        return cmd_stop( &tokens, cfg, chip );
    if( strcasecmp( cmd, "pa" ) == 0 )
        return cmd_pa( &tokens, cfg, chip );
    if( strcasecmp( cmd, "per" ) == 0 )
        return cmd_per( &tokens, cfg, chip );
#if defined( LR20XX ) || defined( SX126X )
    if( strcasecmp( cmd, "xosc" ) == 0 )
        return cmd_xosc( &tokens, cfg, chip );
#endif
#if defined( LR20XX )
    if( strcasecmp( cmd, "temp" ) == 0 )
        return cmd_temp( &tokens, cfg, chip );
    if( strcasecmp( cmd, "tempcomp" ) == 0 )
        return cmd_tempcomp( &tokens, cfg, chip );
    if( strcasecmp( cmd, "ntc" ) == 0 )
        return cmd_ntc( &tokens, cfg, chip );
#endif
    if( strcasecmp( cmd, "pld" ) == 0 )
        return cmd_pld( &tokens, cfg, chip );
    if( strcasecmp( cmd, "show" ) == 0 )
        return cmd_show( &tokens, cfg, chip );
#ifdef __linux__
    if( strcasecmp( cmd, "delay" ) == 0 )
        return cmd_delay( &tokens, cfg, chip );
#else
    if( strcasecmp( cmd, "term" ) == 0 )
        return cmd_term( &tokens, cfg );
#endif

    /* Check if the active modulation module owns this command */
    const modulation_module_t* active_mod = modulation_get_by_id( cfg->modulation );
    if( active_mod != NULL && active_mod->owns_param( cmd ) )
    {
        const region_def_t* region = region_get_by_id( cfg->region );
        return active_mod->set_param( cfg, region, cmd,
                                       tokens.count > 1 ? tokens.tokens[1] : NULL );
    }

    /* Check if ANY module owns it but it's not the active one */
    const modulation_module_t* const* all = modulation_get_all();
    for( int i = 0; all[i] != NULL; i++ )
    {
        if( all[i]->owns_param( cmd ) )
        {
            if( cfg->modulation == MODULATION_NONE )
            {
                printf( "ERROR: Modulation not set.\n" );
            }
            else
            {
                printf( "ERROR: '%s' not valid for %s modulation\n",
                        cmd, cli_state_modulation_str( cfg->modulation ) );
            }
            return -1;
        }
    }

    printf( "ERROR: Unknown command '%s'. Type 'help' for available commands.\n", cmd );
    return -1;
}

/*
 * --- Tab completion ---
 */

static const char* standard_commands[] = {
    "region", "modulation", "freq", "power",
#if defined( LR20XX )
    "agc", "boost-lf", "boost-hf",
#endif
    "status", "start", "stop", "show", "pa",
    "per", "pld",
#if defined( LR20XX ) || defined( SX126X )
    "xosc",
#endif
#if defined( LR20XX )
    "temp", "tempcomp", "ntc",
#endif
#ifdef __linux__
    "delay",
#else
    "term",
#endif
    "version", "help",
#ifdef __linux__
    "exit", "quit",
#endif
    NULL
};

static const char* per_subcmds[]   = { "count", "interval", "payload", "stats", "reset", NULL };
static const char* show_subcmds[]  = { "channels", NULL };
#ifndef __linux__
static const char* term_subcmds[]  = { "ansi", "plain", NULL };
#endif
#if defined( LR20XX )
static const char* xosc_subcmds[]  = { "show", "default", "xta", "xtb", "wait", NULL };
#elif defined( SX126X )
static const char* xosc_subcmds[]  = { "show", "default", "xta", "xtb", NULL };
#endif
#if defined( LR20XX )
static const char* temp_subcmds[]     = { "vbe", "xosc", "ntc", NULL };
static const char* tempcomp_subcmds[] = { "off", "relative", "absolute", NULL };
static const char* ntc_subcmds[]      = { "ratio", "beta", "delay", NULL };
static const char* agc_completions[] = {
    "auto", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
    "g8", "g9", "g10", "g11", "g12", "g13", NULL
};
static const char* boost_completions[] = {
    "auto", "0", "1", "2", "3", "4", "5", "6", "7", NULL
};
#endif

/* Current config and chip pointer for completion context (set before linenoise call) */
static const radio_config_t*  completion_cfg  = NULL;
static const chip_driver_t*   completion_chip = NULL;

void cli_set_completion_context( const radio_config_t* cfg, const chip_driver_t* chip )
{
    completion_cfg  = cfg;
    completion_chip = chip;
}

void cli_completion( const char* buf, linenoiseCompletions* lc )
{
    linenoiseCompletions* completions = lc;
    size_t                len         = strlen( buf );

    /* Find first space to determine if we're completing a subcommand */
    const char* space = strchr( buf, ' ' );

    if( space == NULL )
    {
        /* Completing top-level command — standard commands first */
        for( int i = 0; standard_commands[i] != NULL; i++ )
        {
            if( strncasecmp( buf, standard_commands[i], len ) == 0 )
            {
                char completion[128];
                snprintf( completion, sizeof( completion ), "%s ", standard_commands[i] );
                linenoiseAddCompletion( completions, completion );
            }
        }

        /* Append active modulation's param names */
        if( completion_cfg != NULL )
        {
            const modulation_module_t* mod = modulation_get_by_id( completion_cfg->modulation );
            if( mod != NULL && mod->param_names != NULL )
            {
                for( int i = 0; mod->param_names[i] != NULL; i++ )
                {
                    if( strncasecmp( buf, mod->param_names[i], len ) == 0 )
                    {
                        char completion[128];
                        snprintf( completion, sizeof( completion ), "%s ", mod->param_names[i] );
                        linenoiseAddCompletion( completions, completion );
                    }
                }
            }
        }
    }
    else
    {
        /* Completing subcommand/value */
        size_t      cmd_len = ( size_t ) ( space - buf );
        const char* sub     = space + 1;
        size_t      sub_len = strlen( sub );

        /* Skip extra spaces */
        while( *sub == ' ' && *sub != '\0' )
        {
            sub++;
            sub_len--;
        }

        const char* const* values = NULL;
        char               cmd_prefix[64];
        memcpy( cmd_prefix, buf, cmd_len );
        cmd_prefix[cmd_len] = '\0';

        if( strncasecmp( buf, "start", cmd_len ) == 0 && cmd_len == 5 )
        {
            /* Build start mode completions filtered by region availability */
            static const char* filtered_start_modes[16];
            int j = 0;
            /* Always available */
            filtered_start_modes[j++] = "cw";
            filtered_start_modes[j++] = "modulated";
            filtered_start_modes[j++] = "rx";
            /* Regulatory modes: only if region supports them */
            bool show_reg = true;
            if( completion_cfg != NULL && completion_cfg->region != REGION_NONE )
            {
                const region_def_t* r = region_get_by_id( completion_cfg->region );
                if( !r->fhss_available )
                {
                    show_reg = false;
                }
            }
            if( show_reg )
            {
                filtered_start_modes[j++] = "fhss";
                filtered_start_modes[j++] = "dts";
                filtered_start_modes[j++] = "hybrid";
            }
            filtered_start_modes[j++] = "per-tx";
            filtered_start_modes[j++] = "per-rx";
            filtered_start_modes[j]   = NULL;
            values = filtered_start_modes;
        }
        else if( strncasecmp( buf, "region", cmd_len ) == 0 && cmd_len == 6 )
        {
            /* Build region completions filtered by chip capability */
            static const char* region_completions[8];
            const char* all_regions[] = { "us", "2g4", NULL };  /* EU/JP hidden */
            int j = 0;
            for( int i = 0; all_regions[i] != NULL && j < 7; i++ )
            {
                if( completion_chip != NULL && completion_chip->supports_region != NULL )
                {
                    const region_def_t* r = region_lookup( all_regions[i] );
                    if( r == NULL || !completion_chip->supports_region( r->id ) )
                        continue;
                }
                region_completions[j++] = all_regions[i];
            }
            region_completions[j] = NULL;
            values = region_completions;
        }
        else if( strncasecmp( buf, "modulation", cmd_len ) == 0 && cmd_len == 10 )
        {
            /* Build modulation completions filtered by chip capability */
            static const char* mod_completions[8];
            const modulation_module_t* const* all = modulation_get_all();
            int j = 0;
            for( int i = 0; all[i] != NULL && j < 7; i++ )
            {
                if( completion_chip != NULL && completion_chip->supports_modulation != NULL
                    && !completion_chip->supports_modulation( all[i]->id ) )
                    continue;
                mod_completions[j++] = all[i]->name;
            }
            mod_completions[j++] = "help";
            mod_completions[j]   = NULL;
            values = mod_completions;
        }
#if defined( LR20XX )
        else if( strncasecmp( buf, "agc", cmd_len ) == 0 && cmd_len == 3 )
        {
            values = agc_completions;
        }
        else if( strncasecmp( buf, "boost-lf", cmd_len ) == 0 && cmd_len == 8 )
        {
            values = boost_completions;
        }
        else if( strncasecmp( buf, "boost-hf", cmd_len ) == 0 && cmd_len == 8 )
        {
            values = boost_completions;
        }
#endif
        else if( strncasecmp( buf, "pa", cmd_len ) == 0 && cmd_len == 2 )
        {
            /* Build PA completions: "show", "reset" + all PA param names */
            static const char* pa_completions_dyn[16];
            int j = 0;
            pa_completions_dyn[j++] = "show";
            pa_completions_dyn[j++] = "reset";
            if( completion_chip != NULL )
            {
                for( int i = 0; i < completion_chip->pa_param_count && j < 15; i++ )
                {
                    pa_completions_dyn[j++] = completion_chip->pa_params[i].name;
                }
            }
            pa_completions_dyn[j] = NULL;
            values = pa_completions_dyn;
        }
        else if( strncasecmp( buf, "per", cmd_len ) == 0 && cmd_len == 3 )
        {
            values = per_subcmds;
        }
#if defined( LR20XX ) || defined( SX126X )
        else if( strncasecmp( buf, "xosc", cmd_len ) == 0 && cmd_len == 4 )
        {
            values = xosc_subcmds;
        }
#endif
#if defined( LR20XX )
        else if( strncasecmp( buf, "temp", cmd_len ) == 0 && cmd_len == 4 )
        {
            values = temp_subcmds;
        }
        else if( strncasecmp( buf, "tempcomp", cmd_len ) == 0 && cmd_len == 8 )
        {
            values = tempcomp_subcmds;
        }
        else if( strncasecmp( buf, "ntc", cmd_len ) == 0 && cmd_len == 3 )
        {
            values = ntc_subcmds;
        }
#endif
#ifndef __linux__
        else if( strncasecmp( buf, "term", cmd_len ) == 0 && cmd_len == 4 )
        {
            values = term_subcmds;
        }
#endif
        else if( strncasecmp( buf, "show", cmd_len ) == 0 && cmd_len == 4 )
        {
            values = show_subcmds;
        }
        else if( strncasecmp( buf, "help", cmd_len ) == 0 && cmd_len == 4 )
        {
            /* Build help topic completions */
            static const char* help_topics[32];
            int j = 0;
            help_topics[j++] = "region";
            help_topics[j++] = "modulation";
            help_topics[j++] = "freq";
            help_topics[j++] = "power";
#if defined( LR20XX )
            help_topics[j++] = "agc";
            help_topics[j++] = "boost-lf";
            help_topics[j++] = "boost-hf";
#endif
            help_topics[j++] = "pld";
            help_topics[j++] = "pa";
            help_topics[j++] = "per";
#if defined( LR20XX ) || defined( SX126X )
            help_topics[j++] = "xosc";
#endif
#if defined( LR20XX )
            help_topics[j++] = "temp";
            help_topics[j++] = "tempcomp";
            help_topics[j++] = "ntc";
#endif
            help_topics[j++] = "start";
            help_topics[j++] = "show";
            help_topics[j++] = "delay";
            /* Add active modulation param names */
            if( completion_cfg != NULL )
            {
                const modulation_module_t* mod = modulation_get_by_id( completion_cfg->modulation );
                if( mod != NULL && mod->param_names != NULL )
                {
                    for( int i = 0; mod->param_names[i] != NULL && j < 30; i++ )
                    {
                        help_topics[j++] = mod->param_names[i];
                    }
                }
            }
            help_topics[j] = NULL;
            values = help_topics;
        }
        else if( completion_cfg != NULL )
        {
            /* Check if this is a modulation param with completions */
            const modulation_module_t* mod = modulation_get_by_id( completion_cfg->modulation );
            if( mod != NULL && mod->owns_param( cmd_prefix ) && mod->get_completions != NULL )
            {
                values = mod->get_completions( cmd_prefix, completion_cfg );
            }
        }

        if( values != NULL )
        {
            for( int i = 0; values[i] != NULL; i++ )
            {
                if( strncasecmp( sub, values[i], sub_len ) == 0 )
                {
                    char completion[256];
                    snprintf( completion, sizeof( completion ), "%s %s", cmd_prefix, values[i] );
                    linenoiseAddCompletion( completions, completion );
                }
            }
        }
    }
}

char* cli_hints( const char* buf, int* color, int* bold )
{
    /* Provide hint text in cyan */
    *color = 36; /* cyan */
    *bold  = 0;

    if( strcasecmp( buf, "region " ) == 0 )
    {
        static char region_hint[32];
        region_hint[0] = '<';
        region_hint[1] = '\0';
        const char* all_regions[] = { "us", "2g4", NULL };  /* EU/JP hidden */
        bool first = true;
        for( int i = 0; all_regions[i] != NULL; i++ )
        {
            if( completion_chip != NULL && completion_chip->supports_region != NULL )
            {
                const region_def_t* r = region_lookup( all_regions[i] );
                if( r == NULL || !completion_chip->supports_region( r->id ) )
                    continue;
            }
            if( !first ) strncat( region_hint, "|", sizeof( region_hint ) - 1 );
            strncat( region_hint, all_regions[i], sizeof( region_hint ) - 1 );
            first = false;
        }
        strncat( region_hint, ">", sizeof( region_hint ) - 1 );
        return region_hint;
    }
    if( strcasecmp( buf, "modulation " ) == 0 )
    {
        static char mod_hint[64];
        mod_hint[0] = '<';
        mod_hint[1] = '\0';
        const modulation_module_t* const* all = modulation_get_all();
        bool first = true;
        for( int i = 0; all[i] != NULL; i++ )
        {
            if( completion_chip != NULL && completion_chip->supports_modulation != NULL
                && !completion_chip->supports_modulation( all[i]->id ) )
                continue;
            if( !first ) strncat( mod_hint, "|", sizeof( mod_hint ) - 1 );
            strncat( mod_hint, all[i]->name, sizeof( mod_hint ) - 1 );
            first = false;
        }
        strncat( mod_hint, "|help>", sizeof( mod_hint ) - 1 );
        return mod_hint;
    }
    if( strcasecmp( buf, "freq " ) == 0 )
    {
        static char freq_hint[32];
        if( completion_cfg != NULL && completion_cfg->region != REGION_NONE )
        {
            const region_def_t* r = region_get_by_id( completion_cfg->region );
            snprintf( freq_hint, sizeof( freq_hint ), "<%.1f-%.1f>",
                      ( double ) r->freq_min_mhz, ( double ) r->freq_max_mhz );
        }
        else
        {
            snprintf( freq_hint, sizeof( freq_hint ), "<MHz>" );
        }
        return freq_hint;
    }
    if( strcasecmp( buf, "power " ) == 0 )
    {
        static char power_hint[32];
        if( completion_cfg != NULL && completion_cfg->region != REGION_NONE )
        {
            const region_def_t* r = region_get_by_id( completion_cfg->region );
            snprintf( power_hint, sizeof( power_hint ), "<%d to %+d>",
                      r->power_min_dbm, r->power_max_dbm );
        }
        else
        {
            snprintf( power_hint, sizeof( power_hint ), "<dBm>" );
        }
        return power_hint;
    }
#if defined( LR20XX )
    if( strcasecmp( buf, "agc " ) == 0 )
        return "<auto|g1-g13>";
    if( strcasecmp( buf, "boost-lf " ) == 0 )
        return "<auto|0-7>";
    if( strcasecmp( buf, "boost-hf " ) == 0 )
        return "<auto|0-7>";
#endif
    if( strcasecmp( buf, "pld " ) == 0 )
        return "<1-255|auto>";
    if( strcasecmp( buf, "start " ) == 0 )
    {
        static char start_hint[96];
        start_hint[0] = '<';
        start_hint[1] = '\0';
        strncat( start_hint, "cw|modulated|rx", sizeof( start_hint ) - 1 );
        /* Only show regulatory modes if region supports them */
        bool show_reg = true;
        if( completion_cfg != NULL && completion_cfg->region != REGION_NONE )
        {
            const region_def_t* r = region_get_by_id( completion_cfg->region );
            if( !r->fhss_available )
            {
                show_reg = false;
            }
        }
        if( show_reg )
        {
            strncat( start_hint, "|fhss|dts|hybrid", sizeof( start_hint ) - 1 );
        }
        strncat( start_hint, "|per-tx|per-rx>", sizeof( start_hint ) - 1 );
        return start_hint;
    }
    if( strcasecmp( buf, "show " ) == 0 )
        return "<channels>";
    if( strcasecmp( buf, "pa " ) == 0 )
        return "<show|reset|param value>";
    if( strcasecmp( buf, "per " ) == 0 )
        return "<count|interval|payload|stats|reset>";
#if defined( LR20XX ) || defined( SX126X )
    if( strcasecmp( buf, "xosc " ) == 0 )
#if defined( LR20XX )
        return "<show|default|xta|xtb|wait>";
#else
        return "<show|default|xta|xtb>";
#endif
    if( strncasecmp( buf, "xosc xta ", 9 ) == 0 && strlen( buf ) == 9 )
        return "<0-47>";
    if( strncasecmp( buf, "xosc xtb ", 9 ) == 0 && strlen( buf ) == 9 )
        return "<0-47>";
#if defined( LR20XX )
    if( strncasecmp( buf, "xosc wait ", 10 ) == 0 && strlen( buf ) == 10 )
        return "<0-255>";
#endif
#endif
#if defined( LR20XX )
    if( strcasecmp( buf, "temp " ) == 0 )
        return "<vbe|xosc|ntc>";
    if( strcasecmp( buf, "tempcomp " ) == 0 )
        return "<off|relative|absolute>";
    if( strcasecmp( buf, "ntc " ) == 0 )
        return "<ratio|beta|delay>";
    if( strncasecmp( buf, "ntc ratio ", 10 ) == 0 && strlen( buf ) == 10 )
        return "<R_bias/R_NTC_25C, e.g. 1.2>";
    if( strncasecmp( buf, "ntc beta ", 9 ) == 0 && strlen( buf ) == 9 )
        return "<Kelvin, e.g. 4250>";
    if( strncasecmp( buf, "ntc delay ", 10 ) == 0 && strlen( buf ) == 10 )
        return "<0-255>";
#endif
#ifdef __linux__
    if( strcasecmp( buf, "delay " ) == 0 )
        return "<ms>";
#else
    if( strcasecmp( buf, "term " ) == 0 )
        return "<ansi|plain>";
#endif
    if( strcasecmp( buf, "help " ) == 0 )
        return "<region|modulation|freq|power|pld|pa|per|start|show|bw|sf|cr|...>";

    /* PER sub-parameter hints */
    if( strncasecmp( buf, "per count ", 10 ) == 0 && strlen( buf ) == 10 )
        return "<N, 0=infinite>";
    if( strncasecmp( buf, "per interval ", 13 ) == 0 && strlen( buf ) == 13 )
        return "<ms>";
    if( strncasecmp( buf, "per payload ", 12 ) == 0 && strlen( buf ) == 12 )
        return "<6-255|auto>";

    /* Check modulation module hints */
    /* Extract command before the trailing space */
    size_t len = strlen( buf );
    if( len > 1 && buf[len - 1] == ' ' )
    {
        char cmd[64];
        size_t cmd_len = len - 1;
        if( cmd_len >= sizeof( cmd ) )
        {
            cmd_len = sizeof( cmd ) - 1;
        }
        memcpy( cmd, buf, cmd_len );
        cmd[cmd_len] = '\0';

        /* Ask the active modulation module first, then fall back to others */
        if( completion_cfg != NULL )
        {
            const modulation_module_t* active = modulation_get_by_id( completion_cfg->modulation );
            if( active != NULL && active->owns_param( cmd ) && active->get_hint != NULL )
            {
                const char* hint = active->get_hint( cmd, completion_cfg );
                if( hint != NULL )
                {
                    return ( char* ) hint;
                }
            }
        }
        const modulation_module_t* const* all = modulation_get_all();
        for( int i = 0; all[i] != NULL; i++ )
        {
            if( all[i]->owns_param( cmd ) && all[i]->get_hint != NULL )
            {
                const char* hint = all[i]->get_hint( cmd, completion_cfg );
                if( hint != NULL )
                {
                    return ( char* ) hint;
                }
            }
        }
    }

    return NULL;
}
