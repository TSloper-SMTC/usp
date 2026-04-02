/**
 * @file      main_radio_test.c
 *
 * @brief     Radio Test CLI — interactive REPL for RF and regulatory testing
 *
 * Standalone entry point. Initializes the HAL and chip driver, then runs
 * either:
 *   - Interactive mode: non-blocking linenoise + cooperative tick loop
 *   - Script/pipe mode: blocking sequential command execution (Linux only)
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */

#include "version.h"
#include "cli_state.h"
#include "cli_parser.h"
#include "chip_interface.h"
#include "test_modes.h"
#include "linenoise.h"

#include "smtc_hal_mcu.h"
#include "smtc_hal_gpio.h"
#include "smtc_hal_led.h"
#include "modem_pinout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#ifdef __linux__
#include <fcntl.h>
#endif
#include "platform.h"

#ifndef __linux__
#include "smtc_hal_uart.h"
#include "smtc_hal_watchdog.h"
#endif

/*
 * --- Private state ---
 */

static radio_config_t        g_config;
static const chip_driver_t*  g_chip;

#ifdef __linux__
static volatile sig_atomic_t g_sigint = 0;
#endif

/*
 * --- Radio IRQ notification (set by GPIO thread, consumed by main loop) ---
 */

static volatile sig_atomic_t g_radio_irq_pending = 0;

static void radio_irq_callback( void* context )
{
    ( void ) context;
    g_radio_irq_pending = 1;
}

static hal_gpio_irq_t g_radio_irq = {
    .pin      = RADIO_DIO_MAIN,
    .context  = NULL,
    .callback = radio_irq_callback,
};

bool radio_irq_consume( void )
{
    if( g_radio_irq_pending )
    {
        g_radio_irq_pending = 0;
        return true;
    }
    return false;
}

#ifdef __linux__
static const char* HISTORY_FILE = ".radio_test_history";
#endif

/*
 * --- Signal handling (Linux only — on MCU, Ctrl+C byte 0x03 is handled
 *     natively by linenoise which returns NULL + errno=EAGAIN) ---
 */

#ifdef __linux__
static void sigint_handler( int sig )
{
    ( void ) sig;
    g_sigint = 1;
    test_mode_request_stop();
}
#endif

/*
 * --- Helpers: tick-based mode completion -------------------------------------
 */

/** Returns true if the current mode is tick-based (needs periodic tick calls) */
static bool is_tick_mode( active_mode_t mode )
{
    switch( mode )
    {
    case MODE_MODULATED:
    case MODE_DTS:
    case MODE_PER_TX:
    case MODE_PER_RX:
    case MODE_FHSS:
    case MODE_HYBRID:
    case MODE_EU_TEST:
    case MODE_JP_TEST:
        return true;
    default:
        return false;
    }
}

/**
 * Spin the tick loop until the mode completes or SIGINT.
 * Used in script/pipe mode to block until a tick-based test finishes.
 */
static void drain_tick_mode( void )
{
    while( g_config.active_mode != MODE_IDLE )
    {
        test_mode_tick( &g_config, g_chip );
        platform_sleep_us( 500 );
    }
}

/*
 * --- Script execution (Linux only) ---
 */

#ifdef __linux__
static int run_script( FILE* fp )
{
    char line[512];
    int  lineno = 0;

    while( fgets( line, sizeof( line ), fp ) != NULL )
    {
        lineno++;

        /* Strip trailing newline */
        size_t len = strlen( line );
        while( len > 0 && ( line[len - 1] == '\n' || line[len - 1] == '\r' ) )
        {
            line[--len] = '\0';
        }

        /* Skip blank lines and comments */
        if( len == 0 || line[0] == '#' )
        {
            continue;
        }

        printf( "radio> %s\n", line );

        int rc = cli_execute( line, &g_config, g_chip );
        if( !is_tick_mode( g_config.active_mode ) )
        {
            printf( "\n" );
        }
        if( rc < 0 )
        {
            fprintf( stderr, "Script error at line %d\n", lineno );
            return -1;
        }
        if( rc == 1 )
        {
            /* exit requested */
            return 0;
        }

        /* In script mode, don't block on tick-based modes — let the script
         * use 'delay' and 'stop' to control timing.  The delay command
         * calls test_mode_tick() while waiting so TX/RX continues. */
    }

    /* If a tick mode is still active at end of script, keep ticking
     * until it completes or Ctrl+C stops it. */
    if( is_tick_mode( g_config.active_mode ) )
    {
        printf( "  Ctrl+C to stop\n\n" );
        drain_tick_mode();
    }

    return 0;
}

/*
 * --- Usage ---
 */

static void print_usage( const char* argv0 )
{
    printf( "Usage: %s [OPTIONS]\n", argv0 );
    printf( "\n" );
    printf( "Options:\n" );
    printf( "  --script <file>   Execute commands from file, then exit\n" );
    printf( "  --help            Show this message\n" );
    printf( "\n" );
    printf( "Pipe mode:  echo 'region us' | %s\n", argv0 );
    printf( "Interactive: %s   (starts REPL)\n", argv0 );
}
#endif /* __linux__ */

/*
 * --- MCU linenoise I/O callbacks ---
 */

#ifndef __linux__
static int mcu_ln_read( int fd, void* buf, int n )
{
    ( void ) fd;
    ( void ) n;
    if( !trace_uart_rx_available( ) )
    {
        errno = EAGAIN;
        return -1;
    }
    *( char* ) buf = ( char ) trace_uart_rx_getchar( );
    return 1;
}

static int mcu_ln_write( int fd, const void* buf, int n )
{
    ( void ) fd;
    const uint8_t* p = ( const uint8_t* ) buf;
    int remaining = n;
    while( remaining > 0 )
    {
        uint8_t chunk = ( remaining > 255 ) ? 255 : ( uint8_t ) remaining;
        trace_uart_tx( ( uint8_t* ) p, chunk );
        p         += chunk;
        remaining -= chunk;
    }
    return n;
}
/*
 * --- Plain terminal mode (MCU only) ---
 * Simple blocking line reader: echoes characters, handles backspace,
 * returns on CR. Works in any terminal including VSCode serial monitor.
 */
static char   plain_buf[256];
static uint8_t plain_len = 0;

static void plain_prompt( void )
{
    printf( "radio> " );
    fflush( stdout );
    plain_len = 0;
}

/** Poll for one byte in plain mode.  Returns 1 when a complete line is
 *  ready in plain_buf (null-terminated), 0 if still accumulating. */
static int plain_feed( void )
{
    if( !trace_uart_rx_available() )
        return 0;

    int c = trace_uart_rx_getchar();

    if( c == '\r' || c == '\n' )
    {
        printf( "\n" );
        plain_buf[plain_len] = '\0';
        return 1;
    }
    if( ( c == '\b' || c == 127 ) && plain_len > 0 )
    {
        plain_len--;
        /* Erase character on screen: backspace, space, backspace */
        trace_uart_tx( ( uint8_t* ) "\b \b", 3 );
        return 0;
    }
    if( c == 0x1b ) /* ESC — consume escape sequences (arrow keys etc.) */
    {
        /* Eat up to 2 bytes following ESC (e.g. '[A' for arrow keys) */
        for( int i = 0; i < 2 && trace_uart_rx_available(); i++ )
        {
            trace_uart_rx_getchar();
        }
        return 0;
    }
    if( c == 0x03 ) /* Ctrl+C */
    {
        printf( "\n" );
        plain_buf[0] = '\0';
        plain_len    = 0;
        /* Signal Ctrl+C via errno like linenoise does */
        errno = EAGAIN;
        return -1;
    }
    if( c >= 0x20 && c < 0x7F && plain_len < sizeof( plain_buf ) - 1 )
    {
        plain_buf[plain_len++] = ( char ) c;
        /* Echo the character */
        uint8_t ch = ( uint8_t ) c;
        trace_uart_tx( &ch, 1 );
    }
    return 0;
}
#endif /* !__linux__ */

/*
 * --- Entry point ---
 */

int main( int argc, char* argv[] )
{
#ifdef __linux__
    const char* script_path = NULL;

    /* Parse command-line arguments */
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp( argv[i], "--script" ) == 0 )
        {
            if( i + 1 >= argc )
            {
                fprintf( stderr, "ERROR: --script requires a file path\n" );
                return 1;
            }
            script_path = argv[++i];
        }
        else if( strcmp( argv[i], "--help" ) == 0 || strcmp( argv[i], "-h" ) == 0 )
        {
            print_usage( argv[0] );
            return 0;
        }
        else
        {
            fprintf( stderr, "ERROR: Unknown option '%s'\n", argv[i] );
            print_usage( argv[0] );
            return 1;
        }
    }
#else
    ( void ) argc;
    ( void ) argv;
#endif

    /* Initialize HAL (SPI, GPIO, etc.) */
    hal_mcu_init();
    hal_led_init();

    /* Attach radio IRQ callback — the GPIO thread is already running from
     * hal_mcu_init() but consuming and discarding DIO events.  Now it will
     * set g_radio_irq_pending on each rising edge. */
    hal_gpio_irq_attach( &g_radio_irq );

#ifndef __linux__
    /* MCU: redirect linenoise I/O to UART */
    linenoiseSetIOFns( mcu_ln_read, mcu_ln_write );
    linenoiseSetForceTTY( 1 );
    linenoiseSetColumns( 80 );
#endif

    /* Get chip driver */
    g_chip = chip_get_driver();

    /* Initialize chip */
    if( g_chip->init() != 0 )
    {
        fprintf( stderr, "ERROR: Chip init failed\n" );
        return 1;
    }

    /* Read chip info (displayed after screen clear) */
    char version[64];
    if( g_chip->get_version( version, sizeof( version ) ) != 0 )
    {
        snprintf( version, sizeof( version ), "%s", g_chip->chip_name );
    }

    /* Put chip in standby */
    g_chip->set_standby();

    /* Initialize config state */
    cli_state_init( &g_config );

    /* Seed XOSC defaults from BSP (LR20xx and SX126x; NULL on other chips) */
    if( g_chip->get_xosc_defaults != NULL )
    {
        g_chip->get_xosc_defaults( &g_config.xosc_xta, &g_config.xosc_xtb, &g_config.xosc_wait_us );
    }

#ifdef __linux__
    /* Install signal handler for Ctrl+C */
    struct sigaction sa;
    memset( &sa, 0, sizeof( sa ) );
    sa.sa_handler = sigint_handler;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = 0;
    sigaction( SIGINT, &sa, NULL );

    /* --- Script mode via --script flag --- */
    if( script_path != NULL )
    {
        FILE* fp = fopen( script_path, "r" );
        if( fp == NULL )
        {
            fprintf( stderr, "ERROR: Cannot open script '%s'\n", script_path );
            return 1;
        }
        int rc = run_script( fp );
        fclose( fp );

        /* Stop any active mode before exit */
        if( g_config.active_mode != MODE_IDLE )
        {
            g_chip->stop();
        }
        return ( rc == 0 ) ? 0 : 1;
    }

    /* --- Pipe/stdin mode (non-interactive) --- */
    if( !isatty( STDIN_FILENO ) )
    {
        int rc = run_script( stdin );

        /* Stop any active mode before exit */
        if( g_config.active_mode != MODE_IDLE )
        {
            g_chip->stop();
        }
        return ( rc == 0 ) ? 0 : 1;
    }
#endif /* __linux__ */

    /* --- Interactive REPL with non-blocking input --- */
    printf( "\033[2J\033[H" ); /* clear screen, cursor home */
#ifdef __linux__
    printf( "Radio Test CLI " RADIO_TEST_CLI_VERSION " - type 'help' for commands, 'exit' to quit\n" );
#else
    printf( "Radio Test CLI " RADIO_TEST_CLI_VERSION " - type 'help' for commands\n" );
#endif
#ifndef __linux__
    printf( "Terminal: plain (type 'term ansi' for full editing)\n" );
#endif
    printf( "\nRadio: %s\n\n", version );

    /* Set up linenoise (used on Linux always, on MCU only in ansi mode) */
    cli_set_completion_context( &g_config, g_chip );
    linenoiseSetCompletionCallback( cli_completion );
    linenoiseSetHintsCallback( cli_hints );
    linenoiseHistorySetMaxLen( 100 );
#ifdef __linux__
    linenoiseHistoryLoad( HISTORY_FILE );
#endif

    /* Non-blocking input setup */
    struct linenoiseState ls;
    static char lnbuf[1024];
    bool   editing = false;

#ifdef __linux__
    /* Set stdin to non-blocking */
    int stdin_flags = fcntl( STDIN_FILENO, F_GETFL );
    fcntl( STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK );

    linenoiseEditStart( &ls, STDIN_FILENO, STDOUT_FILENO, lnbuf, sizeof( lnbuf ), "radio> " );
    editing = true;
    test_mode_set_line_state( &ls );
#else
    /* MCU: start in plain mode */
    bool plain_active = true;
    plain_prompt();
#endif

    bool running = true;
    while( running )
    {
#ifdef __linux__
        /* --- Handle Ctrl+C (Linux: via SIGINT handler) --- */
        if( g_sigint )
        {
            g_sigint = 0;

            if( is_tick_mode( g_config.active_mode ) )
            {
                /* test_mode_request_stop() was already called by the handler.
                 * The next tick will transition to DONE. */
            }
            else if( g_config.active_mode != MODE_IDLE )
            {
                /* Fire-and-forget modes: stop immediately */
                if( editing )
                {
                    linenoiseHide( &ls );
                }
                printf( "\nInterrupted - stopping...\n" );
                g_chip->stop();
                g_config.active_mode = MODE_IDLE;
                if( editing )
                {
                    linenoiseShow( &ls );
                }
            }
            else
            {
                /* No active mode — print a new prompt */
                if( editing )
                {
                    linenoiseEditStop( &ls );
                }
                printf( "\n" );
                linenoiseEditStart( &ls, STDIN_FILENO, STDOUT_FILENO,
                                    lnbuf, sizeof( lnbuf ), "radio> " );
                editing = true;
            }
        }
#endif /* __linux__ */

        /* --- Poll for input --- */
#ifndef __linux__
        if( plain_active && is_tick_mode( g_config.active_mode ) )
        {
            /* Tick mode active: only watch for Ctrl+C, discard all other input */
            if( trace_uart_rx_available() )
            {
                int c = trace_uart_rx_getchar();
                if( c == 0x03 ) /* Ctrl+C */
                {
                    printf( "\n" );
                    test_mode_request_stop();
                    drain_tick_mode();
                    plain_prompt();
                }
            }
        }
        else if( plain_active )
        {
            /* --- Plain terminal mode (MCU) --- */
            int prc = plain_feed();
            if( prc != 0 )
            {
                char* line = ( prc == 1 ) ? plain_buf : NULL;

                if( line == NULL )
                {
                    /* Ctrl+C */
                    if( g_config.active_mode != MODE_IDLE )
                    {
                        printf( "Interrupted - stopping...\n" );
                        g_chip->stop();
                        g_config.active_mode = MODE_IDLE;
                    }
                }
                else if( line[0] != '\0' )
                {
                    int rc = cli_execute( line, &g_config, g_chip );
                    if( rc == 1 )
                    {
                        running = false;
                    }
                    else if( is_tick_mode( g_config.active_mode ) )
                    {
                        printf( "  Ctrl+C to stop\n\n" );
                    }
                    else
                    {
                        printf( "\n" );
                    }
                }

                /* Check if user switched to ansi mode */
                if( running && g_config.term_ansi )
                {
                    plain_active = false;
                    linenoiseEditStart( &ls, STDIN_FILENO, STDOUT_FILENO,
                                        lnbuf, sizeof( lnbuf ), "radio> " );
                    editing = true;
                    test_mode_set_line_state( &ls );
                }
                else if( running && !is_tick_mode( g_config.active_mode ) )
                {
                    plain_prompt();
                }
            }
        }
        else
#endif /* !__linux__ */
        if( editing )
        {
            char* line = linenoiseEditFeed( &ls );
            if( line != linenoiseEditMore )
            {
                /* Complete line or EOF */
                linenoiseEditStop( &ls );
                editing = false;
                test_mode_set_line_state( NULL );

                if( line == NULL && errno == EAGAIN )
                {
                    /* Ctrl+C - stop active mode or just refresh prompt */
                    printf( "\n" );
                    if( is_tick_mode( g_config.active_mode ) )
                    {
                        test_mode_request_stop();
                        drain_tick_mode();
                    }
                    else if( g_config.active_mode != MODE_IDLE )
                    {
                        printf( "Interrupted - stopping...\n" );
                        g_chip->stop();
                        g_config.active_mode = MODE_IDLE;
                    }
                }
                else if( line == NULL )
                {
                    /* EOF (Ctrl+D) */
                    running = false;
                }
                else if( line[0] != '\0' )
                {
                    linenoiseHistoryAdd( line );
#ifdef __linux__
                    linenoiseHistorySave( HISTORY_FILE );
#endif

                    int rc = cli_execute( line, &g_config, g_chip );
                    if( rc == 1 )
                    {
                        running = false;
                    }
                    else if( is_tick_mode( g_config.active_mode ) )
                    {
                        printf( "  'stop' or Ctrl+C to stop\n\n" );
                    }
                    else
                    {
                        printf( "\n" );
                    }
                }

#ifndef __linux__
                /* Check if user switched back to plain mode */
                if( running && !g_config.term_ansi )
                {
                    plain_active = true;
                    plain_prompt();
                }
                else
#endif
                if( running )
                {
                    linenoiseEditStart( &ls, STDIN_FILENO, STDOUT_FILENO,
                                        lnbuf, sizeof( lnbuf ), "radio> " );
                    editing = true;
                    test_mode_set_line_state( &ls );
                }
            }
        }

        /* --- Tick active mode --- */
#ifndef __linux__
        bool was_ticking = is_tick_mode( g_config.active_mode );
#endif
        if( is_tick_mode( g_config.active_mode ) )
        {
            test_mode_tick( &g_config, g_chip );
        }
#ifndef __linux__
        /* Re-display prompt when a tick mode completes */
        if( was_ticking && g_config.active_mode == MODE_IDLE )
        {
            if( plain_active )
            {
                plain_prompt();
            }
            else if( !editing )
            {
                linenoiseEditStart( &ls, STDIN_FILENO, STDOUT_FILENO,
                                    lnbuf, sizeof( lnbuf ), "radio> " );
                editing = true;
                test_mode_set_line_state( &ls );
            }
        }
#endif

        /* Yield CPU.  In tick modes we poll at ~500µs for timer-based state
         * transitions.  When idle or fire-and-forget, sleep longer to save CPU. */
        if( is_tick_mode( g_config.active_mode ) )
        {
            platform_sleep_us( 500 );
        }
        else
        {
            platform_sleep_us( 50000 );
        }

#ifndef __linux__
        hal_watchdog_reload();
#endif
    }

    /* Cleanup */
    if( editing )
    {
        linenoiseEditStop( &ls );
    }

#ifdef __linux__
    /* Restore blocking stdin */
    fcntl( STDIN_FILENO, F_SETFL, stdin_flags );
#endif

    if( g_config.active_mode != MODE_IDLE )
    {
        g_chip->stop();
        printf( "Stopped.\n" );
    }

    g_chip->set_standby();
    printf( "Bye.\n" );

    return 0;
}
