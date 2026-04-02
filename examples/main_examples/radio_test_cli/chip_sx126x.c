/**
 * @file      chip_sx126x.c
 *
 * @brief     SX126x chip_interface implementation (direct driver access)
 *
 * Talks directly to the SX126x driver over SPI — no RAC, no RAL, no LBM.
 *
 * Chip variant is determined at compile time only — the SX126x family has
 * no chip-ID register that would allow runtime differentiation.
 *
 *   SX1261 — LP PA, 150–960 MHz, up to +15 dBm
 *   SX1262 — HP PA, 150–960 MHz, up to +22 dBm
 *   SX1268 — HP PA, 410–810 MHz, up to +22 dBm
 *
 * LoRa is the only supported modulation; FLRC is not available on SX126x.
 * WW2G4 region is not applicable (SX126x is sub-GHz only).
 *
 * PA configuration is delegated to ral_sx126x_bsp_get_tx_cfg(), which
 * branches on SX1262/SX1268 vs SX1261 compile-time defines.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "chip_interface.h"
#include "cli_state.h"

#include "sx126x.h"
#include "sx126x_hal.h"
#include "ral_sx126x_bsp.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "platform.h"

/*
 * --- Private state ---
 *
 * The SX126x HAL functions take a `const void* context` parameter.
 * For the Linux HAL implementation, this context is unused (the SPI bus
 * and GPIO pins are global singletons). We pass NULL.
 */

static const void* radio_context = NULL;

void chip_set_radio_context( const void* ctx )
{
    radio_context = ctx;
}

/*
 * --- XOSC trim state ---
 *
 * The SX126x resets XTA/XTB to 0x12 every time it enters STDBY_XOSC.
 * We track the user-requested values here and re-apply them before every
 * TX/RX/CW operation (by switching to STDBY_XOSC first, writing trim,
 * then issuing the TX/RX command).
 */

static uint8_t g_xosc_xta = SX126X_XTAL_TRIMMING_CAPACITOR_DEFAULT_VALUE_STDBY_XOSC;
static uint8_t g_xosc_xtb = SX126X_XTAL_TRIMMING_CAPACITOR_DEFAULT_VALUE_STDBY_XOSC;

/* Apply stored trim values to hardware.  Must only be called on XTAL boards. */
static int sx126x_apply_trim_hw( void )
{
    sx126x_status_t rc;
    rc = sx126x_set_standby( radio_context, SX126X_STANDBY_CFG_XOSC );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }
    rc = sx126x_set_trimming_capacitor_values( radio_context, g_xosc_xta, g_xosc_xtb );
    return ( rc == SX126X_STATUS_OK ) ? 0 : -1;
}

static int sx126x_chip_apply_xosc_trim( uint8_t xta, uint8_t xtb, uint8_t wait_us )
{
    ( void ) wait_us; /* SX126x hardware manages stabilisation — no user wait */

    ral_xosc_cfg_t              xosc_cfg;
    sx126x_tcxo_ctrl_voltages_t tcxo_voltage;
    uint32_t                    tcxo_startup_time;
    ral_sx126x_bsp_get_xosc_cfg( radio_context, &xosc_cfg, &tcxo_voltage, &tcxo_startup_time );
    if( xosc_cfg != RAL_XOSC_CFG_XTAL )
    {
        fprintf( stderr, "xosc: trim not applicable on TCXO boards\n" );
        return -1;
    }
    g_xosc_xta = xta;
    g_xosc_xtb = xtb;
    return sx126x_apply_trim_hw();
}

static void sx126x_chip_get_xosc_defaults( uint8_t* xta, uint8_t* xtb, uint8_t* wait_us )
{
    *xta     = SX126X_XTAL_TRIMMING_CAPACITOR_DEFAULT_VALUE_STDBY_XOSC;
    *xtb     = SX126X_XTAL_TRIMMING_CAPACITOR_DEFAULT_VALUE_STDBY_XOSC;
    *wait_us = 0;
}

/* Re-apply trim before a radio operation if the user has set non-default values. */
static void sx126x_ensure_trim( void )
{
    if( g_xosc_xta != SX126X_XTAL_TRIMMING_CAPACITOR_DEFAULT_VALUE_STDBY_XOSC ||
        g_xosc_xtb != SX126X_XTAL_TRIMMING_CAPACITOR_DEFAULT_VALUE_STDBY_XOSC )
    {
        sx126x_apply_trim_hw();
    }
}

/*
 * --- PA state ---
 *
 * PA config is computed by ral_sx126x_bsp_get_tx_cfg() on every
 * apply_config() call.  The BSP owns the PA tables and power range
 * constants, branching on SX1262/SX1268 (HP) vs SX1261 (LP) at
 * compile time.
 *
 * When the user manually overrides PA params via the `pa` command, the
 * override values are stored here and overlaid on top of the BSP output.
 * pa_cfg is updated by apply_config() and is read back by `pa show` /
 * `pa get_param`.
 */

/* Cached LoRa mod/pkt params from apply_config() — re-used by start_tx()
 * and get_toa_ms(). */
static sx126x_mod_params_lora_t cached_lora_mod_params;
static sx126x_pkt_params_lora_t cached_lora_pkt_params;

static sx126x_pa_cfg_params_t pa_config;
static sx126x_ramp_time_t    pa_cached_ramp_time; /* last-applied ramp (from BSP or override) */
static int8_t  pa_cached_power = 0;               /* last-applied power for `pa show` */

/* Per-parameter override flags and values */
static bool    pa_ovr_power_set        = false;
static int8_t  pa_ovr_power            = 0;
static bool    pa_ovr_duty_cycle_set   = false;
static uint8_t pa_ovr_duty_cycle       = 0;

#if defined( SX1262 ) || defined( SX1268 )
static bool    pa_ovr_hp_max_set       = false;
static uint8_t pa_ovr_hp_max           = 0;
#endif

static bool               pa_ovr_ramp_set = false;
static sx126x_ramp_time_t pa_ovr_ramp_time;

/**
 * Clear all per-parameter PA overrides.
 */
static void sx126x_reset_pa_overrides( void )
{
    pa_ovr_power_set      = false;
    pa_ovr_duty_cycle_set = false;
#if defined( SX1262 ) || defined( SX1268 )
    pa_ovr_hp_max_set     = false;
#endif
    pa_ovr_ramp_set       = false;
}

/**
 * Apply per-param overrides to BSP output and cache results.
 * SX126x is sub-GHz only, so no PA path change detection needed.
 */
static void sx126x_apply_pa_overrides( ral_sx126x_bsp_tx_cfg_output_params_t* tx_output )
{
    if( pa_ovr_duty_cycle_set )
    {
        tx_output->pa_cfg.pa_duty_cycle = pa_ovr_duty_cycle;
    }
#if defined( SX1262 ) || defined( SX1268 )
    if( pa_ovr_hp_max_set )
    {
        tx_output->pa_cfg.hp_max = pa_ovr_hp_max;
    }
#endif
    if( pa_ovr_ramp_set )
    {
        tx_output->pa_ramp_time = pa_ovr_ramp_time;
    }
    if( pa_ovr_power_set )
    {
        tx_output->chip_output_pwr_in_dbm_configured = pa_ovr_power;
    }

    pa_config           = tx_output->pa_cfg;
    pa_cached_ramp_time = tx_output->pa_ramp_time;
    pa_cached_power     = tx_output->chip_output_pwr_in_dbm_configured;
}

/**
 * Map µs value to sx126x ramp time enum.  Returns -1 if not a valid discrete value.
 */
static int map_sx126x_ramp_us( int us, sx126x_ramp_time_t* out )
{
    switch( us )
    {
    case 10:   *out = SX126X_RAMP_10_US;   return 0;
    case 20:   *out = SX126X_RAMP_20_US;   return 0;
    case 40:   *out = SX126X_RAMP_40_US;   return 0;
    case 80:   *out = SX126X_RAMP_80_US;   return 0;
    case 200:  *out = SX126X_RAMP_200_US;  return 0;
    case 800:  *out = SX126X_RAMP_800_US;  return 0;
    case 1700: *out = SX126X_RAMP_1700_US; return 0;
    case 3400: *out = SX126X_RAMP_3400_US; return 0;
    default:   return -1;
    }
}

/**
 * Map sx126x ramp time enum back to µs for display.
 */
static int sx126x_ramp_to_us( sx126x_ramp_time_t ramp )
{
    static const int table[] = { 10, 20, 40, 80, 200, 800, 1700, 3400 };
    int idx = ( int ) ramp;
    if( idx >= 0 && idx < ( int ) ( sizeof( table ) / sizeof( table[0] ) ) )
    {
        return table[idx];
    }
    return -1;
}

/*
 * --- Chip name ---
 */

#if defined( SX1268 )
#define SX126X_CHIP_NAME "SX1268"
#elif defined( SX1262 )
#define SX126X_CHIP_NAME "SX1262"
#elif defined( SX1261 )
#define SX126X_CHIP_NAME "SX1261"
#else
#define SX126X_CHIP_NAME "SX126x"
#endif

/*
 * --- BW/SF/CR mapping helpers ---
 */

static sx126x_lora_bw_t map_bw( uint16_t bw_khz )
{
    switch( bw_khz )
    {
    case 125:
        return SX126X_LORA_BW_125;
    case 250:
        return SX126X_LORA_BW_250;
    case 500:
        return SX126X_LORA_BW_500;
    default:
        return SX126X_LORA_BW_125;
    }
}

static sx126x_lora_sf_t map_sf( uint8_t sf )
{
    switch( sf )
    {
    case 5:
        return SX126X_LORA_SF5;
    case 6:
        return SX126X_LORA_SF6;
    case 7:
        return SX126X_LORA_SF7;
    case 8:
        return SX126X_LORA_SF8;
    case 9:
        return SX126X_LORA_SF9;
    case 10:
        return SX126X_LORA_SF10;
    case 11:
        return SX126X_LORA_SF11;
    case 12:
        return SX126X_LORA_SF12;
    default:
        return SX126X_LORA_SF7;
    }
}

static sx126x_lora_cr_t map_cr( coding_rate_t cr )
{
    switch( cr )
    {
    case CODING_RATE_4_5:
        return SX126X_LORA_CR_4_5;
    case CODING_RATE_4_6:
        return SX126X_LORA_CR_4_6;
    case CODING_RATE_4_7:
        return SX126X_LORA_CR_4_7;
    case CODING_RATE_4_8:
        return SX126X_LORA_CR_4_8;
    default:
        return SX126X_LORA_CR_4_5;
    }
}

/*
 * --- chip_driver_t implementation ---
 */

static int sx126x_chip_init( void )
{
    sx126x_status_t rc;

    /* --- Reset and enter standby --- */

    rc = sx126x_reset( radio_context );
    if( rc != SX126X_STATUS_OK )
    {
        fprintf( stderr, "ERROR: sx126x_reset failed (%d)\n", rc );
        return -1;
    }

    rc = sx126x_set_standby( radio_context, SX126X_STANDBY_CFG_RC );
    if( rc != SX126X_STATUS_OK )
    {
        fprintf( stderr, "ERROR: sx126x_set_standby failed (%d)\n", rc );
        return -1;
    }

    /* --- Regulator mode from BSP (DCDC) --- */

    sx126x_reg_mod_t reg_mode;
    ral_sx126x_bsp_get_reg_mode( radio_context, &reg_mode );
    rc = sx126x_set_reg_mode( radio_context, reg_mode );
    if( rc != SX126X_STATUS_OK )
    {
        fprintf( stderr, "ERROR: sx126x_set_reg_mode failed (%d)\n", rc );
        return -1;
    }

    /* --- DIO2 as RF switch (BSP says true) --- */

    bool dio2_as_rf_switch;
    ral_sx126x_bsp_get_rf_switch_cfg( radio_context, &dio2_as_rf_switch );
    rc = sx126x_set_dio2_as_rf_sw_ctrl( radio_context, dio2_as_rf_switch );
    if( rc != SX126X_STATUS_OK )
    {
        fprintf( stderr, "ERROR: sx126x_set_dio2_as_rf_sw_ctrl failed (%d)\n", rc );
        return -1;
    }

    /* --- XOSC config from BSP (XTAL, no TCXO) --- */

    ral_xosc_cfg_t              xosc_cfg;
    sx126x_tcxo_ctrl_voltages_t tcxo_voltage;
    uint32_t                    tcxo_startup_time;
    ral_sx126x_bsp_get_xosc_cfg( radio_context, &xosc_cfg, &tcxo_voltage, &tcxo_startup_time );
    if( xosc_cfg == RAL_XOSC_CFG_TCXO_RADIO_CTRL )
    {
        rc = sx126x_set_dio3_as_tcxo_ctrl( radio_context, tcxo_voltage, tcxo_startup_time );
        if( rc != SX126X_STATUS_OK )
        {
            fprintf( stderr, "ERROR: sx126x_set_dio3_as_tcxo_ctrl failed (%d)\n", rc );
            return -1;
        }
    }

    /* --- RX boost from BSP (false) --- */

    bool rx_boost;
    ral_sx126x_bsp_get_rx_boost_cfg( radio_context, &rx_boost );
    rc = sx126x_cfg_rx_boosted( radio_context, rx_boost );
    if( rc != SX126X_STATUS_OK )
    {
        fprintf( stderr, "ERROR: sx126x_cfg_rx_boosted failed (%d)\n", rc );
        return -1;
    }

    /* --- Check and clear any post-reset errors --- */

    sx126x_errors_mask_t errors = 0;
    sx126x_get_device_errors( radio_context, &errors );
    if( errors != 0 )
    {
        fprintf( stderr, "WARN: post-reset errors=0x%04X, clearing\n", errors );
        sx126x_clear_device_errors( radio_context );
    }

    /* --- Set buffer base addresses --- */

    rc = sx126x_set_buffer_base_address( radio_context, 0x00, 0x00 );
    if( rc != SX126X_STATUS_OK )
    {
        fprintf( stderr, "ERROR: sx126x_set_buffer_base_address failed (%d)\n", rc );
        return -1;
    }

    /* --- DIO IRQ: TX done, RX done, timeout, CRC error on DIO1 --- */

    rc = sx126x_set_dio_irq_params(
        radio_context,
        SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERROR,
        SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_DONE | SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERROR,
        SX126X_IRQ_NONE,
        SX126X_IRQ_NONE );
    if( rc != SX126X_STATUS_OK )
    {
        fprintf( stderr, "ERROR: sx126x_set_dio_irq_params failed (%d)\n", rc );
        return -1;
    }

    return 0;
}

static int sx126x_chip_set_standby( void )
{
    sx126x_status_t rc = sx126x_set_standby( radio_context, SX126X_STANDBY_CFG_RC );
    return ( rc == SX126X_STATUS_OK ) ? 0 : -1;
}

static int sx126x_chip_get_version( char* version_str, size_t len )
{
    snprintf( version_str, len, "%s", SX126X_CHIP_NAME );
    return 0;
}

static int sx126x_chip_apply_config( const radio_config_t* cfg )
{
    sx126x_status_t rc;
    uint32_t        freq_hz = ( uint32_t ) ( cfg->freq_mhz * 1e6f );

    /* Ensure clean standby state before reconfiguring */
    rc = sx126x_set_standby( radio_context, SX126X_STANDBY_CFG_RC );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }
    sx126x_clear_irq_status( radio_context, SX126X_IRQ_ALL );

    /* Set frequency */
    rc = sx126x_set_rf_freq( radio_context, freq_hz );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    /*
     * PA config — BSP owns the PA tables and power range constants,
     * branching on HP (SX1262/SX1268) vs LP (SX1261) at compile time.
     * When the user has manually overridden params via `pa`, those values
     * are applied on top of the BSP output.
     */
    ral_sx126x_bsp_tx_cfg_input_params_t tx_input = {
        .system_output_pwr_in_dbm = ( int8_t ) cfg->power_dbm,
        .freq_in_hz               = freq_hz,
    };
    ral_sx126x_bsp_tx_cfg_output_params_t tx_output;
    ral_sx126x_bsp_get_tx_cfg( radio_context, &tx_input, &tx_output );
    sx126x_apply_pa_overrides( &tx_output );

    rc = sx126x_set_pa_cfg( radio_context, &pa_config );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    rc = sx126x_set_tx_params( radio_context, pa_cached_power,
                               pa_cached_ramp_time );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    /* Set packet type to LoRa */
    rc = sx126x_set_pkt_type( radio_context, SX126X_PKT_TYPE_LORA );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    /* Set LoRa modulation parameters */
    sx126x_mod_params_lora_t mod_params;
    mod_params.sf   = map_sf( cfg->sf );
    mod_params.bw   = map_bw( cfg->bw_khz );
    mod_params.cr   = map_cr( cfg->cr );
    /* Compute effective LDRO: symbol time (µs) = 2^SF * 1000 / BW_kHz */
    if( cfg->ldro == 1 )
    {
        mod_params.ldro = 1;
    }
    else if( cfg->ldro == 0 )
    {
        mod_params.ldro = 0;
    }
    else
    {
        uint32_t sym_time_us = ( 1u << cfg->sf ) * 1000u / cfg->bw_khz;
        mod_params.ldro = ( sym_time_us > 16000 ) ? 1 : 0;
    }

    rc = sx126x_set_lora_mod_params( radio_context, &mod_params );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    cached_lora_mod_params = mod_params;

    /* Set LoRa packet parameters */
    sx126x_pkt_params_lora_t pkt_params = { 0 };
    pkt_params.preamble_len_in_symb = cfg->preamble;
    pkt_params.header_type = cfg->header_implicit ? SX126X_LORA_PKT_IMPLICIT
                                                  : SX126X_LORA_PKT_EXPLICIT;
    pkt_params.pld_len_in_bytes     = 255; /* max */
    pkt_params.crc_is_on            = cfg->crc_on;
    pkt_params.invert_iq_is_on      = cfg->invert_iq;

    rc = sx126x_set_lora_pkt_params( radio_context, &pkt_params );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    cached_lora_pkt_params = pkt_params;

    /* Set sync word */
    rc = sx126x_set_lora_sync_word( radio_context, cfg->syncword );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    /* Apply TX modulation workaround (required by SX126x errata) */
    sx126x_tx_modulation_workaround( radio_context, SX126X_PKT_TYPE_LORA, mod_params.bw );

    return 0;
}

static int sx126x_chip_start_tx_cw( void )
{
    sx126x_ensure_trim();
    sx126x_status_t rc = sx126x_set_tx_cw( radio_context );
    return ( rc == SX126X_STATUS_OK ) ? 0 : -1;
}

static int sx126x_chip_start_tx_infinite_preamble( void )
{
    sx126x_ensure_trim();
    sx126x_status_t rc = sx126x_set_tx_infinite_preamble( radio_context );
    return ( rc == SX126X_STATUS_OK ) ? 0 : -1;
}

static int sx126x_chip_start_tx( const uint8_t* payload, uint8_t len, uint32_t timeout_ms )
{
    sx126x_status_t rc;

    sx126x_ensure_trim();

    /* Update pld_len for this TX (critical for implicit header mode) */
    sx126x_pkt_params_lora_t pkt_params = cached_lora_pkt_params;
    pkt_params.pld_len_in_bytes = len;
    rc = sx126x_set_lora_pkt_params( radio_context, &pkt_params );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    rc = sx126x_write_buffer( radio_context, 0x00, payload, len );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    rc = sx126x_set_tx( radio_context, timeout_ms );
    return ( rc == SX126X_STATUS_OK ) ? 0 : -1;
}

static int sx126x_chip_start_rx( uint32_t timeout_ms )
{
    sx126x_status_t rc;

    sx126x_ensure_trim();

    if( timeout_ms == 0 )
    {
        /* Continuous RX */
        rc = sx126x_set_rx_with_timeout_in_rtc_step( radio_context, SX126X_RX_CONTINUOUS );
    }
    else
    {
        rc = sx126x_set_rx( radio_context, timeout_ms );
    }
    return ( rc == SX126X_STATUS_OK ) ? 0 : -1;
}

static int sx126x_chip_stop( void )
{
    return sx126x_chip_set_standby();
}

static int sx126x_chip_get_rssi_inst( int16_t* rssi_dbm )
{
    sx126x_status_t rc = sx126x_get_rssi_inst( radio_context, rssi_dbm );
    return ( rc == SX126X_STATUS_OK ) ? 0 : -1;
}

/*
 * --- PA parameter introspection ---
 */

#if defined( SX1262 ) || defined( SX1268 )

static const pa_param_desc_t sx126x_pa_params[] = {
    { "power",         -9, 22, "TX power register value (dBm)", false },
    { "pa_duty_cycle",  0,  4, "PA duty cycle (max 0x04, see DS)", false },
    { "hp_max",         0,  7, "HP PA limit (7=max, enables +22 dBm)", false },
    { "pa_ramp_us",    10, 3400, "PA ramp time in microseconds", false },
};

static int sx126x_set_pa_param( const char* name, int value )
{
    if( strcasecmp( name, "power" ) == 0 )
    {
        if( value < -9 || value > 22 )
        {
            return -1;
        }
        pa_ovr_power     = ( int8_t ) value;
        pa_ovr_power_set = true;
        pa_cached_power  = ( int8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_duty_cycle" ) == 0 )
    {
        if( value < 0 || value > 4 )
        {
            return -1;
        }
        pa_ovr_duty_cycle        = ( uint8_t ) value;
        pa_ovr_duty_cycle_set    = true;
        pa_config.pa_duty_cycle  = ( uint8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "hp_max" ) == 0 )
    {
        if( value < 0 || value > 7 )
        {
            return -1;
        }
        pa_ovr_hp_max        = ( uint8_t ) value;
        pa_ovr_hp_max_set    = true;
        pa_config.hp_max     = ( uint8_t ) value;
        return 0;
    }
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        sx126x_ramp_time_t ramp;
        if( map_sx126x_ramp_us( value, &ramp ) != 0 )
        {
            printf( "ERROR: Invalid ramp time. Valid: 10,20,40,80,200,800,1700,3400 us\n" );
            return -3; /* error already printed */
        }
        pa_ovr_ramp_time = ramp;
        pa_ovr_ramp_set  = true;
        pa_cached_ramp_time = ramp;
        return 0;
    }
    return -2; /* unknown param */
}

static int sx126x_get_pa_param( const char* name, int* value )
{
    if( strcasecmp( name, "power" ) == 0 )
    {
        *value = pa_cached_power;
        return 0;
    }
    if( strcasecmp( name, "pa_duty_cycle" ) == 0 )
    {
        *value = pa_config.pa_duty_cycle;
        return 0;
    }
    if( strcasecmp( name, "hp_max" ) == 0 )
    {
        *value = pa_config.hp_max;
        return 0;
    }
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        *value = sx126x_ramp_to_us( pa_cached_ramp_time );
        return 0;
    }
    return -2;
}

#else  /* SX1261 — LP PA, no hp_max */

static const pa_param_desc_t sx126x_pa_params[] = {
    { "power",         -17, 14, "TX power register value (dBm)", false },
    { "pa_duty_cycle",   0,  7, "PA duty cycle (see DS for max)", false },
    { "pa_ramp_us",     10, 3400, "PA ramp time in microseconds", false },
};

static int sx126x_set_pa_param( const char* name, int value )
{
    if( strcasecmp( name, "power" ) == 0 )
    {
        if( value < -17 || value > 14 )
        {
            return -1;
        }
        pa_ovr_power     = ( int8_t ) value;
        pa_ovr_power_set = true;
        pa_cached_power  = ( int8_t ) value;
        return 0;
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
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        sx126x_ramp_time_t ramp;
        if( map_sx126x_ramp_us( value, &ramp ) != 0 )
        {
            printf( "ERROR: Invalid ramp time. Valid: 10,20,40,80,200,800,1700,3400 us\n" );
            return -3; /* error already printed */
        }
        pa_ovr_ramp_time = ramp;
        pa_ovr_ramp_set  = true;
        pa_cached_ramp_time = ramp;
        return 0;
    }
    return -2;
}

static int sx126x_get_pa_param( const char* name, int* value )
{
    if( strcasecmp( name, "power" ) == 0 )
    {
        *value = pa_cached_power;
        return 0;
    }
    if( strcasecmp( name, "pa_duty_cycle" ) == 0 )
    {
        *value = pa_config.pa_duty_cycle;
        return 0;
    }
    if( strcasecmp( name, "pa_ramp_us" ) == 0 )
    {
        *value = sx126x_ramp_to_us( pa_cached_ramp_time );
        return 0;
    }
    return -2;
}

#endif /* SX1262 || SX1268 */

static void sx126x_refresh_pa_config( const radio_config_t* cfg )
{
    uint32_t freq_hz = ( uint32_t ) ( cfg->freq_mhz * 1e6f );

    ral_sx126x_bsp_tx_cfg_input_params_t tx_input = {
        .system_output_pwr_in_dbm = ( int8_t ) cfg->power_dbm,
        .freq_in_hz               = freq_hz,
    };
    ral_sx126x_bsp_tx_cfg_output_params_t tx_output;
    ral_sx126x_bsp_get_tx_cfg( radio_context, &tx_input, &tx_output );

    sx126x_apply_pa_overrides( &tx_output );
}

static int sx126x_chip_wait_tx_done( uint32_t timeout_ms )
{
    uint32_t elapsed = 0;

    while( elapsed < timeout_ms )
    {
        sx126x_irq_mask_t irq = 0;
        sx126x_status_t   rc  = sx126x_get_irq_status( radio_context, &irq );
        if( rc != SX126X_STATUS_OK )
        {
            return -1;
        }
        if( irq & SX126X_IRQ_TX_DONE )
        {
            sx126x_clear_irq_status( radio_context, SX126X_IRQ_TX_DONE );
            return 0;
        }
        if( irq & SX126X_IRQ_TIMEOUT )
        {
            sx126x_clear_irq_status( radio_context, SX126X_IRQ_TIMEOUT );
            return 1;
        }
        platform_sleep_us( 1000 ); /* 1 ms */
        elapsed++;
    }
    return 1; /* timed out */
}

static int sx126x_chip_receive_packet( uint8_t* buf, uint8_t* buf_len, uint32_t timeout_ms,
                                       int16_t* rssi_dbm, int8_t* snr_raw )
{
    sx126x_status_t rc;

    sx126x_ensure_trim();

    rc = sx126x_set_rx( radio_context, timeout_ms );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    uint32_t elapsed   = 0;
    uint32_t poll_limit = timeout_ms + 200;

    while( elapsed < poll_limit )
    {
        sx126x_irq_mask_t irq = 0;
        rc = sx126x_get_irq_status( radio_context, &irq );
        if( rc != SX126X_STATUS_OK )
        {
            return -1;
        }

        if( irq & SX126X_IRQ_RX_DONE )
        {
            sx126x_clear_irq_status( radio_context, SX126X_IRQ_ALL );

            /* Read packet status */
            sx126x_pkt_status_lora_t pkt_status;
            memset( &pkt_status, 0, sizeof( pkt_status ) );
            sx126x_get_lora_pkt_status( radio_context, &pkt_status );
            *rssi_dbm = ( int16_t ) pkt_status.rssi_pkt_in_dbm;
            *snr_raw  = ( int8_t ) ( pkt_status.snr_pkt_in_db * 4 ); /* convert to ×4 units */

            /* Read buffer status to get payload length and offset */
            sx126x_rx_buffer_status_t buf_status;
            rc = sx126x_get_rx_buffer_status( radio_context, &buf_status );
            if( rc != SX126X_STATUS_OK )
            {
                return -1;
            }
            uint8_t pkt_len = buf_status.pld_len_in_bytes;
            *buf_len = pkt_len;

            rc = sx126x_read_buffer( radio_context, buf_status.buffer_start_pointer, buf, pkt_len );
            if( rc != SX126X_STATUS_OK )
            {
                return -1;
            }
            return 0;
        }

        if( irq & SX126X_IRQ_CRC_ERROR )
        {
            sx126x_clear_irq_status( radio_context, SX126X_IRQ_ALL );

            sx126x_pkt_status_lora_t pkt_status;
            memset( &pkt_status, 0, sizeof( pkt_status ) );
            sx126x_get_lora_pkt_status( radio_context, &pkt_status );
            *rssi_dbm = ( int16_t ) pkt_status.rssi_pkt_in_dbm;
            *snr_raw  = 0;
            *buf_len  = 0;
            return 1;
        }

        if( irq & SX126X_IRQ_TIMEOUT )
        {
            sx126x_clear_irq_status( radio_context, SX126X_IRQ_TIMEOUT );
            return 2;
        }

        platform_sleep_us( 1000 ); /* 1 ms */
        elapsed++;
    }
    return 2; /* timed out (safety net) */
}

static int sx126x_chip_check_irq( uint32_t* irq_flags )
{
    sx126x_irq_mask_t irq = 0;
    sx126x_status_t   rc  = sx126x_get_irq_status( radio_context, &irq );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }

    /* Clear any flags that were set */
    if( irq != 0 )
    {
        sx126x_clear_irq_status( radio_context, irq );
    }

    uint32_t flags = 0;
    if( irq & SX126X_IRQ_TX_DONE )
    {
        flags |= CHIP_IRQ_TX_DONE;
    }
    if( irq & SX126X_IRQ_RX_DONE )
    {
        flags |= CHIP_IRQ_RX_DONE;
    }
    if( irq & SX126X_IRQ_TIMEOUT )
    {
        flags |= CHIP_IRQ_TIMEOUT;
    }
    if( irq & SX126X_IRQ_CRC_ERROR )
    {
        flags |= CHIP_IRQ_CRC_ERROR;
    }
    *irq_flags = flags;
    return 0;
}

static int sx126x_chip_read_rx_packet( uint8_t* buf, uint8_t* buf_len,
                                        int16_t* rssi_dbm, int8_t* snr_raw )
{
    sx126x_status_t rc;

    /* Read packet status */
    sx126x_pkt_status_lora_t pkt_status;
    memset( &pkt_status, 0, sizeof( pkt_status ) );
    sx126x_get_lora_pkt_status( radio_context, &pkt_status );
    *rssi_dbm = ( int16_t ) pkt_status.rssi_pkt_in_dbm;
    *snr_raw  = ( int8_t ) ( pkt_status.snr_pkt_in_db * 4 ); /* convert to ×4 units */

    /* Read buffer status to get payload length and offset */
    sx126x_rx_buffer_status_t buf_status;
    rc = sx126x_get_rx_buffer_status( radio_context, &buf_status );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }
    uint8_t pkt_len = buf_status.pld_len_in_bytes;
    *buf_len = pkt_len;

    rc = sx126x_read_buffer( radio_context, buf_status.buffer_start_pointer, buf, pkt_len );
    if( rc != SX126X_STATUS_OK )
    {
        return -1;
    }
    return 0;
}

static uint32_t sx126x_chip_get_toa_ms( uint8_t pld_len )
{
    sx126x_pkt_params_lora_t pkt = cached_lora_pkt_params;
    pkt.pld_len_in_bytes = pld_len;

    return sx126x_get_lora_time_on_air_in_ms( &pkt, &cached_lora_mod_params );
}

static bool sx126x_supports_modulation( modulation_id_t id )
{
    /* SX126x supports LoRa only — FLRC is not available */
    return ( id == MODULATION_LORA );
}

static bool sx126x_supports_region( region_id_t id )
{
    /* SX126x is sub-GHz only — WW2G4 (2.4 GHz) is not supported */
    return ( id != REGION_WW2G4 );
}

/*
 * --- Driver instance ---
 */

static const chip_driver_t sx126x_driver = {
    .chip_name                  = SX126X_CHIP_NAME,
    .init                       = sx126x_chip_init,
    .set_standby                = sx126x_chip_set_standby,
    .get_version                = sx126x_chip_get_version,
    .apply_config               = sx126x_chip_apply_config,
    .start_tx_cw                = sx126x_chip_start_tx_cw,
    .start_tx_infinite_preamble = sx126x_chip_start_tx_infinite_preamble,
    .start_tx                   = sx126x_chip_start_tx,
    .start_rx                   = sx126x_chip_start_rx,
    .stop                       = sx126x_chip_stop,
    .get_rssi_inst              = sx126x_chip_get_rssi_inst,
    .pa_param_count             = sizeof( sx126x_pa_params ) / sizeof( sx126x_pa_params[0] ),
    .pa_params                  = sx126x_pa_params,
    .set_pa_param               = sx126x_set_pa_param,
    .get_pa_param               = sx126x_get_pa_param,
    .refresh_pa_config          = sx126x_refresh_pa_config,
    .reset_pa_overrides         = sx126x_reset_pa_overrides,
    .supports_modulation        = sx126x_supports_modulation,
    .supports_region            = sx126x_supports_region,
    .get_toa_ms                 = sx126x_chip_get_toa_ms,
    .check_irq                  = sx126x_chip_check_irq,
    .read_rx_packet             = sx126x_chip_read_rx_packet,
    .wait_tx_done               = sx126x_chip_wait_tx_done,
    .receive_packet             = sx126x_chip_receive_packet,
    .apply_xosc_trim            = sx126x_chip_apply_xosc_trim,
    .get_xosc_defaults          = sx126x_chip_get_xosc_defaults,
    .xosc_has_wait              = false,
    .xosc_xta_min_pf            = 11.3, /* SX1261/2 DS Table 4-1 */
    .xosc_xtb_min_pf            = 11.3, /* SX1261/2 DS Table 4-1 (no XTA/XTB distinction) */
};

const chip_driver_t* chip_get_driver( void )
{
    return &sx126x_driver;
}
