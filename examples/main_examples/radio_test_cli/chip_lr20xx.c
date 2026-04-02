/**
 * @file      chip_lr20xx.c
 *
 * @brief     LR20xx chip_interface implementation (direct driver access)
 *
 * Talks directly to the LR20xx driver over SPI — no RAC, no RAL, no LBM.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "chip_interface.h"
#include "cli_state.h"

#include "lr20xx_radio_common.h"
#include "lr20xx_radio_common_types.h"
#include "lr20xx_radio_lora.h"
#include "lr20xx_radio_lora_types.h"
#include "lr20xx_radio_flrc.h"
#include "lr20xx_radio_flrc_types.h"
#include "lr20xx_radio_fifo.h"
#include "lr20xx_system.h"
#include "lr20xx_system_types.h"
#include "lr20xx_hal.h"
#include "ral_lr20xx_bsp.h"
#include "radio_utilities.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "platform.h"

/*
 * --- Private state ---
 *
 * The LR20xx HAL functions take a `const void* context` parameter.
 * For the Linux HAL implementation, this context is unused (the SPI bus
 * and GPIO pins are global singletons). We pass NULL.
 */

static const void* radio_context = NULL;

void chip_set_radio_context( const void* ctx )
{
    radio_context = ctx;
}

/*
 * --- PA state ---
 *
 * PA config and half-power are computed by ral_lr20xx_bsp_get_tx_cfg() on
 * every apply_config() call.  The BSP owns the PA tables, power range
 * constants, and HF/LF selection threshold (>= 1600 MHz).
 *
 * When the user manually overrides PA params via the `pa` command, the
 * override values are stored here and overlaid on top of the BSP output.
 * pa_sel and pa_lf_mode always come from the BSP (frequency-dependent).
 *
 * pa_config is updated by apply_config() to reflect what was last applied
 * and is read back by `pa show` / `pa get_param`.
 */

/*
 * --- Chip variant detection ---
 *
 * Set during init by reading the chip version.  Used by supports_modulation()
 * to gate FLRC (LR2021 only, not LR2022 or future variants).
 */
static bool g_chip_is_lr2021 = false;

/* Cached LoRa packet params from apply_config() — re-used by start_tx()
 * to update pld_len_in_bytes before each transmission. */
static lr20xx_radio_lora_pkt_params_t cached_lora_pkt_params;

/* Cached LoRa mod params from apply_config() — re-used by get_toa_ms()
 * for time-on-air computation. */
static lr20xx_radio_lora_mod_params_t cached_lora_mod_params;

/* Cached FLRC params from apply_config() — re-used by get_toa_ms()
 * when FLRC is active. */
static lr20xx_radio_flrc_pkt_params_t cached_flrc_pkt_params;
static lr20xx_radio_flrc_mod_params_t cached_flrc_mod_params;

/* Track the last-applied modulation so check_irq/read_rx_packet know
 * whether to use LoRa or FLRC packet status APIs. */
static modulation_id_t cached_modulation = MODULATION_LORA;

static lr20xx_radio_common_pa_cfg_t    pa_config;
static lr20xx_radio_common_ramp_time_t pa_cached_ramp_time; /* last-applied ramp (from BSP or override) */
static int8_t  pa_cached_half_power = 0;                    /* last-applied half_power for `pa show` */
static lr20xx_radio_common_pa_selection_t pa_cached_pa_sel = LR20XX_RADIO_COMMON_PA_SEL_LF;

/* Per-parameter override flags and values */
static bool    pa_ovr_half_power_set       = false;
static int8_t  pa_ovr_half_power           = 0;
static bool    pa_ovr_lf_duty_cycle_set    = false;
static uint8_t pa_ovr_lf_duty_cycle        = 0;
static bool    pa_ovr_lf_slices_set        = false;
static uint8_t pa_ovr_lf_slices            = 0;
static bool    pa_ovr_hf_duty_cycle_set    = false;
static uint8_t pa_ovr_hf_duty_cycle        = 0;
static bool    pa_ovr_ramp_set             = false;
static lr20xx_radio_common_ramp_time_t pa_ovr_ramp_time;

/**
 * Clear all per-parameter PA overrides.
 */
static void lr20xx_reset_pa_overrides( void )
{
    pa_ovr_half_power_set    = false;
    pa_ovr_lf_duty_cycle_set = false;
    pa_ovr_lf_slices_set     = false;
    pa_ovr_hf_duty_cycle_set = false;
    pa_ovr_ramp_set          = false;
}

/**
 * Apply per-param overrides to BSP output and cache results.
 * Detects PA path changes (LF↔HF) and clears overrides when the path switches.
 */
static void lr20xx_apply_pa_overrides( ral_lr20xx_bsp_tx_cfg_output_params_t* tx_output )
{
    /* Detect PA path change — clear all overrides if LF↔HF crossed */
    if( tx_output->pa_cfg.pa_sel != pa_cached_pa_sel )
    {
        lr20xx_reset_pa_overrides();
    }

    /* Apply per-param overrides */
    if( pa_ovr_lf_duty_cycle_set )
    {
        tx_output->pa_cfg.pa_lf_duty_cycle = pa_ovr_lf_duty_cycle;
    }
    if( pa_ovr_lf_slices_set )
    {
        tx_output->pa_cfg.pa_lf_slices = pa_ovr_lf_slices;
    }
    if( pa_ovr_hf_duty_cycle_set )
    {
        tx_output->pa_cfg.pa_hf_duty_cycle = pa_ovr_hf_duty_cycle;
    }
    if( pa_ovr_ramp_set )
    {
        tx_output->pa_ramp_time = pa_ovr_ramp_time;
    }
    if( pa_ovr_half_power_set )
    {
        tx_output->chip_output_half_pwr_in_dbm_configured = pa_ovr_half_power;
    }

    /* Cache for `pa show` */
    pa_config           = tx_output->pa_cfg;
    pa_cached_ramp_time = tx_output->pa_ramp_time;
    pa_cached_half_power = tx_output->chip_output_half_pwr_in_dbm_configured;
    pa_cached_pa_sel    = tx_output->pa_cfg.pa_sel;
}

/**
 * Map µs value to lr20xx ramp time enum.  Returns -1 if not a valid discrete value.
 */
static int map_lr20xx_ramp_us( int us, lr20xx_radio_common_ramp_time_t* out )
{
    switch( us )
    {
    case 2:   *out = LR20XX_RADIO_COMMON_RAMP_2_US;   return 0;
    case 4:   *out = LR20XX_RADIO_COMMON_RAMP_4_US;   return 0;
    case 8:   *out = LR20XX_RADIO_COMMON_RAMP_8_US;   return 0;
    case 16:  *out = LR20XX_RADIO_COMMON_RAMP_16_US;  return 0;
    case 32:  *out = LR20XX_RADIO_COMMON_RAMP_32_US;  return 0;
    case 48:  *out = LR20XX_RADIO_COMMON_RAMP_48_US;  return 0;
    case 64:  *out = LR20XX_RADIO_COMMON_RAMP_64_US;  return 0;
    case 80:  *out = LR20XX_RADIO_COMMON_RAMP_80_US;  return 0;
    case 96:  *out = LR20XX_RADIO_COMMON_RAMP_96_US;  return 0;
    case 112: *out = LR20XX_RADIO_COMMON_RAMP_112_US; return 0;
    case 128: *out = LR20XX_RADIO_COMMON_RAMP_128_US; return 0;
    case 144: *out = LR20XX_RADIO_COMMON_RAMP_144_US; return 0;
    case 160: *out = LR20XX_RADIO_COMMON_RAMP_160_US; return 0;
    case 176: *out = LR20XX_RADIO_COMMON_RAMP_176_US; return 0;
    case 192: *out = LR20XX_RADIO_COMMON_RAMP_192_US; return 0;
    case 208: *out = LR20XX_RADIO_COMMON_RAMP_208_US; return 0;
    case 240: *out = LR20XX_RADIO_COMMON_RAMP_240_US; return 0;
    case 272: *out = LR20XX_RADIO_COMMON_RAMP_272_US; return 0;
    case 304: *out = LR20XX_RADIO_COMMON_RAMP_304_US; return 0;
    default:  return -1;
    }
}

/**
 * Map lr20xx ramp time enum back to µs for display.
 */
static int lr20xx_ramp_to_us( lr20xx_radio_common_ramp_time_t ramp )
{
    static const int table[] = { 2, 4, 8, 16, 32, 48, 64, 80, 96, 112,
                                 128, 144, 160, 176, 192, 208, 240, 272, 304 };
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

static lr20xx_radio_lora_bw_t map_bw( uint16_t bw_khz )
{
    switch( bw_khz )
    {
    case 125:
        return LR20XX_RADIO_LORA_BW_125;
    case 250:
        return LR20XX_RADIO_LORA_BW_250;
    case 500:
        return LR20XX_RADIO_LORA_BW_500;
    case 812:
        return LR20XX_RADIO_LORA_BW_812;
    default:
        return LR20XX_RADIO_LORA_BW_125;
    }
}

static lr20xx_radio_lora_sf_t map_sf( uint8_t sf )
{
    switch( sf )
    {
    case 5:
        return LR20XX_RADIO_LORA_SF5;
    case 6:
        return LR20XX_RADIO_LORA_SF6;
    case 7:
        return LR20XX_RADIO_LORA_SF7;
    case 8:
        return LR20XX_RADIO_LORA_SF8;
    case 9:
        return LR20XX_RADIO_LORA_SF9;
    case 10:
        return LR20XX_RADIO_LORA_SF10;
    case 11:
        return LR20XX_RADIO_LORA_SF11;
    case 12:
        return LR20XX_RADIO_LORA_SF12;
    default:
        return LR20XX_RADIO_LORA_SF7;
    }
}

static lr20xx_radio_lora_cr_t map_cr( coding_rate_t cr )
{
    switch( cr )
    {
    case CODING_RATE_4_5:
        return LR20XX_RADIO_LORA_CR_4_5;
    case CODING_RATE_4_6:
        return LR20XX_RADIO_LORA_CR_4_6;
    case CODING_RATE_4_7:
        return LR20XX_RADIO_LORA_CR_4_7;
    case CODING_RATE_4_8:
        return LR20XX_RADIO_LORA_CR_4_8;
    case CODING_RATE_LI_4_5:
        return LR20XX_RADIO_LORA_CR_LI_4_5;
    case CODING_RATE_LI_4_6:
        return LR20XX_RADIO_LORA_CR_LI_4_6;
    case CODING_RATE_LI_4_8:
        return LR20XX_RADIO_LORA_CR_LI_4_8;
    case CODING_RATE_LI_CONV_4_6:
        return LR20XX_RADIO_LORA_CR_LI_CONVOLUTIONAL_4_6;
    case CODING_RATE_LI_CONV_4_8:
        return LR20XX_RADIO_LORA_CR_LI_CONVOLUTIONAL_4_8;
    default:
        return LR20XX_RADIO_LORA_CR_4_5;
    }
}

static lr20xx_radio_flrc_br_bw_t map_flrc_br_bw( uint16_t br_kbps )
{
    switch( br_kbps )
    {
    case 2600:
        return LR20XX_RADIO_FLRC_BR_2_600_BW_2_666;
    case 2080:
        return LR20XX_RADIO_FLRC_BR_2_080_BW_2_222;
    case 1300:
        return LR20XX_RADIO_FLRC_BR_1_300_BW_1_333;
    case 1040:
        return LR20XX_RADIO_FLRC_BR_1_040_BW_1_333;
    case 650:
        return LR20XX_RADIO_FLRC_BR_0_650_BW_0_740;
    case 520:
        return LR20XX_RADIO_FLRC_BR_0_520_BW_0_571;
    case 325:
        return LR20XX_RADIO_FLRC_BR_0_325_BW_0_357;
    case 260:
        return LR20XX_RADIO_FLRC_BR_0_260_BW_0_307;
    default:
        return LR20XX_RADIO_FLRC_BR_1_300_BW_1_333;
    }
}

static lr20xx_radio_flrc_cr_t map_flrc_cr( flrc_cr_t cr )
{
    switch( cr )
    {
    case FLRC_CR_1_2:
        return LR20XX_RADIO_FLRC_CR_1_2;
    case FLRC_CR_3_4:
        return LR20XX_RADIO_FLRC_CR_3_4;
    case FLRC_CR_2_3:
        return LR20XX_RADIO_FLRC_CR_2_3;
    case FLRC_CR_NONE:
        return LR20XX_RADIO_FLRC_CR_NONE;
    default:
        return LR20XX_RADIO_FLRC_CR_3_4;
    }
}

static lr20xx_radio_flrc_pulse_shape_t map_flrc_bt( flrc_bt_t bt )
{
    switch( bt )
    {
    case FLRC_BT_OFF:
        return LR20XX_RADIO_FLRC_PULSE_SHAPE_OFF;
    case FLRC_BT_0_5:
        return LR20XX_RADIO_FLRC_PULSE_SHAPE_BT_05;
    case FLRC_BT_1:
        return LR20XX_RADIO_FLRC_PULSE_SHAPE_BT_1;
    default:
        return LR20XX_RADIO_FLRC_PULSE_SHAPE_BT_05;
    }
}

static lr20xx_radio_flrc_preamble_len_t map_flrc_preamble( flrc_preamble_t p )
{
    switch( p )
    {
    case FLRC_PREAMBLE_4:   return LR20XX_RADIO_FLRC_PREAMBLE_LEN_04_BITS;
    case FLRC_PREAMBLE_8:   return LR20XX_RADIO_FLRC_PREAMBLE_LEN_08_BITS;
    case FLRC_PREAMBLE_12:  return LR20XX_RADIO_FLRC_PREAMBLE_LEN_12_BITS;
    case FLRC_PREAMBLE_16:  return LR20XX_RADIO_FLRC_PREAMBLE_LEN_16_BITS;
    case FLRC_PREAMBLE_20:  return LR20XX_RADIO_FLRC_PREAMBLE_LEN_20_BITS;
    case FLRC_PREAMBLE_24:  return LR20XX_RADIO_FLRC_PREAMBLE_LEN_24_BITS;
    case FLRC_PREAMBLE_28:  return LR20XX_RADIO_FLRC_PREAMBLE_LEN_28_BITS;
    case FLRC_PREAMBLE_32:  return LR20XX_RADIO_FLRC_PREAMBLE_LEN_32_BITS;
    default:                return LR20XX_RADIO_FLRC_PREAMBLE_LEN_32_BITS;
    }
}

static lr20xx_radio_flrc_sync_word_len_t map_flrc_sw_len( flrc_sw_len_t sw )
{
    switch( sw )
    {
    case FLRC_SW_LEN_OFF: return LR20XX_RADIO_FLRC_SYNCWORD_LENGTH_OFF;
    case FLRC_SW_LEN_2:   return LR20XX_RADIO_FLRC_SYNCWORD_LENGTH_2_BYTES;
    case FLRC_SW_LEN_4:   return LR20XX_RADIO_FLRC_SYNCWORD_LENGTH_4_BYTES;
    default:              return LR20XX_RADIO_FLRC_SYNCWORD_LENGTH_4_BYTES;
    }
}

static lr20xx_radio_flrc_crc_types_t map_flrc_crc( flrc_crc_t crc )
{
    switch( crc )
    {
    case FLRC_CRC_OFF: return LR20XX_RADIO_FLRC_CRC_OFF;
    case FLRC_CRC_2:   return LR20XX_RADIO_FLRC_CRC_2_BYTES;
    case FLRC_CRC_3:   return LR20XX_RADIO_FLRC_CRC_3_BYTES;
    case FLRC_CRC_4:   return LR20XX_RADIO_FLRC_CRC_4_BYTES;
    default:           return LR20XX_RADIO_FLRC_CRC_2_BYTES;
    }
}

/*
 * --- chip_driver_t implementation ---
 */

static int lr20xx_chip_init( void )
{
    lr20xx_status_t rc;

    /* --- Reset and enter standby --- */

    rc = lr20xx_system_reset( radio_context );
    if( rc != LR20XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr20xx_system_reset failed (%d)\n", rc );
        return -1;
    }

    rc = lr20xx_system_set_standby_mode( radio_context, LR20XX_SYSTEM_STANDBY_MODE_RC );
    if( rc != LR20XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr20xx_system_set_standby_mode failed (%d)\n", rc );
        return -1;
    }

    /* --- Regulator: use DC-DC for better efficiency --- */

    rc = lr20xx_system_set_reg_mode( radio_context, LR20XX_SYSTEM_REG_MODE_DCDC );
    if( rc != LR20XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr20xx_system_set_reg_mode failed (%d)\n", rc );
        return -1;
    }

    /* --- Low-frequency clock: internal RC --- */

    rc = lr20xx_system_cfg_lfclk( radio_context, LR20XX_SYSTEM_LFCLK_RC );
    if( rc != LR20XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: lr20xx_system_cfg_lfclk failed (%d)\n", rc );
        return -1;
    }

    /* --- Oscillator type and crystal load capacitor trim ---
     * Query the BSP to determine XTAL vs TCXO.  On XTAL boards, apply the
     * load-capacitor trim (SetXoscCpTrim, datasheet s6.11.4): chip reset
     * defaults are non-zero and cause 100% CRC errors for FLRC at narrow
     * bandwidths (BW <= 1.333 MHz).  TCXO boards skip the trim entirely. */
    {
        ral_xosc_cfg_t                      xosc_cfg;
        lr20xx_system_tcxo_supply_voltage_t tcxo_voltage;
        uint32_t                            tcxo_startup_tick = 0;
        ral_lr20xx_bsp_get_xosc_cfg( radio_context, &xosc_cfg, &tcxo_voltage, &tcxo_startup_tick );
        if( xosc_cfg == RAL_XOSC_CFG_XTAL )
        {
            uint8_t xta, xtb, wait_us;
            ral_lr20xx_bsp_get_xosc_trim( radio_context, &xta, &xtb, &wait_us );
            rc = lr20xx_system_configure_xosc( radio_context, xta, xtb, wait_us );
            if( rc != LR20XX_STATUS_OK )
            {
                fprintf( stderr, "WARN: lr20xx_system_configure_xosc failed (%d)\n", rc );
            }
        }
    }

    /* --- Check and clear any post-reset errors --- */

    lr20xx_system_errors_t errors = 0;
    lr20xx_system_get_errors( radio_context, &errors );
    if( errors != 0 )
    {
        fprintf( stderr, "WARN: post-reset errors=0x%04X, clearing\n", errors );
        lr20xx_system_clear_errors( radio_context );
    }

    /* --- DIO configuration (LR2021 has no external RF switch) --- */

    /* DIO 8: IRQ pin (or DIO 9 on legacy EVK) */
#if defined( LEGACY_EVK_LR20XX )
    rc = lr20xx_system_set_dio_function( radio_context, LR20XX_SYSTEM_DIO_9,
                                          LR20XX_SYSTEM_DIO_FUNC_IRQ, LR20XX_SYSTEM_DIO_DRIVE_NONE );
#else
    rc = lr20xx_system_set_dio_function( radio_context, LR20XX_SYSTEM_DIO_8,
                                          LR20XX_SYSTEM_DIO_FUNC_IRQ, LR20XX_SYSTEM_DIO_DRIVE_NONE );
#endif
    if( rc != LR20XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: set_dio_function IRQ failed (%d)\n", rc );
        return -1;
    }

    /* --- Front-end calibration --- */

    /* Frequencies and RX paths sourced from ral_lr20xx_bsp_get_front_end_calibration_cfg() */
    lr20xx_radio_common_front_end_calibration_value_t cal[3];
    ral_lr20xx_bsp_get_front_end_calibration_cfg( radio_context, cal );
    rc = lr20xx_radio_common_calibrate_front_end_helper( radio_context, cal, 3 );
    if( rc != LR20XX_STATUS_OK )
    {
        fprintf( stderr, "ERROR: calibrate_front_end failed (%d)\n", rc );
        return -1;
    }

    /* --- Detect chip variant for runtime capability gating --- */

    lr20xx_system_version_t ver;
    if( lr20xx_system_get_version( radio_context, &ver ) == LR20XX_STATUS_OK )
    {
        g_chip_is_lr2021 = ( ver.major == 0x01 && ver.minor == 0x18 );
    }

    return 0;
}

static int lr20xx_set_standby( void )
{
    lr20xx_status_t rc = lr20xx_system_set_standby_mode( radio_context, LR20XX_SYSTEM_STANDBY_MODE_RC );
    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}

static int lr20xx_get_version( char* version_str, size_t len )
{
    lr20xx_system_version_t ver;
    lr20xx_status_t         rc = lr20xx_system_get_version( radio_context, &ver );
    if( rc != LR20XX_STATUS_OK )
    {
        snprintf( version_str, len, "(read failed)" );
        return -1;
    }

    const char* chip_name = "LR20xx";
    if( ver.major == 0x01 && ver.minor == 0x18 )
    {
        chip_name = "LR2021";
    }
    else if( ver.major == 0x02 )
    {
        chip_name = "LR2022";
    }

    snprintf( version_str, len, "%s [major=0x%02X minor=0x%02X]", chip_name, ver.major, ver.minor );
    return 0;
}

static int lr20xx_apply_config( const radio_config_t* cfg )
{
    lr20xx_status_t rc;
    uint32_t        freq_hz = ( uint32_t ) ( cfg->freq_mhz * 1e6f );

    /* Ensure clean standby state before reconfiguring */
    rc = lr20xx_system_set_standby_mode( radio_context, LR20XX_SYSTEM_STANDBY_MODE_RC );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }
    lr20xx_system_clear_irq_status( radio_context, LR20XX_SYSTEM_IRQ_ALL_MASK );

    cached_modulation = cfg->modulation;

    /*
     * Command order:
     *
     *   1. set_pkt_type          → dcdc_reset
     *   2. set_rf_freq
     *   3. set_rx_path           → dcdc_configure
     *   4–5. Modulation-specific + PA config (order differs by modulation):
     *        FLRC: mod_params → pkt_params → PA → syncwords (matches per_flrc demo)
     *        LoRa: PA → mod_params → pkt_params → syncword  (matches tx_cw / RAC path)
     *   6. dio_irq
     *
     * IMPORTANT: set_pkt_type MUST be called for ALL modes (including CW
     * with MODULATION_NONE) so the dcdc_reset workaround fires.  Likewise
     * set_lora_mod_params MUST always be called so dcdc_configure leaves
     * the DCDC in the correct final state for TX.  CW and infinite-preamble
     * modes don't care about the actual SF/BW/CR values — only the DCDC
     * side-effects matter.
     */

    /* 1. Packet type — always LoRa unless explicitly FLRC */
    lr20xx_radio_common_pkt_type_t pkt_type = LR20XX_RADIO_COMMON_PKT_TYPE_LORA;
    if( cfg->modulation == MODULATION_FLRC )
    {
        pkt_type = LR20XX_RADIO_COMMON_PKT_TYPE_FLRC;
    }
    rc = lr20xx_radio_common_set_pkt_type( radio_context, pkt_type );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    /* 2. Frequency */
    rc = lr20xx_radio_common_set_rf_freq( radio_context, freq_hz );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    /* 3. RX path — after set_rf_freq, matching the RAC path where
     *    ral_lr20xx_set_rf_freq() bundles both set_rf_freq and set_rx_path.
     *    dcdc_configure fires here but is overridden by set_lora_mod_params. */
    lr20xx_radio_common_rx_path_t            rx_path;
    lr20xx_radio_common_rx_path_boost_mode_t boost_mode;
    ral_lr20xx_bsp_get_rx_cfg( radio_context, freq_hz, &rx_path, &boost_mode );
    if( rx_path == LR20XX_RADIO_COMMON_RX_PATH_LF )
        boost_mode = ( cfg->rx_boost_lf >= 0 )
                         ? ( lr20xx_radio_common_rx_path_boost_mode_t ) cfg->rx_boost_lf
                         : LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_NONE;
    else
        boost_mode = ( cfg->rx_boost_hf >= 0 )
                         ? ( lr20xx_radio_common_rx_path_boost_mode_t ) cfg->rx_boost_hf
                         : LR20XX_RADIO_COMMON_RX_PATH_BOOST_MODE_4;
    rc = lr20xx_radio_common_set_rx_path( radio_context, rx_path, boost_mode );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    /* 3b. AGC gain — applies to all modulations and both RX paths */
    rc = lr20xx_radio_common_set_agc_gain( radio_context,
                                            ( lr20xx_radio_common_gain_step_t ) cfg->agc_gain );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    /* 4–5. Modulation-specific config + PA config.
     *
     * FLRC path: mod_params → pkt_params → PA → syncwords  (matches per_flrc demo)
     * LoRa path: PA → mod_params → pkt_params → syncword   (existing working order)
     */
    ral_lr20xx_bsp_tx_cfg_input_params_t  tx_input  = { .system_output_pwr_in_dbm = ( int8_t ) cfg->power_dbm,
                                                        .freq_in_hz               = freq_hz };
    ral_lr20xx_bsp_tx_cfg_output_params_t tx_output;

    if( cfg->modulation == MODULATION_FLRC )
    {
        /* FLRC: mod_params → pkt_params → PA → syncwords
         * This order matches the per_flrc demo (ralf_setup_flrc + ral_set_tx_cfg)
         * and ensures dcdc_configure fires in the correct sequence. */

        /* 4a. Modulation params */
        lr20xx_radio_flrc_mod_params_t mod_params;
        mod_params.br_bw  = map_flrc_br_bw( cfg->flrc_br_kbps );
        mod_params.cr     = map_flrc_cr( cfg->flrc_cr );
        mod_params.shape  = map_flrc_bt( cfg->flrc_bt );

        rc = lr20xx_radio_flrc_set_modulation_params( radio_context, &mod_params );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        /* 4b. Packet params — must be set BEFORE syncwords so the chip knows
         *     the syncword length and match mode when syncword bytes are written. */
        lr20xx_radio_flrc_pkt_params_t pkt_params = { 0 };
        pkt_params.preamble_len     = map_flrc_preamble( cfg->flrc_preamble );
        pkt_params.sync_word_len    = map_flrc_sw_len( cfg->flrc_sw_len );
        pkt_params.tx_syncword      = ( lr20xx_radio_flrc_tx_syncword_t ) cfg->flrc_tx_sw;
        pkt_params.match_sync_word  = ( lr20xx_radio_flrc_rx_match_sync_word_t ) cfg->flrc_rx_sw;
        pkt_params.header_type      = cfg->flrc_header_fixed ? LR20XX_RADIO_FLRC_PKT_FIX_LEN
                                                             : LR20XX_RADIO_FLRC_PKT_VAR_LEN;
        pkt_params.pld_len_in_bytes = 255;
        pkt_params.crc_type         = map_flrc_crc( cfg->flrc_crc );

        rc = lr20xx_radio_flrc_set_pkt_params( radio_context, &pkt_params );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        /* 4c. PA config — after mod/pkt params to match demo order */
        ral_lr20xx_bsp_get_tx_cfg( radio_context, &tx_input, &tx_output );
        lr20xx_apply_pa_overrides( &tx_output );

        rc = lr20xx_radio_common_set_pa_cfg( radio_context, &pa_config );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        rc = lr20xx_radio_common_set_tx_params( radio_context, tx_output.chip_output_half_pwr_in_dbm_configured,
                                                tx_output.pa_ramp_time );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        /* 4d. Syncwords — write all 3 to match demo RX path and avoid stale
         *     register state.  Register 1 uses user config; registers 2/3 are
         *     zeroed so the correlator has clean state. */
        static const uint8_t flrc_syncword_zero[LR20XX_RADIO_FLRC_SYNCWORD_LENGTH] = { 0x00, 0x00, 0x00, 0x00 };

        rc = lr20xx_radio_flrc_set_syncword( radio_context, 1, cfg->flrc_syncword );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }
        rc = lr20xx_radio_flrc_set_syncword( radio_context, 2, flrc_syncword_zero );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }
        rc = lr20xx_radio_flrc_set_syncword( radio_context, 3, flrc_syncword_zero );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        cached_flrc_mod_params = mod_params;
        cached_flrc_pkt_params = pkt_params;
    }
    else
    {
        /* LoRa / MODULATION_NONE: PA → mod_params → pkt_params (existing working order) */

        /* PA config */
        ral_lr20xx_bsp_get_tx_cfg( radio_context, &tx_input, &tx_output );
        lr20xx_apply_pa_overrides( &tx_output );

        rc = lr20xx_radio_common_set_pa_cfg( radio_context, &pa_config );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        rc = lr20xx_radio_common_set_tx_params( radio_context, tx_output.chip_output_half_pwr_in_dbm_configured,
                                                tx_output.pa_ramp_time );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        /* LoRa mod params — for MODULATION_LORA use user settings,
         * for MODULATION_NONE use defaults (CW/test modes only need
         * the DCDC side-effects, not the actual parameters). */
        lr20xx_radio_lora_mod_params_t mod_params;
        if( cfg->modulation == MODULATION_LORA )
        {
            mod_params.sf  = map_sf( cfg->sf );
            mod_params.bw  = map_bw( cfg->bw_khz );
            mod_params.cr  = map_cr( cfg->cr );
        }
        else
        {
            mod_params.sf = LR20XX_RADIO_LORA_SF7;
            mod_params.bw = LR20XX_RADIO_LORA_BW_125;
            mod_params.cr = LR20XX_RADIO_LORA_CR_4_5;
        }
        mod_params.ppm = lr20xx_radio_lora_get_recommended_ppm_offset( mod_params.sf, mod_params.bw );

        cached_lora_mod_params = mod_params;

        rc = lr20xx_radio_lora_set_modulation_params( radio_context, &mod_params );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        if( cfg->modulation == MODULATION_LORA )
        {
            /* Packet parameters — use max length for RX.
             * start_tx() overrides pld_len_in_bytes before each TX. */
            lr20xx_radio_lora_pkt_params_t pkt_params = { 0 };
            pkt_params.preamble_len_in_symb = cfg->preamble;
            pkt_params.pkt_mode = cfg->header_implicit ? LR20XX_RADIO_LORA_PKT_IMPLICIT
                                                       : LR20XX_RADIO_LORA_PKT_EXPLICIT;
            pkt_params.pld_len_in_bytes     = 255; /* max for RX */
            pkt_params.crc = cfg->crc_on ? LR20XX_RADIO_LORA_CRC_ENABLED
                                         : LR20XX_RADIO_LORA_CRC_DISABLED;
            pkt_params.iq = cfg->invert_iq ? LR20XX_RADIO_LORA_IQ_INVERTED
                                           : LR20XX_RADIO_LORA_IQ_STANDARD;

            cached_lora_pkt_params = pkt_params;

            rc = lr20xx_radio_lora_set_packet_params( radio_context, &pkt_params );
            if( rc != LR20XX_STATUS_OK )
            {
                return -1;
            }

            rc = lr20xx_radio_lora_set_syncword( radio_context, cfg->syncword );
            if( rc != LR20XX_STATUS_OK )
            {
                return -1;
            }
        }
    }

    /* 6. DIO IRQ — broad mask for general use; CW/test modes narrow it
     *    in their respective start functions. */
#if defined( LEGACY_EVK_LR20XX )
    rc = lr20xx_system_set_dio_irq_cfg( radio_context, LR20XX_SYSTEM_DIO_9,
                                         LR20XX_SYSTEM_IRQ_TX_DONE | LR20XX_SYSTEM_IRQ_RX_DONE |
                                             LR20XX_SYSTEM_IRQ_TIMEOUT | LR20XX_SYSTEM_IRQ_CRC_ERROR );
#else
    rc = lr20xx_system_set_dio_irq_cfg( radio_context, LR20XX_SYSTEM_DIO_8,
                                         LR20XX_SYSTEM_IRQ_TX_DONE | LR20XX_SYSTEM_IRQ_RX_DONE |
                                             LR20XX_SYSTEM_IRQ_TIMEOUT | LR20XX_SYSTEM_IRQ_CRC_ERROR );
#endif

    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}

static int lr20xx_start_tx_cw( void )
{
    lr20xx_status_t rc;

    /* Narrow IRQ to TX_DONE only — the tx_cw example does this.
     * apply_config() already handled all DCDC workarounds (set_pkt_type
     * and set_lora_mod_params); no need to repeat them here. */
#if defined( LEGACY_EVK_LR20XX )
    rc = lr20xx_system_set_dio_irq_cfg( radio_context, LR20XX_SYSTEM_DIO_9, LR20XX_SYSTEM_IRQ_TX_DONE );
#else
    rc = lr20xx_system_set_dio_irq_cfg( radio_context, LR20XX_SYSTEM_DIO_8, LR20XX_SYSTEM_IRQ_TX_DONE );
#endif
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    rc = lr20xx_radio_common_set_tx_test_mode( radio_context, LR20XX_RADIO_COMMON_TX_TEST_MODE_CONTINUOUS_WAVE );
    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}

static int lr20xx_start_tx_infinite_preamble( void )
{
    lr20xx_status_t rc;

    /* Same as start_tx_cw — narrow IRQ, then start test mode.
     * DCDC workarounds already handled by apply_config(). */
#if defined( LEGACY_EVK_LR20XX )
    rc = lr20xx_system_set_dio_irq_cfg( radio_context, LR20XX_SYSTEM_DIO_9, LR20XX_SYSTEM_IRQ_TX_DONE );
#else
    rc = lr20xx_system_set_dio_irq_cfg( radio_context, LR20XX_SYSTEM_DIO_8, LR20XX_SYSTEM_IRQ_TX_DONE );
#endif
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    rc = lr20xx_radio_common_set_tx_test_mode( radio_context, LR20XX_RADIO_COMMON_TX_TEST_MODE_INFINITE_PREAMBLE );
    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}


static int lr20xx_start_tx( const uint8_t* payload, uint8_t len, uint32_t timeout_ms )
{
    lr20xx_status_t rc;

    /* Update pld_len_in_bytes before TX.  apply_config() sets 255 for RX,
     * but the LR20xx uses this register for TX length too.
     * FLRC variable-length packets embed the length in the header so no
     * update is needed, but fixed-length mode requires an explicit set. */
    if( cached_modulation == MODULATION_FLRC )
    {
        if( cached_flrc_pkt_params.header_type == LR20XX_RADIO_FLRC_PKT_FIX_LEN )
        {
            lr20xx_radio_flrc_pkt_params_t pkt_params = cached_flrc_pkt_params;
            pkt_params.pld_len_in_bytes = len;
            rc = lr20xx_radio_flrc_set_pkt_params( radio_context, &pkt_params );
            if( rc != LR20XX_STATUS_OK )
            {
                return -1;
            }
        }
    }
    else
    {
        lr20xx_radio_lora_pkt_params_t pkt_params = cached_lora_pkt_params;
        pkt_params.pld_len_in_bytes = len;
        rc = lr20xx_radio_lora_set_packet_params( radio_context, &pkt_params );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }
    }

    rc = lr20xx_radio_fifo_write_tx( radio_context, payload, len );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    rc = lr20xx_radio_common_set_tx( radio_context, timeout_ms );
    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}

static int lr20xx_start_rx( uint32_t timeout_ms )
{
    lr20xx_status_t rc;

    /* Clear RX FIFO and any pending IRQs from configuration — mirrors
     * lr20xx_receive_packet() and ral_lr20xx_set_rx().  Without this,
     * a spurious IRQ generated during apply_config() (e.g., during
     * set_pkt_type for FLRC) can fire immediately after entering RX. */
    lr20xx_radio_fifo_clear_rx( radio_context );
    lr20xx_system_clear_irq_status( radio_context, LR20XX_SYSTEM_IRQ_ALL_MASK );

    if( timeout_ms == 0 )
    {
        /* Continuous RX */
        rc = lr20xx_radio_common_set_rx_with_timeout_in_rtc_step( radio_context, 0x00FFFFFF );
    }
    else
    {
        rc = lr20xx_radio_common_set_rx( radio_context, timeout_ms );
    }
    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}

static int lr20xx_stop( void )
{
    return lr20xx_set_standby();
}

static int lr20xx_get_rssi_inst( int16_t* rssi_dbm )
{
    uint8_t half_dbm_count = 0;
    lr20xx_status_t rc = lr20xx_radio_common_get_rssi_inst( radio_context, rssi_dbm, &half_dbm_count );
    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}

/*
 * --- PA parameter introspection ---
 */

static const pa_param_desc_t lr20xx_pa_params[] = {
    { "half_power",       -39, 44,  "TX power in 0.5 dB steps", false },
    { "pa_sel",             0,  1,  "PA path: 0=LF, 1=HF (read-only)", true },
    { "pa_lf_duty_cycle",   0, 31,  "Low-freq PA duty cycle", false },
    { "pa_lf_slices",       0,  7,  "Low-freq PA number of slices", false },
    { "pa_hf_duty_cycle",  16, 31,  "HF duty cycle (16=max, 31=lowest)", false },
    { "pa_ramp_us",         2, 304, "PA ramp time in microseconds", false },
};

static int lr20xx_set_pa_param( const char* name, int value )
{
    if( strcasecmp( name, "half_power" ) == 0 )
    {
        if( value < -39 || value > 44 )
        {
            return -1;
        }
        pa_ovr_half_power     = ( int8_t ) value;
        pa_ovr_half_power_set = true;
        pa_cached_half_power  = ( int8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_sel" ) == 0 )
    {
        return -4; /* read-only */
    }
    if( strcasecmp( name, "pa_lf_duty_cycle" ) == 0 )
    {
        if( value < 0 || value > 31 )
        {
            return -1;
        }
        pa_ovr_lf_duty_cycle        = ( uint8_t ) value;
        pa_ovr_lf_duty_cycle_set    = true;
        pa_config.pa_lf_duty_cycle  = ( uint8_t ) value; /* reflect immediately in `pa show` */
        return 0;
    }
    if( strcasecmp( name, "pa_lf_slices" ) == 0 )
    {
        if( value < 0 || value > 7 )
        {
            return -1;
        }
        pa_ovr_lf_slices        = ( uint8_t ) value;
        pa_ovr_lf_slices_set    = true;
        pa_config.pa_lf_slices  = ( uint8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_hf_duty_cycle" ) == 0 )
    {
        if( value < 16 || value > 31 )
        {
            return -1;
        }
        pa_ovr_hf_duty_cycle        = ( uint8_t ) value;
        pa_ovr_hf_duty_cycle_set    = true;
        pa_config.pa_hf_duty_cycle  = ( uint8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        lr20xx_radio_common_ramp_time_t ramp;
        if( map_lr20xx_ramp_us( value, &ramp ) != 0 )
        {
            printf( "ERROR: Invalid ramp time (us). Valid values:\n"
                    "  2,4,8,16,32,48,64,80,96,112,128,144,160,176,\n"
                    "  192,208,240,272,304\n" );
            return -3; /* error already printed */
        }
        pa_ovr_ramp_time = ramp;
        pa_ovr_ramp_set  = true;
        pa_cached_ramp_time = ramp;
        return 0;
    }
    return -2; /* unknown param */
}

static int lr20xx_get_pa_param( const char* name, int* value )
{
    if( strcasecmp( name, "half_power" ) == 0 )
    {
        *value = pa_cached_half_power;
        return 0;
    }
    if( strcasecmp( name, "pa_sel" ) == 0 )
    {
        *value = ( int ) pa_cached_pa_sel;
        return 0;
    }
    if( strcasecmp( name, "pa_lf_duty_cycle" ) == 0 )
    {
        *value = pa_config.pa_lf_duty_cycle;
        return 0;
    }
    if( strcasecmp( name, "pa_lf_slices" ) == 0 )
    {
        *value = pa_config.pa_lf_slices;
        return 0;
    }
    if( strcasecmp( name, "pa_hf_duty_cycle" ) == 0 )
    {
        *value = pa_config.pa_hf_duty_cycle;
        return 0;
    }
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        *value = lr20xx_ramp_to_us( pa_cached_ramp_time );
        return 0;
    }
    return -2;
}

static void lr20xx_refresh_pa_config( const radio_config_t* cfg )
{
    uint32_t freq_hz = ( uint32_t ) ( cfg->freq_mhz * 1e6f );

    ral_lr20xx_bsp_tx_cfg_input_params_t tx_input = {
        .system_output_pwr_in_dbm = ( int8_t ) cfg->power_dbm,
        .freq_in_hz               = freq_hz,
    };
    ral_lr20xx_bsp_tx_cfg_output_params_t tx_output;
    ral_lr20xx_bsp_get_tx_cfg( radio_context, &tx_input, &tx_output );

    lr20xx_apply_pa_overrides( &tx_output );
}

static int lr20xx_wait_tx_done( uint32_t timeout_ms )
{
    uint32_t elapsed = 0;

    while( elapsed < timeout_ms )
    {
        lr20xx_system_irq_mask_t irq = 0;
        lr20xx_status_t rc = lr20xx_system_get_and_clear_irq_status( radio_context, &irq );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }
        if( irq & LR20XX_SYSTEM_IRQ_TX_DONE )
        {
            return 0;
        }
        if( irq & LR20XX_SYSTEM_IRQ_TIMEOUT )
        {
            return 1;
        }
        platform_sleep_us( 1000 ); /* 1 ms */
        elapsed++;
    }
    return 1; /* timed out */
}

static int lr20xx_receive_packet( uint8_t* buf, uint8_t* buf_len, uint32_t timeout_ms,
                                  int16_t* rssi_dbm, int8_t* snr_raw )
{
    lr20xx_status_t rc;

    /* Clear RX FIFO and pending IRQs before entering RX — matches RAL's
     * ral_lr20xx_set_rx().  Without the FIFO clear, stale payload data from a
     * previous reception is returned instead of the new packet.  Without the
     * IRQ clear, a leftover RX_DONE from the previous session fires immediately
     * and the stale data is consumed as if it were a new packet. */
    lr20xx_radio_fifo_clear_rx( radio_context );
    lr20xx_system_clear_irq_status( radio_context, LR20XX_SYSTEM_IRQ_ALL_MASK );

    if( timeout_ms == 0 )
    {
        /* Continuous RX — same as start_rx(0) */
        rc = lr20xx_radio_common_set_rx_with_timeout_in_rtc_step( radio_context, 0x00FFFFFF );
    }
    else
    {
        rc = lr20xx_radio_common_set_rx( radio_context, timeout_ms );
    }
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    uint32_t elapsed = 0;
    /* For continuous RX (timeout_ms==0) poll indefinitely; otherwise add 200ms safety margin */
    bool continuous = ( timeout_ms == 0 );

    while( continuous || elapsed < ( timeout_ms + 200 ) )
    {
        lr20xx_system_irq_mask_t irq = 0;
        rc = lr20xx_system_get_and_clear_irq_status( radio_context, &irq );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }

        if( irq & LR20XX_SYSTEM_IRQ_RX_DONE )
        {
            /* Read packet status — on LR20xx the actual received payload
             * length comes from GetLoraPacketStatus, not from the generic
             * GetRxPacketLength (which returns the configured max). */
            lr20xx_radio_lora_packet_status_t pkt_status;
            memset( &pkt_status, 0, sizeof( pkt_status ) );
            rc = lr20xx_radio_lora_get_packet_status( radio_context, &pkt_status );
            if( rc != LR20XX_STATUS_OK )
            {
                return -1;
            }
            *rssi_dbm = pkt_status.rssi_pkt_in_dbm;
            *snr_raw  = pkt_status.snr_pkt_raw;

            /* Read payload using the length reported by the LoRa header */
            uint8_t pkt_len = pkt_status.packet_length_bytes;
            *buf_len = pkt_len;

            rc = lr20xx_radio_fifo_read_rx( radio_context, buf, pkt_len );
            if( rc != LR20XX_STATUS_OK )
            {
                return -1;
            }
            return 0;
        }

        if( irq & LR20XX_SYSTEM_IRQ_CRC_ERROR )
        {
            /* Read RSSI from packet status even for CRC error */
            lr20xx_radio_lora_packet_status_t pkt_status;
            memset( &pkt_status, 0, sizeof( pkt_status ) );
            lr20xx_radio_lora_get_packet_status( radio_context, &pkt_status );
            *rssi_dbm = pkt_status.rssi_pkt_in_dbm;
            *snr_raw  = 0;
            *buf_len  = 0;
            return 1;
        }

        if( irq & LR20XX_SYSTEM_IRQ_TIMEOUT )
        {
            return 2;
        }

        platform_sleep_us( 1000 ); /* 1 ms */
        elapsed++;
    }
    return 2; /* timed out (safety net) */
}

static int lr20xx_check_irq( uint32_t* irq_flags )
{
    lr20xx_system_irq_mask_t irq = 0;
    lr20xx_status_t rc = lr20xx_system_get_and_clear_irq_status( radio_context, &irq );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }

    uint32_t flags = 0;
    if( irq & LR20XX_SYSTEM_IRQ_TX_DONE )
    {
        flags |= CHIP_IRQ_TX_DONE;
    }
    if( irq & LR20XX_SYSTEM_IRQ_RX_DONE )
    {
        flags |= CHIP_IRQ_RX_DONE;
    }
    if( irq & LR20XX_SYSTEM_IRQ_TIMEOUT )
    {
        flags |= CHIP_IRQ_TIMEOUT;
    }
    if( irq & LR20XX_SYSTEM_IRQ_CRC_ERROR )
    {
        flags |= CHIP_IRQ_CRC_ERROR;
    }
    *irq_flags = flags;
    return 0;
}

static int lr20xx_read_rx_packet( uint8_t* buf, uint8_t* buf_len,
                                   int16_t* rssi_dbm, int8_t* snr_raw )
{
    lr20xx_status_t rc;

    if( cached_modulation == MODULATION_FLRC )
    {
        lr20xx_radio_flrc_pkt_status_t pkt_status;
        memset( &pkt_status, 0, sizeof( pkt_status ) );
        rc = lr20xx_radio_flrc_get_pkt_status( radio_context, &pkt_status );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }
        *rssi_dbm = pkt_status.rssi_avg_in_dbm;
        *snr_raw  = 0; /* FLRC has no SNR — use sentinel 0 */

        uint8_t pkt_len = ( uint8_t ) pkt_status.packet_length_bytes;
        *buf_len = pkt_len;

        rc = lr20xx_radio_fifo_read_rx( radio_context, buf, pkt_len );
        if( rc != LR20XX_STATUS_OK )
        {
            return -1;
        }
        return 0;
    }

    /* LoRa path */
    lr20xx_radio_lora_packet_status_t pkt_status;
    memset( &pkt_status, 0, sizeof( pkt_status ) );
    rc = lr20xx_radio_lora_get_packet_status( radio_context, &pkt_status );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }
    *rssi_dbm = pkt_status.rssi_pkt_in_dbm;
    *snr_raw  = pkt_status.snr_pkt_raw;

    uint8_t pkt_len = pkt_status.packet_length_bytes;
    *buf_len = pkt_len;

    rc = lr20xx_radio_fifo_read_rx( radio_context, buf, pkt_len );
    if( rc != LR20XX_STATUS_OK )
    {
        return -1;
    }
    return 0;
}

static uint32_t lr20xx_get_toa_ms( uint8_t pld_len )
{
    if( cached_modulation == MODULATION_FLRC )
    {
        lr20xx_radio_flrc_pkt_params_t pkt = cached_flrc_pkt_params;
        pkt.pld_len_in_bytes = pld_len;
        uint32_t us = lr20xx_get_flrc_time_on_air_in_us( &pkt, &cached_flrc_mod_params );
        return ( us + 999 ) / 1000;   /* round up µs → ms */
    }

    /* LoRa (default) */
    lr20xx_radio_lora_pkt_params_t pkt = cached_lora_pkt_params;
    pkt.pld_len_in_bytes = pld_len;
    return lr20xx_radio_lora_get_time_on_air_in_ms( &pkt, &cached_lora_mod_params );
}

static bool lr20xx_supports_modulation( modulation_id_t id )
{
    if( id == MODULATION_FLRC )
    {
        return g_chip_is_lr2021;
    }
    return true; /* all other modulations are supported */
}

static bool lr20xx_supports_region( region_id_t id )
{
    ( void ) id;
    return true; /* LR20xx supports all regions */
}

static void lr20xx_get_xosc_defaults( uint8_t* xta, uint8_t* xtb, uint8_t* wait_us )
{
    ral_lr20xx_bsp_get_xosc_trim( radio_context, xta, xtb, wait_us );
}

static int lr20xx_apply_xosc_trim( uint8_t xta, uint8_t xtb, uint8_t wait_us )
{
    ral_xosc_cfg_t                      xosc_cfg;
    lr20xx_system_tcxo_supply_voltage_t tcxo_voltage;
    uint32_t                            tcxo_startup_tick = 0;
    ral_lr20xx_bsp_get_xosc_cfg( radio_context, &xosc_cfg, &tcxo_voltage, &tcxo_startup_tick );
    if( xosc_cfg != RAL_XOSC_CFG_XTAL )
    {
        fprintf( stderr, "xosc: not supported on TCXO boards\n" );
        return -1;
    }
    lr20xx_status_t rc = lr20xx_system_configure_xosc( radio_context, xta, xtb, wait_us );
    return ( rc == LR20XX_STATUS_OK ) ? 0 : -1;
}

/*
 * --- Driver instance ---
 */

#if defined( LR2021 )
#define LR20XX_CHIP_NAME "LR2021"
#elif defined( LR2022 )
#define LR20XX_CHIP_NAME "LR2022"
#else
#define LR20XX_CHIP_NAME "LR20xx"
#endif

static const chip_driver_t lr20xx_driver = {
    .chip_name                = LR20XX_CHIP_NAME,
    .init                     = lr20xx_chip_init,
    .set_standby              = lr20xx_set_standby,
    .get_version              = lr20xx_get_version,
    .apply_config             = lr20xx_apply_config,
    .start_tx_cw              = lr20xx_start_tx_cw,
    .start_tx_infinite_preamble = lr20xx_start_tx_infinite_preamble,
    .start_tx                 = lr20xx_start_tx,
    .start_rx                 = lr20xx_start_rx,
    .stop                     = lr20xx_stop,
    .get_rssi_inst            = lr20xx_get_rssi_inst,
    .pa_param_count           = sizeof( lr20xx_pa_params ) / sizeof( lr20xx_pa_params[0] ),
    .pa_params                = lr20xx_pa_params,
    .set_pa_param             = lr20xx_set_pa_param,
    .get_pa_param             = lr20xx_get_pa_param,
    .refresh_pa_config        = lr20xx_refresh_pa_config,
    .reset_pa_overrides       = lr20xx_reset_pa_overrides,
    .supports_modulation      = lr20xx_supports_modulation,
    .supports_region          = lr20xx_supports_region,
    .get_toa_ms               = lr20xx_get_toa_ms,
    .check_irq                = lr20xx_check_irq,
    .read_rx_packet           = lr20xx_read_rx_packet,
    .wait_tx_done             = lr20xx_wait_tx_done,
    .receive_packet           = lr20xx_receive_packet,
    .apply_xosc_trim          = lr20xx_apply_xosc_trim,
    .get_xosc_defaults        = lr20xx_get_xosc_defaults,
    .xosc_has_wait            = true,
    .xosc_xta_min_pf          = 11.3, /* LR2021 DS Table 6-67 */
    .xosc_xtb_min_pf          = 11.1, /* LR2021 DS Table 6-68 */
};

const chip_driver_t* chip_get_driver( void )
{
    return &lr20xx_driver;
}
