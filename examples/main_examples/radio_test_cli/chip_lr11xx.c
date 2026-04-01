/**
 * @file      chip_lr11xx.c
 *
 * @brief     LR11xx chip_interface implementation (direct driver access)
 *
 * Talks directly to the LR11xx driver over SPI — no RAC, no RAL, no LBM.
 *
 * Chip variant is detected at runtime via lr11xx_system_get_version():
 *   LR1110 — type=0x01, sub-GHz only, GNSS+Wi-Fi capable (not used here)
 *   LR1120 — type=0x02, sub-GHz + 2.4 GHz + S-band + L-band
 *   LR1121 — type=0x03, sub-GHz + 2.4 GHz + S-band + L-band (no GNSS/Wi-Fi)
 *
 * LoRa is the only supported modulation; FLRC is not available on LR11xx.
 * WW2G4 region is supported on LR1120/LR1121 (runtime-gated).
 *
 * PA configuration is delegated to ral_lr11xx_bsp_get_tx_cfg(), which
 * selects LP/HP (sub-GHz) or HF (>=1.6 GHz) PA based on frequency.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2026. All rights reserved.
 */

#include "chip_interface.h"
#include "cli_state.h"

#include "lr11xx_radio.h"
#include "lr11xx_radio_types.h"
#include "lr11xx_system.h"
#include "lr11xx_system_types.h"
#include "lr11xx_regmem.h"
#include "lr11xx_hal.h"
#include "ral_lr11xx_bsp.h"
#include "radio_utilities.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "platform.h"

/*
 * --- Private state ---
 *
 * The LR11xx HAL functions take a `const void* context` parameter.
 * For the Linux HAL implementation, this context is unused (the SPI bus
 * and GPIO pins are global singletons). We pass NULL.
 */

static const void* radio_context = NULL;

void chip_set_radio_context( const void* ctx )
{
    radio_context = ctx;
}

/*
 * --- Chip variant detection ---
 *
 * Set during init by reading the chip version.  Used by supports_region()
 * to gate WW2G4 (LR1120/LR1121 only, not LR1110).
 */
static lr11xx_system_version_type_t g_chip_type = LR11XX_SYSTEM_VERSION_TYPE_LR1110;

/* Cached LoRa params from apply_config() — re-used by start_tx() and get_toa_ms(). */
static lr11xx_radio_mod_params_lora_t cached_lora_mod_params;
static lr11xx_radio_pkt_params_lora_t cached_lora_pkt_params;

/*
 * --- PA state ---
 *
 * PA config is computed by ral_lr11xx_bsp_get_tx_cfg() on every apply_config()
 * call.  The BSP owns the PA tables and selects LP/HP/HF based on frequency.
 *
 * When the user manually overrides PA params via the `pa` command, the override
 * values are stored here and overlaid on top of the BSP output.
 */

static lr11xx_radio_pa_cfg_t    pa_config;
static lr11xx_radio_ramp_time_t pa_cached_ramp_time;
static int8_t  pa_cached_power = 0;
static lr11xx_radio_pa_selection_t pa_cached_pa_sel = LR11XX_RADIO_PA_SEL_LP;

/* Per-parameter override flags and values */
static bool    pa_ovr_power_set        = false;
static int8_t  pa_ovr_power            = 0;
static bool    pa_ovr_duty_cycle_set   = false;
static uint8_t pa_ovr_duty_cycle       = 0;
static bool    pa_ovr_hp_sel_set       = false;
static uint8_t pa_ovr_hp_sel           = 0;
static bool    pa_ovr_ramp_set         = false;
static lr11xx_radio_ramp_time_t pa_ovr_ramp_time;

/**
 * Clear all per-parameter PA overrides.
 */
static void lr11xx_reset_pa_overrides( void )
{
    pa_ovr_power_set      = false;
    pa_ovr_duty_cycle_set = false;
    pa_ovr_hp_sel_set     = false;
    pa_ovr_ramp_set       = false;
}

/**
 * Apply per-param overrides to BSP output and cache results.
 * Detects PA path changes (LP↔HP↔HF) and clears overrides when the path switches.
 */
static void lr11xx_apply_pa_overrides( ral_lr11xx_bsp_tx_cfg_output_params_t* tx_output )
{
    /* Detect PA path change — clear all overrides if path crossed */
    if( tx_output->pa_cfg.pa_sel != pa_cached_pa_sel )
    {
        lr11xx_reset_pa_overrides();
    }

    /* Apply per-param overrides */
    if( pa_ovr_duty_cycle_set )
    {
        tx_output->pa_cfg.pa_duty_cycle = pa_ovr_duty_cycle;
    }
    if( pa_ovr_hp_sel_set )
    {
        tx_output->pa_cfg.pa_hp_sel = pa_ovr_hp_sel;
    }
    if( pa_ovr_ramp_set )
    {
        tx_output->pa_ramp_time = pa_ovr_ramp_time;
    }
    if( pa_ovr_power_set )
    {
        tx_output->chip_output_pwr_in_dbm_configured = pa_ovr_power;
    }

    /* Cache for `pa show` */
    pa_config           = tx_output->pa_cfg;
    pa_cached_ramp_time = tx_output->pa_ramp_time;
    pa_cached_power     = tx_output->chip_output_pwr_in_dbm_configured;
    pa_cached_pa_sel    = tx_output->pa_cfg.pa_sel;
}

/*
 * --- Ramp time mapping ---
 */

/**
 * Map µs value to lr11xx ramp time enum.  Returns -1 if not a valid discrete value.
 */
static int map_lr11xx_ramp_us( int us, lr11xx_radio_ramp_time_t* out )
{
    switch( us )
    {
    case 16:  *out = LR11XX_RADIO_RAMP_16_US;  return 0;
    case 32:  *out = LR11XX_RADIO_RAMP_32_US;  return 0;
    case 48:  *out = LR11XX_RADIO_RAMP_48_US;  return 0;
    case 64:  *out = LR11XX_RADIO_RAMP_64_US;  return 0;
    case 80:  *out = LR11XX_RADIO_RAMP_80_US;  return 0;
    case 96:  *out = LR11XX_RADIO_RAMP_96_US;  return 0;
    case 112: *out = LR11XX_RADIO_RAMP_112_US; return 0;
    case 128: *out = LR11XX_RADIO_RAMP_128_US; return 0;
    case 144: *out = LR11XX_RADIO_RAMP_144_US; return 0;
    case 160: *out = LR11XX_RADIO_RAMP_160_US; return 0;
    case 176: *out = LR11XX_RADIO_RAMP_176_US; return 0;
    case 192: *out = LR11XX_RADIO_RAMP_192_US; return 0;
    case 208: *out = LR11XX_RADIO_RAMP_208_US; return 0;
    case 240: *out = LR11XX_RADIO_RAMP_240_US; return 0;
    case 272: *out = LR11XX_RADIO_RAMP_272_US; return 0;
    case 304: *out = LR11XX_RADIO_RAMP_304_US; return 0;
    default:  return -1;
    }
}

/**
 * Map lr11xx ramp time enum back to µs for display.
 */
static int lr11xx_ramp_to_us( lr11xx_radio_ramp_time_t ramp )
{
    static const int table[] = { 16, 32, 48, 64, 80, 96, 112, 128,
                                 144, 160, 176, 192, 208, 240, 272, 304 };
    int idx = ( int ) ramp;
    if( idx >= 0 && idx < ( int ) ( sizeof( table ) / sizeof( table[0] ) ) )
    {
        return table[idx];
    }
    return -1;
}

/*
 * --- BW/SF/CR mapping helpers ---
 */

static lr11xx_radio_lora_bw_t map_bw( uint16_t bw_khz )
{
    switch( bw_khz )
    {
    case 125:
        return LR11XX_RADIO_LORA_BW_125;
    case 250:
        return LR11XX_RADIO_LORA_BW_250;
    case 500:
        return LR11XX_RADIO_LORA_BW_500;
    case 812:
        return LR11XX_RADIO_LORA_BW_800;
    default:
        return LR11XX_RADIO_LORA_BW_125;
    }
}

static lr11xx_radio_lora_sf_t map_sf( uint8_t sf )
{
    switch( sf )
    {
    case 5:
        return LR11XX_RADIO_LORA_SF5;
    case 6:
        return LR11XX_RADIO_LORA_SF6;
    case 7:
        return LR11XX_RADIO_LORA_SF7;
    case 8:
        return LR11XX_RADIO_LORA_SF8;
    case 9:
        return LR11XX_RADIO_LORA_SF9;
    case 10:
        return LR11XX_RADIO_LORA_SF10;
    case 11:
        return LR11XX_RADIO_LORA_SF11;
    case 12:
        return LR11XX_RADIO_LORA_SF12;
    default:
        return LR11XX_RADIO_LORA_SF7;
    }
}

static lr11xx_radio_lora_cr_t map_cr( coding_rate_t cr )
{
    switch( cr )
    {
    case CODING_RATE_4_5:
        return LR11XX_RADIO_LORA_CR_4_5;
    case CODING_RATE_4_6:
        return LR11XX_RADIO_LORA_CR_4_6;
    case CODING_RATE_4_7:
        return LR11XX_RADIO_LORA_CR_4_7;
    case CODING_RATE_4_8:
        return LR11XX_RADIO_LORA_CR_4_8;
    case CODING_RATE_LI_4_5:
        return LR11XX_RADIO_LORA_CR_LI_4_5;
    case CODING_RATE_LI_4_6:
        return LR11XX_RADIO_LORA_CR_LI_4_6;
    case CODING_RATE_LI_4_8:
        return LR11XX_RADIO_LORA_CR_LI_4_8;
    default:
        return LR11XX_RADIO_LORA_CR_4_5;
    }
}

/**
 * Compute LDRO auto value.  Enable when symbol time exceeds 16 ms.
 * Symbol time = 2^SF / BW_Hz.
 */
static uint8_t compute_ldro_auto( uint8_t sf, uint16_t bw_khz )
{
    /* symbol_time_us = (1 << sf) * 1000 / bw_khz  (in µs, avoiding float) */
    uint32_t symbol_time_us = ( ( uint32_t ) 1 << sf ) * 1000 / bw_khz;
    return ( symbol_time_us > 16000 ) ? 1 : 0;
}

/*
 * --- chip_driver_t implementation ---
 */

static int lr11xx_chip_init( void )
{
    lr11xx_status_t rc;

    /* --- Reset and enter standby --- */

    if( lr11xx_hal_reset( radio_context ) != LR11XX_HAL_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr11xx_hal_reset failed\n" );
        return -1;
    }

    rc = lr11xx_system_set_standby( radio_context, LR11XX_SYSTEM_STANDBY_CFG_RC );
    if( rc != LR11XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr11xx_system_set_standby failed (%d)\n", rc );
        return -1;
    }

    /* --- BSP-driven init sequence (matches RAL ral_lr11xx_init() order) --- */

    /* 1. SPI CRC (from BSP — typically disabled) */
    {
        bool crc_is_activated = false;
        ral_lr11xx_bsp_get_crc_state( radio_context, &crc_is_activated );
        if( crc_is_activated )
        {
            rc = lr11xx_system_enable_spi_crc( radio_context, true );
            if( rc != LR11XX_STATUS_OK )
            {
                fprintf( stderr, "WARN: lr11xx_system_enable_spi_crc failed (%d)\n", rc );
            }
        }
    }

    /* 2. Regulator mode (from BSP — typically DC-DC) */
    {
        lr11xx_system_reg_mode_t reg_mode;
        ral_lr11xx_bsp_get_reg_mode( radio_context, &reg_mode );
        rc = lr11xx_system_set_reg_mode( radio_context, reg_mode );
        if( rc != LR11XX_STATUS_OK )
        {
            fprintf( stderr, "ERROR: lr11xx_system_set_reg_mode failed (%d)\n", rc );
            return -1;
        }
    }

    /* 3. RF switch configuration (from BSP — DIO pin mapping per radio state) */
    {
        lr11xx_system_rfswitch_cfg_t rf_switch_cfg;
        ral_lr11xx_bsp_get_rf_switch_cfg( radio_context, &rf_switch_cfg );
        rc = lr11xx_system_set_dio_as_rf_switch( radio_context, &rf_switch_cfg );
        if( rc != LR11XX_STATUS_OK )
        {
            fprintf( stderr, "ERROR: lr11xx_system_set_dio_as_rf_switch failed (%d)\n", rc );
            return -1;
        }
    }

    /* 4. Oscillator: TCXO or XTAL (from BSP).
     * If TCXO, configure the supply voltage and startup time, then
     * run all calibrations (the chip skips auto-calibration when using TCXO). */
    {
        ral_xosc_cfg_t                          xosc_cfg;
        lr11xx_system_tcxo_supply_voltage_t     tcxo_voltage;
        uint32_t                                tcxo_startup_tick = 0;
        ral_lr11xx_bsp_get_xosc_cfg( radio_context, &xosc_cfg, &tcxo_voltage, &tcxo_startup_tick );
        if( xosc_cfg == RAL_XOSC_CFG_TCXO_RADIO_CTRL )
        {
            rc = lr11xx_system_set_tcxo_mode( radio_context, tcxo_voltage, tcxo_startup_tick );
            if( rc != LR11XX_STATUS_OK )
            {
                fprintf( stderr, "WARN: lr11xx_system_set_tcxo_mode failed (%d)\n", rc );
            }
            /* Run all calibrations — TCXO boards skip POR auto-cal */
            rc = lr11xx_system_calibrate( radio_context, 0x3F );
            if( rc != LR11XX_STATUS_OK )
            {
                fprintf( stderr, "WARN: lr11xx_system_calibrate failed (%d)\n", rc );
            }
        }
    }

    /* 5. RX boost mode (from BSP — typically disabled) */
    {
        bool rx_boost_is_activated = false;
        ral_lr11xx_bsp_get_rx_boost_cfg( radio_context, &rx_boost_is_activated );
        rc = lr11xx_radio_cfg_rx_boosted( radio_context, rx_boost_is_activated );
        if( rc != LR11XX_STATUS_OK )
        {
            fprintf( stderr, "WARN: lr11xx_radio_cfg_rx_boosted failed (%d)\n", rc );
        }
    }

    /* --- Low-frequency clock: internal RC --- */

    rc = lr11xx_system_cfg_lfclk( radio_context, LR11XX_SYSTEM_LFCLK_RC, false );
    if( rc != LR11XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr11xx_system_cfg_lfclk failed (%d)\n", rc );
        return -1;
    }

    /* --- Detect chip variant --- */

    lr11xx_system_version_t ver;
    if( lr11xx_system_get_version( radio_context, &ver ) == LR11XX_STATUS_OK )
    {
        g_chip_type = ver.type;
    }

    /* --- Check and clear any post-reset errors --- */

    uint16_t errors = 0;
    lr11xx_system_get_errors( radio_context, &errors );
    if( errors != 0 )
    {
        fprintf( stderr, "WARN: post-reset errors=0x%04X, clearing\n", errors );
        lr11xx_system_clear_errors( radio_context );
    }

    /* --- DIO IRQ configuration ---
     * Route TX_DONE, RX_DONE, TIMEOUT, CRC_ERROR to DIO1.
     * DIO2 is unused (pass 0). */

    lr11xx_system_irq_mask_t irq_mask = LR11XX_SYSTEM_IRQ_TX_DONE |
                                        LR11XX_SYSTEM_IRQ_RX_DONE |
                                        LR11XX_SYSTEM_IRQ_TIMEOUT |
                                        LR11XX_SYSTEM_IRQ_CRC_ERROR;
    rc = lr11xx_system_set_dio_irq_params( radio_context, irq_mask, 0 );
    if( rc != LR11XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr11xx_system_set_dio_irq_params failed (%d)\n", rc );
        return -1;
    }

    return 0;
}

static int lr11xx_set_standby( void )
{
    lr11xx_status_t rc = lr11xx_system_set_standby( radio_context, LR11XX_SYSTEM_STANDBY_CFG_RC );
    return ( rc == LR11XX_STATUS_OK ) ? 0 : -1;
}

static int lr11xx_get_version( char* version_str, size_t len )
{
    lr11xx_system_version_t ver;
    lr11xx_status_t         rc = lr11xx_system_get_version( radio_context, &ver );
    if( rc != LR11XX_STATUS_OK )
    {
        snprintf( version_str, len, "(read failed)" );
        return -1;
    }

    const char* chip_name = "LR11xx";
    if( ver.type == LR11XX_SYSTEM_VERSION_TYPE_LR1110 )
    {
        chip_name = "LR1110";
    }
    else if( ver.type == LR11XX_SYSTEM_VERSION_TYPE_LR1120 )
    {
        chip_name = "LR1120";
    }
    else if( ver.type == LR11XX_SYSTEM_VERSION_TYPE_LR1121 )
    {
        chip_name = "LR1121";
    }

    snprintf( version_str, len, "%s [hw=0x%02X type=0x%02X fw=0x%04X]",
              chip_name, ver.hw, ver.type, ver.fw );
    return 0;
}

static int lr11xx_apply_config( const radio_config_t* cfg )
{
    lr11xx_status_t rc;
    uint32_t        freq_hz = ( uint32_t ) ( cfg->freq_mhz * 1e6f );

    /* Ensure clean standby state before reconfiguring */
    rc = lr11xx_system_set_standby( radio_context, LR11XX_SYSTEM_STANDBY_CFG_RC );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }
    lr11xx_system_clear_irq_status( radio_context, LR11XX_SYSTEM_IRQ_ALL_MASK );

    /* 1. Packet type — always LoRa (only modulation supported on LR11xx CLI) */
    rc = lr11xx_radio_set_pkt_type( radio_context, LR11XX_RADIO_PKT_TYPE_LORA );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    /* 2. Frequency + RSSI calibration (matches RAL: set_rf_freq + set_rssi_calibration) */
    rc = lr11xx_radio_set_rf_freq( radio_context, freq_hz );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    /* Apply frequency-dependent RSSI calibration table from BSP */
    {
        lr11xx_radio_rssi_calibration_table_t rssi_cal_table;
        ral_lr11xx_bsp_get_rssi_calibration_table( radio_context, freq_hz, &rssi_cal_table );
        rc = lr11xx_radio_set_rssi_calibration( radio_context, &rssi_cal_table );
        if( rc != LR11XX_STATUS_OK )
        {
            return -1;
        }
    }

    /* 3. PA config */
    ral_lr11xx_bsp_tx_cfg_input_params_t  tx_input  = { .system_output_pwr_in_dbm = ( int8_t ) cfg->power_dbm,
                                                        .freq_in_hz               = freq_hz };
    ral_lr11xx_bsp_tx_cfg_output_params_t tx_output;
    ral_lr11xx_bsp_get_tx_cfg( radio_context, &tx_input, &tx_output );
    lr11xx_apply_pa_overrides( &tx_output );

    rc = lr11xx_radio_set_pa_cfg( radio_context, &pa_config );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    rc = lr11xx_radio_set_tx_params( radio_context, tx_output.chip_output_pwr_in_dbm_configured,
                                     tx_output.pa_ramp_time );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    /* 4. LoRa modulation params */
    lr11xx_radio_mod_params_lora_t mod_params;
    if( cfg->modulation == MODULATION_LORA )
    {
        mod_params.sf = map_sf( cfg->sf );
        mod_params.bw = map_bw( cfg->bw_khz );
        mod_params.cr = map_cr( cfg->cr );
        /* LDRO: -1 = auto, 0 = off, 1 = on */
        if( cfg->ldro < 0 )
        {
            mod_params.ldro = compute_ldro_auto( cfg->sf, cfg->bw_khz );
        }
        else
        {
            mod_params.ldro = ( uint8_t ) cfg->ldro;
        }
    }
    else
    {
        /* MODULATION_NONE: use defaults for CW/test modes */
        mod_params.sf   = LR11XX_RADIO_LORA_SF7;
        mod_params.bw   = LR11XX_RADIO_LORA_BW_125;
        mod_params.cr   = LR11XX_RADIO_LORA_CR_4_5;
        mod_params.ldro = 0;
    }

    cached_lora_mod_params = mod_params;

    rc = lr11xx_radio_set_lora_mod_params( radio_context, &mod_params );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    /* 5. LoRa packet params */
    if( cfg->modulation == MODULATION_LORA )
    {
        lr11xx_radio_pkt_params_lora_t pkt_params = { 0 };
        pkt_params.preamble_len_in_symb = cfg->preamble;
        pkt_params.header_type = cfg->header_implicit ? LR11XX_RADIO_LORA_PKT_IMPLICIT
                                                      : LR11XX_RADIO_LORA_PKT_EXPLICIT;
        pkt_params.pld_len_in_bytes     = 255; /* max for RX */
        pkt_params.crc = cfg->crc_on ? LR11XX_RADIO_LORA_CRC_ON
                                     : LR11XX_RADIO_LORA_CRC_OFF;
        pkt_params.iq = cfg->invert_iq ? LR11XX_RADIO_LORA_IQ_INVERTED
                                       : LR11XX_RADIO_LORA_IQ_STANDARD;

        cached_lora_pkt_params = pkt_params;

        rc = lr11xx_radio_set_lora_pkt_params( radio_context, &pkt_params );
        if( rc != LR11XX_STATUS_OK )
        {
            return -1;
        }

        /* 6. Sync word */
        rc = lr11xx_radio_set_lora_sync_word( radio_context, cfg->syncword );
        if( rc != LR11XX_STATUS_OK )
        {
            return -1;
        }
    }

    /* 7. DIO IRQ — broad mask for general use */
    lr11xx_system_irq_mask_t irq_mask = LR11XX_SYSTEM_IRQ_TX_DONE |
                                        LR11XX_SYSTEM_IRQ_RX_DONE |
                                        LR11XX_SYSTEM_IRQ_TIMEOUT |
                                        LR11XX_SYSTEM_IRQ_CRC_ERROR;
    rc = lr11xx_system_set_dio_irq_params( radio_context, irq_mask, 0 );

    return ( rc == LR11XX_STATUS_OK ) ? 0 : -1;
}

static int lr11xx_start_tx_cw( void )
{
    lr11xx_status_t rc = lr11xx_radio_set_tx_cw( radio_context );
    return ( rc == LR11XX_STATUS_OK ) ? 0 : -1;
}

static int lr11xx_start_tx_infinite_preamble( void )
{
    lr11xx_status_t rc = lr11xx_radio_set_tx_infinite_preamble( radio_context );
    return ( rc == LR11XX_STATUS_OK ) ? 0 : -1;
}

static int lr11xx_start_tx( const uint8_t* payload, uint8_t len, uint32_t timeout_ms )
{
    lr11xx_status_t rc;

    /* Update pld_len_in_bytes before TX.  apply_config() sets 255 for RX. */
    lr11xx_radio_pkt_params_lora_t pkt_params = cached_lora_pkt_params;
    pkt_params.pld_len_in_bytes = len;
    rc = lr11xx_radio_set_lora_pkt_params( radio_context, &pkt_params );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    rc = lr11xx_regmem_write_buffer8( radio_context, payload, len );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    rc = lr11xx_radio_set_tx( radio_context, timeout_ms );
    return ( rc == LR11XX_STATUS_OK ) ? 0 : -1;
}

static int lr11xx_start_rx( uint32_t timeout_ms )
{
    lr11xx_status_t rc;

    /* Clear pending IRQs before entering RX */
    lr11xx_system_clear_irq_status( radio_context, LR11XX_SYSTEM_IRQ_ALL_MASK );

    if( timeout_ms == 0 )
    {
        /* Continuous RX — must use RTC step API with 0xFFFFFF */
        rc = lr11xx_radio_set_rx_with_timeout_in_rtc_step( radio_context, 0x00FFFFFF );
    }
    else
    {
        rc = lr11xx_radio_set_rx( radio_context, timeout_ms );
    }
    return ( rc == LR11XX_STATUS_OK ) ? 0 : -1;
}

static int lr11xx_stop( void )
{
    return lr11xx_set_standby();
}

static int lr11xx_get_rssi_inst( int16_t* rssi_dbm )
{
    int8_t rssi_i8 = 0;
    lr11xx_status_t rc = lr11xx_radio_get_rssi_inst( radio_context, &rssi_i8 );
    *rssi_dbm = ( int16_t ) rssi_i8;
    return ( rc == LR11XX_STATUS_OK ) ? 0 : -1;
}

/*
 * --- PA parameter introspection ---
 */

static const pa_param_desc_t lr11xx_pa_params[] = {
    { "power",          -17, 22,  "TX power in dBm",                        false },
    { "pa_sel",           0,  2,  "PA path: 0=LP, 1=HP, 2=HF (read-only)", true  },
    { "pa_reg_supply",    0,  1,  "PA supply: 0=VREG, 1=VBAT (read-only)", true  },
    { "pa_duty_cycle",    0,  7,  "PA duty cycle",                          false },
    { "pa_hp_sel",        0,  7,  "HP PA slices",                           false },
    { "pa_ramp_us",      16, 304, "PA ramp time in microseconds",           false },
};

static int lr11xx_set_pa_param( const char* name, int value )
{
    if( strcasecmp( name, "power" ) == 0 )
    {
        if( value < -17 || value > 22 )
        {
            return -1;
        }
        pa_ovr_power     = ( int8_t ) value;
        pa_ovr_power_set = true;
        pa_cached_power  = ( int8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_sel" ) == 0 )
    {
        return -4; /* read-only */
    }
    if( strcasecmp( name, "pa_reg_supply" ) == 0 )
    {
        return -4; /* read-only */
    }
    if( strcasecmp( name, "pa_duty_cycle" ) == 0 )
    {
        if( value < 0 || value > 7 )
        {
            return -1;
        }
        pa_ovr_duty_cycle        = ( uint8_t ) value;
        pa_ovr_duty_cycle_set    = true;
        pa_config.pa_duty_cycle  = ( uint8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_hp_sel" ) == 0 )
    {
        if( value < 0 || value > 7 )
        {
            return -1;
        }
        pa_ovr_hp_sel        = ( uint8_t ) value;
        pa_ovr_hp_sel_set    = true;
        pa_config.pa_hp_sel  = ( uint8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        lr11xx_radio_ramp_time_t ramp;
        if( map_lr11xx_ramp_us( value, &ramp ) != 0 )
        {
            printf( "ERROR: Invalid ramp time (us). Valid values:\n"
                    "  16,32,48,64,80,96,112,128,144,160,\n"
                    "  176,192,208,240,272,304\n" );
            return -3; /* error already printed */
        }
        pa_ovr_ramp_time    = ramp;
        pa_ovr_ramp_set     = true;
        pa_cached_ramp_time = ramp;
        return 0;
    }
    return -2; /* unknown param */
}

static int lr11xx_get_pa_param( const char* name, int* value )
{
    if( strcasecmp( name, "power" ) == 0 )
    {
        *value = pa_cached_power;
        return 0;
    }
    if( strcasecmp( name, "pa_sel" ) == 0 )
    {
        *value = ( int ) pa_cached_pa_sel;
        return 0;
    }
    if( strcasecmp( name, "pa_reg_supply" ) == 0 )
    {
        *value = ( int ) pa_config.pa_reg_supply;
        return 0;
    }
    if( strcasecmp( name, "pa_duty_cycle" ) == 0 )
    {
        *value = pa_config.pa_duty_cycle;
        return 0;
    }
    if( strcasecmp( name, "pa_hp_sel" ) == 0 )
    {
        *value = pa_config.pa_hp_sel;
        return 0;
    }
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        *value = lr11xx_ramp_to_us( pa_cached_ramp_time );
        return 0;
    }
    return -2;
}

static void lr11xx_refresh_pa_config( const radio_config_t* cfg )
{
    uint32_t freq_hz = ( uint32_t ) ( cfg->freq_mhz * 1e6f );

    ral_lr11xx_bsp_tx_cfg_input_params_t tx_input = {
        .system_output_pwr_in_dbm = ( int8_t ) cfg->power_dbm,
        .freq_in_hz               = freq_hz,
    };
    ral_lr11xx_bsp_tx_cfg_output_params_t tx_output;
    ral_lr11xx_bsp_get_tx_cfg( radio_context, &tx_input, &tx_output );

    lr11xx_apply_pa_overrides( &tx_output );
}

static int lr11xx_wait_tx_done( uint32_t timeout_ms )
{
    uint32_t elapsed = 0;

    while( elapsed < timeout_ms )
    {
        lr11xx_system_irq_mask_t irq = 0;
        lr11xx_status_t rc = lr11xx_system_get_and_clear_irq_status( radio_context, &irq );
        if( rc != LR11XX_STATUS_OK )
        {
            return -1;
        }
        if( irq & LR11XX_SYSTEM_IRQ_TX_DONE )
        {
            return 0;
        }
        if( irq & LR11XX_SYSTEM_IRQ_TIMEOUT )
        {
            return 1;
        }
        platform_sleep_us( 1000 ); /* 1 ms */
        elapsed++;
    }
    return 1; /* timed out */
}

static int lr11xx_receive_packet( uint8_t* buf, uint8_t* buf_len, uint32_t timeout_ms,
                                  int16_t* rssi_dbm, int8_t* snr_raw )
{
    lr11xx_status_t rc;

    /* Clear pending IRQs before entering RX */
    lr11xx_system_clear_irq_status( radio_context, LR11XX_SYSTEM_IRQ_ALL_MASK );

    if( timeout_ms == 0 )
    {
        rc = lr11xx_radio_set_rx_with_timeout_in_rtc_step( radio_context, 0x00FFFFFF );
    }
    else
    {
        rc = lr11xx_radio_set_rx( radio_context, timeout_ms );
    }
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    uint32_t elapsed = 0;
    bool continuous = ( timeout_ms == 0 );

    while( continuous || elapsed < ( timeout_ms + 200 ) )
    {
        lr11xx_system_irq_mask_t irq = 0;
        rc = lr11xx_system_get_and_clear_irq_status( radio_context, &irq );
        if( rc != LR11XX_STATUS_OK )
        {
            return -1;
        }

        if( irq & LR11XX_SYSTEM_IRQ_RX_DONE )
        {
            /* Read packet status for RSSI/SNR */
            lr11xx_radio_pkt_status_lora_t pkt_status;
            memset( &pkt_status, 0, sizeof( pkt_status ) );
            rc = lr11xx_radio_get_lora_pkt_status( radio_context, &pkt_status );
            if( rc != LR11XX_STATUS_OK )
            {
                return -1;
            }
            *rssi_dbm = ( int16_t ) pkt_status.rssi_pkt_in_dbm;
            /* LR11xx SNR is in 1 dB units; vtable expects 0.25 dB units */
            *snr_raw  = pkt_status.snr_pkt_in_db * 4;

            /* Read payload via buffer status + regmem */
            lr11xx_radio_rx_buffer_status_t buf_status;
            rc = lr11xx_radio_get_rx_buffer_status( radio_context, &buf_status );
            if( rc != LR11XX_STATUS_OK )
            {
                return -1;
            }
            uint8_t pkt_len = buf_status.pld_len_in_bytes;
            *buf_len = pkt_len;

            rc = lr11xx_regmem_read_buffer8( radio_context, buf,
                                             buf_status.buffer_start_pointer, pkt_len );
            if( rc != LR11XX_STATUS_OK )
            {
                return -1;
            }
            return 0;
        }

        if( irq & LR11XX_SYSTEM_IRQ_CRC_ERROR )
        {
            /* Read RSSI even for CRC error */
            lr11xx_radio_pkt_status_lora_t pkt_status;
            memset( &pkt_status, 0, sizeof( pkt_status ) );
            lr11xx_radio_get_lora_pkt_status( radio_context, &pkt_status );
            *rssi_dbm = ( int16_t ) pkt_status.rssi_pkt_in_dbm;
            *snr_raw  = 0;
            *buf_len  = 0;
            return 1;
        }

        if( irq & LR11XX_SYSTEM_IRQ_TIMEOUT )
        {
            return 2;
        }

        platform_sleep_us( 1000 ); /* 1 ms */
        elapsed++;
    }
    return 2; /* timed out (safety net) */
}

static int lr11xx_check_irq( uint32_t* irq_flags )
{
    lr11xx_system_irq_mask_t irq = 0;
    lr11xx_status_t rc = lr11xx_system_get_and_clear_irq_status( radio_context, &irq );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }

    uint32_t flags = 0;
    if( irq & LR11XX_SYSTEM_IRQ_TX_DONE )
    {
        flags |= CHIP_IRQ_TX_DONE;
    }
    if( irq & LR11XX_SYSTEM_IRQ_RX_DONE )
    {
        flags |= CHIP_IRQ_RX_DONE;
    }
    if( irq & LR11XX_SYSTEM_IRQ_TIMEOUT )
    {
        flags |= CHIP_IRQ_TIMEOUT;
    }
    if( irq & LR11XX_SYSTEM_IRQ_CRC_ERROR )
    {
        flags |= CHIP_IRQ_CRC_ERROR;
    }
    *irq_flags = flags;
    return 0;
}

static int lr11xx_read_rx_packet( uint8_t* buf, uint8_t* buf_len,
                                   int16_t* rssi_dbm, int8_t* snr_raw )
{
    lr11xx_status_t rc;

    /* Packet status */
    lr11xx_radio_pkt_status_lora_t pkt_status;
    memset( &pkt_status, 0, sizeof( pkt_status ) );
    rc = lr11xx_radio_get_lora_pkt_status( radio_context, &pkt_status );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }
    *rssi_dbm = ( int16_t ) pkt_status.rssi_pkt_in_dbm;
    /* LR11xx SNR is in 1 dB units; vtable expects 0.25 dB units */
    *snr_raw  = pkt_status.snr_pkt_in_db * 4;

    /* Buffer status for length and offset */
    lr11xx_radio_rx_buffer_status_t buf_status;
    rc = lr11xx_radio_get_rx_buffer_status( radio_context, &buf_status );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }
    uint8_t pkt_len = buf_status.pld_len_in_bytes;
    *buf_len = pkt_len;

    /* Read payload from regmem buffer */
    rc = lr11xx_regmem_read_buffer8( radio_context, buf,
                                     buf_status.buffer_start_pointer, pkt_len );
    if( rc != LR11XX_STATUS_OK )
    {
        return -1;
    }
    return 0;
}

static uint32_t lr11xx_get_toa_ms( uint8_t pld_len )
{
    lr11xx_radio_pkt_params_lora_t pkt = cached_lora_pkt_params;
    pkt.pld_len_in_bytes = pld_len;
    return lr11xx_radio_get_lora_time_on_air_in_ms( &pkt, &cached_lora_mod_params );
}

static bool lr11xx_supports_modulation( modulation_id_t id )
{
    /* LR11xx supports LoRa only — FLRC is not available */
    return ( id == MODULATION_LORA );
}

static bool lr11xx_supports_region( region_id_t id )
{
    if( id == REGION_WW2G4 )
    {
        /* 2.4 GHz TX requires LR1120 or LR1121; LR1110 is sub-GHz only */
        return ( g_chip_type == LR11XX_SYSTEM_VERSION_TYPE_LR1120 ||
                 g_chip_type == LR11XX_SYSTEM_VERSION_TYPE_LR1121 );
    }
    return true;
}

/*
 * --- Driver instance ---
 */

#if defined( LR1110 )
#define LR11XX_CHIP_NAME "LR1110"
#elif defined( LR1120 )
#define LR11XX_CHIP_NAME "LR1120"
#elif defined( LR1121 )
#define LR11XX_CHIP_NAME "LR1121"
#else
#define LR11XX_CHIP_NAME "LR11xx"
#endif

static const chip_driver_t lr11xx_driver = {
    .chip_name                  = LR11XX_CHIP_NAME,
    .init                       = lr11xx_chip_init,
    .set_standby                = lr11xx_set_standby,
    .get_version                = lr11xx_get_version,
    .apply_config               = lr11xx_apply_config,
    .start_tx_cw                = lr11xx_start_tx_cw,
    .start_tx_infinite_preamble = lr11xx_start_tx_infinite_preamble,
    .start_tx                   = lr11xx_start_tx,
    .start_rx                   = lr11xx_start_rx,
    .stop                       = lr11xx_stop,
    .get_rssi_inst              = lr11xx_get_rssi_inst,
    .pa_param_count             = sizeof( lr11xx_pa_params ) / sizeof( lr11xx_pa_params[0] ),
    .pa_params                  = lr11xx_pa_params,
    .set_pa_param               = lr11xx_set_pa_param,
    .get_pa_param               = lr11xx_get_pa_param,
    .refresh_pa_config          = lr11xx_refresh_pa_config,
    .reset_pa_overrides         = lr11xx_reset_pa_overrides,
    .supports_modulation        = lr11xx_supports_modulation,
    .supports_region            = lr11xx_supports_region,
    .get_toa_ms                 = lr11xx_get_toa_ms,
    .check_irq                  = lr11xx_check_irq,
    .read_rx_packet             = lr11xx_read_rx_packet,
    .wait_tx_done               = lr11xx_wait_tx_done,
    .receive_packet             = lr11xx_receive_packet,
    .apply_xosc_trim            = NULL, /* not supported on LR11xx */
    .get_xosc_defaults          = NULL, /* not supported on LR11xx */
};

const chip_driver_t* chip_get_driver( void )
{
    return &lr11xx_driver;
}
