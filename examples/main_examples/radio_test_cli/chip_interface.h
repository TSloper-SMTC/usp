/**
 * @file      chip_interface.h
 *
 * @brief     Thin chip abstraction for radio test CLI
 *
 * This is NOT a RAL. It's a minimal function-pointer table that normalizes
 * the different chip driver APIs (SX126x, LR11xx, LR20xx) into a common
 * interface for the test CLI. Only one backend is compiled in (selected
 * at build time via -DRAC_RADIO=<chip>).
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef CHIP_INTERFACE_H
#define CHIP_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cli_state.h"

/*
 * Portable IRQ flag bits — returned by check_irq()
 */
#define CHIP_IRQ_TX_DONE   ( 1u << 0 )
#define CHIP_IRQ_RX_DONE   ( 1u << 1 )
#define CHIP_IRQ_TIMEOUT   ( 1u << 2 )
#define CHIP_IRQ_CRC_ERROR ( 1u << 3 )

/*
 * Forward declarations
 */
typedef struct radio_config_s radio_config_t;

/*
 * PA parameter descriptor — used by `pa show` and `pa <param> <value>`
 */
typedef struct
{
    const char* name;
    int         min;
    int         max;
    const char* description;
    bool        read_only; /**< true = display-only, cannot be set via `pa <param> <value>` */
} pa_param_desc_t;

/*
 * Chip driver interface — populated by the compile-time-selected backend
 */
typedef struct
{
    /** Human-readable chip name, e.g. "LR2021", "SX1262" */
    const char* chip_name;

    /** Reset and initialize the chip. Returns 0 on success. */
    int ( *init )( void );

    /** Put chip into standby mode */
    int ( *set_standby )( void );

    /** Get chip version string. Returns 0 on success. */
    int ( *get_version )( char* version_str, size_t len );

    /** Apply full radio configuration (freq, PA, mod params, pkt params) from radio_config_t */
    int ( *apply_config )( const radio_config_t* cfg );

    /** Start CW (unmodulated carrier) */
    int ( *start_tx_cw )( void );

    /** Start infinite preamble (modulated signal) */
    int ( *start_tx_infinite_preamble )( void );

    /** Start TX with payload. timeout_ms=0 for no timeout. */
    int ( *start_tx )( const uint8_t* payload, uint8_t len, uint32_t timeout_ms );

    /** Start RX. timeout_ms=0 for continuous. */
    int ( *start_rx )( uint32_t timeout_ms );

    /** Stop any active radio operation (return to standby) */
    int ( *stop )( void );

    /** Read instantaneous RSSI (dBm). Radio must be in RX. Returns 0 on success. */
    int ( *get_rssi_inst )( int16_t* rssi_dbm );

    /* --- PA introspection --- */

    /** Number of PA parameters available for this chip */
    int pa_param_count;

    /** Array of PA parameter descriptors */
    const pa_param_desc_t* pa_params;

    /** Set a PA parameter by name. Returns 0 on success. */
    int ( *set_pa_param )( const char* name, int value );

    /** Get a PA parameter by name. Returns 0 on success. */
    int ( *get_pa_param )( const char* name, int* value );

    /** Refresh PA config cache from BSP without touching hardware.
     *  Call after changing power or frequency so `pa show` is up to date. */
    void ( *refresh_pa_config )( const radio_config_t* cfg );

    /** Clear all per-parameter PA overrides, returning to BSP table values.
     *  Called by `pa reset`, `power <N>`, and on PA path changes. */
    void ( *reset_pa_overrides )( void );

    /**
     * Returns true if the chip supports the given modulation.
     * May be NULL if all compiled modulations are supported.
     * Used at runtime to gate modulations that require a specific chip
     * variant (e.g. FLRC requires LR2021, not LR2022).
     */
    bool ( *supports_modulation )( modulation_id_t id );

    /**
     * Returns true if the chip supports the given region.
     * May be NULL if all compiled regions are supported.
     * Used to filter tab-completion and help output at runtime
     * (e.g. SX126x is sub-GHz only and does not support WW2G4).
     */
    bool ( *supports_region )( region_id_t id );

    /**
     * Non-blocking IRQ check. Reads and clears the chip's IRQ status register
     * and maps hardware-specific flags to portable CHIP_IRQ_* bits.
     * Returns 0 on success, -1 on SPI/driver error.
     */
    int ( *check_irq )( uint32_t* irq_flags );

    /**
     * Read a received packet from the FIFO after check_irq() returned
     * CHIP_IRQ_RX_DONE. Reads payload data and packet status (RSSI, SNR).
     * snr_raw is in 0.25 dB units (divide by 4 for dB).
     * Returns 0 on success, -1 on error.
     */
    int ( *read_rx_packet )( uint8_t* buf, uint8_t* buf_len,
                             int16_t* rssi_dbm, int8_t* snr_raw );

    /**
     * Compute time-on-air in ms for a given payload length using the
     * active modulation's cached params from the last apply_config().
     * Returns 0 on error.
     * May be NULL if the chip backend does not support TOA computation.
     */
    uint32_t ( *get_toa_ms )( uint8_t pld_len );

    /**
     * Poll IRQ register until TX_DONE or timeout.
     * Returns 0=done, 1=timeout, -1=error.
     */
    int ( *wait_tx_done )( uint32_t timeout_ms );

    /**
     * Start RX, poll IRQ until packet received, CRC error, or timeout.
     * Reads payload and packet status into caller-supplied buffers.
     * Returns: 0=ok, 1=crc_error (rssi set, buf_len=0), 2=timeout, -1=error.
     * snr_raw is in 0.25 dB units (divide by 4 for dB).
     */
    int ( *receive_packet )( uint8_t* buf, uint8_t* buf_len, uint32_t timeout_ms,
                             int16_t* rssi_dbm, int8_t* snr_raw );

    /**
     * Apply XOSC capacitor trim values immediately to hardware.
     * xta and xtb are in the range 0-47; wait_us is the stabilization delay
     * (LR20xx only — SX126x ignores wait_us, the hardware handles stabilisation).
     * Returns 0 on success, -1 on failure.  NULL if chip has no XOSC trim.
     */
    int ( *apply_xosc_trim )( uint8_t xta, uint8_t xtb, uint8_t wait_us );

    /**
     * Read the BSP default XOSC trim values for this board.
     * Used to seed cli_state and to restore defaults via 'xosc default'.
     * NULL if chip has no XOSC trim.
     */
    void ( *get_xosc_defaults )( uint8_t* xta, uint8_t* xtb, uint8_t* wait_us );

    /**
     * True if the chip supports a user-configurable XOSC stabilisation delay
     * (wait_us).  False for SX126x, where stabilisation is hardware-managed.
     */
    bool xosc_has_wait;

    /**
     * Minimum capacitance of the XTA trimming capacitor in pF (reg value 0x00).
     * LR2021 DS Table 6-67: 11.3 pF.  SX1261/2 DS Table 4-1: 11.3 pF.
     */
    double xosc_xta_min_pf;

    /**
     * Minimum capacitance of the XTB trimming capacitor in pF (reg value 0x00).
     * LR2021 DS Table 6-68: 11.1 pF.
     * SX1261/2 DS Table 4-1: 11.3 pF (no distinction between XTA and XTB).
     */
    double xosc_xtb_min_pf;
} chip_driver_t;

/**
 * Get the chip driver for the compiled-in chip.
 * Implemented by chip_lr20xx.c, chip_sx126x.c, or chip_lr11xx.c
 */
const chip_driver_t* chip_get_driver( void );

/**
 * Set the opaque radio context passed to all driver HAL functions.
 * On baremetal/Linux the context is NULL (global singletons).
 * On Zephyr the glue layer populates this from device tree before chip_init().
 */
void chip_set_radio_context( const void* ctx );

#ifdef __cplusplus
}
#endif

#endif /* CHIP_INTERFACE_H */
