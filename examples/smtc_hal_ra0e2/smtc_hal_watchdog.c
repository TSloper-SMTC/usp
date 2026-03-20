/**
 * @file      smtc_hal_watchdog.c
 *
 * @brief     Watchdog Hardware Abstraction Layer implementation for RA0E2
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 *
 * RA0E2 IWDT Configuration (via OFS0 option byte at 0x400):
 * - Timeout: 2048 cycles × /256 divider = 32 seconds
 * - Auto-start: Enabled (starts counting after reset)
 * - Stop in sleep: Enabled (stops counting during sleep mode)
 * - Reset on timeout: Enabled (generates reset, not NMI)
 *
 * The IWDT clock is 16.384 kHz (dedicated, not LOCO).
 *
 * NOTE: JLink must do a chip erase before programming to ensure OFS0
 * option byte is written. Use: erase + loadfile in JLink script.
 */

#include "smtc_hal_watchdog.h"
#include "hal_data.h"
#include "r_iwdt.h"

void hal_watchdog_init( void )
{
    /*
     * Open IWDT instance. Required before refresh even in auto-start mode.
     *
     * Configuration is set via OFS0 option byte in bsp_mcu_ofs_cfg.h:
     * - TOPS = 3 (2048 cycles)
     * - CKS = 5 (/256 divider)
     * - SLCSTP = 1 (stop counting in sleep)
     * - RSTIRQS = 1 (reset output on timeout)
     */
    R_IWDT_Open( &g_wdt0_ctrl, &g_wdt0_cfg );
}

void hal_watchdog_reload( void )
{
    /* Refresh IWDT counter */
    R_IWDT_Refresh( &g_wdt0_ctrl );
}

/* --- EOF ------------------------------------------------------------------ */
