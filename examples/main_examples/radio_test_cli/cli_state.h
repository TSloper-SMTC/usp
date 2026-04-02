/**
 * @file      cli_state.h
 *
 * @brief     Radio configuration state and active mode tracking
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef CLI_STATE_H
#define CLI_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*
 * --- Enumerations ---
 */

typedef enum
{
    REGION_NONE = 0,
    REGION_US,
    REGION_EU,
    REGION_JP,
    REGION_WW2G4,
} region_id_t;

typedef enum
{
    MODULATION_NONE = 0,
    MODULATION_LORA,
    MODULATION_GFSK,
    MODULATION_FLRC,
    /* Future: MODULATION_BPSK, MODULATION_LRFHSS, etc. */
} modulation_id_t;

/**
 * FLRC coding rate (independent of LoRa coding_rate_t).
 * Maps to lr20xx_radio_flrc_cr_t via map_flrc_cr() in chip_lr20xx.c.
 */
typedef enum
{
    FLRC_CR_1_2 = 0,
    FLRC_CR_3_4,
    FLRC_CR_2_3,
    FLRC_CR_NONE,
} flrc_cr_t;

/**
 * FLRC pulse shape (BT filter).
 * Maps to lr20xx_radio_flrc_pulse_shape_t via map_flrc_bt() in chip_lr20xx.c.
 */
typedef enum
{
    FLRC_BT_OFF = 0,
    FLRC_BT_0_5,
    FLRC_BT_1,
} flrc_bt_t;

/**
 * FLRC preamble length (discrete values only — chip hardware constraint).
 * Maps to lr20xx_radio_flrc_preamble_len_t in chip_lr20xx.c.
 */
typedef enum
{
    FLRC_PREAMBLE_4  = 0,
    FLRC_PREAMBLE_8,
    FLRC_PREAMBLE_12,
    FLRC_PREAMBLE_16,
    FLRC_PREAMBLE_20,
    FLRC_PREAMBLE_24,
    FLRC_PREAMBLE_28,
    FLRC_PREAMBLE_32,
} flrc_preamble_t;

/**
 * FLRC syncword length.
 * Maps to lr20xx_radio_flrc_sync_word_len_t in chip_lr20xx.c.
 */
typedef enum
{
    FLRC_SW_LEN_OFF = 0,
    FLRC_SW_LEN_2,
    FLRC_SW_LEN_4,
} flrc_sw_len_t;

/**
 * FLRC CRC type.
 * Maps to lr20xx_radio_flrc_crc_types_t in chip_lr20xx.c.
 */
typedef enum
{
    FLRC_CRC_OFF = 0,
    FLRC_CRC_2,
    FLRC_CRC_3,
    FLRC_CRC_4,
} flrc_crc_t;

/**
 * AGC gain step — maps to lr20xx_radio_common_gain_step_t.
 * Common across all modulations and RX paths.
 */
typedef enum
{
    AGC_GAIN_AUTO = 0, /* Enable automatic gain control */
    AGC_GAIN_G1   = 1,
    AGC_GAIN_G2   = 2,
    AGC_GAIN_G3   = 3,
    AGC_GAIN_G4   = 4,
    AGC_GAIN_G5   = 5,
    AGC_GAIN_G6   = 6,
    AGC_GAIN_G7   = 7,
    AGC_GAIN_G8   = 8,
    AGC_GAIN_G9   = 9,
    AGC_GAIN_G10  = 10,
    AGC_GAIN_G11  = 11,
    AGC_GAIN_G12  = 12,
    AGC_GAIN_G13  = 13,
} agc_gain_t;

typedef enum
{
    MODE_IDLE = 0,
    MODE_CW,
    MODE_MODULATED,
    MODE_RX,
    MODE_FHSS,
    MODE_DTS,
    MODE_HYBRID,
    MODE_EU_TEST,
    MODE_JP_TEST,
    MODE_PER_TX,
    MODE_PER_RX,
} active_mode_t;

typedef enum
{
    CODING_RATE_4_5 = 0,
    CODING_RATE_4_6,
    CODING_RATE_4_7,
    CODING_RATE_4_8,
#if defined( LR20XX ) || defined( LR11XX )
    CODING_RATE_LI_4_5,
    CODING_RATE_LI_4_6,
    CODING_RATE_LI_4_8,
#endif
#if defined( LR20XX )
    CODING_RATE_LI_CONV_4_6,
    CODING_RATE_LI_CONV_4_8,
#endif
} coding_rate_t;

/*
 * --- Radio configuration struct ---
 */

typedef struct radio_config_s
{
    /* Region */
    region_id_t region;

    /* Modulation */
    modulation_id_t modulation;

    /* Common parameters */
    float      freq_mhz;
    int        power_dbm;
    agc_gain_t agc_gain;   /* AGC gain step (auto or G1-G13) */
    int8_t     rx_boost_lf; /* LF path boost: -1=auto (0), 0-7 explicit */
    int8_t     rx_boost_hf; /* HF path boost: -1=auto (4), 0-7 explicit */

    /* LoRa-specific */
    uint16_t bw_khz;       /* 125, 250, 500 */
    uint8_t  sf;           /* 7-12 */
    coding_rate_t cr;
    uint16_t preamble;     /* 4-65535 symbols */
    uint8_t  syncword;     /* 0x12 or 0x34 */
    bool     header_implicit; /* true = implicit, false = explicit */
    bool     crc_on;          /* true = CRC enabled */
    bool     invert_iq;       /* true = inverted IQ */
    int8_t   ldro;            /* -1 = auto, 0 = off, 1 = on */

    /* GFSK-specific (Phase 2) */
    uint32_t bitrate_bps;
    uint32_t fdev_hz;
    uint32_t rxbw_hz;

    /* FLRC-specific */
    uint16_t  flrc_br_kbps; /* 260, 325, 520, 650, 1040, 1300, 2080, 2600 */
    flrc_cr_t flrc_cr;
    flrc_bt_t flrc_bt;

    /* FLRC packet parameters */
    flrc_preamble_t flrc_preamble;  /* preamble length (4-32 bits, discrete) */
    flrc_sw_len_t   flrc_sw_len;    /* syncword length: off, 2B, 4B */
    uint8_t         flrc_tx_sw;     /* TX syncword index: 0=none, 1-3 */
    uint8_t         flrc_rx_sw;     /* RX syncword match: 0=off, 1-7 (bitmask combo) */
    bool            flrc_header_fixed; /* true=fixed length, false=variable */
    flrc_crc_t      flrc_crc;       /* CRC type: off, 2B, 3B, 4B */
    uint8_t         flrc_syncword[4]; /* syncword register 1 bytes (MSB first) */

    /* Active mode */
    active_mode_t active_mode;

    /* Regulatory mode params */
    uint8_t  hybrid_mask;   /* 0-7 for hybrid mode */
    uint8_t  reg_dr;        /* data rate for regulatory test modes (0-6) */
    uint32_t reg_count;     /* packet count limit (0 = unlimited) */
    uint32_t reg_delay_ms;  /* inter-packet delay in ms */

    /* PER test parameters */
    uint32_t per_count;         /* packets for TX side to send; 0=infinite (default 100) */
    uint32_t per_interval_ms;   /* inter-packet gap on TX side (default 500) */
    uint8_t  per_payload_size;  /* payload size 6-255 (default 16) */

    /* Modulated TX payload override (0 = auto-compute from dwell time) */
    uint16_t mod_pld_size;

#ifndef __linux__
    bool term_ansi;         /* true = linenoise (ANSI), false = plain echo */
#endif

    /* XOSC capacitor trim (LR20xx and SX126x) — seeded from BSP at startup */
    uint8_t xosc_xta;       /* XTA cap trim 0-47 */
    uint8_t xosc_xtb;       /* XTB cap trim 0-47 */
    uint8_t xosc_wait_us;   /* XOSC stabilization delay µs */
} radio_config_t;

/*
 * --- Public API ---
 */

/** Initialize state to all-unset defaults */
void cli_state_init( radio_config_t* cfg );

/** Print current status to stdout */
void cli_state_print_status( const radio_config_t* cfg );

/** Get human-readable string for region */
const char* cli_state_region_str( region_id_t region );

/** Get human-readable string for modulation */
const char* cli_state_modulation_str( modulation_id_t mod );

/** Get human-readable string for active mode */
const char* cli_state_mode_str( active_mode_t mode );

/** Get human-readable string for coding rate */
const char* cli_state_cr_str( coding_rate_t cr );

/** Get human-readable string for FLRC coding rate */
const char* cli_state_flrc_cr_str( flrc_cr_t cr );

/** Get human-readable string for FLRC pulse shape */
const char* cli_state_flrc_bt_str( flrc_bt_t bt );

/** Get human-readable string for FLRC preamble length */
const char* cli_state_flrc_preamble_str( flrc_preamble_t p );

/** Get human-readable string for FLRC syncword length */
const char* cli_state_flrc_sw_len_str( flrc_sw_len_t sw );

/** Get human-readable string for FLRC CRC type */
const char* cli_state_flrc_crc_str( flrc_crc_t crc );

/** Get human-readable string for FLRC RX syncword match mode */
const char* cli_state_flrc_rx_sw_str( uint8_t rx_sw );

/** Get human-readable string for AGC gain step */
const char* cli_state_agc_gain_str( agc_gain_t gain );

#ifdef __cplusplus
}
#endif

#endif /* CLI_STATE_H */
