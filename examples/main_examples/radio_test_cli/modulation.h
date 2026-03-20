/**
 * @file      modulation.h
 *
 * @brief     Modulation module interface and registry
 *
 * Each modulation (LoRa, GFSK, ...) registers a module that owns its
 * parameters, defaults, validation, and help.  The CLI dispatches to
 * the active module — no hardcoded modulation knowledge in the parser.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef MODULATION_H
#define MODULATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cli_state.h"
#include <stdbool.h>

/* Forward-declare — region.h includes cli_state.h which we already have */
typedef struct region_def_s region_def_t;

/**
 * Modulation module vtable.
 *
 * Every modulation backend (mod_lora.c, mod_gfsk.c, ...) provides one
 * static instance of this struct and returns it from modulation_get_all().
 */
typedef struct modulation_module_s
{
    const char*     name;   /* "lora", "gfsk" */
    modulation_id_t id;

    /** Load region-specific defaults for this modulation into cfg. */
    void ( *load_defaults )( radio_config_t* cfg, region_id_t region );

    /**
     * Set a modulation-specific parameter.
     * Returns 0 on success, -1 on error (already printed).
     */
    int ( *set_param )( radio_config_t* cfg, const region_def_t* region,
                        const char* name, const char* value );

    /** Returns true if this module owns the given parameter name. */
    bool ( *owns_param )( const char* name );

    /** Print modulation-specific status lines. */
    void ( *print_status )( const radio_config_t* cfg );

    /** Print modulation-specific help lines (overview). */
    void ( *print_help )( void );

    /** Print detailed context-aware help for a specific parameter.
     *  Returns true if help was printed, false if param not owned. */
    bool ( *print_param_help )( const char* param_name, const radio_config_t* cfg );

    /** NULL-terminated list of parameter names (for tab completion). */
    const char* const* param_names;

    /** Return NULL-terminated completions for a subcommand value.
     *  Config is provided for context-aware filtering. */
    const char* const* ( *get_completions )( const char* param_name,
                                             const radio_config_t* cfg );

    /** Return a hint string for a parameter, e.g. "<125|250|500>".
     *  Config is provided for context-aware filtering (region, BW, etc.). */
    const char* ( *get_hint )( const char* param_name, const radio_config_t* cfg );
} modulation_module_t;

/**
 * Look up a modulation module by name ("lora", "gfsk").
 * Returns NULL if not found.
 */
const modulation_module_t* modulation_lookup( const char* name );

/**
 * Get a modulation module by its enum id.
 * Returns NULL for MODULATION_NONE or unknown ids.
 */
const modulation_module_t* modulation_get_by_id( modulation_id_t id );

/**
 * Return a NULL-terminated array of all registered modulation modules.
 */
const modulation_module_t* const* modulation_get_all( void );

#ifdef __cplusplus
}
#endif

#endif /* MODULATION_H */
