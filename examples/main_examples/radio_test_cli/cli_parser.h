/**
 * @file      cli_parser.h
 *
 * @brief     Command tokenizer, dispatch table, help system, tab completion
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef CLI_PARSER_H
#define CLI_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cli_state.h"
#include "chip_interface.h"
#include "linenoise.h"
#include <stdbool.h>

/**
 * Execute a single command line.
 * Returns 0 on success, -1 on error (already printed), 1 to request exit.
 */
int cli_execute( const char* line, radio_config_t* cfg, const chip_driver_t* chip );

/**
 * Linenoise completion callback — call linenoiseSetCompletionCallback with this.
 */
void cli_completion( const char* buf, linenoiseCompletions* lc );

/**
 * Linenoise hints callback — call linenoiseSetHintsCallback with this.
 */
char* cli_hints( const char* buf, int* color, int* bold );

/**
 * Set the config and chip pointers used by tab completion.
 * Call once before entering the REPL.
 */
void cli_set_completion_context( const radio_config_t* cfg, const chip_driver_t* chip );

#ifdef __cplusplus
}
#endif

#endif /* CLI_PARSER_H */
