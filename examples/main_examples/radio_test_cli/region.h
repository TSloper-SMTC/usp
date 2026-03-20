/**
 * @file      region.h
 *
 * @brief     Region definitions, defaults, and validation
 *
 * Region constraints are modulation-agnostic: frequency range, power
 * limits, and regulatory mode availability.  Modulation-specific
 * constraints (BW options, SF ranges, etc.) live in each modulation
 * module (see modulation.h / mod_lora.c).
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef REGION_H
#define REGION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cli_state.h"
#include <stdbool.h>

/*
 * --- Region constraint structure ---
 */

typedef struct region_def_s
{
    region_id_t id;
    const char* name;        /* "US902-928", "EU863-870", "JP AS923-1" */
    const char* short_name;  /* "us", "eu", "jp" */

    /* Frequency range */
    float freq_min_mhz;
    float freq_max_mhz;

    /* Power */
    int power_min_dbm;
    int power_max_dbm;

    /* Defaults (RF-envelope only) */
    float default_freq_mhz;
    int   default_power_dbm;

    /* Regulatory mode availability */
    bool fhss_available;
    bool dts_available;
    bool hybrid_available;
} region_def_t;

/**
 * Look up a region by short name ("us", "eu", "jp").
 * Returns NULL if not found.
 */
const region_def_t* region_lookup( const char* name );

/**
 * Load region defaults into the config.
 * Sets freq, power, and region ID.  Resets modulation to NONE.
 */
void region_load_defaults( radio_config_t* cfg, const region_def_t* region );

/**
 * Validate frequency for the given region.
 * Returns NULL on success, or an error message string.
 */
const char* region_validate_freq( const region_def_t* region, float freq_mhz );

/**
 * Validate TX power for the given region.
 * Returns NULL on success, or an error message string.
 */
const char* region_validate_power( const region_def_t* region, int power_dbm );

/**
 * Get the region definition for a region_id.
 * Returns NULL if REGION_NONE.
 */
const region_def_t* region_get_by_id( region_id_t id );

#ifdef __cplusplus
}
#endif

#endif /* REGION_H */
