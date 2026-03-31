# ArduSimple Interposer Board on Raspberry Pi

## Overview

The [ArduSimple Raspberry Pi Adapter for simpleRTK2B][product] is an
interposer board that connects a simpleRTK2B-form-factor module to the
Raspberry Pi 40-pin GPIO header. The LR2021 LoRa Plus evaluation board
uses this same form factor and plugs directly into the adapter.

Because the interposer routes signals to different pins than the default
USP Linux HAL expects, several configuration changes are required.

This document describes the pin mapping, OS-level configuration, and
source code changes needed to use the ArduSimple interposer with the USP
on a Raspberry Pi.

**References:**
- [Product page][product]
- [Schematic (PDF)][schematic]

[product]:   https://www.ardusimple.com/product/raspberry-adapter-for-simplertk2b/
[schematic]: https://www.ardusimple.com/wp-content/uploads/2022/01/RPI_arduino_shield_add-on-schematic.pdf

## Pin Mapping

### Signal Routing

The interposer routes the LR2021 signals to the following Raspberry Pi
GPIO pins (BCM numbering):

| Signal   | BCM GPIO    | Physical Pin | Description                                  |
|----------|-------------|--------------|----------------------------------------------|
| SPI MOSI | GPIO 10     | Pin 19       | SPI0 MOSI (default)                          |
| SPI MISO | GPIO 9      | Pin 21       | SPI0 MISO (default)                          |
| SPI SCLK | GPIO 11     | Pin 23       | SPI0 SCLK (default)                          |
| SPI CS   | **GPIO 23** | Pin 16       | Chip select (remapped from default GPIO 8)   |
| NRST     | **GPIO 4**  | Pin 7        | Radio reset (active-low)                     |
| BUSY     | **GPIO 18** | Pin 12       | Radio busy status                            |
| DIO/IRQ  | **GPIO 5**  | Pin 29       | Radio interrupt (rising edge)                |

**Bold** entries differ from the Raspberry Pi's SPI0 defaults or from
the USP default pin assignments.

### Comparison with Default USP Pin Assignments

| Signal   | Default (BCM) | ArduSimple (BCM) |
|----------|---------------|------------------|
| SPI CS   | GPIO 8 (CE0)  | GPIO 23          |
| NRST     | GPIO 12       | GPIO 4           |
| BUSY     | GPIO 5        | GPIO 18          |
| DIO/IRQ  | GPIO 16       | GPIO 5           |

## Required Changes

### 1. Raspberry Pi Boot Configuration

Two changes are needed in `/boot/firmware/config.txt`:

```ini
# Enable SPI and remap chip select from CE0 (GPIO 8) to GPIO 23
dtparam=spi=on
dtoverlay=spi0-cs,cs0_pin=23

# Configure GPIO 18 (BUSY pin) as input with no pull resistor.
# Disables the default pull-down on GPIO 18 so it does not
# interfere with the radio BUSY signal.
gpio=18=ip,pn
```

`dtoverlay=spi0-cs,cs0_pin=23` -- The ArduSimple interposer routes
the LR2021 chip select to GPIO 23 instead of the Raspberry Pi's default
SPI0 CE0 (GPIO 8). This device tree overlay tells the SPI driver to
assert GPIO 23 as the chip select for `/dev/spidev0.0`.

`gpio=18=ip,pn` -- Configures GPIO 18 as an input with no pull resistor
(`pn` = pull none) at boot time. This disables the default pull-down on
GPIO 18 so it does not interfere with the LR2021 BUSY signal.

A reboot is required after modifying `config.txt`.

### 2. GPIO Pin Definitions

Update `examples/modem_pinout_linux.h` to match the interposer routing:

```c
// Radio specific pinout for ArduSimple interposer
#define RADIO_NRST     RPI_GPIO4    // was RPI_GPIO12
#define RADIO_BUSY_PIN RPI_GPIO18   // was RPI_GPIO5
#define RADIO_DIOX     RPI_GPIO5    // was RPI_GPIO16
```

The diff:

```diff
-#define RADIO_NRST     RPI_GPIO12
-#define RADIO_BUSY_PIN RPI_GPIO5
-#define RADIO_DIOX     RPI_GPIO16
+#define RADIO_NRST     RPI_GPIO4
+#define RADIO_BUSY_PIN RPI_GPIO18
+#define RADIO_DIOX     RPI_GPIO5
```

### 3. SPI and GPIO Device Paths (no change required)

The SPI device (`/dev/spidev0.0`) and GPIO chip (`/dev/gpiochip0`)
remain unchanged. The `config.txt` overlay handles the CS remapping at
the kernel level, so the application sees the same spidev interface.

## Verification

After applying all changes and rebooting:

1. **Check SPI device exists:**

   ```bash
   ls -l /dev/spidev0.0
   ```

2. **Check GPIO chip:**

   ```bash
   gpioinfo gpiochip0 | grep -E "line\s+(4|5|18|23)"
   ```

3. **Verify the CS overlay loaded:**

   ```bash
   dtoverlay -l
   # Should show spi0-cs in the list
   ```

4. **Build the application:**

   ```bash
   rm -Rf build/
   env CFLAGS="-Wno-incompatible-pointer-types" \
   cmake -S examples -B build \
     -DCMAKE_BUILD_TYPE=MinSizeRel \
     -DBOARD=LINUX \
     -DRAC_RADIO=lr2021 \
     -DAPP=PERIODICAL_UPLINK \
     -UCMAKE_C_FLAGS \
     -G Ninja
   cmake --build build --target periodical_uplink
   ```

   For `tx_cw` (continuous wave transmit, useful for verifying RF output):

   ```bash
   rm -Rf build/
   env CFLAGS="-Wno-incompatible-pointer-types" \
   cmake -S examples -B build \
     -DCMAKE_BUILD_TYPE=MinSizeRel \
     -DBOARD=LINUX \
     -DRAC_RADIO=lr2021 \
     -DAPP=TX_CW \
     -UCMAKE_C_FLAGS \
     -G Ninja
   cmake --build build --target tx_cw
   ```

   > **GCC 14+:** The `CFLAGS="-Wno-incompatible-pointer-types"` and
   > `-UCMAKE_C_FLAGS` flags are required. GCC 14 promotes
   > `-Wincompatible-pointer-types` to an error by default, which breaks
   > vendor LBM code.

5. **Run the application:**

   ```bash
   sudo ./build/periodical_uplink
   ```

   `sudo` is required for GPIO and SPI access unless udev rules have
   been configured.

## Notes

- The LR2021 antenna switching is handled internally via DIO5/DIO6 --
  no external antenna switch GPIOs are needed.

- Radio sleep/wakeup via NSS toggling is not supported on Linux (the
  spidev driver manages CS automatically). This is a platform-level
  limitation, not specific to the interposer.

- The `example_options.h` file contains LoRaWAN credentials (DevEUI,
  JoinEUI, AppKey) and region settings that must be configured for your
  network -- these are independent of the interposer hardware.

---

## LR11xx (LR1110/LR1120/LR1121) Shield Configuration

The LR11xx evaluation shields use the same ArduSimple interposer but
have a different DIO/IRQ pin and three on-board LEDs. This section
covers the additional boot configuration required for LR11xx shields.

### Signal Routing

| Signal   | BCM GPIO    | Physical Pin | Description                                  |
|----------|-------------|--------------|----------------------------------------------|
| SPI MOSI | GPIO 10     | Pin 19       | SPI0 MOSI (default)                          |
| SPI MISO | GPIO 9      | Pin 21       | SPI0 MISO (default)                          |
| SPI SCLK | GPIO 11     | Pin 23       | SPI0 SCLK (default)                          |
| SPI CS   | **GPIO 23** | Pin 16       | Chip select (remapped from default GPIO 8)   |
| NRST     | **GPIO 4**  | Pin 7        | Radio reset (active-low)                     |
| BUSY     | **GPIO 18** | Pin 12       | Radio busy status                            |
| DIO/IRQ  | **GPIO 27** | Pin 13       | Radio interrupt (rising edge)                |

### LED Pin Mapping

The LR11xx shields have three LEDs routed through the interposer:

| LED      | Arduino Pin | BCM GPIO    | Physical Pin |
|----------|-------------|-------------|--------------|
| TX       | A4          | **GPIO 2**  | Pin 3        |
| RX       | A5          | **GPIO 3**  | Pin 5        |
| Sniffing | D4          | **GPIO 7**  | Pin 26       |

GPIO 2 and GPIO 3 are the Raspberry Pi's I2C1 bus (SDA/SCL) and have
1.8k hardware pull-ups to 3.3V. GPIO 7 is SPI0 CE1. Both the I2C and
SPI drivers must be reconfigured to release these pins for LED use.

### Raspberry Pi Boot Configuration

Add the following to `/boot/firmware/config.txt`:

```ini
# Enable SPI with one chip select only (frees GPIO 7 / SPI0 CE1)
dtparam=spi=on
dtoverlay=spi0-1cs,cs0_pin=23

# Disable I2C (frees GPIO 2 and GPIO 3 from hardware pull-ups)
dtparam=i2c_arm=off

# Drive LED GPIOs low at boot so LEDs are off before the application starts
gpio=2,3,7=op,dl

# Configure GPIO 18 (BUSY pin) as input with no pull resistor
gpio=18=ip,pn
```

`dtoverlay=spi0-1cs,cs0_pin=23` -- Configures SPI0 with only one chip
select remapped to GPIO 23, releasing GPIO 7 (CE1) for the sniffing LED.

`dtparam=i2c_arm=off` -- Disables the I2C1 peripheral so GPIO 2 and
GPIO 3 are not claimed by the I2C driver. Without this, the 1.8k
hardware pull-ups keep the TX and RX LEDs on regardless of software
control.

`gpio=2,3,7=op,dl` -- Drives the three LED GPIOs as outputs, low, at
boot time. This ensures LEDs are off immediately, before any application
starts.

A reboot is required after modifying `config.txt`.

### Verification

After rebooting, confirm the LED GPIOs are free:

```bash
gpioinfo gpiochip0 | grep -E "line\s+(2|3|7)\b"
```

All three lines should show as `unused` (or claimed by `gpio_out` from
the `gpio=` overlay, which does not prevent userspace access).
