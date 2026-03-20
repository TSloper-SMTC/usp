/**
 * @file      per_counter.h
 *
 * @brief     Packet Error Rate (PER) counter and statistics
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#ifndef PER_COUNTER_H
#define PER_COUNTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint32_t tx_sent;        /* tx: packets sent */
    uint32_t rx_received;    /* rx: good packets (with valid seq) */
    uint32_t rx_crc_errors;  /* rx: CRC errors */
    uint32_t rx_gaps;        /* rx: total implied losses from seq gaps */
    uint16_t first_seq;      /* rx: first seq received */
    uint16_t last_seq;       /* rx: last good seq */
    int32_t  rssi_sum;       /* int32 to avoid overflow over 1000+ packets */
    int16_t  rssi_min;
    int16_t  rssi_max;
    int32_t  snr_sum_raw;    /* raw ×4 units */
    int8_t   snr_min_raw;
    int8_t   snr_max_raw;
    bool     is_tx_side;     /* which side ran */
    bool     has_snr;        /* false for modulations that have no SNR (e.g. FLRC) */
    bool     has_data;
} per_counter_t;

/** Reset all fields */
void per_counter_reset( per_counter_t* c );

/** TX side: record one transmitted packet */
void per_counter_add_tx( per_counter_t* c );

/**
 * RX side: record one successfully received packet.
 * snr_raw is in 0.25 dB units (divide by 4 for dB).
 */
void per_counter_add_rx( per_counter_t* c, uint16_t seq, int16_t rssi, int8_t snr_raw );

/** RX side: record one CRC error (packet received but corrupted) */
void per_counter_add_crc_error( per_counter_t* c, int16_t rssi );

/** RX side: record implied missing packets from a sequence gap */
void per_counter_add_gap( per_counter_t* c, uint16_t gap_count );

/** Print a formatted summary to stdout */
void per_counter_print( const per_counter_t* c );

/** Module-level last result, used by 'per stats' */
const per_counter_t* per_counter_get_last( void );

/** Save result as last (pass NULL to clear) */
void per_counter_save_last( const per_counter_t* c );

#ifdef __cplusplus
}
#endif

#endif /* PER_COUNTER_H */
