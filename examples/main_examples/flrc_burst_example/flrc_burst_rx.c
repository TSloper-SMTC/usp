/*!
 * @file      flrc_burst_rx.c
 *
 * @brief     Simple FLRC burst example
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2026. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "smtc_rac.h"
#include "smtc_rac_api.h"
#include "smtc_sw_platform_helper.h"
#include "smtc_modem_hal.h"

#include "flrc_burst_rx.h"
#include "flrc_burst_utils.h"

#include "apps_configuration.h"

#define RAC_LOG_APP_PREFIX "FLRC-BURST"
#include "smtc_rac_log.h"
#include "smtc_hal_dbg_trace.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE MACROS-----------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE CONSTANTS -------------------------------------------------------
 */

#define FLRC_RADIO_PAYLOAD_MAX_LENGTH ( RAC_FLRC_BURST_RADIO_PAYLOAD_MAX_LENGTH )

#define RX_BUFFER_MAX_SIZE ( 255 )

#define DELAY_BEFORE_RX_DATA_MS 15
/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE TYPES -----------------------------------------------------------
 */

typedef enum flrc_burst_rx_state_e
{
    FLRC_BURST_RX_STATE_IDLE,
    FLRC_BURST_RX_STATE_CAD,
    FLRC_BURST_RX_STATE_WOR_ACK_TX,
    FLRC_BURST_RX_STATE_RX_DATA,
} flrc_burst_rx_state_t;

typedef struct flrc_burst_rx_protocol_rx_s
{
    flrc_burst_rx_state_t state;
    uint8_t               radio_access_id;
    smtc_rac_context_t*   transaction;
    uint8_t*              rx_data;       // Pointer to the buffer of the total payload received
    uint32_t              rx_data_size;  // Size of the buffer pointed by rx_data
    uint8_t               rx_payload_buffer[FLRC_RADIO_PAYLOAD_MAX_LENGTH];
    uint16_t              rx_bytes_received;
    uint32_t              rx_data_size_from_wor;   // Size of the total payload to receive indicated in the WOR received
    ral_flrc_raw_bit_rate_t br_bw_from_wor;        // Received in the WOR
    ral_flrc_cr_t           coding_rate_from_wor;  // Coding rate received in the WOR
    uint32_t                crc_data_from_wor;     // CRC of the total payload received in the WOR
    void ( *user_callback )( void );
    int32_t lora_freq_offset_hz;

    uint16_t nb_packets_received;  /*!< Number of packets of the burst received successfully. */
    uint16_t nb_packets_crc_error; /*!< Number of packets of the burst received with a CRC error. */
    int32_t  rssi_mean;
} flrc_burst_rx_protocol_rx_t;

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */
static flrc_burst_rx_protocol_rx_t flrc_burst_rx        = { 0 };
static flrc_burst_rx_config_t      flrc_burst_rx_config = { 0 };

static uint8_t wor_ack_payload[20];
static uint8_t wor_payload_rx[RX_BUFFER_MAX_SIZE];

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DECLARATION -------------------------------------------
 */

static void post_transaction_callback( rp_status_t status );
static void pre_transaction_callback( void );
static void start_new_rx_transaction( void );
static void prepare_rx_data_packet( void );
static void listen_rx_data_packet( uint32_t timestamp );

// WOR
static void prepare_rx_wor_packet( void );
static void listen_wor_packet( uint32_t timestamp_ms );
static void prepare_wor_ack_tx_packet( void );
static void send_wor_ack_tx_packet( uint32_t timestamp_ms );

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

void flrc_burst_protocol_rx_init( uint8_t* payload, uint32_t payload_size, void ( *user_callback )( void ) )
{
    SMTC_HAL_TRACE_INFO( "FLRC burst RX init\n" );

    memset( &flrc_burst_rx, 0, sizeof( flrc_burst_rx ) );
    memset( &flrc_burst_rx_config, 0, sizeof( flrc_burst_rx_config ) );
    flrc_burst_rx_reset_config( );

    // initialize radio access
    flrc_burst_rx.radio_access_id = SMTC_SW_PLATFORM( smtc_rac_open_radio( RAC_HIGH_PRIORITY ) );
    SMTC_HAL_TRACE_INFO( "Radio access ID: %u\n", flrc_burst_rx.radio_access_id );

    // get transaction context pointer
    flrc_burst_rx.transaction = smtc_rac_get_context( flrc_burst_rx.radio_access_id );
    flrc_burst_rx.transaction->scheduler_config.callback_post_radio_transaction = post_transaction_callback;
    flrc_burst_rx.transaction->scheduler_config.callback_pre_radio_transaction  = pre_transaction_callback;

    flrc_burst_rx.rx_data      = payload;
    flrc_burst_rx.rx_data_size = payload_size;

    flrc_burst_rx.user_callback = user_callback;
    start_new_rx_transaction( );
}

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DEFINITION --------------------------------------------
 */

static void post_transaction_callback( rp_status_t status )
{
    switch( status )
    {
    case RP_STATUS_TX_DONE:
        if( flrc_burst_rx.state == FLRC_BURST_RX_STATE_WOR_ACK_TX )
        {
            flrc_burst_rx.state = FLRC_BURST_RX_STATE_RX_DATA;
            prepare_rx_data_packet( );
            listen_rx_data_packet( flrc_burst_rx.transaction->smtc_rac_data_result.radio_end_timestamp_ms +
                                   DELAY_BEFORE_RX_DATA_MS );
        }

        break;
    case RP_STATUS_RX_PACKET:
        if( flrc_burst_rx.state == FLRC_BURST_RX_STATE_CAD )
        {
            if( ( wor_payload_rx[0] == 'W' ) && ( wor_payload_rx[1] == 'O' ) && ( wor_payload_rx[2] == 'R' ) )
            {
                const uint32_t rx_data_size_from_wor_tmp = wor_payload_rx[3] + ( wor_payload_rx[4] << 8 ) +
                                                           ( wor_payload_rx[5] << 16 ) + ( wor_payload_rx[6] << 24 );
                if( rx_data_size_from_wor_tmp <= flrc_burst_rx.rx_data_size )
                {
                    flrc_burst_rx.rx_data_size_from_wor = rx_data_size_from_wor_tmp;

                    flrc_burst_rx.br_bw_from_wor       = wor_payload_rx[7];
                    flrc_burst_rx.coding_rate_from_wor = wor_payload_rx[8];
                    flrc_burst_rx.crc_data_from_wor    = ( ( uint32_t ) wor_payload_rx[9] & 0xFF ) |
                                                      ( ( ( uint32_t ) wor_payload_rx[10] << 8 ) & 0xFF00 ) |
                                                      ( ( ( uint32_t ) wor_payload_rx[11] << 16 ) & 0xFF0000 ) |
                                                      ( ( ( uint32_t ) wor_payload_rx[12] << 24 ) & 0xFF000000 );

                    flrc_burst_rx.lora_freq_offset_hz =
                        flrc_burst_rx.transaction->smtc_rac_data_result.lora_freq_offset_hz;
                    flrc_burst_rx.state = FLRC_BURST_RX_STATE_WOR_ACK_TX;
                    prepare_wor_ack_tx_packet( );
                    send_wor_ack_tx_packet( flrc_burst_rx.transaction->smtc_rac_data_result.radio_end_timestamp_ms +
                                            WOR_ACK_DELAY_MS );
                }
                else
                {
                    SMTC_HAL_TRACE_WARNING( "WOR packet received with data size greater than expected\n" );
                    flrc_burst_rx.state = FLRC_BURST_RX_STATE_CAD;
                }
            }
        }
        else if( flrc_burst_rx.state == FLRC_BURST_RX_STATE_RX_DATA )
        {
            if( ( flrc_burst_rx.rx_bytes_received + flrc_burst_rx.transaction->smtc_rac_data_result.rx_size ) <=
                flrc_burst_rx.transaction->radio_params.flrc_burst.burst_rx_size )
            {
                // The packet chunk is of the appropriate size
                memcpy( &flrc_burst_rx.rx_data[flrc_burst_rx.rx_bytes_received], flrc_burst_rx.rx_payload_buffer,
                        flrc_burst_rx.transaction->smtc_rac_data_result.rx_size );
                flrc_burst_rx.rx_bytes_received += flrc_burst_rx.transaction->smtc_rac_data_result.rx_size;

                flrc_burst_rx.rssi_mean = flrc_burst_rx.rssi_mean * flrc_burst_rx.nb_packets_received;
                flrc_burst_rx.rssi_mean += flrc_burst_rx.transaction->smtc_rac_data_result.rssi_result;
                flrc_burst_rx.nb_packets_received++;
                flrc_burst_rx.rssi_mean /= flrc_burst_rx.nb_packets_received;
            }

            // Check that the last chunk of the burst is received
            if( flrc_burst_rx.rx_bytes_received == flrc_burst_rx.transaction->radio_params.flrc_burst.burst_rx_size )
            {
                smtc_rac_flrc_burst_rx_done( flrc_burst_rx.radio_access_id );
            }
        }
        break;

    case RP_STATUS_RADIO_UNLOCKED:
    {
        SMTC_HAL_TRACE_INFO( "Radio unlocked\n" );

        if( flrc_burst_rx.state == FLRC_BURST_RX_STATE_RX_DATA )
        {
            SMTC_HAL_TRACE_INFO( "Data received (%u bytes)\n", flrc_burst_rx.rx_bytes_received );
            SMTC_HAL_TRACE_INFO(
                "FLRC BURST freq_hz: %" PRIu32 ", freq_offset_hz: %" PRId32 ", raw bit rate: %s\n",
                flrc_burst_rx.transaction->radio_params.flrc_burst.frequency_in_hz,
                flrc_burst_rx.transaction->radio_params.flrc_burst.rx_frequency_offset_in_hz,
                ral_flrc_raw_bit_rate_to_str( flrc_burst_rx.transaction->radio_params.flrc_burst.raw_bit_rate ) );

            SMTC_HAL_TRACE_INFO( "FLRC Rx BURST: Radio ( burst stats : packet rx %u, crc error %u, rssi mean %d)\n",
                                 flrc_burst_rx.nb_packets_received, flrc_burst_rx.nb_packets_crc_error,
                                 flrc_burst_rx.rssi_mean );

            if( flrc_burst_rx.rx_bytes_received == flrc_burst_rx.transaction->radio_params.flrc_burst.burst_rx_size )
            {
                uint32_t crc_computed = generate_crc( flrc_burst_rx.rx_data, flrc_burst_rx.rx_bytes_received );
                if( crc_computed != flrc_burst_rx.crc_data_from_wor )
                {
                    SMTC_HAL_TRACE_WARNING( "Data is invalid\n" );
                }
                else
                {
                    if( flrc_burst_rx.user_callback != NULL )
                    {
                        flrc_burst_rx.user_callback( );
                    }
                }
            }
        }

        flrc_burst_rx.state = FLRC_BURST_RX_STATE_CAD;
        break;
    }
    case RP_STATUS_RX_TIMEOUT:
        if( flrc_burst_rx.state == FLRC_BURST_RX_STATE_RX_DATA )
        {
            SMTC_HAL_TRACE_WARNING( "Rx timeout\n" );
        }
        flrc_burst_rx.state = FLRC_BURST_RX_STATE_CAD;
        break;
    case RP_STATUS_RX_CRC_ERROR:
        flrc_burst_rx.nb_packets_crc_error++;
        break;
    case RP_STATUS_TASK_ABORTED:
    {
        SMTC_HAL_TRACE_WARNING( "FLRC BURST PROTOCOL task aborted\n" );
        if( flrc_burst_rx.state == FLRC_BURST_RX_STATE_RX_DATA )
        {
            SMTC_HAL_TRACE_INFO(
                "FLRC Rx BURST : Task aborted ( burst stats : packet rx %u, crc error %u, rssi mean %d)\n",
                flrc_burst_rx.nb_packets_received, flrc_burst_rx.nb_packets_crc_error, flrc_burst_rx.rssi_mean );
        }
        flrc_burst_rx.state = FLRC_BURST_RX_STATE_CAD;
        break;
    }
    default:
    {
        SMTC_HAL_TRACE_ERROR( "Unknown transaction status: %d\n", status );
    }
    }

    // Restart RX
    if( flrc_burst_rx.state == FLRC_BURST_RX_STATE_CAD )
    {
        prepare_rx_wor_packet( );
        if( ( int32_t ) flrc_burst_rx.transaction->smtc_rac_data_result.radio_start_timestamp_ms + 1000 -
                smtc_modem_hal_get_time_in_ms( ) <=
            0 )
        {
            listen_wor_packet( smtc_modem_hal_get_time_in_ms( ) + 10 );
        }
        else
        {
            listen_wor_packet( flrc_burst_rx.transaction->smtc_rac_data_result.radio_start_timestamp_ms + 1000 );
        }
    }
}

static void pre_transaction_callback( void )
{
}

static void start_new_rx_transaction( void )
{
    if( flrc_burst_rx.state != FLRC_BURST_RX_STATE_IDLE )
    {
        return;
    }
    flrc_burst_rx.state = FLRC_BURST_RX_STATE_CAD;
    prepare_rx_wor_packet( );
    listen_wor_packet( smtc_modem_hal_get_time_in_ms( ) + 10 );
}

static void prepare_rx_data_packet( void )
{
    flrc_burst_rx.nb_packets_crc_error = 0;
    flrc_burst_rx.nb_packets_received  = 0;
    flrc_burst_rx.rssi_mean            = 0;

    flrc_burst_rx.transaction->modulation_type                                   = SMTC_RAC_MODULATION_FLRC_BURST;
    flrc_burst_rx.transaction->radio_params.flrc_burst.is_tx                     = false;
    flrc_burst_rx.transaction->radio_params.flrc_burst.rx_frequency_offset_in_hz = flrc_burst_rx.lora_freq_offset_hz;
    flrc_burst_rx.transaction->radio_params.flrc_burst.raw_bit_rate              = flrc_burst_rx.br_bw_from_wor;
    flrc_burst_rx.transaction->radio_params.flrc_burst.cr                        = flrc_burst_rx.coding_rate_from_wor;
    flrc_burst_rx.transaction->radio_params.flrc_burst.pulse_shape               = DATA_FLRC_PULSE_SHAPE;
    flrc_burst_rx.transaction->radio_params.flrc_burst.sync_word_len             = DATA_FLRC_SYNCWORD_LEN;
    flrc_burst_rx.transaction->radio_params.flrc_burst.tx_syncword_index         = DATA_FLRC_SYNCWORD;
    flrc_burst_rx.transaction->radio_params.flrc_burst.match_sync_word           = DATA_FLRC_MATCH_SYNCWORD;
    flrc_burst_rx.transaction->radio_params.flrc_burst.crc_type                  = DATA_FLRC_CRC;
    flrc_burst_rx.transaction->radio_params.flrc_burst.sync_word[0]              = &default_syncword_1[0];
    flrc_burst_rx.transaction->radio_params.flrc_burst.sync_word[1]              = &default_syncword_2[0];
    flrc_burst_rx.transaction->radio_params.flrc_burst.sync_word[2]              = &default_syncword_3[0];
    flrc_burst_rx.transaction->radio_params.flrc_burst.crc_seed                  = 0xffffffff;
    flrc_burst_rx.transaction->radio_params.flrc_burst.crc_polynomial            = 0x755B;

    // Apply configuration from flrc_burst_rx_config
    flrc_burst_rx.transaction->radio_params.flrc_burst.frequency_in_hz = flrc_burst_rx_config.frequency_in_hz;
    flrc_burst_rx.transaction->radio_params.flrc_burst.preamble_len    = flrc_burst_rx_config.preamble_len_in_bits;
    flrc_burst_rx.transaction->radio_params.flrc_burst.pld_is_fix      = flrc_burst_rx_config.pld_is_fix;
    flrc_burst_rx.transaction->radio_params.flrc_burst.max_rx_size     = flrc_burst_rx_config.max_rx_size;

    // Indicate the buffer to receive packets
    flrc_burst_rx.transaction->smtc_rac_data_buffer_setup.rx_payload_buffer = flrc_burst_rx.rx_payload_buffer;
    flrc_burst_rx.transaction->smtc_rac_data_buffer_setup.size_of_rx_payload_buffer =
        sizeof( flrc_burst_rx.rx_payload_buffer );

    flrc_burst_rx.transaction->scheduler_config.scheduling = SMTC_RAC_ASAP_TRANSACTION;

    // Indicate the total size of the data to receive
    flrc_burst_rx.transaction->radio_params.flrc_burst.burst_rx_size = flrc_burst_rx.rx_data_size_from_wor;
}

static void listen_rx_data_packet( uint32_t timestamp )
{
    flrc_burst_rx.rx_bytes_received                           = 0;
    flrc_burst_rx.transaction->scheduler_config.start_time_ms = timestamp;

    SMTC_MODEM_HAL_PANIC_ON_FAILURE( smtc_rac_submit_radio_transaction( flrc_burst_rx.radio_access_id ) ==
                                     SMTC_RAC_SUCCESS );
}

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE WOR FUNCTIONS DEFINITION --------------------------------------------
 */

static void prepare_rx_wor_packet( void )
{
    // configure transaction context
    flrc_burst_rx.transaction->modulation_type = SMTC_RAC_MODULATION_LORA;

    flrc_burst_rx.transaction->radio_params.lora.is_tx                = false;
    flrc_burst_rx.transaction->radio_params.lora.is_ranging_exchange  = false;
    flrc_burst_rx.transaction->radio_params.lora.frequency_in_hz      = WOR_RF_FREQ_IN_HZ;
    flrc_burst_rx.transaction->radio_params.lora.sf                   = WOR_LORA_SPREADING_FACTOR;
    flrc_burst_rx.transaction->radio_params.lora.bw                   = WOR_LORA_BANDWIDTH;
    flrc_burst_rx.transaction->radio_params.lora.cr                   = WOR_LORA_CODING_RATE;
    flrc_burst_rx.transaction->radio_params.lora.preamble_len_in_symb = WOR_LORA_LONG_PREAMBLE_LENGTH;
    flrc_burst_rx.transaction->radio_params.lora.header_type          = LORA_PKT_LEN_MODE;
    flrc_burst_rx.transaction->radio_params.lora.invert_iq_is_on      = WOR_LORA_IQ;
    flrc_burst_rx.transaction->radio_params.lora.crc_is_on            = WOR_LORA_CRC;
    flrc_burst_rx.transaction->radio_params.lora.sync_word            = LORA_SYNCWORD;
    flrc_burst_rx.transaction->radio_params.lora.rx_timeout_ms        = WOR_RX_TIMEOUT;
    flrc_burst_rx.transaction->radio_params.lora.symb_nb_timeout      = WOR_SYM_NB_TIMEOUT;
    flrc_burst_rx.transaction->radio_params.lora.max_rx_size          = RX_BUFFER_MAX_SIZE;

    flrc_burst_rx.transaction->smtc_rac_data_buffer_setup.rx_payload_buffer         = wor_payload_rx;
    flrc_burst_rx.transaction->smtc_rac_data_buffer_setup.size_of_rx_payload_buffer = RX_BUFFER_MAX_SIZE;

    flrc_burst_rx.transaction->scheduler_config.start_time_ms = 0;  // set at each transaction
    flrc_burst_rx.transaction->scheduler_config.scheduling    = SMTC_RAC_ASAP_TRANSACTION;
}

static void listen_wor_packet( uint32_t timestamp_ms )
{
    flrc_burst_rx.transaction->scheduler_config.callback_post_radio_transaction = post_transaction_callback;
    flrc_burst_rx.transaction->scheduler_config.callback_pre_radio_transaction  = pre_transaction_callback;
    flrc_burst_rx.transaction->scheduler_config.start_time_ms                   = timestamp_ms;
    SMTC_MODEM_HAL_PANIC_ON_FAILURE( smtc_rac_submit_radio_transaction( flrc_burst_rx.radio_access_id ) ==
                                     SMTC_RAC_SUCCESS );
}

static void prepare_wor_ack_tx_packet( void )
{
    flrc_burst_rx.transaction->modulation_type = SMTC_RAC_MODULATION_LORA;

    flrc_burst_rx.transaction->radio_params.lora.is_tx                = true;
    flrc_burst_rx.transaction->radio_params.lora.is_ranging_exchange  = false;
    flrc_burst_rx.transaction->radio_params.lora.frequency_in_hz      = WOR_RF_FREQ_IN_HZ;
    flrc_burst_rx.transaction->radio_params.lora.tx_power_in_dbm      = TX_OUTPUT_POWER_DBM;
    flrc_burst_rx.transaction->radio_params.lora.sf                   = WOR_LORA_SPREADING_FACTOR;
    flrc_burst_rx.transaction->radio_params.lora.bw                   = WOR_LORA_BANDWIDTH;
    flrc_burst_rx.transaction->radio_params.lora.cr                   = WOR_LORA_CODING_RATE;
    flrc_burst_rx.transaction->radio_params.lora.preamble_len_in_symb = WOR_ACK_LORA_PREAMBLE_LENGTH;
    flrc_burst_rx.transaction->radio_params.lora.header_type          = LORA_PKT_LEN_MODE;
    flrc_burst_rx.transaction->radio_params.lora.invert_iq_is_on      = WOR_ACK_LORA_IQ;
    flrc_burst_rx.transaction->radio_params.lora.crc_is_on            = WOR_LORA_CRC;
    flrc_burst_rx.transaction->radio_params.lora.sync_word            = LORA_SYNCWORD;
    flrc_burst_rx.transaction->radio_params.lora.tx_size              = sizeof( wor_ack_payload );

    flrc_burst_rx.transaction->lbt_context.lbt_enabled = false;

    wor_ack_payload[0] = 'W';
    wor_ack_payload[1] = 'O';
    wor_ack_payload[2] = 'R';
    wor_ack_payload[3] = ' ';
    wor_ack_payload[4] = 'O';
    wor_ack_payload[5] = 'K';

    flrc_burst_rx.transaction->smtc_rac_data_buffer_setup.tx_payload_buffer         = wor_ack_payload;
    flrc_burst_rx.transaction->smtc_rac_data_buffer_setup.size_of_tx_payload_buffer = sizeof( wor_ack_payload );

    flrc_burst_rx.transaction->scheduler_config.scheduling = SMTC_RAC_SCHEDULED_TRANSACTION;
}

static void send_wor_ack_tx_packet( uint32_t timestamp_ms )
{
    flrc_burst_rx.transaction->scheduler_config.start_time_ms = timestamp_ms;
    SMTC_MODEM_HAL_PANIC_ON_FAILURE( smtc_rac_submit_radio_transaction( flrc_burst_rx.radio_access_id ) ==
                                     SMTC_RAC_SUCCESS );
}

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC CONFIGURATION AND RESULTS FUNCTIONS ------------------------------
 */

void flrc_burst_rx_set_config( const flrc_burst_rx_config_t* config )
{
    if( config != NULL )
    {
        flrc_burst_rx_config.frequency_in_hz      = config->frequency_in_hz;
        flrc_burst_rx_config.preamble_len_in_bits = config->preamble_len_in_bits;
        flrc_burst_rx_config.pld_is_fix           = config->pld_is_fix;
        flrc_burst_rx_config.max_rx_size          = config->max_rx_size;

        SMTC_HAL_TRACE_INFO( "FLRC burst RX config set: freq=%" PRIu32 " Hz, preamble=%" PRIu32
                             " bits, pld_is_fix=%s, max_rx_size=%" PRIu32 "\n",
                             config->frequency_in_hz, config->preamble_len_in_bits,
                             config->pld_is_fix ? "true" : "false", config->max_rx_size );
    }
}

void flrc_burst_rx_get_config( flrc_burst_rx_config_t* config )
{
    if( config != NULL )
    {
        config->frequency_in_hz      = flrc_burst_rx_config.frequency_in_hz;
        config->preamble_len_in_bits = flrc_burst_rx_config.preamble_len_in_bits;
        config->pld_is_fix           = flrc_burst_rx_config.pld_is_fix;
        config->max_rx_size          = flrc_burst_rx_config.max_rx_size;
    }
}

void flrc_burst_rx_reset_config( void )
{
    flrc_burst_rx_config.frequency_in_hz      = DATA_RF_FREQ_IN_HZ;
    flrc_burst_rx_config.preamble_len_in_bits = DATA_FLRC_PREAMBLE_BITS;
    flrc_burst_rx_config.pld_is_fix           = DATA_FLRC_PLD_IS_FIX;
    flrc_burst_rx_config.max_rx_size          = FLRC_RADIO_PAYLOAD_MAX_LENGTH;

    SMTC_HAL_TRACE_INFO( "FLRC burst RX config reset to defaults\n" );
}

void flrc_burst_rx_get_results( flrc_burst_rx_results_t* results )
{
    if( results != NULL )
    {
        results->payload              = flrc_burst_rx.rx_data;
        results->rx_data_size         = flrc_burst_rx.rx_bytes_received;
        results->nb_packets_received  = flrc_burst_rx.nb_packets_received;
        results->nb_packets_crc_error = flrc_burst_rx.nb_packets_crc_error;
        results->rssi_mean            = flrc_burst_rx.rssi_mean;
    }
}

uint8_t flrc_burst_rx_get_radio_access_id( void )
{
    return flrc_burst_rx.radio_access_id;
}
