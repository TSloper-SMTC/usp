/**
 * @file      test_modes.h
 *
 * @brief     Radio test mode implementations (CW, modulated, RX, regulatory)
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef TEST_MODES_H
#define TEST_MODES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cli_state.h"
#include "chip_interface.h"

/* Forward declaration for linenoise integration */
struct linenoiseState;

/**
 * Start a test mode.
 *
 * For fire-and-forget modes (CW, modulated, RX, DTS): applies config,
 * starts the radio, sets cfg->active_mode, and returns immediately.
 *
 * For tick-based modes (PER TX/RX, FHSS, hybrid, EU-test, JP-test):
 * arms the state machine and sets cfg->active_mode.  The caller must then
 * call test_mode_tick() periodically until cfg->active_mode == MODE_IDLE.
 *
 * In non-interactive mode (pipe/script), this function blocks internally
 * by spinning test_mode_tick() until the mode completes.
 *
 * Returns 0 on success, -1 on error (prints error to stdout).
 */
int test_mode_start( radio_config_t* cfg, const chip_driver_t* chip, active_mode_t mode );

/**
 * Advance the active tick-based mode by one step.
 * Call this from the main loop when cfg->active_mode != MODE_IDLE.
 * Returns 0 normally, -1 on fatal error.
 * When the mode completes, cfg->active_mode is set to MODE_IDLE.
 */
int test_mode_tick( radio_config_t* cfg, const chip_driver_t* chip );

/**
 * Stop the currently active mode.
 * Returns 0 on success, -1 on error.
 */
int test_mode_stop( radio_config_t* cfg, const chip_driver_t* chip );

/**
 * Request an asynchronous stop (called from signal handler).
 * The tick function will transition to DONE on the next call.
 */
void test_mode_request_stop( void );

/**
 * Provide the linenoise edit state so tick functions can call
 * linenoiseHide/Show when printing mid-edit.  Pass NULL to clear.
 */
void test_mode_set_line_state( struct linenoiseState* ls );

/**
 * Consume a pending radio DIO IRQ notification.
 *
 * Returns true (exactly once) after the GPIO IRQ thread detects a rising
 * edge on RADIO_DIOX.  Tick state machines call this to gate expensive
 * SPI reads — chip->check_irq() is only called when an edge has fired.
 *
 * Defined in main_radio_test.c where the GPIO callback is registered.
 */
bool radio_irq_consume( void );

#ifdef __cplusplus
}
#endif

#endif /* TEST_MODES_H */
