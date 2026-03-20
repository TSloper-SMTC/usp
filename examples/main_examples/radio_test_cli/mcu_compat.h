/**
 * @file      mcu_compat.h
 *
 * @brief     POSIX compatibility shim for baremetal MCU targets
 *
 * On Linux: includes standard POSIX headers (unistd, time, signal, fcntl).
 * On MCU: provides inline implementations using the HAL.
 */
#ifndef MCU_COMPAT_H
#define MCU_COMPAT_H

#ifdef __linux__

#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>

#else /* Baremetal MCU */

#include <unistd.h>   /* STDIN_FILENO, STDOUT_FILENO (provided by newlib) */
#include <signal.h>   /* sig_atomic_t (provided by newlib) */
#include <time.h>     /* time_t, struct timespec (provided by newlib) */
#include <errno.h>

#include "smtc_hal_mcu.h"
#include "smtc_hal_rtc.h"

/* --- usleep() via HAL microsecond busy-wait --- */
static inline int usleep( unsigned int us )
{
    hal_mcu_wait_us( ( int32_t ) us );
    return 0;
}

/* --- time() via RTC ms counter (for srand seeding) --- */
static inline time_t mcu_compat_time( time_t* t )
{
    time_t val = ( time_t ) hal_rtc_get_time_ms( );
    if( t != NULL )
    {
        *t = val;
    }
    return val;
}
#define time( x ) mcu_compat_time( x )

/* --- clock_gettime() via RTC ms counter --- */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

static inline int clock_gettime( int clk_id, struct timespec* tp )
{
    ( void ) clk_id;
    uint32_t ms = hal_rtc_get_time_ms( );
    tp->tv_sec  = ( time_t )( ms / 1000 );
    tp->tv_nsec = ( long )( ms % 1000 ) * 1000000L;
    return 0;
}

#endif /* __linux__ */

#endif /* MCU_COMPAT_H */
