# FLRC Burst

This application demonstrates **FLRC burst transfer**: sending or receiving a large block of data (e.g. 20 kB) as a continuous stream of fixed-size radio frames at high data rate, with minimal gap between frames.

The FLRP Protocol implemented in flrc_burst example is Work In Progress and available for demonstration. A more Stable version will be available soon.

## What is a burst?

A **burst** is a back-to-back sequence of FLRC packets with a short, fixed inter-frame spacing (e.g. 200 µs). The radio uses a double-buffer (ping-pong) scheme so that the next packet can be loaded while the current one is still being transmitted, keeping the channel busy and maximizing throughput.

## How burst TX works

- **Double-buffer pipeline**: Packet N+1 must be loaded into the radio before packet N has finished transmitting. When the radio reports that packet N is sent (`RP_STATUS_REQUEST_NEXT_TX_PAYLOAD`), the application then loads **packet N+2** into the buffer that was just freed (packet N+1 is already in the other buffer and is being sent).
- **Lock / Unlock**: The radio is locked only for the duration of the burst. On the receiver, the **Lock/Unlock** events are used so that Rx CRC and payload verification are done **after** the radio is unlocked, reducing the time the radio is held locked.

## Key features

- Sub-GHz single-channel FLRC; ~2.08 Mbps PHY (US) / ~1.04 Mbps raw (EU), 200 µs minimum inter-frame.
- File transfer of 20 kB in one burst (511 bytes per frame).
- Preliminary LoRa WOR sync frame with acknowledgement before the first burst.
- Single direction: initiator sends, receiver listens. No block ack, no protocol security or header.
- Parameters configurable via `apps_configuration.h`.

## Configuration

| Parameter         | Description                                      |
|-------------------|--------------------------------------------------|
| `ROLE`            | `1` = receiver, `2` = transmitter                |
| `RP_MARGIN_DELAY` | RAC margin delay (e.g. `10`)                     |
| `BOARD`           | e.g. `NUCLEO_L476`                               |
| `RAC_RADIO`       | e.g. `lr2021`                                    |
| `LEGACY_EVK_LR20XX` | `ON` or `OFF` (see Compilation)                |

## Compilation

Target: STM32L4 (e.g. NUCLEO-L476) with LR2021. Build with cmake/ninja; define `ROLE` and `RP_MARGIN_DELAY` for your application (receiver: ROLE=1, transmitter: ROLE=2).

**LoRa Plus EVK (LR2021):**
```bash
rm -Rf build/ ; env CFLAGS="-DOPTIMIZED_SPI" cmake -L -S examples  -B build -DCMAKE_BUILD_TYPE=Debug -DBOARD=NUCLEO_L476 -DRAC_RADIO=lr2021 -DRP_MARGIN_DELAY=10 -UCMAKE_C_FLAGS -G Ninja; cmake --build build --target flrc_burst_tx flrc_burst_rx
```

**Non Public Legacy LR20XX Shield:**
```bash
rm -Rf build/ ; env CFLAGS="-DOPTIMIZED_SPI" cmake -L -S examples  -B build -DCMAKE_BUILD_TYPE=Debug -DBOARD=NUCLEO_L476 -DRAC_RADIO=lr2021 -DLEGACY_EVK_LR20XX=ON -DRP_MARGIN_DELAY=10 -UCMAKE_C_FLAGS -G Ninja; cmake --build build --target flrc_burst_tx flrc_burst_rx
```

Flash `flrc_burst_rx` on the receiver and `flrc_burst_tx` on the transmitter. You can adapt parameters in `apps_configuration.h`.
**Example of `openocd` command to flash:**
```bash
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "adapter serial <SERIAL_NUMBER>" -c "program build/flrc_burst_rx verify reset exit"
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "adapter serial <SERIAL_NUMBER>" -c "program build/flrc_burst_tx verify reset exit"
```

## Usage

1. Flash the receiver image on one device and the transmitter image on another (NUCLEO-L476 + LR2021 / LoRa Plus EVK).
2. Receiver listens for a WOR sync frame, then the FLRC burst.
3. Transmitter: press the user button or use the application trigger to start a burst.

## Expected Output

## Limitations

- STM32L4 only in this example.
- Not currently running in CMAKE_BUILD_TYPE Release or MinSizeRel
- Using a work in progress rework of the spi transfer system
- Single transfer direction; basic send API, subject to change.
