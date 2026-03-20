/*!
 * @file      flrc_burst_rx.h
 *
 * @brief     Simple FLRC burst example with LORA and FLRC Burst modulation
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

#ifndef FLRC_BURST_RX_H
#define FLRC_BURST_RX_H

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include <stdint.h>
#include <stdbool.h>

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC TYPES ------------------------------------------------------------
 */

/**
 * @brief FLRC burst RX configuration parameters (can be overridden via command)
 */
typedef struct flrc_burst_rx_config_s
{
    uint32_t frequency_in_hz;      /*!< RF frequency in Hz */
    uint32_t preamble_len_in_bits; /*!< Preamble length in bits */
    bool     pld_is_fix;           /*!< Fixed (true) or variable (false) payload length */
    uint32_t max_rx_size;          /*!< Maximum RX payload size in bytes */
} flrc_burst_rx_config_t;

/**
 * @brief FLRC burst RX results and statistics
 */
typedef struct flrc_burst_rx_results_s
{
    uint8_t* payload;              /*!< Pointer to received data buffer */
    uint32_t rx_data_size;         /*!< Size of received data in bytes */
    uint16_t nb_packets_received;  /*!< Number of packets received successfully */
    uint16_t nb_packets_crc_error; /*!< Number of packets with CRC errors */
    int32_t  rssi_mean;            /*!< Mean RSSI value in dBm */
} flrc_burst_rx_results_t;

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS PROTOTYPES ---------------------------------------------
 */

/**
 * @brief Initialize FLRC burst RX mode
 *
 * @param [in] payload Pointer to buffer for storing received data
 * @param [in] payload_size Size of the payload buffer
 * @param [in] user_callback Optional callback function called when data is received
 */
void flrc_burst_protocol_rx_init( uint8_t* payload, uint32_t payload_size, void ( *user_callback )( void ) );

/**
 * @brief Set custom configuration parameters for FLRC burst RX
 *
 * @param [in] config Pointer to configuration structure
 */
void flrc_burst_rx_set_config( const flrc_burst_rx_config_t* config );

/**
 * @brief Get current configuration parameters for FLRC burst RX
 *
 * @param [out] config Pointer to configuration structure to fill
 */
void flrc_burst_rx_get_config( flrc_burst_rx_config_t* config );

/**
 * @brief Reset configuration to defaults
 */
void flrc_burst_rx_reset_config( void );

/**
 * @brief Get FLRC burst RX results and statistics
 *
 * @param [out] results Pointer to results structure to fill
 */
void flrc_burst_rx_get_results( flrc_burst_rx_results_t* results );

/**
 * @brief Get the radio access ID used by FLRC burst RX
 *
 * This ID can be used with NHM_CMD_USP_GET_RESULTS to retrieve the full payload
 * via NHM segmented transfer.
 *
 * @return The radio access ID (valid after flrc_burst_protocol_rx_init is called)
 */
uint8_t flrc_burst_rx_get_radio_access_id( void );

#endif  // __FLRC_BURST_RX_H__
