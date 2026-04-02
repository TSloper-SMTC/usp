# Radio Test CLI — User Manual

**Version:** v0.5.0
**Product:** Semtech Unified Software Platform (USP)
**Platforms:** NUCLEO-L476RG · NUCLEO-L073RZ · FPB-RA0E2 · Linux (Raspberry Pi) · Zephyr (nRF54L15)
**Chips:** SX1261 · SX1262 · SX1268 · LR1110 · LR1120 · LR1121 · LR2021 · LR2022

---

## Table of Contents

- [Part 1 — Quick Start](#part-1--quick-start)
  - [1.1 Requirements](#11-requirements)
  - [1.2 First Test: CW in Three Commands](#12-first-test-cw-in-three-commands)
- [Part 2 — Test Procedures](#part-2--test-procedures)
  - [2.1 CW / Conducted Power Measurement](#21-cw--conducted-power-measurement)
  - [2.2 FCC FHSS Test — 64-Channel Hopping](#22-fcc-fhss-test--64-channel-hopping)
  - [2.3 FCC DTS Test — 500 kHz Digital Modulation](#23-fcc-dts-test--500-khz-digital-modulation)
  - [2.4 FCC Hybrid Test — 8-Channel Sub-Band](#24-fcc-hybrid-test--8-channel-sub-band)
  - [2.5 PER Link Test](#25-per-link-test)
  - [2.6 FLRC Test (LR2021)](#26-flrc-test-lr2021)
- [Part 3 — Command Reference](#part-3--command-reference)
  - [3.1 Region and Modulation](#31-region-and-modulation)
  - [3.2 LoRa Parameters](#32-lora-parameters)
  - [3.3 FLRC Parameters](#33-flrc-parameters)
  - [3.4 PA Configuration](#34-pa-configuration)
  - [3.5 Test Modes and Inline Overrides](#35-test-modes-and-inline-overrides)
  - [3.6 PER Configuration](#36-per-configuration)
  - [3.7 Utility Commands and Scripting](#37-utility-commands-and-scripting)
- [Part 4 — Reference Tables](#part-4--reference-tables)
  - [4.1 Region Reference](#41-region-reference)
  - [4.2 Chip Compatibility](#42-chip-compatibility)
  - [4.3 Quick-Reference Command Table](#43-quick-reference-command-table)
- [Part 5 — Troubleshooting](#part-5--troubleshooting)

---

# Part 1 — Quick Start

Build the CLI, connect your radio, and run your first test.

## 1.1 Requirements

**Semtech radio module:** LR1110, LR1120, LR1121, LR2021, LR2022, SX1261, SX1262, or SX1268 connected via SPI/GPIO.

### Linux (Raspberry Pi)

| Item | Requirement |
|------|-------------|
| Host platform | Raspberry Pi with SPI enabled |
| SPI interface | `/dev/spidev0.0` |
| GPIO interface | `/dev/gpiochip0` |
| User groups | User must be in the `spi` and `gpio` groups |
| Build tools | GCC 12+, CMake 3.25+, Ninja |

### Baremetal (STM32 / RA0E2)

| Item | Requirement |
|------|-------------|
| Evaluation board | NUCLEO-L476RG, NUCLEO-L073RZ, or FPB-RA0E2 |
| Build tools | ARM GCC toolchain (`arm-none-eabi-gcc` 12+), CMake 3.25+, Ninja |
| Serial terminal | ANSI-capable emulator (e.g. Tera Term, PuTTY, minicom) at 115200 baud |

## 1.2 First Test: CW in Three Commands

```
region us
power 22
start cw --freq 915.0
```

The radio is now transmitting an unmodulated carrier at 915 MHz, +22 dBm. Type `stop` or press **Ctrl+C** to halt.

**What just happened:** `region us` loaded US915 defaults. `power 22` set the PA. `start cw` started transmission with a one-shot frequency override. No modulation setup is needed for CW.

> **Note:** The CLI saves history to `.radio_test_history`. Use **Up/Down arrows** to recall previous commands. Press **Tab** to complete commands and parameter values.

---

# Part 2 — Test Procedures

Step-by-step guides for each test scenario — from CW power measurements to FCC pre-compliance and PER link tests. Each section explains purpose, parameter choices, and what to expect. For complete parameter syntax, see Part 3.

## 2.1 CW / Conducted Power Measurement

**Purpose:** Measure conducted output power and verify frequency accuracy. Also used to confirm PA configuration before modulated testing.

### Minimum setup

```
region us
power 22
start cw --freq 915.0
```

No modulation is required for CW. The radio transmits a pure carrier at the specified frequency and power.

### What to adjust

| Parameter | How to Set It | Why |
|-----------|---------------|-----|
| `freq` | Band edges and center (e.g. 902.3, 915.0, 927.5 MHz) | Verify power flatness across the band |
| `power` | Step through the power range | Confirm power accuracy at each level |
| `pa` `pa_ramp_us` | `pa pa_ramp_us 16` | Faster ramp; observe switching transients on the analyzer |

### Inline power sweep (scripted)

```
region us
start cw --freq 915.0 --power 22
delay 10000
stop
start cw --freq 915.0 --power 14
delay 10000
stop
start cw --freq 915.0 --power 0
delay 10000
stop
exit
```

> **Note:** `--freq` and `--power` on `start` are one-shot overrides. They do not change the stored configuration, so you can sweep without reconfiguring between runs.

## 2.2 FCC FHSS Test — 64-Channel Hopping

**Purpose:** Demonstrate compliance with FCC §15.247(a)(1) — frequency hopping spread spectrum. The radio hops across all 64 US915 uplink channels at the configured data rate, proving the system uses at least 50 hopping frequencies with a channel occupancy time below 400 ms.

### Minimum setup

```
region us
modulation lora
start fhss --dr 0 --count 0
```

This is a complete, valid FHSS test. The CLI loads LoRa defaults for DR0 (SF10/125 kHz), computes the maximum compliant payload, and hops indefinitely through all 64 channels.

### Which DR to use

| DR | SF | BW | Notes |
|----|----|----|-------|
| DR0 | SF10 | 125 kHz | Most commonly requested by test labs; longest airtime per hop |
| DR3 | SF7 | 125 kHz | Shortest airtime at 125 kHz; useful for rapid channel cycling |
| DR4 | SF8 | 500 kHz | 500 kHz bandwidth; different PA path on some chips |

For FCC pre-compliance the test lab will specify which DR to use. DR0 is most commonly requested.

### Dwell time

At DR0 (SF10/125 kHz) with the maximum auto-computed payload, each hop occupies approximately 370 ms — safely under the FCC 400 ms limit. The CLI warns if a manual payload size would exceed this limit.

### Adding inter-hop delay

```
start fhss --dr 0 --count 64 --delay 100
```

The `--delay` option adds a pause (in milliseconds) between hops. This gives the spectrum analyzer time to settle and capture each channel cleanly. A delay of 50–100 ms is typical for swept-tuned analyzers.

### Verifying the channel plan

```
show channels
```

Lists all 64 US915 uplink channels with their index and frequency. Use this to verify the channel plan matches your test plan before starting.

## 2.3 FCC DTS Test — 500 kHz Digital Modulation

**Purpose:** Demonstrate compliance with FCC §15.247(a)(3) — digital transmission system (DTS). The radio transmits continuously on a single frequency using 500 kHz LoRa bandwidth, proving the modulated signal meets spectral mask and power requirements without frequency hopping.

### Setup

```
region us
modulation lora
bw 500
sf 7
cr 4/5
pld auto
start dts --freq 915.0
```

**Why these parameters:**

- `bw 500` — 500 kHz is required for DTS qualification under §15.247(a)(3)
- `sf 7` — lowest spreading factor gives the widest occupied bandwidth, stressing the spectral mask
- `cr 4/5` — minimum coding rate (LoRaWAN standard)
- `pld auto` — maximum payload; no dwell-time limit applies at 500 kHz

> **Note:** No dwell-time limit applies to 500 kHz DTS operation.

### Varying the test frequency

```
start dts --freq 903.0
start dts --freq 915.0
start dts --freq 927.0
```

## 2.4 FCC Hybrid Test — 8-Channel Sub-Band

**Purpose:** Demonstrate compliance for systems that hop across fewer than 50 channels within an 8-channel sub-band of the US915 plan. The radio hops through 8 consecutive channels using 500 kHz LoRa bandwidth.

### Setup

```
region us
modulation lora
bw 500
sf 7
start hybrid --mask 0 --dr 4 --count 0
```

### Sub-band mask reference

| Mask | Channels | Frequency Range |
|------|----------|-----------------|
| 0 | ch 0–7 | 902.3–903.7 MHz |
| 1 | ch 8–15 | 903.9–905.3 MHz |
| 2 | ch 16–23 | 905.5–906.9 MHz |
| 3 | ch 24–31 | 907.1–908.5 MHz |
| 4 | ch 32–39 | 908.7–910.1 MHz |
| 5 | ch 40–47 | 910.3–911.7 MHz |
| 6 | ch 48–55 | 911.9–913.3 MHz |
| 7 | ch 56–63 | 913.5–914.9 MHz |

DR4 (SF8/500 kHz) is the standard LoRaWAN data rate for 500 kHz channels and is the natural choice for hybrid testing.

### Sweeping all sub-bands (script)

```
start hybrid --mask 0 --dr 4 --count 8 --delay 100
start hybrid --mask 1 --dr 4 --count 8 --delay 100
start hybrid --mask 2 --dr 4 --count 8 --delay 100
start hybrid --mask 3 --dr 4 --count 8 --delay 100
start hybrid --mask 4 --dr 4 --count 8 --delay 100
start hybrid --mask 5 --dr 4 --count 8 --delay 100
start hybrid --mask 6 --dr 4 --count 8 --delay 100
start hybrid --mask 7 --dr 4 --count 8 --delay 100
```

## 2.5 PER Link Test

**Purpose:** Measure packet error rate between two radio units to verify link budget, sensitivity, and antenna performance. One unit transmits a known number of packets; the other counts received packets and reports RSSI, SNR, and PER.

### Setup — Unit A (transmitter)

```
region us
modulation lora
freq 915.0
power 14
bw 125
sf 10
cr 4/5
per count 1000
per interval 200
per payload 16
start per-tx
```

### Setup — Unit B (receiver)

Configure identically (region, modulation, freq, bw, sf, cr, syncword, header, crc), then:

```
region us
modulation lora
freq 915.0
bw 125
sf 10
cr 4/5
start per-rx
```

> **Warning:** Start the receiver before the transmitter. The receiver must be listening before the first packet is sent, or the initial packets will be lost and inflate the PER result.

### Reading results

```
per stats
```

```
Last PER test result (rx):
  Received:   997
  CRC errors:   0
  PER:        0.30%
  RSSI avg:  -78 dBm
  SNR avg:   +9.2 dB
```

### Sensitivity sweep

Reduce TX power in steps to find the sensitivity threshold where PER degrades:

```
start per-tx --power 14
start per-tx --power 10
start per-tx --power 5
start per-tx --power 0
start per-tx --power -5
```

## 2.6 FLRC Test (LR2021)

**Purpose:** Test the FLRC (Fast Long Range Communication) modulation mode available on the LR2021 radio. FLRC provides higher data rates (up to 2600 kbps). Available on LR2021 only, in `us` and `2g4` regions.

### Conducted power / spectral mask

```
region 2g4
modulation flrc
br 1300
cr none
bt bt0.5
preamble 32
sw_len 4
header variable
crc 2
pld 127
start modulated --freq 2440.0 --power 10
```

**Parameter guidance:**

- `bt bt0.5` — Gaussian BT product of 0.5 gives a good balance of spectral efficiency and ISI; tighter spectral mask
- `bt off` — disables Gaussian filtering for maximum data throughput at the cost of wider spectral occupancy
- `cr none` — no coding rate overhead; use `1/2` or `3/4` for forward error correction in noisy environments

### FLRC PER link test

**Unit A (TX):**

```
region 2g4
modulation flrc
br 1300
cr none
bt bt0.5
preamble 32
sw_len 4
syncword ED592398
header variable
crc 2
per count 1000
per interval 50
per payload 64
start per-tx --freq 2440.0 --power 10
```

**Unit B (RX):**

```
region 2g4
modulation flrc
br 1300
cr none
bt bt0.5
preamble 32
sw_len 4
syncword ED592398
header variable
crc 2
start per-rx --freq 2440.0
```

> **Note:** Both units must use identical `br`, `cr`, `bt`, `preamble`, `sw_len`, `syncword`, `header`, and `crc` settings. The `syncword` value must match between transmitter and receiver.

---

# Part 3 — Command Reference

Complete syntax and valid values for every command and parameter. Use this when you need exact details — Part 2 covers when and why to use them.

## 3.1 Region and Modulation

#### `region`

```
region <us|2g4>
```

Sets the regulatory region and loads default frequency and power values. Must be the first command issued after startup.

| Region ID | Band | Frequency Range | Max Power | Regulatory Modes |
|-----------|------|-----------------|-----------|------------------|
| `us` | US915 | 902.0–928.0 MHz | +22 dBm | FHSS, DTS, Hybrid |
| `2g4` | WW-2G4 | 2400.0–2480.0 MHz | +12 dBm | — |

#### `modulation`

```
modulation <lora|flrc>
modulation help
```

Sets the modulation type and loads region-appropriate parameter defaults. `modulation help` lists all modulations supported by the connected chip.

LoRa is available on all chips. FLRC is available on LR2021 only, in `us` and `2g4` regions.

#### `freq`

```
freq <MHz>
```

Sets the transmit/receive center frequency in MHz. The value must be within the current region's allowed range.

#### `power`

```
power <dBm>
```

Sets the transmit output power in dBm. The value is validated against the current region's minimum and maximum limits.

#### `agc`

```
agc <auto|g1|g2|...|g13>
```

Sets the receiver gain mode. `auto` (default) enables automatic gain control. `g1` through `g13` select a fixed gain step (g1 = maximum gain, g13 = minimum gain). Available on LR20xx chips only.

| Value | Description |
|-------|-------------|
| `auto` | Automatic gain control (default) |
| `g1`–`g13` | Fixed gain step (g1 = max gain, g13 = min gain) |

#### `boost-lf` / `boost-hf`

```
boost-lf <auto|0-7>
boost-hf <auto|0-7>
```

Sets the RX boost level for the low-frequency (`boost-lf`) or high-frequency (`boost-hf`) receive path. `auto` uses the default value (0 for LF, 4 for HF). Available on LR20xx chips only.

#### `xosc`

```
xosc <show|default|xta|xtb[|wait]> [value]
```

Manages crystal oscillator trimming capacitor values. Available on LR20xx and SX126x chips.

| Subcommand | Chips | Description |
|------------|-------|-------------|
| `xosc show` | All | Display current XTA and XTB values (with pF conversion) |
| `xosc default` | All | Restore BSP default trim values |
| `xosc xta <0-47>` | All | Set XTA capacitor trim (11.3 + N × 0.47 pF) |
| `xosc xtb <0-47>` | All | Set XTB capacitor trim (11.1 + N × 0.47 pF) |
| `xosc wait <0-255>` | LR20xx only | Set stabilization delay in microseconds |

On SX126x the XOSC stabilisation delay is managed by the hardware state machine and is not user-configurable; `xosc wait` is not available. Values set via `xosc xta`/`xosc xtb` persist across CLI commands and are re-applied automatically before every TX/RX operation.

#### `status`

```
status
```

Displays all current configuration parameters in a single view, including region, modulation, frequency, power, modulation-specific parameters, PA overrides, and test mode state.

## 3.2 LoRa Parameters

LoRa parameters are set by typing the parameter name directly at the prompt after `modulation lora` is active. All parameters are validated against regional constraints before being applied.

#### `bw`

```
bw <kHz>
```

| Region | Valid Values (kHz) |
|--------|--------------------|
| US915 | 125, 250, 500 |
| WW-2G4 | 812 |

> **Warning:** Changing bandwidth may invalidate the current spreading factor. The CLI warns when this occurs; use `sf` to adjust before starting a test.

#### `sf`

```
sf <5-12>
```

| BW (kHz) | US915 SF Range | LoRaWAN DR Equivalent |
|----------|----------------|-----------------------|
| 125 | SF5–SF10 | DR0 (SF10) – DR3 (SF7) |
| 250 | SF5–SF12 | — |
| 500 | SF5–SF12 | DR4 (SF8/500k) |

#### `cr`

```
cr <4/5|4/6|4/7|4/8>          — all chips
cr <li4/5|li4/6|li4/8>        — LR11xx, LR20xx only
cr <lic4/6|lic4/8>            — LR20xx only
```

Sets the LoRa coding rate. Standard rates `4/5` through `4/8` are available on all chips. Long interleaved (LI) rates are available on LR11xx and LR20xx. Long interleaved convolutional (LI-Conv) rates are LR20xx only.

| Value | Type | Chips |
|-------|------|-------|
| `4/5`, `4/6`, `4/7`, `4/8` | Standard | All |
| `li4/5`, `li4/6`, `li4/8` | Long interleaved | LR11xx, LR20xx |
| `lic4/6`, `lic4/8` | Long interleaved convolutional | LR20xx only |

#### `preamble`

```
preamble <4–65535>
```

Sets the LoRa preamble length in symbols. Default is 8 symbols (LoRaWAN standard). Longer preambles improve reception reliability at the cost of airtime.

#### `syncword`

```
syncword <hex>
```

Sets the LoRa sync word as a one-byte hexadecimal value.

| Value | Meaning |
|-------|---------|
| `0x34` | LoRaWAN public network (default for US) |
| `0x12` | Private network |
| `0x21` | WW-2G4 public (default for 2g4) |

#### `header`

```
header <explicit|implicit>
```

In **explicit** mode (default), the packet header carries the payload length, coding rate, and CRC presence flag. In **implicit** mode, no header is transmitted — both TX and RX must agree on the payload length and CRC setting in advance.

#### `crc`

```
crc <on|off>
```

Enables or disables the LoRa payload CRC. Default is `on`.

#### `iq`

```
iq <standard|inverted>
```

Sets the I/Q polarity. `standard` (default) uses normal I/Q mapping. `inverted` swaps I and Q, which is used by some LoRaWAN downlink configurations. Available on all chips.

#### `pld`

```
pld <1–255|auto>
```

Sets the payload size in bytes for continuous modulated transmission (`start modulated`). Setting `auto` computes the maximum payload that fits within the FCC 400 ms dwell-time limit for the current LoRa configuration.

> **Note:** **FCC dwell time:** At 125 kHz and 250 kHz bandwidths, the FCC limits each transmission to 400 ms per channel before hopping is required. The CLI calculates time-on-air and warns if the configured payload exceeds this limit. 500 kHz (DTS) is exempt from the dwell-time rule.

#### `ldro`

```
ldro <on|off|auto>
```

Controls the low data rate optimization flag. When set to `auto` (default), the CLI enables LDRO automatically when the symbol duration exceeds 16 ms (SF11/125 kHz and SF12/125 kHz). Force `on` or `off` to override. Available on SX126x and LR11xx only (not available on LR20xx).

## 3.3 FLRC Parameters

FLRC parameters are available after `modulation flrc` is active. FLRC is supported on LR2021 only, in `us` and `2g4` regions.

#### `br`

```
br <260|325|520|650|1040|1300|2080|2600>
```

Sets the FLRC bit rate in kbps. Higher bit rates reduce airtime but require better link budget.

| Value (kbps) | Bandwidth |
|--------------|-----------|
| 260 | 312 kHz |
| 325 | 312 kHz |
| 520 | 624 kHz |
| 650 | 624 kHz |
| 1040 | 1248 kHz |
| 1300 | 1248 kHz |
| 2080 | 2496 kHz |
| 2600 | 2496 kHz |

#### `cr`

```
cr <1/2|2/3|3/4|none>
```

Sets the FLRC forward error correction coding rate. `none` disables FEC for maximum throughput. Default is `3/4`.

#### `bt`

```
bt <off|bt0.5|bt1>
```

Controls the Gaussian pulse-shaping filter. `bt0.5` gives tighter spectral mask (recommended). `bt1` provides moderate filtering. `off` disables filtering for maximum throughput. Default is `bt0.5`.

#### `preamble`

```
preamble <4|8|12|16|20|24|28|32>
```

Sets the FLRC preamble length in bits. Must be a multiple of 4 from 4 to 32.

#### `sw_len`

```
sw_len <off|2|4>
```

Sets the sync word length in bytes. `off` disables the sync word. Default is `4`.

#### `tx_sw`

```
tx_sw <off|1|2|3>
```

Selects which syncword register to transmit with. `off` disables syncword transmission. Default is `1`.

#### `rx_sw`

```
rx_sw <off|1-7>
```

Selects which syncword register(s) to match on receive, as a bitmask. Default is `1` (syncword 1 only).

| Value | Matches |
|-------|---------|
| `off` / `0` | None (syncword matching disabled) |
| `1` | sw1 |
| `2` | sw2 |
| `3` | sw1 \| sw2 |
| `4` | sw3 |
| `5` | sw1 \| sw3 |
| `6` | sw2 \| sw3 |
| `7` | sw1 \| sw2 \| sw3 |

#### `header`

```
header <variable|fixed>
```

`variable` (default) includes the payload length in the packet header. `fixed` omits the length header — both TX and RX must agree on the payload length.

#### `crc`

```
crc <off|2|3|4>
```

Sets the CRC length in bytes. `off` disables CRC. Default is `2`.

#### `syncword`

```
syncword <hex>
```

Sets the 4-byte syncword value for register 1. Enter 1 to 8 hex digits; values shorter than 8 digits are zero-padded from the left. Default is `0xED592398`.

> **Note:** **FLRC syncword architecture:** `syncword` sets the byte values in the syncword register. `tx_sw` selects which register to transmit with. `rx_sw` controls which registers are matched on receive. For basic operation, set `syncword` to the desired value and leave `tx_sw` and `rx_sw` at their defaults (both `1`).

## 3.4 PA Configuration

The power amplifier is automatically configured when the `power` command is used. For advanced measurements requiring manual PA control, use the `pa` command.

```
pa show
pa reset
pa <param> <value>
```

`pa show` displays the current PA register values and valid ranges. `pa reset` clears all manual overrides and restores the automatic PA configuration for the current power level. `pa <param> <value>` sets an individual PA register.

**Override semantics:**

- Manual PA overrides take effect immediately and persist until `pa reset` is called or the `power` command is used
- The `power` command recalculates all PA registers from the target power level, clearing any manual overrides
- Overrides are not saved between sessions

### LR20xx (LR2021, LR2022)

| Parameter | Range | Description |
|-----------|-------|-------------|
| `half_power` | −39 to 44 | TX power in 0.5 dB steps |
| `pa_sel` | 0–1 (read-only) | PA path: 0 = LF, 1 = HF |
| `pa_lf_duty_cycle` | 0–31 | Low-freq PA duty cycle |
| `pa_lf_slices` | 0–7 | Low-freq PA number of slices |
| `pa_hf_duty_cycle` | 16–31 | HF duty cycle (16 = max, 31 = lowest) |
| `pa_ramp_us` | 2–304 | PA ramp time in microseconds |

> **Note:** `pa_sel` is read-only and set automatically based on frequency: LF PA for sub-GHz, HF PA for 2.4 GHz. `pa_ramp_us` accepts discrete values only: 2, 4, 8, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 240, 272, 304.

### SX1262 / SX1268

| Parameter | Range | Description |
|-----------|-------|-------------|
| `power` | −9 to 22 | TX power register value (dBm) |
| `pa_duty_cycle` | 0–4 | PA duty cycle (max 0x04, see datasheet) |
| `hp_max` | 0–7 | HP PA limit (7 = max, enables +22 dBm) |
| `pa_ramp_us` | 10–3400 | PA ramp time in microseconds |

### SX1261

| Parameter | Range | Description |
|-----------|-------|-------------|
| `power` | −17 to 14 | TX power register value (dBm) |
| `pa_duty_cycle` | 0–7 | PA duty cycle (see datasheet for max) |
| `pa_ramp_us` | 10–3400 | PA ramp time in microseconds |

### LR11xx (LR1110, LR1120, LR1121)

| Parameter | Range | Description |
|-----------|-------|-------------|
| `power` | −17 to 22 | TX power in dBm |
| `pa_sel` | 0–2 (read-only) | PA path: 0 = LP, 1 = HP, 2 = HF |
| `pa_reg_supply` | 0–1 (read-only) | PA supply: 0 = VREG, 1 = VBAT |
| `pa_duty_cycle` | 0–7 | PA duty cycle |
| `pa_hp_sel` | 0–7 | HP PA slices |
| `pa_ramp_us` | 16–304 | PA ramp time in microseconds |

> **Note:** `pa_sel` and `pa_reg_supply` are read-only — the BSP selects LP/HP/HF based on frequency and power level. `pa_ramp_us` accepts discrete values only: 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 240, 272, 304.

## 3.5 Test Modes and Inline Overrides

Test modes are started with `start <mode>` and stopped with `stop` or **Ctrl+C**. Only one mode may be active at a time. Configuration commands are blocked while any mode is running.

> **Note:** On baremetal MCU targets, the CLI runs in a polled main loop. `stop` is the primary way to halt a test. **Ctrl+C** is handled on Linux only.

#### `start cw`

Transmits a continuous single-frequency unmodulated carrier. No modulation needs to be configured — only frequency and power are required.

```
start cw [--freq <MHz>] [--power <dBm>]
```

#### `start modulated`

Transmits packets back-to-back using the current modulation configuration. Used for occupied bandwidth measurements and spectral mask tests. Payload size is set with the `pld` command.

```
start modulated [--freq <MHz>] [--power <dBm>]
```

#### `start rx`

Places the radio in continuous receive mode. Each received packet is printed with RSSI and SNR. CRC errors are reported separately.

```
start rx [--freq <MHz>]
```

#### `start fhss`

Transmits using the US915 64-channel FHSS plan. FCC §15.247(a)(1) pre-compliance.

```
start fhss [--dr <0-6>] [--count <N>] [--delay <ms>] [--power <dBm>]
```

#### `start dts`

Transmits continuously on a fixed frequency at 500 kHz bandwidth. FCC §15.247(a)(3) pre-compliance.

```
start dts [--freq <MHz>] [--power <dBm>] [--count <N>] [--delay <ms>]
```

#### `start hybrid`

Hops through 8 consecutive channels within a selectable sub-band. The `--mask` argument is required.

```
start hybrid --mask <0-7> [--dr <0-6>] [--count <N>] [--delay <ms>] [--power <dBm>]
```

#### `start per-tx` / `start per-rx`

Runs a PER test between two radio units. Configure PER parameters with the `per` command before starting.

```
start per-tx [--freq <MHz>] [--power <dBm>]
start per-rx [--freq <MHz>]
```

#### `stop`

Stops the currently running test mode and returns to idle.

```
stop
```

### Inline overrides

The `start` command accepts inline overrides that modify parameters for a single test run without changing the stored configuration.

| Override | Applicable Modes | Description |
|----------|------------------|-------------|
| `--freq <MHz>` | All | One-shot frequency override |
| `--power <dBm>` | All TX modes | One-shot power override |
| `--dr <0-6>` | fhss, hybrid | LoRaWAN data rate for this run |
| `--count <N>` | fhss, dts, hybrid | Packet/hop count (0 = infinite) |
| `--delay <ms>` | fhss, dts, hybrid | Inter-packet/hop delay |
| `--mask <0-7>` | hybrid (required) | 8-channel sub-band selection |

## 3.6 PER Configuration

| Command | Description |
|---------|-------------|
| `per count <N>` | Packets TX will send; 0 = run indefinitely (default: 100) |
| `per interval <ms>` | Inter-packet gap on TX side; must be > 0 (default: 500 ms) |
| `per payload <6-255\|auto>` | Payload size per packet; `auto` = max for FCC dwell time (default: auto) |
| `per stats` | Display last test result (available any time, even during a test) |
| `per reset` | Clear last saved result |

> **Note:** `per stats` and `per reset` are always available. All other `per` subcommands require the radio to be idle — use `stop` first.

## 3.7 Utility Commands and Scripting

#### `show channels`

Displays the complete LoRaWAN channel plan for the current region, listing each channel index and its center frequency.

```
show channels
```

#### `delay`

Pauses execution for the specified duration in milliseconds. If a test mode is active, it continues to run during the delay — this is intended for use in scripts to set observation windows.

```
delay <ms>
```

#### `term`

```
term <ansi|plain>
```

Switches the terminal input mode between `ansi` (arrow keys, history, tab completion) and `plain` (basic line input). MCU only — not available on Linux. With no arguments, shows the current mode.

#### `version`

Displays the CLI version string and build information.

```
version
```

#### `help`

Shows the command reference. Provide a command name for context-sensitive detail.

```
help [command]
```

#### `exit` / `quit`

Stops any active test, closes the radio, and exits the CLI.

```
exit
```

### Scripting / Pipe Mode

When stdin is not a terminal (i.e. piped or redirected input), the CLI runs in non-interactive script mode: commands are read sequentially from stdin and the CLI exits when EOF is reached.

```
./build/radio_test_cli <<'EOF'
region us
modulation lora
freq 915.0
power 22
start cw
delay 30000
stop
exit
EOF
```

> **Note:** Commands that return an error do not stop script execution. Output is written to stdout in the same format as interactive mode, making it suitable for capture with `tee` or redirection.

---

# Part 4 — Reference Tables

Region parameters, chip feature matrix, and the complete command quick-reference.

## 4.1 Region Reference

### US915

| Parameter | Value |
|-----------|-------|
| Frequency range | 902.0–928.0 MHz |
| Default frequency | 902.3 MHz (LoRaWAN ch0) |
| TX power range | −10 to +22 dBm (LR20xx); −9 to +22 dBm (SX1262/SX1268); −17 to +22 dBm (SX1261, LR11xx) |
| Default TX power | +22 dBm |
| LoRa BW options | 125, 250, 500 kHz |
| LoRa SF (125 kHz) | SF5–SF10 |
| LoRa SF (250/500 kHz) | SF5–SF12 |
| Default SF/BW | SF10 / 125 kHz (DR0) |
| Default sync word | 0x34 (LoRaWAN public) |
| Regulatory modes | FHSS (64-ch), DTS (500 kHz), Hybrid (8-ch sub-band) |

### US915 Data Rates

| DR | Modulation | SF | BW | Bit Rate (approx) |
|----|------------|----|----|--------------------|
| DR0 | LoRa | SF10 | 125 kHz | 980 bps |
| DR1 | LoRa | SF9 | 125 kHz | 1760 bps |
| DR2 | LoRa | SF8 | 125 kHz | 3125 bps |
| DR3 | LoRa | SF7 | 125 kHz | 5470 bps |
| DR4 | LoRa | SF8 | 500 kHz | 12500 bps |

### WW-2G4 (LR2021, LR2022, LR1120, LR1121)

| Parameter | Value |
|-----------|-------|
| Frequency range | 2400.0–2480.0 MHz |
| Default frequency | 2423.0 MHz |
| TX power range | −17 to +12 dBm (LR20xx); −18 to +12 dBm (LR11xx) |
| Default TX power | +10 dBm |
| LoRa BW | 812 kHz |
| LoRa SF range | SF5–SF12 |
| Default sync word | 0x21 (WW-2G4 public) |
| FLRC | Available (LR2021 only) |
| Regulatory modes | None (handled by host protocol stack) |

## 4.2 Chip Compatibility

| Feature | LR2021 | LR2022 | LR1110 | LR1120 | LR1121 | SX1261 | SX1262 | SX1268 |
|---------|--------|--------|--------|--------|--------|--------|--------|--------|
| LoRa (Sub-GHz) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| LoRa (2.4 GHz) | ✓ | ✓ | — | ✓ | ✓ | — | — | — |
| FLRC | ✓ | — | — | — | — | — | — | — |
| Region: us | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| Region: 2g4 | ✓ | ✓ | — | ✓ | ✓ | — | — | — |
| FHSS / DTS / Hybrid | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| PER TX/RX | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| AGC control | ✓ | ✓ | — | — | — | — | — | — |
| RX boost (LF/HF) | ✓ | ✓ | — | — | — | — | — | — |
| XOSC trimming | ✓ | ✓ | — | — | — | ✓ | ✓ | ✓ |
| LED indicators | — | — | ✓ | ✓ | ✓ | — | — | — |
| I/Q polarity | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| LDRO control | — | — | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| CR LI/LI-Conv | ✓ | ✓ | ✓ | ✓ | ✓ | — | — | — |

## 4.3 Quick-Reference Command Table

| Command | Syntax | Description |
|---------|--------|-------------|
| `region` | `region <us\|2g4>` | Set region; load defaults |
| `modulation` | `modulation <lora\|flrc>` | Set modulation; load defaults |
| `freq` | `freq <MHz>` | Set center frequency |
| `power` | `power <dBm>` | Set TX power |
| `agc` | `agc <auto\|g1-g13>` | Receiver gain control (LR20xx only) |
| `boost-lf` | `boost-lf <auto\|0-7>` | RX boost level, LF path (LR20xx only) |
| `boost-hf` | `boost-hf <auto\|0-7>` | RX boost level, HF path (LR20xx only) |
| `xosc` | `xosc <show\|default\|xta\|xtb[\|wait]>` | Crystal oscillator trimming (LR20xx + SX126x; `wait` LR20xx only) |
| `status` | `status` | Show complete current configuration |
| `bw` | `bw <kHz>` | LoRa bandwidth (125/250/500/812) |
| `sf` | `sf <5-12>` | LoRa spreading factor |
| `cr` | `cr <4/5\|4/6\|4/7\|4/8\|li4/5\|li4/6\|li4/8\|lic4/6\|lic4/8>` | LoRa coding rate |
| `preamble` | `preamble <symbols>` | Preamble length |
| `syncword` | `syncword <hex>` | LoRa sync word (e.g. 0x34) |
| `header` | `header <explicit\|implicit>` | LoRa header mode |
| `crc` | `crc <on\|off>` | LoRa CRC enable |
| `iq` | `iq <standard\|inverted>` | LoRa I/Q polarity |
| `pld` | `pld <1-255\|auto>` | Modulated TX payload size |
| `ldro` | `ldro <on\|off\|auto>` | Low data rate optimization (SX126x, LR11xx only) |
| `br` | `br <260\|325\|520\|650\|1040\|1300\|2080\|2600>` | FLRC bit rate (kbps) |
| `cr` (FLRC) | `cr <1/2\|2/3\|3/4\|none>` | FLRC coding rate |
| `bt` | `bt <off\|bt0.5\|bt1>` | FLRC Gaussian BT filter |
| `preamble` (FLRC) | `preamble <4\|8\|12\|16\|20\|24\|28\|32>` | FLRC preamble length (bits) |
| `sw_len` | `sw_len <off\|2\|4>` | FLRC sync word length (bytes) |
| `tx_sw` | `tx_sw <off\|1\|2\|3>` | FLRC TX sync word index |
| `rx_sw` | `rx_sw <off\|1-7>` | FLRC RX sync word match (bitmask) |
| `header` (FLRC) | `header <variable\|fixed>` | FLRC header type |
| `crc` (FLRC) | `crc <off\|2\|3\|4>` | FLRC CRC length (bytes) |
| `syncword` (FLRC) | `syncword <hex>` | FLRC sync word value (e.g. ED592398) |
| `pa show` | `pa show` | Display PA parameters and ranges |
| `pa reset` | `pa reset` | Clear PA overrides |
| `pa <p> <v>` | `pa <param> <value>` | Set PA parameter manually |
| `start cw` | `start cw [overrides]` | Unmodulated carrier TX |
| `start modulated` | `start modulated [overrides]` | Continuous modulated TX |
| `start rx` | `start rx [overrides]` | Continuous receive mode |
| `start fhss` | `start fhss [overrides]` | 64-channel FHSS TX (US only) |
| `start dts` | `start dts [overrides]` | DTS 500 kHz TX (US only) |
| `start hybrid` | `start hybrid --mask N [overrides]` | Hybrid 8-ch TX (US only) |
| `start per-tx` | `start per-tx [overrides]` | PER test transmitter |
| `start per-rx` | `start per-rx [overrides]` | PER test receiver |
| `stop` | `stop` | Stop active test mode |
| `per count` | `per count <N>` | PER packet count (0 = infinite) |
| `per interval` | `per interval <ms>` | PER inter-packet gap |
| `per payload` | `per payload <6-255\|auto>` | PER payload size |
| `per stats` | `per stats` | Show last PER test result |
| `per reset` | `per reset` | Clear last PER result |
| `show channels` | `show channels` | Display region channel plan |
| `delay` | `delay <ms>` | Pause execution (scripting) |
| `term` | `term <ansi\|plain>` | Terminal input mode (MCU only) |
| `version` | `version` | Show CLI version |
| `help` | `help [command]` | Show help |
| `exit` / `quit` | `exit` | Exit CLI |

---

# Part 5 — Troubleshooting

Common errors and how to resolve them.

### SPI device not found

**Symptom:** `Failed to open /dev/spidev0.0` on startup.

**Fix:** Enable SPI on the Raspberry Pi. Run `sudo raspi-config`, navigate to Interface Options > SPI, and enable it. Reboot. Verify with `ls /dev/spidev*`.

### Permission denied on SPI or GPIO

**Symptom:** `Permission denied` when accessing `/dev/spidev0.0` or `/dev/gpiochip0`.

**Fix:** Add your user to the required groups:

```
sudo usermod -aG spi,gpio $USER
```

Log out and back in for the group changes to take effect.

### Build fails on GCC 14

**Symptom:** Compilation error about incompatible pointer types in vendor code.

**Fix:** Add the warning suppression flag:

```
env CFLAGS="-Wno-incompatible-pointer-types" \
  cmake -UCMAKE_C_FLAGS -S examples -B build -DBOARD=LINUX -DRAC_RADIO=lr2021 -G Ninja
```

### Radio not responding / no RX packets

**Symptom:** Commands succeed but no signal is observed on the spectrum analyzer, or the receiver sees no packets.

**Fix:** Check the following:

- Verify the SPI connection and that the correct radio shield is seated properly
- Confirm the `-DRAC_RADIO=` build flag matches the actual radio chip connected
- For PER tests, ensure both units have identical modulation parameters (bw, sf, cr, syncword, header, crc)
- Check that the frequency is within the selected region's valid range

### FLRC: "not supported by this chip"

**Symptom:** `modulation flrc` returns an error.

**Fix:** FLRC is available on LR2021 only. Verify the build was compiled with `-DRAC_RADIO=lr2021`. FLRC is available in both `us` and `2g4` regions.

### Region 2g4: "not supported by this chip"

**Symptom:** `region 2g4` returns an error on SX126x or LR11xx.

**Fix:** The 2.4 GHz region is only available on chips with a 2.4 GHz radio path: LR2021, LR2022, LR1120, and LR1121. SX1261, SX1262, SX1268, and LR1110 are sub-GHz only.

### AGC command not recognized

**Symptom:** `agc auto` returns an error.

**Fix:** The `agc` command is available on LR20xx chips only (LR2021, LR2022). It is not supported on SX126x or LR11xx hardware.

---

*Radio Test CLI v0.5.0 | Copyright Semtech Corporation 2026. All rights reserved. | Confidential*
