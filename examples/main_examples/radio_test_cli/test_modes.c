/**
 * @file      test_modes.c
 *
 * @brief     Radio test mode implementations
 *
 * Cooperative tick-based architecture: blocking test modes (PER, FHSS,
 * hybrid, EU-test, JP-test) are implemented as state machines that advance
 * one step per test_mode_tick() call.  The main loop alternates between
 * CLI input polling and tick dispatch.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "test_modes.h"
#include "stack_region.h"
#include "stack_lbt.h"
#include "per_counter.h"
#include "linenoise.h"
#include "smtc_hal_rtc.h"
#include "smtc_hal_led.h"

#include <stdio.h>
#include <stdlib.h>
#include "mcu_compat.h"
#include <string.h>

/*
 * --- Shared tick state -------------------------------------------------------
 */

static volatile sig_atomic_t s_stop_requested = 0;
static struct linenoiseState* s_line_state    = NULL;

void test_mode_request_stop( void )
{
    s_stop_requested = 1;
}

void test_mode_set_line_state( struct linenoiseState* ls )
{
    s_line_state = ls;
}

/** Wrap printf with linenoiseHide/Show when a line edit is active */
static void tick_hide( void )
{
    if( s_line_state != NULL )
    {
        linenoiseHide( s_line_state );
    }
}

static void tick_show( void )
{
    if( s_line_state != NULL )
    {
        linenoiseShow( s_line_state );
    }
}

/** Get monotonic clock in milliseconds (HAL-portable) */
static uint32_t now_ms( void )
{
    return hal_rtc_get_time_ms();
}

/*
 * --- PER TX tick state machine -------------------------------------------
 */

typedef enum
{
    PER_TX_PENDING,
    PER_TX_DELAY,
    PER_TX_DONE,
} per_tx_state_t;

typedef struct
{
    per_tx_state_t state;
    per_counter_t      counter;
    uint32_t           seq;
    uint32_t           count;
    uint32_t           interval_ms;
    uint8_t            pld_size;
    uint32_t           next_tx_ms;
    uint8_t            payload[256];
} per_tx_tick_t;

static per_tx_tick_t s_per_tx;

static int per_tx_arm( radio_config_t* cfg, const chip_driver_t* chip )
{
    int rc = chip->apply_config( cfg );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply radio configuration\n" );
        return -1;
    }

    per_tx_tick_t* s = &s_per_tx;
    memset( s, 0, sizeof( *s ) );
    s->count       = cfg->per_count;
    s->interval_ms = cfg->per_interval_ms;

    /* Resolve payload size: 0 = auto (maximize airtime within 400 ms FCC dwell limit) */
    if( cfg->per_payload_size == 0 && chip->get_toa_ms != NULL )
    {
        s->pld_size = 6;
        for( uint8_t n = 255; n >= 6; n-- )
        {
            if( chip->get_toa_ms( n ) < 400 )
            {
                s->pld_size = n;
                break;
            }
        }
    }
    else
    {
        s->pld_size = cfg->per_payload_size < 6 ? 6 : cfg->per_payload_size;
    }

    /* Warn if TOA exceeds limits (manual payload only — auto already ensures compliance) */
    if( cfg->per_payload_size > 0 && chip->get_toa_ms != NULL )
    {
        uint32_t toa_ms = chip->get_toa_ms( s->pld_size );
        /* 400 ms dwell limit applies to 125/250 kHz BW only; 500 kHz is DTS (no limit) */
        if( toa_ms >= 400 && cfg->bw_khz != 500 )
        {
            printf( "WARNING: TOA (~%u ms) exceeds the 400 ms FCC dwell limit."
                    " Reduce payload size.\n", (unsigned) toa_ms );
        }
        if( toa_ms >= s->interval_ms )
        {
            printf( "ERROR: TOA (~%u ms) exceeds per interval (%u ms)."
                    " Increase interval with: per interval <ms>\n",
                    (unsigned) toa_ms, (unsigned) s->interval_ms );
            return -1;
        }
    }

    per_counter_reset( &s->counter );
    s->counter.is_tx_side = true;

    if( s->count == 0 )
    {
        printf( "PER TX: infinite packets, %ums interval, %u byte payload\n",
                (unsigned) s->interval_ms, (unsigned) s->pld_size );
    }
    else
    {
        printf( "PER TX: %u packets, %ums interval, %u byte payload\n",
                (unsigned) s->count, (unsigned) s->interval_ms, (unsigned) s->pld_size );
    }
    if( cfg->modulation == MODULATION_FLRC )
    {
        printf( "  %.3f MHz  %ukbps/CR%s/BT%s  %+d dBm\n\n",
                ( double ) cfg->freq_mhz, cfg->flrc_br_kbps,
                cli_state_flrc_cr_str( cfg->flrc_cr ),
                cli_state_flrc_bt_str( cfg->flrc_bt ), cfg->power_dbm );
    }
    else
    {
        printf( "  %.3f MHz  SF%u/BW%u/CR%s  %+d dBm\n\n",
                ( double ) cfg->freq_mhz, cfg->sf, cfg->bw_khz,
                cli_state_cr_str( cfg->cr ), cfg->power_dbm );
    }

    /* Build and send first packet */
    uint16_t count16    = ( s->count > 0xFFFF ) ? 0xFFFF : ( uint16_t ) s->count;
    uint16_t interval16 = ( s->interval_ms > 0xFFFF ) ? 0xFFFF : ( uint16_t ) s->interval_ms;
    s->payload[0] = 0;
    s->payload[1] = 0;
    s->payload[2] = ( uint8_t ) ( ( count16 >> 8 ) & 0xFF );
    s->payload[3] = ( uint8_t ) ( count16 & 0xFF );
    s->payload[4] = ( uint8_t ) ( ( interval16 >> 8 ) & 0xFF );
    s->payload[5] = ( uint8_t ) ( interval16 & 0xFF );
    for( uint8_t i = 6; i < s->pld_size; i++ )
    {
        s->payload[i] = 0xAA;
    }

    rc = chip->start_tx( s->payload, s->pld_size, 5000 );
    if( rc != 0 )
    {
        printf( "ERROR: start_tx failed at seq=0\n" );
        return -1;
    }

    hal_led_set( HAL_LED_TX, true );
    s->state = PER_TX_PENDING;
    s->seq   = 0;

    cfg->active_mode = MODE_PER_TX;
    return 0;
}

static int per_tx_tick( radio_config_t* cfg, const chip_driver_t* chip )
{
    per_tx_tick_t* s = &s_per_tx;

    if( s_stop_requested )
    {
        s->state = PER_TX_DONE;
    }

    switch( s->state )
    {
    case PER_TX_PENDING:
    {
        uint32_t irq = 0;
        if( radio_irq_consume() )
        {
            int rc = chip->check_irq( &irq );
            if( rc != 0 )
            {
                tick_hide();
                printf( "ERROR: check_irq failed\n" );
                s->state = PER_TX_DONE;
                goto per_tx_done;
            }
        }
        if( irq & CHIP_IRQ_TX_DONE )
        {
            hal_led_set( HAL_LED_TX, false );
            per_counter_add_tx( &s->counter );
            tick_hide();
            if( s->count != 0 )
            {
                printf( "  [%4u/%u]  seq=0x%04X  TX ok\n",
                        (unsigned)(s->seq + 1), (unsigned) s->count, (unsigned) s->seq );
            }
            else
            {
                printf( "  [%4u]  seq=0x%04X  TX ok\n",
                        (unsigned)(s->seq + 1), (unsigned) s->seq );
            }
            tick_show();

            s->seq++;
            if( s->count != 0 && s->seq >= s->count )
            {
                s->state = PER_TX_DONE;
                goto per_tx_done;
            }
            else
            {
                s->next_tx_ms = now_ms() + s->interval_ms;
                s->state      = PER_TX_DELAY;
            }
        }
        else if( irq & CHIP_IRQ_TIMEOUT )
        {
            hal_led_set( HAL_LED_TX, false );
            tick_hide();
            printf( "WARN: TX timeout at seq=0x%04X\n", (unsigned) s->seq );
            tick_show();
            s->seq++;
            if( s->count != 0 && s->seq >= s->count )
            {
                s->state = PER_TX_DONE;
                goto per_tx_done;
            }
            else
            {
                s->next_tx_ms = now_ms() + s->interval_ms;
                s->state      = PER_TX_DELAY;
            }
        }
        /* else: no event this tick */
        break;
    }

    case PER_TX_DELAY:
    {
        if( now_ms() >= s->next_tx_ms )
        {
            /* Build next payload */
            uint16_t seq16 = ( uint16_t ) s->seq;
            s->payload[0] = ( uint8_t ) ( ( seq16 >> 8 ) & 0xFF );
            s->payload[1] = ( uint8_t ) ( seq16 & 0xFF );

            int rc = chip->start_tx( s->payload, s->pld_size, 5000 );
            if( rc != 0 )
            {
                tick_hide();
                printf( "ERROR: start_tx failed at seq=%u\n", (unsigned) s->seq );
                s->state = PER_TX_DONE;
                goto per_tx_done;
            }
            else
            {
                hal_led_set( HAL_LED_TX, true );
                s->state = PER_TX_PENDING;
            }
        }
        break;
    }

    case PER_TX_DONE:
    per_tx_done:
    {
        hal_led_set( HAL_LED_TX, false );
        chip->stop();
        tick_hide();
        printf( "\nPER TX: complete - %u packets sent.\n", (unsigned) s->counter.tx_sent );
        tick_show();
        per_counter_save_last( &s->counter );
        cfg->active_mode = MODE_IDLE;
        break;
    }
    }

    return 0;
}

/*
 * --- PER RX tick state machine --------------------------------------------
 */

typedef enum
{
    PER_RX_ACTIVE,
    PER_RX_DONE,
} per_rx_state_t;

typedef struct
{
    per_rx_state_t state;
    per_counter_t     counter;
    bool              first_packet;
    uint16_t          last_seq;
    uint16_t          tx_count;
    uint16_t          tx_interval;
    uint32_t          deadline_ms;   /* 0 = no deadline (infinite mode) */
    uint32_t          rx_timeout_ms; /* timeout for current RX window */
} per_rx_tick_t;

static per_rx_tick_t s_per_rx;

static int per_rx_arm( radio_config_t* cfg, const chip_driver_t* chip )
{
    int rc = chip->apply_config( cfg );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply radio configuration\n" );
        return -1;
    }

    per_rx_tick_t* s = &s_per_rx;
    memset( s, 0, sizeof( *s ) );

    per_counter_reset( &s->counter );
    s->counter.is_tx_side = false;
    s->counter.has_snr    = ( cfg->modulation != MODULATION_FLRC );
    s->first_packet      = true;
    s->last_seq          = 0;
    s->tx_count      = cfg->per_count;
    s->tx_interval   = cfg->per_interval_ms;
    s->deadline_ms   = 0; /* set on first event (CRC error or valid packet) */

    if( cfg->modulation == MODULATION_FLRC )
    {
        printf( "PER RX: listening on %.3f MHz  %ukbps/CR%s/BT%s\n",
                ( double ) cfg->freq_mhz, cfg->flrc_br_kbps,
                cli_state_flrc_cr_str( cfg->flrc_cr ),
                cli_state_flrc_bt_str( cfg->flrc_bt ) );
    }
    else
    {
        printf( "PER RX: listening on %.3f MHz  SF%u/BW%u/CR%s\n",
                ( double ) cfg->freq_mhz, cfg->sf, cfg->bw_khz,
                cli_state_cr_str( cfg->cr ) );
    }
    printf( "  Waiting for first packet...\n\n" );

    /* Enter RX with 1s timeout — tick will re-enter as needed */
    s->rx_timeout_ms = 1000;
    rc = chip->start_rx( s->rx_timeout_ms );
    if( rc != 0 )
    {
        printf( "ERROR: start_rx failed\n" );
        return -1;
    }

    hal_led_set( HAL_LED_RX, true );
    s->state = PER_RX_ACTIVE;
    cfg->active_mode = MODE_PER_RX;
    return 0;
}

static int per_rx_reenter_rx( const chip_driver_t* chip, per_rx_tick_t* s )
{
    /* Compute timeout for next RX window */
    if( s->deadline_ms == 0 )
    {
        s->rx_timeout_ms = 1000;
    }
    else
    {
        uint32_t n = now_ms();
        if( n >= s->deadline_ms )
        {
            s->rx_timeout_ms = 0; /* will trigger DONE check */
            return 0;
        }
        uint32_t remaining = s->deadline_ms - n;
        s->rx_timeout_ms = ( remaining > 0xFFFFFFFF ) ? 0xFFFFFFFF : ( uint32_t ) remaining;
    }

    int rc = chip->start_rx( s->rx_timeout_ms );
    if( rc != 0 )
    {
        return -1;
    }
    return 0;
}

static int per_rx_tick( radio_config_t* cfg, const chip_driver_t* chip )
{
    per_rx_tick_t* s = &s_per_rx;

    if( s_stop_requested )
    {
        s->state = PER_RX_DONE;
    }

    switch( s->state )
    {
    case PER_RX_ACTIVE:
    {
        uint32_t irq = 0;
        int      rc  = 0;
        if( radio_irq_consume() )
        {
            rc = chip->check_irq( &irq );
            if( rc != 0 )
            {
                tick_hide();
                printf( "\nPER RX: stopped - IRQ check error\n" );
                s->state = PER_RX_DONE;
                goto per_rx_done;
            }
        }

        if( irq & CHIP_IRQ_CRC_ERROR )
        {
            /* Read RSSI even for CRC error */
            uint8_t  tmp_buf[256];
            uint8_t  tmp_len = 0;
            int16_t  rssi    = 0;
            int8_t   snr     = 0;
            chip->read_rx_packet( tmp_buf, &tmp_len, &rssi, &snr );

            per_counter_add_crc_error( &s->counter, rssi );
            tick_hide();
            printf( "  [%4u]  CRC error   RSSI=%+d dBm\n",
                    (unsigned)(s->counter.rx_received + s->counter.rx_crc_errors + s->counter.rx_gaps),
                    ( int ) rssi );
            tick_show();

            /* Set deadline on first event so RX auto-stops even if
             * no valid packet is ever decoded (100% CRC errors). */
            if( s->deadline_ms == 0 && s->tx_count > 0 && s->tx_interval > 0 )
            {
                s->deadline_ms = now_ms()
                               + ( uint64_t ) s->tx_count * s->tx_interval
                               + ( uint64_t ) s->tx_interval * 2;
            }

            if( per_rx_reenter_rx( chip, s ) != 0 )
            {
                s->state = PER_RX_DONE;
            }
            break;
        }

        if( irq & CHIP_IRQ_RX_DONE )
        {
            uint8_t  buf[256];
            uint8_t  buf_len = 0;
            int16_t  rssi    = 0;
            int8_t   snr_raw = 0;

            rc = chip->read_rx_packet( buf, &buf_len, &rssi, &snr_raw );
            if( rc != 0 )
            {
                tick_hide();
                printf( "\nPER RX: stopped - read_rx_packet error\n" );
                s->state = PER_RX_DONE;
                goto per_rx_done;
            }

            if( buf_len < 6 )
            {
                per_counter_add_crc_error( &s->counter, rssi );
                tick_hide();
                printf( "  [%4u]  short packet (%u bytes)  RSSI=%+d dBm\n",
                        (unsigned)(s->counter.rx_received + s->counter.rx_crc_errors + s->counter.rx_gaps),
                        buf_len, ( int ) rssi );
                tick_show();
            }
            else
            {
                uint16_t seq = ( ( uint16_t ) buf[0] << 8 ) | buf[1];

                if( s->first_packet )
                {
                    s->last_seq        = ( uint16_t ) ( seq - 1 );
                    s->first_packet    = false;
                    s->tx_count    = ( ( uint16_t ) buf[2] << 8 ) | buf[3];
                    s->tx_interval = ( ( uint16_t ) buf[4] << 8 ) | buf[5];
                    s->counter.tx_count = s->tx_count;

                    tick_hide();
                    if( s->tx_count > 0 && s->tx_interval > 0 )
                    {
                        uint32_t remaining = ( uint32_t ) ( s->tx_count - seq - 1 );
                        s->deadline_ms = now_ms()
                                       + ( uint64_t ) remaining * s->tx_interval
                                       + ( uint64_t ) s->tx_interval * 2;
                        printf( "  TX side: %u packets, %ums interval - "
                                "deadline in ~%us\n",
                                s->tx_count, s->tx_interval,
                                ( unsigned ) ( ( remaining * s->tx_interval
                                                 + s->tx_interval * 2 ) / 1000 ) );
                    }
                    else
                    {
                        printf( "  TX side: infinite mode\n" );
                    }
                    tick_show();
                }
                else if( s->tx_count > 0 && s->tx_interval > 0 )
                {
                    uint32_t remaining = ( s->tx_count > seq + 1 )
                                       ? ( uint32_t ) ( s->tx_count - seq - 1 )
                                       : 0;
                    s->deadline_ms = now_ms()
                                   + ( uint64_t ) remaining * s->tx_interval
                                   + ( uint64_t ) s->tx_interval * 2;
                }

                /* Gap detection */
                uint16_t gap = ( uint16_t ) ( seq - s->last_seq - 1 );
                tick_hide();
                if( gap > 0 )
                {
                    per_counter_add_gap( &s->counter, gap );
                    if( s->counter.has_snr )
                        printf( "  [%4u]  seq=0x%04X  RSSI=%+d dBm  SNR=%+.1f dB  ok\n"
                                "          ^ gap: %u missed before 0x%04X\n",
                                (unsigned)(s->counter.rx_received + s->counter.rx_crc_errors + s->counter.rx_gaps),
                                seq, ( int ) rssi, ( double ) snr_raw / 4.0, (unsigned) gap, seq );
                    else
                        printf( "  [%4u]  seq=0x%04X  RSSI=%+d dBm  ok\n"
                                "          ^ gap: %u missed before 0x%04X\n",
                                (unsigned)(s->counter.rx_received + s->counter.rx_crc_errors + s->counter.rx_gaps),
                                seq, ( int ) rssi, (unsigned) gap, seq );
                }
                else
                {
                    if( s->counter.has_snr )
                        printf( "  [%4u]  seq=0x%04X  RSSI=%+d dBm  SNR=%+.1f dB  ok\n",
                                (unsigned)(s->counter.rx_received + s->counter.rx_crc_errors + s->counter.rx_gaps + 1),
                                seq, ( int ) rssi, ( double ) snr_raw / 4.0 );
                    else
                        printf( "  [%4u]  seq=0x%04X  RSSI=%+d dBm  ok\n",
                                (unsigned)(s->counter.rx_received + s->counter.rx_crc_errors + s->counter.rx_gaps + 1),
                                seq, ( int ) rssi );
                }
                tick_show();

                per_counter_add_rx( &s->counter, seq, rssi, snr_raw );
                s->last_seq = seq;

                /* Stop condition: received the last packet —
                 * fall through to PER_RX_DONE immediately so the
                 * summary prints without a prompt flash in between. */
                if( s->tx_count > 0 && seq >= s->tx_count - 1 )
                {
                    tick_hide();
                    printf( "\nPER RX: received last packet (seq=0x%04X, count=%u)\n",
                            seq, s->tx_count );
                    s->state = PER_RX_DONE;
                    goto per_rx_done;
                }
            }

            if( per_rx_reenter_rx( chip, s ) != 0 )
            {
                s->state = PER_RX_DONE;
            }
            break;
        }

        if( irq & CHIP_IRQ_TIMEOUT )
        {
            if( s->deadline_ms != 0 && now_ms() >= s->deadline_ms )
            {
                tick_hide();
                printf( "\nPER RX: stopped - deadline reached (TX side should be done)\n" );
                s->state = PER_RX_DONE;
                goto per_rx_done;
            }
            /* Re-enter RX */
            if( per_rx_reenter_rx( chip, s ) != 0 )
            {
                s->state = PER_RX_DONE;
            }
            break;
        }

        /* No event this tick */
        break;
    }

    case PER_RX_DONE:
    per_rx_done:
    {
        hal_led_set( HAL_LED_RX, false );
        chip->stop();
        tick_hide();
        per_counter_print( &s->counter );
        tick_show();
        per_counter_save_last( &s->counter );
        cfg->active_mode = MODE_IDLE;
        break;
    }
    }

    return 0;
}

/*
 * --- Modulated TX tick state machine -----------------------------------------
 *
 * Transmits repeated LoRa packets with random payload at >98% duty cycle.
 * Uses implicit header + CRC off for maximum payload.  Payload size is
 * dynamically computed to fit within the region's dwell time constraint.
 */

typedef enum
{
    MOD_TX_PENDING,
    MOD_TX_DONE,
} mod_tx_state_t;

typedef struct
{
    mod_tx_state_t state;
    const char*    label;    /* "Modulated TX" or "DTS TX" */
    uint8_t        pld_size;
    uint8_t        payload[256];
} mod_tx_tick_t;

static mod_tx_tick_t s_mod_tx;

/** Get region dwell time limit in ms (0 = no per-packet limit) */
static uint32_t region_dwell_time_ms( region_id_t region )
{
    switch( region )
    {
    case REGION_US:
    case REGION_JP:
        return 400;
    default:
        return 0;
    }
}

/*
 * Compute minimum inter-packet gap (ms) to guarantee FCC 15.247 dwell
 * compliance under worst-case sliding-window alignment.
 *
 * With round-robin hopping and no gap, a channel can be revisited
 * floor(window / (N * TOA)) + 1 times.  That total dwell exceeds 400 ms
 * for most TOA values.  The gap stretches the cycle so the extra visit
 * cannot start before the window closes.
 *
 * Returns 0 if no gap is needed (TOA >= dwell limit or single visit safe).
 */
static uint32_t compute_dwell_gap_ms( uint32_t toa_ms, uint8_t n_channels,
                                      uint32_t window_ms )
{
    if( toa_ms == 0 || toa_ms >= 400 )
    {
        return 0;
    }

    uint32_t max_visits = 400 / toa_ms;   /* floor division */
    if( max_visits == 0 )
    {
        return 0;
    }

    uint32_t divisor = ( uint32_t ) n_channels * max_visits;
    uint32_t slot_ms = ( window_ms + divisor - 1 ) / divisor;   /* ceil */
    int32_t  gap     = ( int32_t ) slot_ms - ( int32_t ) toa_ms;

    if( gap <= 0 )
    {
        return 0;
    }

    return ( uint32_t ) gap + 1;   /* +1 ms margin for tick jitter */
}

static void mod_tx_fill_random( uint8_t* buf, uint8_t len )
{
    for( uint8_t i = 0; i < len; i++ )
    {
        buf[i] = ( uint8_t ) ( rand() & 0xFF );
    }
}

static int mod_tx_arm( radio_config_t* cfg, const chip_driver_t* chip )
{
    if( chip->get_toa_ms == NULL )
    {
        printf( "ERROR: Chip backend does not support modulated TX mode\n" );
        return -1;
    }

    int rc = chip->apply_config( cfg );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply radio configuration\n" );
        return -1;
    }

    mod_tx_tick_t* s = &s_mod_tx;
    memset( s, 0, sizeof( *s ) );
    s->label = "Modulated TX";

    /* Determine payload size */
    uint32_t dwell_ms = region_dwell_time_ms( cfg->region );

    if( cfg->mod_pld_size > 0 )
    {
        /* User override */
        s->pld_size = ( uint8_t ) cfg->mod_pld_size;
    }
    else if( dwell_ms > 0 )
    {
        /* Auto-compute: search from 255 down for largest payload that fits */
        uint8_t best = 0;
        bool found = false;
        for( int len = 255; len >= 1; len-- )
        {
            uint32_t toa = chip->get_toa_ms( ( uint8_t ) len );
            if( toa > 0 && toa <= dwell_ms )
            {
                best = ( uint8_t ) len;
                found = true;
                break;
            }
        }
        if( !found )
        {
            if( cfg->modulation == MODULATION_FLRC )
            {
                printf( "ERROR: No valid payload size fits within %u ms dwell time\n"
                        "  (%ukbps is too slow for this region)\n",
                        (unsigned) dwell_ms, (unsigned) cfg->flrc_br_kbps );
            }
            else
            {
                printf( "ERROR: No valid payload size fits within %u ms dwell time\n"
                        "  (SF%u/BW%u is too slow for this region)\n",
                        (unsigned) dwell_ms, (unsigned) cfg->sf, (unsigned) cfg->bw_khz );
            }
            return -1;
        }
        s->pld_size = best;
    }
    else
    {
        s->pld_size = 255;
    }

    uint32_t toa_ms = chip->get_toa_ms( s->pld_size );

    if( cfg->modulation == MODULATION_FLRC )
    {
        printf( "%s: %.3f MHz  %ukbps/CR%s/BT%s  %+d dBm\n",
                s->label, ( double ) cfg->freq_mhz, (unsigned) cfg->flrc_br_kbps,
                cli_state_flrc_cr_str( cfg->flrc_cr ),
                cli_state_flrc_bt_str( cfg->flrc_bt ), cfg->power_dbm );
        printf( "  Header: %s, CRC: %s, payload=%u bytes, TOA=%u ms\n",
                cfg->flrc_header_fixed ? "fixed" : "variable",
                cli_state_flrc_crc_str( cfg->flrc_crc ),
                (unsigned) s->pld_size, (unsigned) toa_ms );
    }
    else
    {
        printf( "%s: %.3f MHz  SF%u/BW%u/CR%s  %+d dBm\n",
                s->label, ( double ) cfg->freq_mhz, (unsigned) cfg->sf, (unsigned) cfg->bw_khz,
                cli_state_cr_str( cfg->cr ), cfg->power_dbm );
        printf( "  Header: %s, CRC: %s, payload=%u bytes, TOA=%u ms\n",
                cfg->header_implicit ? "implicit" : "explicit",
                cfg->crc_on ? "on" : "off",
                (unsigned) s->pld_size, (unsigned) toa_ms );
    }
    if( dwell_ms > 0 )
    {
        if( toa_ms > dwell_ms )
        {
            printf( "  WARNING: TOA exceeds %u ms dwell time limit for %s\n",
                    (unsigned) dwell_ms, cli_state_region_str( cfg->region ) );
        }
        else
        {
            printf( "  Region %s: %u ms dwell time limit\n",
                    cli_state_region_str( cfg->region ), (unsigned) dwell_ms );
        }
    }
    printf( "\n" );

    /* Seed random (time-based, good enough for test payloads) */
    srand( ( unsigned ) time( NULL ) );

    /* Send first packet */
    mod_tx_fill_random( s->payload, s->pld_size );
    rc = chip->start_tx( s->payload, s->pld_size, 5000 );
    if( rc != 0 )
    {
        printf( "ERROR: start_tx failed\n" );
        return -1;
    }

    hal_led_set( HAL_LED_TX, true );
    s->state = MOD_TX_PENDING;

    cfg->active_mode = MODE_MODULATED;
    return 0;
}

static int mod_tx_tick( radio_config_t* cfg, const chip_driver_t* chip )
{
    mod_tx_tick_t* s = &s_mod_tx;

    if( s_stop_requested )
    {
        s->state = MOD_TX_DONE;
    }

    switch( s->state )
    {
    case MOD_TX_PENDING:
    {
        uint32_t irq = 0;
        int      rc  = 0;
        if( radio_irq_consume() )
        {
            rc = chip->check_irq( &irq );
            if( rc != 0 )
            {
                tick_hide();
                printf( "ERROR: check_irq failed\n" );
                s->state = MOD_TX_DONE;
                goto mod_tx_done;
            }
        }
        if( irq & ( CHIP_IRQ_TX_DONE | CHIP_IRQ_TIMEOUT ) )
        {
            /* Immediately send next packet — minimize dead time */
            mod_tx_fill_random( s->payload, s->pld_size );
            rc = chip->start_tx( s->payload, s->pld_size, 5000 );
            if( rc != 0 )
            {
                tick_hide();
                printf( "ERROR: start_tx failed\n" );
                s->state = MOD_TX_DONE;
                goto mod_tx_done;
            }
        }
        break;
    }

    case MOD_TX_DONE:
    mod_tx_done:
    {
        hal_led_set( HAL_LED_TX, false );
        chip->stop();
        tick_hide();
        printf( "%s: stopped.\n", s->label );
        tick_show();
        cfg->active_mode = MODE_IDLE;
        break;
    }
    }

    return 0;
}

/*
 * --- Hopping tick state machine (FHSS, hybrid, EU-test, JP-test) -------------
 */

/** Simple 16-byte test payload */
static const uint8_t test_payload[] = {
    0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55,
    0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55
};

typedef enum
{
    HOP_GET_CHANNEL,
    HOP_TX_PENDING,
    HOP_TX_DELAY,
    HOP_DONE,
} hop_state_t;

typedef struct
{
    hop_state_t     state;
    const char*     mode_name;
    bool            do_lbt;
    region_id_t     region;
    uint32_t        count;
    uint32_t        delay_ms;
    uint8_t         dr;
    uint32_t        pkt_num;
    uint32_t        next_tx_ms;
    uint32_t        freq_hz;   /* current channel freq */
    float           saved_freq;
    uint8_t         pld_size;           /* 0 = fixed test_payload, N = N random bytes */
    uint8_t         payload[255];       /* scratch buffer for per-packet random payload */
} hop_tick_t;

static hop_tick_t s_hop;

static int hop_arm( radio_config_t* cfg, const chip_driver_t* chip,
                    const char* mode_name, bool do_lbt )
{
    ( void ) chip;

    hop_tick_t* h = &s_hop;
    memset( h, 0, sizeof( *h ) );
    h->mode_name = mode_name;
    h->do_lbt    = do_lbt;
    h->region    = cfg->region;
    h->count     = cfg->reg_count;
    h->delay_ms  = cfg->reg_delay_ms;
    h->dr        = cfg->reg_dr;
    h->pkt_num   = 0;
    h->saved_freq = cfg->freq_mhz;
    h->state     = HOP_GET_CHANNEL;

    if( h->count == 0 )
    {
        printf( "%s: starting (DR%u, delay=%ums, count=unlimited)\n",
                mode_name, (unsigned) h->dr, (unsigned) h->delay_ms );
    }
    else
    {
        printf( "%s: starting (DR%u, delay=%ums, count=%u)\n",
                mode_name, (unsigned) h->dr, (unsigned) h->delay_ms, (unsigned) h->count );
    }

    return 0;
}

static int hop_tick( radio_config_t* cfg, const chip_driver_t* chip )
{
    hop_tick_t* h = &s_hop;

    if( s_stop_requested )
    {
        h->state = HOP_DONE;
    }

    switch( h->state )
    {
    case HOP_GET_CHANNEL:
    {
        if( h->count != 0 && h->pkt_num >= h->count )
        {
            h->state = HOP_DONE;
            break;
        }

        /* Select next channel */
        h->freq_hz = 0;
        int rc = stack_region_get_next_channel( h->dr, &h->freq_hz );
        if( rc != 0 )
        {
            tick_hide();
            printf( "%s: no channel available (duty cycle exhausted?)\n", h->mode_name );
            tick_show();

            if( h->region == REGION_EU )
            {
                /* EU duty cycle: schedule retry after 1s */
                h->next_tx_ms = now_ms() + 1000;
                h->state      = HOP_TX_DELAY;
                break;
            }
            h->state = HOP_DONE;
            break;
        }

        /* EU: check duty-cycle budget */
        if( h->region == REGION_EU )
        {
            uint32_t toa_est_ms = 50;
            if( !stack_region_is_tx_allowed( h->freq_hz, toa_est_ms ) )
            {
                tick_hide();
                printf( "%s [#%u]: %.3f MHz - duty cycle exhausted, waiting...\n",
                        h->mode_name, (unsigned)(h->pkt_num + 1), ( double ) h->freq_hz / 1e6 );
                tick_show();
                h->next_tx_ms = now_ms() + 1000;
                h->state      = HOP_TX_DELAY;
                break;
            }
        }

        /* JP: Listen Before Talk (~5ms blocking sub-call, acceptable) */
        if( h->do_lbt )
        {
            int16_t            rssi   = -128;
            stack_lbt_result_t lbt_rc = stack_lbt_check( chip, h->freq_hz, &rssi );
            if( lbt_rc == LBT_CHANNEL_BUSY )
            {
                tick_hide();
                printf( "%s [#%u]: %.3f MHz - LBT BUSY (RSSI %+d dBm)\n",
                        h->mode_name, (unsigned)(h->pkt_num + 1), ( double ) h->freq_hz / 1e6, rssi );
                tick_show();
                stack_region_mask_channel_used();
                h->next_tx_ms = now_ms() + h->delay_ms;
                h->state      = HOP_TX_DELAY;
                break;
            }
            else if( lbt_rc == LBT_ERROR )
            {
                tick_hide();
                printf( "%s: LBT error, aborting\n", h->mode_name );
                tick_show();
                h->state = HOP_DONE;
                break;
            }
            tick_hide();
            printf( "%s [#%u]: %.3f MHz - LBT clear (RSSI %+d dBm), ",
                    h->mode_name, (unsigned)(h->pkt_num + 1), ( double ) h->freq_hz / 1e6, rssi );
            tick_show();
        }

        /* Retune radio and transmit */
        cfg->freq_mhz = ( float ) ( ( double ) h->freq_hz / 1e6 );
        int apply_rc = chip->apply_config( cfg );
        cfg->freq_mhz = h->saved_freq;

        if( apply_rc != 0 )
        {
            tick_hide();
            printf( "%s: apply_config failed\n", h->mode_name );
            tick_show();
            h->state = HOP_DONE;
            break;
        }

        /* Build per-packet payload */
        const uint8_t* tx_payload;
        uint8_t        tx_len;
        if( h->pld_size > 0 )
        {
            for( uint8_t i = 0; i < h->pld_size; i++ )
            {
                h->payload[i] = ( uint8_t ) ( rand() & 0xFF );
            }
            tx_payload = h->payload;
            tx_len     = h->pld_size;
        }
        else
        {
            tx_payload = test_payload;
            tx_len     = ( uint8_t ) sizeof( test_payload );
        }

        int tx_rc = chip->start_tx( tx_payload, tx_len, 5000 );
        if( tx_rc != 0 )
        {
            tick_hide();
            printf( "%s: start_tx failed\n", h->mode_name );
            h->state = HOP_DONE;
            goto hop_done;
        }

        hal_led_set( HAL_LED_TX, true );
        h->pkt_num++;

        tick_hide();
        if( !h->do_lbt )
        {
            printf( "%s [#%u]: %.3f MHz TX\n",
                    h->mode_name, (unsigned) h->pkt_num, ( double ) h->freq_hz / 1e6 );
        }
        else
        {
            printf( "TX\n" );
        }
        tick_show();

        stack_region_record_tx( h->freq_hz, 50 );
        stack_region_mask_channel_used();

        h->state = HOP_TX_PENDING;
        break;
    }

    case HOP_TX_PENDING:
    {
        uint32_t irq = 0;
        if( radio_irq_consume() )
        {
            int rc = chip->check_irq( &irq );
            if( rc != 0 )
            {
                tick_hide();
                printf( "%s: IRQ check error\n", h->mode_name );
                h->state = HOP_DONE;
                goto hop_done;
            }
        }
        if( irq & ( CHIP_IRQ_TX_DONE | CHIP_IRQ_TIMEOUT ) )
        {
            hal_led_set( HAL_LED_TX, false );
            chip->stop();
            if( h->delay_ms > 0 )
            {
                h->next_tx_ms = now_ms() + h->delay_ms;
                h->state      = HOP_TX_DELAY;
            }
            else
            {
                h->state = HOP_GET_CHANNEL;
            }
        }
        /* else: still transmitting */
        break;
    }

    case HOP_TX_DELAY:
    {
        if( now_ms() >= h->next_tx_ms )
        {
            h->state = HOP_GET_CHANNEL;
        }
        break;
    }

    case HOP_DONE:
    hop_done:
    {
        hal_led_set( HAL_LED_TX, false );
        chip->stop();
        tick_hide();
        printf( "%s: stopped after %u packets\n", h->mode_name, (unsigned) h->pkt_num );
        tick_show();
        cfg->active_mode = MODE_IDLE;
        break;
    }
    }

    return 0;
}

/*
 * --- Mode: FHSS (US915 64-channel hopping) ----------------------------------
 */
static int start_fhss( radio_config_t* cfg, const chip_driver_t* chip )
{
    if( cfg->region != REGION_US )
    {
        printf( "ERROR: FHSS requires US region\n" );
        return -1;
    }

    if( chip->get_toa_ms == NULL )
    {
        printf( "ERROR: Chip backend does not support modulated TX mode\n" );
        return -1;
    }

    if( cfg->bw_khz != 125 && cfg->bw_khz != 250 )
    {
        printf( "ERROR: FHSS requires BW 125 or 250 kHz (current: %u kHz)\n", cfg->bw_khz );
        return -1;
    }

    int rc = stack_region_init( REGION_US );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to initialise US915 stack region\n" );
        return -1;
    }

    rc = chip->apply_config( cfg );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply radio configuration\n" );
        return -1;
    }

    /* Determine payload size (same auto-sizing as start modulated) */
    uint8_t  pld_size;
    uint32_t dwell_ms = region_dwell_time_ms( cfg->region );

    if( cfg->mod_pld_size > 0 )
    {
        pld_size = ( uint8_t ) cfg->mod_pld_size;
    }
    else if( dwell_ms > 0 )
    {
        uint8_t best  = 0;
        bool    found = false;
        for( int len = 255; len >= 1; len-- )
        {
            uint32_t toa = chip->get_toa_ms( ( uint8_t ) len );
            if( toa > 0 && toa <= dwell_ms )
            {
                best  = ( uint8_t ) len;
                found = true;
                break;
            }
        }
        if( !found )
        {
            if( cfg->modulation == MODULATION_FLRC )
            {
                printf( "ERROR: No valid payload size fits within %u ms dwell time\n"
                        "  (%ukbps is too slow for this region)\n",
                        (unsigned) dwell_ms, (unsigned) cfg->flrc_br_kbps );
            }
            else
            {
                printf( "ERROR: No valid payload size fits within %u ms dwell time\n"
                        "  (SF%u/BW%u is too slow for this region)\n",
                        (unsigned) dwell_ms, (unsigned) cfg->sf, (unsigned) cfg->bw_khz );
            }
            return -1;
        }
        pld_size = best;
    }
    else
    {
        pld_size = 255;
    }

    uint32_t toa_ms    = chip->get_toa_ms( pld_size );
    uint8_t  n_ch      = 64;      /* US915: 64 × 125 kHz channels */
    uint32_t window_ms = 20000;   /* FCC 15.247(a)(1)(i): any 20 s period */
    uint32_t gap_ms    = compute_dwell_gap_ms( toa_ms, n_ch, window_ms );

    rc = hop_arm( cfg, chip, "FHSS", false );
    if( rc != 0 )
    {
        return -1;
    }

    if( gap_ms > s_hop.delay_ms )
    {
        s_hop.delay_ms = gap_ms;
    }

    s_hop.pld_size = pld_size;
    srand( ( unsigned ) time( NULL ) );

    if( cfg->modulation == MODULATION_FLRC )
    {
        printf( "  %ukbps/CR%s/BT%s  %+d dBm\n",
                (unsigned) cfg->flrc_br_kbps, cli_state_flrc_cr_str( cfg->flrc_cr ),
                cli_state_flrc_bt_str( cfg->flrc_bt ), cfg->power_dbm );
        printf( "  Header: %s, CRC: %s, payload=%u bytes, TOA=%u ms\n",
                cfg->flrc_header_fixed ? "fixed" : "variable",
                cli_state_flrc_crc_str( cfg->flrc_crc ),
                (unsigned) pld_size, (unsigned) toa_ms );
    }
    else
    {
        printf( "  SF%u/BW%u/CR%s  %+d dBm\n",
                (unsigned) cfg->sf, (unsigned) cfg->bw_khz, cli_state_cr_str( cfg->cr ), cfg->power_dbm );
        printf( "  Header: %s, CRC: %s, payload=%u bytes, TOA=%u ms\n",
                cfg->header_implicit ? "implicit" : "explicit",
                cfg->crc_on ? "on" : "off",
                (unsigned) pld_size, (unsigned) toa_ms );
    }
    if( dwell_ms > 0 )
    {
        if( toa_ms > dwell_ms )
        {
            printf( "  WARNING: TOA exceeds %u ms dwell time limit for %s\n",
                    (unsigned) dwell_ms, cli_state_region_str( cfg->region ) );
        }
        else
        {
            printf( "  Region %s: %u ms dwell time limit\n",
                    cli_state_region_str( cfg->region ), (unsigned) dwell_ms );
        }
    }
    if( s_hop.delay_ms > 0 )
    {
        printf( "  Inter-packet gap: %u ms (dwell compliance)\n", (unsigned) s_hop.delay_ms );
    }
    printf( "\n" );

    cfg->active_mode = MODE_FHSS;
    return 0;
}

/*
 * --- Mode: DTS (US915 single-channel modulated TX, BW500) --------------------
 *
 * Validates BW 500 kHz to satisfy the §15.247(c)(1) ≥500 kHz 6 dB bandwidth
 * requirement.  All other parameters (SF, freq, header, CRC) come from the
 * current config.  Uses the same modulated packet tick machinery.
 */
static int start_dts( radio_config_t* cfg, const chip_driver_t* chip )
{
    if( cfg->region != REGION_US )
    {
        printf( "ERROR: DTS requires US region\n" );
        return -1;
    }

    if( chip->get_toa_ms == NULL )
    {
        printf( "ERROR: Chip backend does not support modulated TX mode\n" );
        return -1;
    }

    if( cfg->bw_khz != 500 )
    {
        printf( "ERROR: DTS requires BW 500 kHz (current: %u kHz)\n", cfg->bw_khz );
        return -1;
    }

    int rc = chip->apply_config( cfg );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply radio configuration\n" );
        return -1;
    }

    mod_tx_tick_t* s = &s_mod_tx;
    memset( s, 0, sizeof( *s ) );
    s->label = "DTS TX";

    uint32_t dwell_ms = region_dwell_time_ms( cfg->region );

    if( cfg->mod_pld_size > 0 )
    {
        s->pld_size = ( uint8_t ) cfg->mod_pld_size;
    }
    else if( dwell_ms > 0 )
    {
        uint8_t best  = 0;
        bool    found = false;
        for( int len = 255; len >= 1; len-- )
        {
            uint32_t toa = chip->get_toa_ms( ( uint8_t ) len );
            if( toa > 0 && toa <= dwell_ms )
            {
                best  = ( uint8_t ) len;
                found = true;
                break;
            }
        }
        if( !found )
        {
            printf( "ERROR: No valid payload size fits within %u ms dwell time\n"
                    "  (SF%u/BW%u is too slow for this region)\n",
                    (unsigned) dwell_ms, (unsigned) cfg->sf, (unsigned) cfg->bw_khz );
            return -1;
        }
        s->pld_size = best;
    }
    else
    {
        s->pld_size = 255;
    }

    uint32_t toa_ms = chip->get_toa_ms( s->pld_size );

    printf( "%s: %.3f MHz  SF%u/BW%u/CR%s  %+d dBm\n",
            s->label, ( double ) cfg->freq_mhz, (unsigned) cfg->sf, (unsigned) cfg->bw_khz,
            cli_state_cr_str( cfg->cr ), cfg->power_dbm );
    printf( "  Header: %s, CRC: %s, payload=%u bytes, TOA=%u ms\n",
            cfg->header_implicit ? "implicit" : "explicit",
            cfg->crc_on ? "on" : "off",
            (unsigned) s->pld_size, (unsigned) toa_ms );
    if( dwell_ms > 0 )
    {
        if( toa_ms > dwell_ms )
        {
            printf( "  WARNING: TOA exceeds %u ms dwell time limit for %s\n",
                    (unsigned) dwell_ms, cli_state_region_str( cfg->region ) );
        }
        else
        {
            printf( "  Region %s: %u ms dwell time limit\n",
                    cli_state_region_str( cfg->region ), (unsigned) dwell_ms );
        }
    }
    printf( "\n" );

    srand( ( unsigned ) time( NULL ) );

    mod_tx_fill_random( s->payload, s->pld_size );
    rc = chip->start_tx( s->payload, s->pld_size, 5000 );
    if( rc != 0 )
    {
        printf( "ERROR: start_tx failed\n" );
        return -1;
    }

    hal_led_set( HAL_LED_TX, true );
    s->state = MOD_TX_PENDING;

    cfg->active_mode = MODE_DTS;
    return 0;
}

/*
 * --- Mode: Hybrid (US915 8-channel sub-band hopping) -------------------------
 */
static int start_hybrid( radio_config_t* cfg, const chip_driver_t* chip )
{
    if( cfg->region != REGION_US )
    {
        printf( "ERROR: Hybrid requires US region\n" );
        return -1;
    }

    if( chip->get_toa_ms == NULL )
    {
        printf( "ERROR: Chip backend does not support modulated TX mode\n" );
        return -1;
    }

    if( cfg->bw_khz != 125 && cfg->bw_khz != 250 )
    {
        printf( "ERROR: Hybrid requires BW 125 or 250 kHz (current: %u kHz)\n", cfg->bw_khz );
        return -1;
    }

    int rc = stack_region_init( REGION_US );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to initialise US915 stack region\n" );
        return -1;
    }

    uint16_t ch_mask = ( uint16_t ) ( 1u << cfg->hybrid_mask );
    rc = stack_region_apply_channel_mask( 5, ch_mask );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply sub-band mask %u\n", cfg->hybrid_mask );
        return -1;
    }

    /* US915 CH0 = 902.3 MHz, 200 kHz spacing; each sub-band spans 8 channels (1.6 MHz) */
    float sub_low  = 902.3f + ( float ) cfg->hybrid_mask * 1.6f;
    float sub_high = sub_low + 1.4f;
    printf( "Hybrid: sub-band %u selected (channels %u-%u, %.1f-%.1f MHz)\n",
            cfg->hybrid_mask,
            ( unsigned ) cfg->hybrid_mask * 8,
            ( unsigned ) cfg->hybrid_mask * 8 + 7,
            ( double ) sub_low, ( double ) sub_high );

    rc = chip->apply_config( cfg );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply radio configuration\n" );
        return -1;
    }

    /* Determine payload size (same auto-sizing as start modulated) */
    uint8_t  pld_size;
    uint32_t dwell_ms = region_dwell_time_ms( cfg->region );

    if( cfg->mod_pld_size > 0 )
    {
        pld_size = ( uint8_t ) cfg->mod_pld_size;
    }
    else if( dwell_ms > 0 )
    {
        uint8_t best  = 0;
        bool    found = false;
        for( int len = 255; len >= 1; len-- )
        {
            uint32_t toa = chip->get_toa_ms( ( uint8_t ) len );
            if( toa > 0 && toa <= dwell_ms )
            {
                best  = ( uint8_t ) len;
                found = true;
                break;
            }
        }
        if( !found )
        {
            if( cfg->modulation == MODULATION_FLRC )
            {
                printf( "ERROR: No valid payload size fits within %u ms dwell time\n"
                        "  (%ukbps is too slow for this region)\n",
                        (unsigned) dwell_ms, (unsigned) cfg->flrc_br_kbps );
            }
            else
            {
                printf( "ERROR: No valid payload size fits within %u ms dwell time\n"
                        "  (SF%u/BW%u is too slow for this region)\n",
                        (unsigned) dwell_ms, (unsigned) cfg->sf, (unsigned) cfg->bw_khz );
            }
            return -1;
        }
        pld_size = best;
    }
    else
    {
        pld_size = 255;
    }

    uint32_t toa_ms    = chip->get_toa_ms( pld_size );
    uint8_t  n_ch      = 8;       /* Hybrid: 8 × 125 kHz channels per sub-band */
    uint32_t window_ms = ( uint32_t ) n_ch * dwell_ms;   /* 15.247(f): N × 0.4 s */
    uint32_t gap_ms    = compute_dwell_gap_ms( toa_ms, n_ch, window_ms );

    rc = hop_arm( cfg, chip, "Hybrid", false );
    if( rc != 0 )
    {
        return -1;
    }

    if( gap_ms > s_hop.delay_ms )
    {
        s_hop.delay_ms = gap_ms;
    }

    s_hop.pld_size = pld_size;
    srand( ( unsigned ) time( NULL ) );

    if( cfg->modulation == MODULATION_FLRC )
    {
        printf( "  %ukbps/CR%s/BT%s  %+d dBm\n",
                (unsigned) cfg->flrc_br_kbps, cli_state_flrc_cr_str( cfg->flrc_cr ),
                cli_state_flrc_bt_str( cfg->flrc_bt ), cfg->power_dbm );
        printf( "  Header: %s, CRC: %s, payload=%u bytes, TOA=%u ms\n",
                cfg->flrc_header_fixed ? "fixed" : "variable",
                cli_state_flrc_crc_str( cfg->flrc_crc ),
                (unsigned) pld_size, (unsigned) toa_ms );
    }
    else
    {
        printf( "  SF%u/BW%u/CR%s  %+d dBm\n",
                (unsigned) cfg->sf, (unsigned) cfg->bw_khz, cli_state_cr_str( cfg->cr ), cfg->power_dbm );
        printf( "  Header: %s, CRC: %s, payload=%u bytes, TOA=%u ms\n",
                cfg->header_implicit ? "implicit" : "explicit",
                cfg->crc_on ? "on" : "off",
                (unsigned) pld_size, (unsigned) toa_ms );
    }
    if( dwell_ms > 0 )
    {
        if( toa_ms > dwell_ms )
        {
            printf( "  WARNING: TOA exceeds %u ms dwell time limit for %s\n",
                    (unsigned) dwell_ms, cli_state_region_str( cfg->region ) );
        }
        else
        {
            printf( "  Region %s: %u ms dwell time limit\n",
                    cli_state_region_str( cfg->region ), (unsigned) dwell_ms );
        }
    }
    if( s_hop.delay_ms > 0 )
    {
        printf( "  Inter-packet gap: %u ms (dwell compliance)\n", (unsigned) s_hop.delay_ms );
    }
    printf( "\n" );

    cfg->active_mode = MODE_HYBRID;
    return 0;
}

/*
 * --- Mode: EU-test (EU868 duty-cycle-gated hopping) --------------------------
 */
static int start_eu_test( radio_config_t* cfg, const chip_driver_t* chip )
{
    if( cfg->region != REGION_EU )
    {
        printf( "ERROR: eu-test requires EU region\n" );
        return -1;
    }

    int rc = stack_region_init( REGION_EU );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to initialise EU868 stack region\n" );
        return -1;
    }

    rc = hop_arm( cfg, chip, "EU-test", false );
    if( rc != 0 )
    {
        return -1;
    }

    cfg->active_mode = MODE_EU_TEST;
    return 0;
}

/*
 * --- Mode: JP-test (AS923 LBT-gated hopping) --------------------------------
 */
static int start_jp_test( radio_config_t* cfg, const chip_driver_t* chip )
{
    if( cfg->region != REGION_JP )
    {
        printf( "ERROR: jp-test requires JP region\n" );
        return -1;
    }

    int rc = stack_region_init( REGION_JP );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to initialise AS923 stack region\n" );
        return -1;
    }

    rc = hop_arm( cfg, chip, "JP-test", true );
    if( rc != 0 )
    {
        return -1;
    }

    cfg->active_mode = MODE_JP_TEST;
    return 0;
}

/*
 * --- Public API --------------------------------------------------------------
 */

int test_mode_start( radio_config_t* cfg, const chip_driver_t* chip, active_mode_t mode )
{
    int rc;

    s_stop_requested = 0;

    /* Tick-based modes: arm the state machine */
    switch( mode )
    {
    case MODE_MODULATED:
        return mod_tx_arm( cfg, chip );

    case MODE_FHSS:
        return start_fhss( cfg, chip );

    case MODE_DTS:
        return start_dts( cfg, chip );

    case MODE_HYBRID:
        return start_hybrid( cfg, chip );

    case MODE_EU_TEST:
        return start_eu_test( cfg, chip );

    case MODE_JP_TEST:
        return start_jp_test( cfg, chip );

    case MODE_PER_TX:
        return per_tx_arm( cfg, chip );

    case MODE_PER_RX:
        return per_rx_arm( cfg, chip );

    default:
        break;
    }

    /* Non-regulatory modes: apply config first */
    rc = chip->apply_config( cfg );
    if( rc != 0 )
    {
        printf( "ERROR: Failed to apply radio configuration\n" );
        return -1;
    }

    switch( mode )
    {
    case MODE_CW:
        rc = chip->start_tx_cw();
        if( rc == 0 )
        {
            hal_led_set( HAL_LED_TX, true );
            printf( "CW transmitting at %.3f MHz, %+d dBm\n", ( double ) cfg->freq_mhz, cfg->power_dbm );
        }
        break;

    case MODE_RX:
        rc = chip->start_rx( 0 ); /* continuous */
        if( rc == 0 )
        {
            hal_led_set( HAL_LED_RX, true );
            printf( "RX mode active at %.3f MHz\n", ( double ) cfg->freq_mhz );
        }
        break;

    default:
        printf( "ERROR: Unknown mode\n" );
        return -1;
    }

    if( rc != 0 )
    {
        printf( "ERROR: Failed to start %s mode\n", cli_state_mode_str( mode ) );
        return -1;
    }

    cfg->active_mode = mode;
    return 0;
}

int test_mode_tick( radio_config_t* cfg, const chip_driver_t* chip )
{
    switch( cfg->active_mode )
    {
    case MODE_MODULATED:
    case MODE_DTS:
        return mod_tx_tick( cfg, chip );

    case MODE_PER_TX:
        return per_tx_tick( cfg, chip );

    case MODE_PER_RX:
        return per_rx_tick( cfg, chip );

    case MODE_FHSS:
    case MODE_HYBRID:
    case MODE_EU_TEST:
    case MODE_JP_TEST:
        return hop_tick( cfg, chip );

    default:
        /* Fire-and-forget modes (CW, RX, DTS) don't tick */
        return 0;
    }
}

int test_mode_stop( radio_config_t* cfg, const chip_driver_t* chip )
{
    if( cfg->active_mode == MODE_IDLE )
    {
        printf( "No active mode.\n" );
        return 0;
    }

    /* For tick-based modes, request stop and drain until idle so the
     * summary prints before we return to the caller. */
    switch( cfg->active_mode )
    {
    case MODE_MODULATED:
    case MODE_DTS:
    case MODE_PER_TX:
    case MODE_PER_RX:
    case MODE_FHSS:
    case MODE_HYBRID:
    case MODE_EU_TEST:
    case MODE_JP_TEST:
        s_stop_requested = 1;
        while( cfg->active_mode != MODE_IDLE )
        {
            test_mode_tick( cfg, chip );
            usleep( 500 );
        }
        return 0;

    default:
        break;
    }

    /* Fire-and-forget modes: stop immediately */
    hal_led_set( HAL_LED_TX, false );
    hal_led_set( HAL_LED_RX, false );
    int rc = chip->stop();
    if( rc != 0 )
    {
        printf( "ERROR: Failed to stop radio\n" );
        return -1;
    }

    printf( "Stopped.\n" );
    cfg->active_mode = MODE_IDLE;
    return 0;
}
