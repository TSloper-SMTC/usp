/**
 * @file      modem_pinout_zephyr.h
 *
 * @brief     Zephyr pin mapping for radio_test_cli
 *
 * On Zephyr, physical pin assignments come from the device tree overlay.
 * This header maps the abstract RADIO_* names used by the CLI code to
 * enum values in smtc_hal_gpio_pin_names.h.  The Zephyr GPIO HAL
 * implementation translates these enum values to gpio_dt_spec lookups.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2025. All rights reserved.
 */
#ifndef __MODEM_PINOUT_ZEPHYR_H__
#define __MODEM_PINOUT_ZEPHYR_H__

/*
 * Pin enum values — defined here directly to avoid include-path conflicts
 * with smtc_hal_linux/smtc_hal_gpio_pin_names.h (which usp_zephyr's subsys
 * adds globally).  These values must match smtc_hal_zephyr/smtc_hal_gpio_pin_names.h.
 */
#ifndef NC
#define NC ( -1 )
#endif

#define RADIO_SPI_ID   1
#define RADIO_SPI_MOSI NC
#define RADIO_SPI_MISO NC
#define RADIO_SPI_SCLK NC

/* Radio control pins — abstract IDs resolved by Zephyr GPIO HAL via DT */
#define RADIO_NSS       0   /* ZEPHYR_PIN_RADIO_NSS */
#define RADIO_NRST      1   /* ZEPHYR_PIN_RADIO_NRST */
#define RADIO_BUSY_PIN  2   /* ZEPHYR_PIN_RADIO_BUSY */
#define RADIO_DIO_MAIN  3   /* ZEPHYR_PIN_RADIO_DIO */

/* LEDs */
#define SMTC_LED_TX     4   /* ZEPHYR_PIN_LED_TX */
#define SMTC_LED_RX     5   /* ZEPHYR_PIN_LED_RX */
#define SMTC_LED_SCAN   NC

/* Not used by radio_test_cli */
#define RADIO_LNA_CTRL              NC
#define EXTI_BUTTON                 NC
#define HW_MODEM_COMMAND_PIN        NC
#define HW_MODEM_EVENT_PIN          NC
#define SX126X_RADIO_RF_SWITCH_CTRL NC
#define RADIO_ANTENNA_SWITCH        NC

/* Debug UART — Zephyr console is configured via device tree */
#define DEBUG_UART_TX NC
#define DEBUG_UART_RX NC

#endif /* __MODEM_PINOUT_ZEPHYR_H__ */
