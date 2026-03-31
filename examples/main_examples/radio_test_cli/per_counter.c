/**
 * @file      per_counter.c
 *
 * @brief     Packet Error Rate (PER) counter and statistics
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "per_counter.h"
#include <stdio.h>
#include <string.h>

/* Module-level storage for the last completed test result */
static per_counter_t s_last_result;
static bool          s_has_last = false;

void per_counter_reset( per_counter_t* c )
{
    memset( c, 0, sizeof( *c ) );
    c->rssi_min    = 0;   /* will be set on first rx */
    c->rssi_max    = -32767;
    c->snr_min_raw = 127;
    c->snr_max_raw = -128;
    c->has_data    = false;
}

void per_counter_add_tx( per_counter_t* c )
{
    c->tx_sent++;
    c->is_tx_side = true;
    c->has_data  = true;
}

void per_counter_add_rx( per_counter_t* c, uint16_t seq, int16_t rssi, int8_t snr_raw )
{
    if( c->rx_received == 0 )
    {
        /* First good packet: initialise min/max */
        c->rssi_min    = rssi;
        c->rssi_max    = rssi;
        c->snr_min_raw = snr_raw;
        c->snr_max_raw = snr_raw;
    }
    else
    {
        if( rssi < c->rssi_min ) c->rssi_min = rssi;
        if( rssi > c->rssi_max ) c->rssi_max = rssi;
        if( snr_raw < c->snr_min_raw ) c->snr_min_raw = snr_raw;
        if( snr_raw > c->snr_max_raw ) c->snr_max_raw = snr_raw;
    }

    c->rssi_sum  += ( int32_t ) rssi;
    c->snr_sum_raw += ( int32_t ) snr_raw;

    if( c->rx_received == 0 )
    {
        c->first_seq = seq;
    }
    c->last_seq = seq;
    c->rx_received++;
    c->is_tx_side = false;
    c->has_data  = true;
}

void per_counter_add_crc_error( per_counter_t* c, int16_t rssi )
{
    c->rx_crc_errors++;
    /* Don't fold CRC-error RSSI into min/max — noise-triggered CRC errors
     * (e.g. -103 dBm) would drag down the range and misrepresent the link. */
    ( void ) rssi;
    c->is_tx_side = false;
    c->has_data  = true;
}

void per_counter_add_gap( per_counter_t* c, uint16_t gap_count )
{
    c->rx_gaps += gap_count;
    c->has_data = true;
}

void per_counter_print( const per_counter_t* c )
{
    printf( "  ---------------------------------\n" );

    if( c->is_tx_side )
    {
        printf( "  Sent      : %u\n", (unsigned) c->tx_sent );
        printf( "  ---------------------------------\n" );
        return;
    }

    /* RX summary */
    /* seq_span (last_seq - first_seq + 1) already includes gaps — it covers
     * every sequence slot from first to last.  CRC errors have no decodable
     * seq so they are invisible to the span.
     *
     * CRC errors that exceed the gap count cannot correspond to lost TX
     * packets — they are spurious noise-triggered detections.
     * noise_crc = max(0, crc_errors - gaps).
     *
     * Fall back to received + crc + gaps when there are no good packets
     * (i.e. we have no sequence information at all). */
    /* Use TX count (from packet header) when known; fall back to seq span
     * for infinite mode or when no valid packets were decoded. */
    uint32_t total_expected;
    if( c->tx_count > 0 )
    {
        total_expected = c->tx_count;
    }
    else if( c->rx_received > 0 )
    {
        uint32_t seq_span = ( uint32_t ) ( ( uint16_t ) ( c->last_seq - c->first_seq + 1 ) );
        total_expected = seq_span;
    }
    else
    {
        total_expected = c->rx_received + c->rx_crc_errors + c->rx_gaps;
    }

    uint32_t lost = total_expected > c->rx_received ? total_expected - c->rx_received : 0;
    double per_pct = 0.0;
    if( total_expected > 0 )
    {
        per_pct = 100.0 * ( double ) lost / ( double ) total_expected;
    }

    uint32_t noise_crc = ( c->rx_crc_errors > c->rx_gaps )
                       ? c->rx_crc_errors - c->rx_gaps : 0;

    printf( "  Received  : %u\n", (unsigned) c->rx_received );
    if( c->rx_crc_errors > 0 )
    {
        if( noise_crc == c->rx_crc_errors )
            printf( "  CRC errors: %u  (noise)\n", (unsigned) c->rx_crc_errors );
        else if( noise_crc > 0 )
            printf( "  CRC errors: %u  (%u noise)\n", (unsigned) c->rx_crc_errors, (unsigned) noise_crc );
        else
            printf( "  CRC errors: %u\n", (unsigned) c->rx_crc_errors );
    }
    else
    {
        printf( "  CRC errors: %u\n", (unsigned) c->rx_crc_errors );
    }
    printf( "  Gaps      : %u\n", (unsigned) c->rx_gaps );
    printf( "  Expected  : %u\n", (unsigned) total_expected );
    printf( "  PER       : %.2f%%\n", per_pct );

    if( c->rx_received > 0 )
    {
        double rssi_avg = ( double ) c->rssi_sum / ( double ) c->rx_received;
        double snr_avg  = ( double ) c->snr_sum_raw / ( 4.0 * ( double ) c->rx_received );
        double snr_min  = ( double ) c->snr_min_raw / 4.0;
        double snr_max  = ( double ) c->snr_max_raw / 4.0;

        printf( "  RSSI avg  : %+.1f dBm  (min %+d  max %+d)\n",
                rssi_avg, ( int ) c->rssi_min, ( int ) c->rssi_max );
        if( c->has_snr )
        {
            printf( "  SNR avg   : %+.1f dB  (min %+.1f  max %+.1f)\n",
                    snr_avg, snr_min, snr_max );
        }
    }

    printf( "  ---------------------------------\n" );
}

const per_counter_t* per_counter_get_last( void )
{
    if( !s_has_last )
    {
        return NULL;
    }
    return &s_last_result;
}

void per_counter_save_last( const per_counter_t* c )
{
    if( c == NULL )
    {
        s_has_last = false;
        memset( &s_last_result, 0, sizeof( s_last_result ) );
    }
    else
    {
        s_last_result = *c;
        s_has_last    = true;
    }
}
