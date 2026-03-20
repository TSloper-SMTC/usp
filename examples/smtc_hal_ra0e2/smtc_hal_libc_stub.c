/**
 * @file      smtc_hal_libc_stub.c
 *
 * @brief     MCU Hardware Abstraction Layer implementation
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
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

/*
 * -----------------------------------------------------------------------------
 * --- DEPENDENCIES ------------------------------------------------------------
 */

#include <stdint.h>   // C99 types
#include <stdbool.h>  // bool type
#include <errno.h>

#include "smtc_hal_mcu.h"
#include "smtc_hal_uart.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE MACROS-----------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE CONSTANTS -------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE TYPES -----------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DECLARATION -------------------------------------------
 */

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

int __attribute__( ( weak ) ) _close( int fildes )
{
    mcu_panic( );
    return -1;
}
int __attribute__( ( weak ) ) _lseek( int file, int ptr, int dir )
{
    mcu_panic( );
    return -1;
}
int _read( int file, char* ptr, int len )
{
    ( void ) file;
    ( void ) ptr;
    ( void ) len;
    errno = EAGAIN;
    return -1;
}
int _write( int file, char* ptr, int len )
{
    ( void ) file;
    for( int i = 0; i < len; i++ )
    {
        if( ptr[i] == '\n' )
        {
            static const uint8_t cr = '\r';
            trace_uart_tx( ( uint8_t* ) &cr, 1 );
        }
        trace_uart_tx( ( uint8_t* ) &ptr[i], 1 );
    }
    return len;
}
int _isatty( int file )
{
    ( void ) file;
    return 1;
}

#include <sys/stat.h>
int _fstat( int file, struct stat* st )
{
    ( void ) file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _getpid( void )
{
    return 1;
}

int _kill( int pid, int sig )
{
    ( void ) pid;
    ( void ) sig;
    errno = EINVAL;
    return -1;
}

/* --- EOF ------------------------------------------------------------------ */
