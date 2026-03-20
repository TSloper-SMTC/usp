# Known Limitations

This document presents the current known limitations of USP and their workarounds, when available.

### ⚠️ This release is a **FEATURE RELEASE - UNSTABLE**

This preview release is not intended for production use. A stable release will be available soon

### The FLRP features & examples are experimental

The FLRP Protocol implemented in flrc_burst example is Work In Progress and available for demonstration. A more Stable version will be available soon.

### rf_certification example & FCC duty-cyle limit

FCC test application currently cannot reach the required 98% channel duty-cycle limit. A fix is in progress.

### porting_tests application limitation

On STM32L476RG, 2 tests are not passing :
- With LR2021 : `porting_test_get_time/Get time in millisecond` : `NOK: Time is not coherent with radio irq : expected 1966ms / get 1968ms (margin +/-1ms)`
  - This issue is under investigation, but currently, It did not prevent to pass OK through the Semtech full Validation Process
- `porting_test_stop_timer` : the `hal_lp_timer_stop()` function is not functional, this is under fix for next release.

### Support of NUCLEO-L073RZ & Renesas FPB-RA0E2 is experimental

Not all samples compile with those platforms.
Only periodical_uplink sample was tested with limited validation.

### hw_modem integration (#131)

hw_modem is an application embedding most of the USP platform on the tested MCU. This MCU can then be controlled by UART to test USP & LoRa Basics Modem API.
However, `modem-bridge`, a bridge application between the hw_modem MCU and the controlling computer, is not provided.
hw_modem documentation will be completed in future releases.

### hw_modem: STORE & FORWARD integration (#119)

Store & Forward service is not functional in hw_modem. The defines used in cmd_parser.c are not activated.
Store & Forward service is functional in geolocation example.

### Some programmed packets could be dropped (seen in Relay RX) (#130)

During validation, it was discovered that some packets could be dropped under certain circumstances. When this occurs, the following message may appear:
> task schedule aborted because in the past -1

This issue was observed during validation of Relay RX with STM32L476RG & Wio-LR2021, but may also occur occasionally with other features and radios.
If this issue occurs, try extending the `RP_MARGIN_DELAY` value from `8` to `12` in the following file: `smtc_rac_lib/radio_planner/src/radio_planner_types.h`.

### Geolocation Tools are missing (#129)

The geolocation application from Legacy LoRa Basics Modem 3_geolocation_on_lora_edge Application suite was ported to USP.
Nevertheless, the following tools are not yet available for USP:
- lr11xx_flasher

If required, It can be retrieved from [LR11xx Updater tool](https://github.com/Lora-net/SWTL001).

### USP API: `smtc_rac_submit_radio_transaction()` with out-of-range frequency is accepted (#98)

When requested frequency is out of range, no error is returned and the software seems to run normally, but the requested frequency is not used. Instead, the previously set frequency is used.
In future releases, an error will be returned or the firmware will reset with panic for out-of-range frequency.

### USP API: `smtc_rac_submit_radio_transaction()` with LR20xx & BW 7, 10, 15, 20 causes Division by zero (#94)

When using the LR20xx radio with LoRa modulation and BW 7, 10, 15, 20, the software crashes with a Division by zero exception.
In future releases, an error will be returned or the firmware will reset with panic for out-of-range BW.

### The RAL_LORA_CAD_LBT CAD mode of `cad`example is not functional (#125)

The `cad`example exposes 3 modes (TYPE_OF_CAD) :
- RAL_LORA_CAD_LBT  // perform CAD and then TX
- RAL_LORA_CAD_RX   // perform CAD and then RX
- RAL_LORA_CAD_ONLY // perform CAD and stop the radio

The `RAL_LORA_CAD_LBT` mode is currently not functional (both for USP Zephyr & USP baremetal).
