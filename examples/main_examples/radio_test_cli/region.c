/**
 * @file      region.c
 *
 * @brief     Region definitions, defaults, and parameter validation
 *
 * RF-envelope constraints only (frequency range, power max, regulatory
 * flags).  Modulation-specific constraints (BW, SF, CR, etc.) are owned
 * by each modulation module — see mod_lora.c.
 *
 * Default freq/power values are derived from LoRaWAN Regional Parameters:
 *   - US: 902.0-928.0 MHz, max ~30 dBm, default 22 dBm
 *   - EU: ch1 = 868.1 MHz, max ~16 dBm, default 14 dBm
 *   - JP: ch1 = 923.2 MHz, max ~13 dBm, default 13 dBm
 *   - WW2G4: 2400-2480 MHz, HF PA max +12 dBm, EIRP default 10 dBm
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "region.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/*
 * --- Region definitions ---
 */

/*
 * Frequency ranges are LoRaWAN Regional Parameters values.
 * Authoritative Hz constants: FREQMIN_US_915 / FREQMAX_US_915 etc. in
 * region_us_915_defs.h, region_eu_868_defs.h, region_as_923_defs.h,
 * region_ww_2g4_defs.h.  Stored here as float MHz because region_def_t
 * uses MHz throughout; conversion (/ 1e6f) would be unreadable.
 */

/*
 * Minimum TX power — chip-dependent, sourced from BSP clamping limits:
 *   SX1262/SX1268 HP PA: -9 dBm   (SX126X_HP_MIN_OUTPUT_POWER)
 *   SX1261 LP PA:        -17 dBm  (SX126X_LP_MIN_OUTPUT_POWER)
 *   LR20xx LF PA:        -10 dBm  (LR20XX_LF_MIN_OUTPUT_POWER)
 *   LR20xx HF PA:        -17 dBm  (LR20XX_HF_MIN_OUTPUT_POWER)
 * Sub-GHz regions use the LF/HP PA; WW2G4 uses the HF PA.
 */
#if defined( SX1261 )
#define POWER_MIN_SUB_GHZ  -17
#elif defined( SX1262 ) || defined( SX1268 )
#define POWER_MIN_SUB_GHZ  -9
#elif defined( LR11XX )
#define POWER_MIN_SUB_GHZ  -17  /* LR11XX_LP_MIN_OUTPUT_POWER */
#define POWER_MIN_HF       -18  /* LR11XX_HF_MIN_OUTPUT_POWER */
#else /* LR20xx */
#define POWER_MIN_SUB_GHZ  -10
#define POWER_MIN_HF       -17
#endif

static const region_def_t region_us = {
    .id         = REGION_US,
    .name       = "US902-928",
    .short_name = "us",

    /* FREQMIN_US_915 = 902000000 Hz, FREQMAX_US_915 = 928000000 Hz */
    .freq_min_mhz = 902.0f,
    .freq_max_mhz = 928.0f,
    /* Regulatory ceiling is 30 dBm but LR20xx LF PA tops out at 22 dBm
     * (LR20XX_LF_MAX_OUTPUT_POWER).  Cap here so the CLI rejects values
     * the BSP would silently clamp rather than appearing to accept them. */
    .power_min_dbm = POWER_MIN_SUB_GHZ,
    .power_max_dbm = 22,

    .default_freq_mhz  = 902.3f,
    .default_power_dbm  = 22,

    .fhss_available   = true,
    .dts_available    = true,
    .hybrid_available = true,
};

static const region_def_t region_eu __attribute__((unused)) = {
    .id         = REGION_EU,
    .name       = "EU863-870",
    .short_name = "eu",

    /* FREQMIN_EU_868 = 863000000 Hz, FREQMAX_EU_868 = 870000000 Hz */
    .freq_min_mhz = 863.0f,
    .freq_max_mhz = 870.0f,
    /* TX_POWER_EIRP_EU_868 = 16 dBm */
    .power_min_dbm = POWER_MIN_SUB_GHZ,
    .power_max_dbm = 16,

    .default_freq_mhz  = 868.1f,
    .default_power_dbm  = 16,

    .fhss_available   = false,
    .dts_available    = false,
    .hybrid_available = false,
};

static const region_def_t region_jp __attribute__((unused)) = {
    .id         = REGION_JP,
    .name       = "JP AS923-1",
    .short_name = "jp",

    /*
     * AS923-1 channel plan (Group 1): FREQMIN_GRP1_AS_923 = 915000000 Hz,
     * FREQMAX_GRP1_AS_923 = 928000000 Hz.  The lower bound 920.6 MHz is the
     * start of the AS923-1 uplink channel allocation; 928.0 MHz is the full
     * band upper edge (corrects HI-1: was incorrectly 923.4 MHz).
     */
    .freq_min_mhz = 920.6f,
    .freq_max_mhz = 928.0f,
    .power_min_dbm = POWER_MIN_SUB_GHZ,
    .power_max_dbm = 13,

    .default_freq_mhz  = 923.2f,
    .default_power_dbm  = 13,

    .fhss_available   = false,
    .dts_available    = false,
    .hybrid_available = false,
};

#ifndef SX126X
static const region_def_t region_ww2g4 = {
    .id         = REGION_WW2G4,
    .name       = "WW2G4",
    .short_name = "2g4",

    /* FREQMIN_WW_2G4 = 2400000000 Hz, FREQMAX_WW_2G4 = 2480000000 Hz */
    .freq_min_mhz = 2400.0f,
    .freq_max_mhz = 2480.0f,
    .power_min_dbm = POWER_MIN_HF,
    .power_max_dbm = 12,

    /* RX2 window frequency as a sensible default test frequency */
    .default_freq_mhz  = 2423.0f,
    .default_power_dbm  = 10,

    /* No sub-GHz regulatory modes applicable */
    .fhss_available   = false,
    .dts_available    = false,
    .hybrid_available = false,
};
#endif /* SX126X */

#ifndef SX126X
static const region_def_t* all_regions[] = { &region_us, &region_ww2g4 };
static const int           num_regions   = 2;
#else
static const region_def_t* all_regions[] = { &region_us };
static const int           num_regions   = 1;
#endif /* SX126X */

/*
 * --- Public functions ---
 */

const region_def_t* region_lookup( const char* name )
{
    for( int i = 0; i < num_regions; i++ )
    {
        if( strcasecmp( name, all_regions[i]->short_name ) == 0 )
        {
            return all_regions[i];
        }
    }
    return NULL;
}

const region_def_t* region_get_by_id( region_id_t id )
{
    for( int i = 0; i < num_regions; i++ )
    {
        if( all_regions[i]->id == id )
        {
            return all_regions[i];
        }
    }
    return NULL;
}

void region_load_defaults( radio_config_t* cfg, const region_def_t* region )
{
    cfg->region    = region->id;
    cfg->freq_mhz = region->default_freq_mhz;
    cfg->power_dbm = region->default_power_dbm;

    /* Reset modulation — user must re-select after region change */
    cfg->modulation = MODULATION_NONE;
}

const char* region_validate_freq( const region_def_t* region, float freq_mhz )
{
    static char errbuf[128];
    if( freq_mhz < region->freq_min_mhz || freq_mhz > region->freq_max_mhz )
    {
        snprintf( errbuf, sizeof( errbuf ), "%.1f MHz out of range for %s (%.1f-%.1f)",
                  ( double ) freq_mhz, region->short_name,
                  ( double ) region->freq_min_mhz, ( double ) region->freq_max_mhz );
        return errbuf;
    }
    return NULL;
}

const char* region_validate_power( const region_def_t* region, int power_dbm )
{
    static char errbuf[128];
    if( power_dbm < region->power_min_dbm )
    {
        snprintf( errbuf, sizeof( errbuf ), "%+d dBm below minimum %+d dBm for %s",
                  power_dbm, region->power_min_dbm, region->short_name );
        return errbuf;
    }
    if( power_dbm > region->power_max_dbm )
    {
        snprintf( errbuf, sizeof( errbuf ), "%+d dBm exceeds max %+d dBm for %s",
                  power_dbm, region->power_max_dbm, region->short_name );
        return errbuf;
    }
    return NULL;
}
