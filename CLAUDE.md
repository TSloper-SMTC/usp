# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

USP (Unified Software Platform) is a radio abstraction layer for LoRa/LoRaWAN applications. It provides priority-based radio access scheduling across multiple modulations (LoRa, FSK, LR-FHSS, FLRC) and integrates LoRa Basics Modem (LBM) v4.9.0.

**Current version**: v1.1.1 (USP repo; experimental, not for production).

## Build Commands

CMake + Ninja build system. Always clean build directory first.

**Build a single example (e.g. periodical_uplink for STM32L476 with LR2021):**
```bash
rm -Rf build/
cmake -L -S examples -B build -DCMAKE_BUILD_TYPE=MinSizeRel -DBOARD=NUCLEO_L476 -DRAC_RADIO=lr2021 -G Ninja
cmake --build build --target periodical_uplink
```

**Build all examples:**
```bash
cmake --build build --target all_examples
```

**Build for native Linux with virtual radio (no hardware needed):**
```bash
rm -Rf build/
cmake -S examples -B build -DBOARD=LINUX -DRAC_RADIO=udp_pf -DCMAKE_BUILD_TYPE=MinSizeRel -G Ninja
cmake --build build --target periodical_uplink
```

**Build for ARM Linux (Raspberry Pi) with LR2021:**

> On Raspberry Pi, use `-DBOARD=LINUX`. `LINUX_ARM`/`LINUX_ARM64` are for cross-compilation from an x86 host only.

```bash
rm -Rf build/
cmake -S examples -B build -DBOARD=LINUX -DRAC_RADIO=lr2021 -DCMAKE_BUILD_TYPE=MinSizeRel -G Ninja
cmake --build build --target periodical_uplink
```

**Cross-compile for 32-bit ARM Linux (from x86 host):**
```bash
rm -Rf build/
cmake -S examples -B build -DBOARD=LINUX_ARM -DRAC_RADIO=lr2021 -DCMAKE_BUILD_TYPE=MinSizeRel -G Ninja
cmake --build build --target periodical_uplink
```

**Cross-compile for 64-bit ARM Linux (from x86 host):**
```bash
rm -Rf build/
cmake -S examples -B build -DBOARD=LINUX_ARM64 -DRAC_RADIO=lr2021 -DCMAKE_BUILD_TYPE=MinSizeRel -G Ninja
cmake --build build --target periodical_uplink
```

### Required CMake Variables

- `-DBOARD=` : `NUCLEO_L476`, `NUCLEO_L073`, `LINUX`, `LINUX_ARM`, `LINUX_ARM64`, `FPB_RA0E2`
- `-DRAC_RADIO=` : `sx1261`, `sx1262`, `sx1268`, `lr1110`, `lr1120`, `lr1121`, `lr2021`, `lr2022`, `udp_pf`
- `-DAPP=` (optional): uppercase app name (e.g. `PERIODICAL_UPLINK`, `HW_MODEM`, `GEOLOCATION`). Specifying `-DAPP` for `HW_MODEM`/`GEOLOCATION` enables most LBM features; for others it sets minimal config.

### Flashing (STM32)

```bash
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/<target> verify reset exit"
```
Add `-c "adapter serial <SERIAL>"` before `-c "program ..."` when multiple ST-LINKs are connected.

### Flashing (FPB-RA0E2)

Uses J-Link (the FPB-RA0E2 has an on-board J-Link OB). Device ID is `R7FA0E209`.

```bash
cat > /tmp/jlink_flash.jlink << 'EOF'
loadbin build/radio_test_cli.bin, 0x00000000
r
g
q
EOF
JLinkExe -device R7FA0E209 -if SWD -speed 4000 -autoconnect 1 -CommandFile /tmp/jlink_flash.jlink
```

### Toolchain Requirements

- GCC >= 13.3, CMake >= 3.28, Ninja >= 1.11, OpenOCD >= 0.12 (for STM32), JLinkExe (for RA0E2)

## Architecture

### Core Libraries (`smtc_rac_lib/`)

- **RAC (Radio Access Component)** - Main API (`smtc_rac_api/smtc_rac_api.h`). Transaction-based radio operations with 5 priority levels (VERY_HIGH to VERY_LOW). Key functions: `smtc_rac_open_radio()`, `smtc_rac_submit_radio_transaction()`, `smtc_rac_close_radio()`.
- **Radio Planner** (`radio_planner/`) - Core scheduling engine with hook-based priority system managing task queuing and preemption.
- **RAL** (`smtc_ral/`) - Low-level hardware abstraction for radio families.
- **RALF** (`smtc_ralf/`) - Radio family abstraction format layer.
- **Radio Drivers** (`radio_drivers/`) - Chipset drivers for LR20xx, LR11xx, SX126x.

### Protocol Stack (`protocols/lbm_lib/`)

LoRa Basics Modem v4.9.0 - LoRaWAN stack (Classes A/B/C, multicast, relay, FUOTA). Public API in `smtc_modem_api/smtc_modem.h`.

### HAL Implementations (`examples/smtc_hal_*/`)

Platform-specific code (SPI, GPIO, timers, flash, RNG):
- `smtc_hal_l4/` - STM32L4 (NUCLEO-L476RG) - primary validated platform
- `smtc_hal_l0_LL/` - STM32L0 (NUCLEO-L073RZ)
- `smtc_hal_linux/` - Linux/Raspberry Pi
- `smtc_hal_ra0e2/` - Renesas FPB-RA0E2

### Example Applications (`examples/main_examples/`)

Examples include `periodical_uplink`, `ping_pong`, `ranging_demo`, `packet_error_rate`, `packet_error_rate_fsk`, `packet_error_rate_flrc`, `lrfhss`, `cad`, `spectral_scan`, `hw_modem`, `geolocation`, etc. Directory names use `_example` suffix (e.g. `ping_pong_example/`). Each has `main_<name>.c` and `app_<name>.c/.h`.

### radio_test_cli (Primary Active Example)

Standalone interactive RF test CLI. No RAC/RAL/LBM dependencies — talks directly to radio drivers. Build with `--target radio_test_cli` using any standard build command above.

**Supported boards:** LINUX, LINUX_ARM, LINUX_ARM64, NUCLEO_L476, NUCLEO_L073, FPB_RA0E2. Zephyr: Xiao nRF54L15, nRF54L15 DK (via usp_zephyr module).

**Flash budgets:** ~97 KB on L476 (9%), ~98 KB on L073 (50%), ~99 KB on RA0E2 (76%). Fits on all three.

Version is tracked in `examples/main_examples/radio_test_cli/version.h` — update all four defines there when bumping. The user manual (md/html/pdf) must be updated to match.

**User manual editing rules:**
- The HTML file (`radio_test_cli_user_manual.html`) is **hand-crafted** with extensive CSS styling (Semtech teal theme, page layout, headers/footers, table formatting). It is the authoritative source for PDF generation.
- **NEVER regenerate the HTML using pandoc or any markdown-to-HTML converter.** This destroys all styling. Edit the HTML directly when adding or updating content.
- The markdown file (`.md`) is a content reference only — it is NOT used to generate the HTML.
- After editing the HTML, regenerate the PDF with `weasyprint` (see below).
- When adding new sections, match the existing HTML structure and CSS classes (e.g. `<div class="cmd-syntax">`, `<div class="callout-warn">`).

**Build for FPB-RA0E2 (baremetal):**
```bash
rm -Rf build/
env CFLAGS="-DBSP_CFG_HEAP_BYTES=0x400" \
  cmake -S examples -B build -DCMAKE_BUILD_TYPE=MinSizeRel -DBOARD=FPB_RA0E2 -DRAC_RADIO=lr2021 -G Ninja
cmake --build build --target radio_test_cli
```
The `CFLAGS` heap override is required on RA0E2 only — newlib's `printf` needs heap for its stdio buffer. The `#ifndef` guard in `bsp_cfg.h` allows this override without affecting other examples.

**Serial terminal setup (baremetal):** Use Tera Term with **Receive: CR** mode. The MCU `_write` stub translates `\n`→`\r\n` for printf output, while linenoise sends raw `\r` for in-place line editing. These two paths require CR-only receive mode to work correctly together.

Generate user manual PDF:
```bash
cd examples/main_examples/radio_test_cli/doc
weasyprint radio_test_cli_user_manual.html radio_test_cli_user_manual.pdf
```

## radio_test_cli Display Conventions

When adding or modifying CLI output in radio_test_cli, follow these rules for consistency:

- **Disabling a feature:** Always use `off` (not `none`, `disabled`, etc.). Input may accept aliases like `none` for convenience, but display must show `off`.
- **Byte counts:** Use `<N> bytes` (e.g. `2 bytes`, `4 bytes`), not `<N>B` or bare numbers.
- **Hex values:** Always prefix with `0x` (e.g. `0x34`, `0xED592398`).
- **Syncword index display:** Use `sw1`, `sw2`, `sw3` prefix style (not bare `1`, `2`, `3`).
- **Units in status lines:** Always include the unit (`bits`, `bytes`, `symbols`, `kbps`, `kHz`, `dBm`, `ms`, `µs`).
- **Parameter names shared across modulations** (`preamble`, `cr`, `crc`, `header`, `syncword`): The active modulation module owns the parameter. Each modulation defines its own valid values and display format independently.

## Key Configuration

**Logging profiles** (cmake): `RAC_LOG_PROFILE` / `RAC_LIB_LOG_PROFILE` = `DEFAULT`, `MINIMAL`, `VERBOSE`, `ALL`, `OFF`

**LBM features** (cmake): `LBM_REGIONS`, `LBM_CLASS_B`, `LBM_CLASS_C`, `LBM_GEOLOCATION`, `LBM_FUOTA`, `LBM_RELAY_TX/RX`, `LBM_STORE_AND_FORWARD`

**LoRaWAN credentials** (via CFLAGS with `-UCMAKE_C_FLAGS`):
```bash
env CFLAGS="-DMODEM_EXAMPLE_REGION=SMTC_MODEM_REGION_EU_868 -DUSER_LORAWAN_DEVICE_EUI='{...}' -DUSER_LORAWAN_JOIN_EUI='{...}' -DUSER_LORAWAN_APP_KEY='{...}'" cmake ...
```

**Virtual radio env vars**: `UDP_PF_SERVER_ADDR`, `UDP_PF_SERVER_PORT`, `UDP_PF_GATEWAY_EUI`

## Known Limitations

See `doc/KNOWN_LIMITATIONS.md`. Key issues:
- FLRP/FLRC burst is experimental
- "task schedule aborted because in the past -1": increase `RP_MARGIN_DELAY` from 8 to 12 in `smtc_rac_lib/radio_planner/src/radio_planner_types.h`
- LR20xx BW 7/10/15/20 causes division by zero
- NUCLEO-L073RZ: limited HAL (missing `hal_flash_get_page_size`), LBM examples overflow 192KB flash. radio_test_cli works (50% flash)
- FPB-RA0E2: limited flash (128 KB), LBM examples won't fit

## Zephyr Workspace

usp_zephyr repo cloned at `~/zephyr-workspaces/usp-zephyr-workspace/usp_zephyr`. Zephyr SDK at `~/zephyr-sdks/zephyr-sdk-0.17.4`. Virtual environment at `~/zephyr-environments/zephyr-v4.2.0-sdk0.17.4`.

## Sister Repository

Fixes are maintained in parallel in `usp-adc` (remote: `git@github.com:TSloper-SMTC/usp-adc.git`, SSH). Use `/port-to-usp-adc` skill to cherry-pick commits. Local mirror branch `apply-fixes-usp-adc` tracks `usp-adc/feature/radio-test-cli`. Note: `usp-adc` has only the PDF in doc (no .md/.html) — drop those files when resolving cherry-pick conflicts.

## Key Documentation

- `doc/usp_porting_guide.md` - Porting to new MCU/radio
- `doc/usp_lbm_porting_guide.md` - Migrating from LBM to USP
- `smtc_rac_lib/README.md` - RAC API details
- `protocols/lbm_lib/README.md` - LBM documentation
- `examples/main_examples/README.md` - Example guide
- `~/downloads/61979758.LR2021_V1_1_datasheet.pdf` - LR2021 datasheet v1.1

## Claude Code Skills

Three project skills are configured in `.claude/skills/`:
- `/version-bump` — update `version.h` + user manual md/html + regenerate PDF
- `/port-to-usp-adc` — cherry-pick commits to usp-adc sister repo (handles doc conflict auto-resolution)
- `/deploy-rpi5-node02` — SCP built binary/binaries to `tim@rpi5-node02.local:~/downloads/`

## Git Push Discipline

**This branch (`feature/radio-test-cli`) is tracked by customers.** History cannot be rewritten after pushing. Every push is permanent and visible to downstream consumers.

### Before any `git push`, challenge the user with these questions:

1. **Is this a version-bump-worthy change?** If it doesn't warrant at least a patch bump (v0.2.x), bundle it with the next change that does. Doc-only and CLAUDE.md changes do not warrant a push on their own.
2. **Is the feature complete and tested on hardware?** Half-done features should not be pushed. Wait until verified on a real board.
3. **Are there fixup commits that should be squashed?** Before pushing, always check for local commits that are fixing previous local commits (e.g., "fix typo", "oops", "revert + redo"). Squash these with `git rebase -i` so only clean, intentional commits reach the remote.
4. **Would a customer benefit from pulling right now?** If nobody gains anything by pulling today, wait.

### Push criteria (at least one must be true):

- Bug fix that affects users currently blocked on the issue
- New CLI command, mode, or board support — complete and hardware-tested
- Version bump with meaningful changelog
- Critical fix needed for a customer demo or certification test

### Do NOT push for:

- CLAUDE.md or internal doc changes alone
- Mid-implementation work that compiles but isn't tested
- Refactors that change no user-visible behavior
- "Fix the fix" sequences — squash locally first

### Pre-push workflow:

```bash
# 1. Check for squashable commits
git log --oneline origin/feature/radio-test-cli..HEAD

# 2. If multiple commits, squash related ones
git rebase -i origin/feature/radio-test-cli

# 3. Bump version if warranted (/version-bump)

# 4. Push
git push
```

### When the user says "push" or "let's push":

Do NOT just run `git push`. Instead, review the unpushed commits, apply the criteria above, and either confirm the push makes sense or explain why waiting might be better. Offer to squash if there are fixup commits. The user has explicitly asked for this gatekeeping.

## Zephyr RTOS Support (feature/zephyr-support branch)

radio_test_cli supports Zephyr via the usp_zephyr module import mechanism. Shared CLI code is
compiled for all three platforms (Linux, baremetal, Zephyr) from the same source files.

**Platform abstraction:** `platform.h` + `platform_linux.c` / `platform_baremetal.c` / `platform_zephyr.c`
selected by the build system. No `#ifdef` for timing — `platform_sleep_us()` and `platform_time_ms()`.
The old `mcu_compat.h` is deleted.

**Zephyr files staged in `usp_zephyr/`** at repo root (copy to real usp_zephyr repo for building):
- `radio_test_cli/CMakeLists.txt` — auto-detects radio from DT (LR20xx/LR11xx/SX126x)
- `radio_test_cli/src/zephyr_hal_bridge.c` — radio context from DT, DIO IRQ bridge
- `radio_test_cli/src/smtc_hal_zephyr/` — Zephyr HAL implementations

**Key architectural decisions:**
- radio_test_cli bypasses RAC — does NOT use `SMTC_SW_PLATFORM_INIT()` or `CONFIG_USP_MAIN_THREAD`
- Radio context is `const struct device*` from `DT_CHOSEN(zephyr_lorawan_transceiver)` — passed via `chip_set_radio_context()`
- DIO IRQ bridge: `lora_transceiver_board_attach_interrupt()` → `hal_gpio_fire_irq()` → stored callback in GPIO HAL pin table
- `modem_pinout_zephyr.h` hardcodes pin values (not enum names) due to include-path conflict with `smtc_hal_linux` headers added globally by usp_zephyr's subsys. `_Static_assert` validates they match.
- Files NOT compiled on Zephyr (usp_zephyr module provides them): `stack_hal_shim.c`, `smtc_duty_cycle.c`, `ral_lr20xx_bsp.c`, `radio_utilities.c`, `smtc_hal_led.c`
- `ral_lr20xx_bsp_get_xosc_trim()` stub in bridge (usp_zephyr BSP lacks it — DT binding needs extending)

**Build (Xiao nRF54L15 + LR2021):**
```bash
west build --pristine --board xiao_nrf54l15/nrf54l15/cpuapp --shield semtech_wio_lr2021 samples/usp/sdk/radio_test_cli
```

**Build (nRF54L15 DK + LR2021 via mbed adapter):**
```bash
west build --pristine --board nrf54l15dk/nrf54l15/cpuapp --shield semtech_nrf54l15dk_mbed_interface --shield semtech_mbed_wio_interface --shield semtech_wio_lr2021 samples/usp/sdk/radio_test_cli
```

**Dev workflow:** Symlink this repo as the USP module for instant iteration:
```bash
cd ~/zephyr-workspaces/usp-zephyr-workspace
rm -rf modules/lib/usp
ln -s ~/semtech/usp/radio_test_cli modules/lib/usp
```

**Zephyr prj.conf notes:**
- `CONFIG_NEWLIB_LIBC_FLOAT_PRINTF=y` for float printf (not `-u _printf_float`)
- `CONFIG_LOG_MODE_IMMEDIATE=y` to prevent boot banner appearing after CLI banner
- `CONFIG_SHELL=n` — we use linenoise, not Zephyr shell
- `CONFIG_HEAP_MEM_POOL_SIZE=8192` for linenoise history allocation

## Testing

No automated test suite. Validation is hardware-based (radio TX/RX on real boards).
