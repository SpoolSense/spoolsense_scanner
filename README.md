<p align="center">
  <img src="docs/spoolsense-logo.png" width="200" alt="SpoolSense">
</p>

# SpoolSense Scanner

## Overview
SpoolSense Scanner is an ESP32-based NFC scanner designed for managing 3D printer filament spools using NFC tags. It integrates with the SpoolSense ecosystem and supports multiple NFC tag formats including OpenPrintTag, TigerTag, OpenTag3D, and OpenSpool.

The scanner allows users to tap a filament spool to identify it, retrieve metadata from the NFC tag, and trigger external automation or spool tracking workflows. A built-in web UI at `spoolsense.local` provides tag reading, writing, device configuration, and over-the-air firmware updates — no apps or external tools required.

## Documentation

Full setup guides, hardware wiring, printer compatibility, and deeper integration walkthroughs live on **[spoolsense.org](https://spoolsense.org)**:

- [What is SpoolSense](https://spoolsense.org/intro/) — start here
- [Web Flasher](https://spoolsense.org/installation/web-flasher/) — flash from your browser, no installs
- [Printer Compatibility](https://spoolsense.org/resources/compatibility/) — Klipper, AFC, Toolchanger, Snapmaker U1, Prusa, Creality
- [Snapmaker U1 Integration](https://spoolsense.org/installation/snapmaker-u1/)
- [Bambu Lab AMS Integration](https://spoolsense.org/installation/bambu-ams/)
- [Tag Formats Reference](https://spoolsense.org/resources/tag-formats/)

This README is a high-level overview. For setup, troubleshooting, and full integration guides, head to the docs site.

## Web UI

After connecting to WiFi, open **`http://spoolsense.local`** from any browser on your local network. The landing page provides access to all tools:

- **Tag Reader** — Auto-detects tag format (OpenPrintTag, TigerTag, OpenTag3D, OpenSpool, generic UID) and displays all data read-only
- **OpenPrintTag Writer** — Write filament data to ISO15693 tags using the OpenPrintTag format
- **TigerTag Writer** — Write filament data to NTAG213/215 tags using the TigerTag binary format
- **OpenTag3D Writer** — Write filament data to NTAG215/216 tags using the OpenTag3D NDEF format
- **OpenSpool Writer** — Write filament data to NTAG215/216 tags using the OpenSpool NDEF JSON format
- **Configuration** — Change WiFi, MQTT, Spoolman, and hardware settings from the browser
- **Firmware Update** — Check for new firmware versions from GitHub, view release notes, and update over WiFi with one click

<p align="center">
  <a href="docs/writerwebui2.png"><img src="docs/writerwebui2.png" width="280" alt="Tag writer form"></a>
  &nbsp;&nbsp;
  <a href="docs/writerwebui1.png"><img src="docs/writerwebui1.png" width="280" alt="Tag writer write progress"></a>
</p>

## Features

* **Multi-format NFC Support:** Read and write OpenPrintTag (ISO15693), TigerTag (ISO14443A NTAG213/215), OpenTag3D (ISO14443A NTAG215/216), and OpenSpool (ISO14443A NTAG215/216) tags. Bambu Lab MIFARE Classic tags are read with full decryption (vendor, material, color, weight, temperatures, dry info). NFC+ (UID-only) tags are detected for Spoolman registration by UID.
* **Dual NFC Reader Support:** PN5180 (ISO15693 + ISO14443A, all tag formats) and PN532 (ISO14443A only). Selected at runtime via NVS — both compiled into a single binary.
* **3x4 Matrix Keypad (optional):** Scan a spool, type a tool number, press # to assign via Moonraker's ASSIGN_SPOOL macro. For toolchanger and multi-tool setups.
* **Built-in Tag Writer:** Write filament metadata directly from the web UI — material, manufacturer, weight, color, density, diameter, temperatures, and more. Separate writer pages for each tag format. Material and brand fields are type-to-search with auto-fill for temperatures and density. A Read button on each writer page loads an existing tag's data for re-writing.
* **NFC+ Registration:** Register plain NFC tags (NTAG215, etc.) in Spoolman using the tag's UID as identifier. No data written to the tag — fill in filament details in the web UI and create the Spoolman entry directly.
* **Tag Reader:** Auto-detect any supported tag format and display all data in a clean read-only view.
* **Spoolman Enrichment:** When a smart tag is scanned, the reader page shows Spoolman-sourced fields (remaining weight, bed temp, spool ID) with blue badges. Writer pages include an editable Spoolman Enrichment section for data Spoolman can store that the tag format cannot — saved to Spoolman on write with vendor/filament deduplication and confirmation.
* **Web Configuration:** Change WiFi, MQTT, Spoolman, and hardware settings from the browser at `spoolsense.local/config`. Settings saved to NVS and persist across OTA updates.
* **OTA Firmware Updates:** Check for updates from GitHub releases with release notes, one-click download and flash, or manual .bin upload. Dual partition layout with automatic rollback on failed update.
* **MQTT Integration:** Publishes spool state to MQTT for use by the SpoolSense middleware or any MQTT subscriber. Includes optional Home Assistant auto-discovery.
* **Automatic Spoolman Registration:** When a tagged spool is scanned, the scanner automatically creates or updates the spool entry in Spoolman — no manual data entry needed. If a tag is re-written with different filament, the old spool is automatically archived and a new one created. Requires the `nfc_id` extra field in Spoolman (the [installer](https://github.com/SpoolSense/spoolsense-installer) can create this for you).
* **Device ID on Landing Page:** The scanner's unique device ID is displayed prominently on the home page for easy middleware configuration.
* **PrusaLink Integration (Experimental):** Automatic print monitoring and filament tracking for Prusa printers via PrusaLink API. Pre-print filament mismatch and temperature warnings. Multi-tool XL support. Enable via web config at `spoolsense.local/config`. **Looking for testers — if you have a Prusa printer, please try it and report issues.**
* **Snapmaker U1 Direct-Mode Integration:** Push spool data directly to a Snapmaker U1 toolchanger via the extended firmware's external filament-detection API. All six tag formats supported. One scanner per toolhead channel; multi-scanner setups supported. Requires paxx12 Extended Firmware. [Setup guide →](https://spoolsense.org/installation/snapmaker-u1/)
* **TFT Display (optional):** ST7789 240×240 (square) or GC9A01 (round) color display with filament color swatch, tag format labels, and spool info. Driver selectable at runtime via web config. Alternative to the 16x2 LCD.
* **LCD Display (optional):** 16x2 I2C LCD for device status, NFC scan results, and system information.
* **Status LED:** Visual feedback for boot, WiFi, tag detection, write progress, and filament color display.

## Supported Tag Formats

| Format | Protocol | Support |
|--------|----------|---------|
| OpenPrintTag | ISO15693 | Full read/write — CBOR/NDEF filament data, weight tracking, Spoolman sync |
| TigerTag | ISO14443A (NTAG213/215) | Full read/write — binary format with material, brand, color, weight, temperatures |
| OpenTag3D | ISO14443A (NTAG215/216) | Full read/write — NDEF binary format with material, color, weight, density, temperatures, extended fields |
| OpenSpool | ISO14443A (NTAG215/216) | Full read/write — NDEF JSON format with brand, material, color, nozzle temperatures |
| Bambu Lab | ISO14443A (MIFARE Classic) | Full read — HKDF-derived MIFARE Classic keys decrypt material, vendor, color, weight, temperatures, dry temp/time |
| UID-only (NTAG215, etc.) | ISO14443A | UID detected and published; middleware looks up spool by UID |

- OpenPrintTag spec: [openprinttag.org](https://openprinttag.org/generator/)
- TigerTag spec: [TigerTag RFID Guide](https://github.com/TigerTag-Project/TigerTag-RFID-Guide)
- OpenTag3D spec: [OpenTag3D](https://opentag3d.com/)
- OpenSpool spec: [OpenSpool](https://github.com/spuder/OpenSpool)

## Requirements

- **WiFi network** — the scanner connects to your local network
- **MQTT broker** — required for middleware and Home Assistant integration (e.g. [Mosquitto](https://mosquitto.org/)). Home Assistant users typically already have this via the Mosquitto add-on. Not needed if using the scanner as a standalone tag reader/writer with direct Spoolman sync.
- **Spoolman** (optional) — for automatic spool tracking and filament management

## Installation

### Option 1: SpoolSense Installer (recommended)

The installer handles everything — firmware download, WiFi/MQTT/Spoolman configuration, and flashing:

```bash
curl -sL https://raw.githubusercontent.com/SpoolSense/spoolsense-installer/main/install.sh -o /tmp/install.sh && bash /tmp/install.sh
```

Configuration is stored in NVS (non-volatile storage) and survives OTA firmware updates.

### Option 2: Build from Source

1. Install [PlatformIO](https://platformio.org/)
2. Copy the example config: `cp include/UserConfig.example.h include/UserConfig.h`
3. Edit `include/UserConfig.h` with your settings (WiFi, MQTT, Spoolman, etc.)
4. Flash:
   - **WROOM:** `pio run -e esp32dev -t upload`
   - **S3-Zero:** `pio run -e esp32s3zero -t upload`
   - **C6 DevKitC-1:** `pio run -e esp32c6 -t upload`
   - **Seeed XIAO ESP32-C6:** `pio run -e seeed_xiao_esp32c6 -t upload`
   - **C5 DevKitC-1:** `pio run -e esp32c5 -t upload`
5. **Important for OTA updates:** Run the installer in "Config only" mode to write your settings to NVS. Without this, OTA updates will overwrite your compiled-in settings with defaults.
   ```bash
   curl -sL https://raw.githubusercontent.com/SpoolSense/spoolsense-installer/main/install.sh -o /tmp/install.sh && bash /tmp/install.sh
   ```
   Select **"Config only (source builds)"** when prompted.

## Web UI Access

Once the scanner is running, open **`http://spoolsense.local`** in your browser.

1. **Retrieve your Scanner ID** — Your device ID is displayed on the landing page. You'll need this when configuring the SpoolSense middleware.
2. **Test your scanner** — Try reading a tag on the Tag Reader page, or write a test tag using any of the writer pages.
3. **Firmware updates** — Navigate to `spoolsense.local/update` to check for new versions, view release notes, and update over WiFi with one click. Manual `.bin` upload is also available.

# Hardware Setup

## Hardware Needed
*   NFC Reader (one of):
    - **PN5180** (recommended) — ISO15693 + ISO14443A, all tag formats — [AITRIP PN5180](https://www.amazon.com/dp/B0BXY1Y7PX) (tested)
    - **PN532** — ISO14443A only (no SLIX2/OpenPrintTag on ISO15693). Cheaper and smaller.
*   ESP32 (one of):
    - **ESP32-WROOM** — [Freenove ESP32-WROOM](https://www.amazon.com/dp/B0C9THDPXP) (tested). Recommended if using LCD + keypad.
    - **ESP32-S3-Zero / S3-Zero-M** — Smaller form factor with onboard WS2812 RGB LED. M variant has pre-soldered pin headers.
    - **ESP32-S3-DevKitC-1-N16R8** — Larger flash + PSRAM, separate SPI buses for PN5180 and TFT.
    - **ESP32-C3 SuperMini** — Smallest variant. NFC reader + LCD + LED only (no TFT/keypad).
    - **Seeed Studio XIAO ESP32-C6** — Compact 4MB ESP32-C6 board for NFC-only builds with PN5180.

    See [spoolsense.org/getting-started/choose-board](https://spoolsense.org/getting-started/choose-board/) for trade-offs and recommendations.
*   USB-C cable
*   Jumper wires: female-to-female Dupont wires (8 minimum, more if adding extras)
*   LCD Screen: [16x2 I2C LCD](https://a.co/d/dryhwvd) (optional)
*   Status LED: SK6812 RGBW (WROOM, optional external) or onboard WS2812 RGB (S3-Zero, built in)
*   3x4 Matrix Keypad: [membrane keypad](https://www.amazon.com/dp/B0DZ26VVR7) (optional, for toolchanger tool assignment)

## Enclosures

As of March 2026 only a few printable cases exist. We need the community's help! If you design one, [open an issue](https://github.com/SpoolSense/spoolsense_scanner/issues) with photos and files.

See [spoolsense.org/contributing/enclosure-design](https://spoolsense.org/contributing/enclosure-design/) for design guidelines and what's needed.

### Designs Still Needed

- **Standalone scanner case (ESP32-WROOM-32 + PN5180)** — wall or desk mount with flat NFC placement area
- **BoxTurtle AFC lane mount** — bracket or tray that positions the PN5180 antenna alongside an AFC BoxTurtle lane for tags on loaded spools

If you have a design, [open an issue](https://github.com/SpoolSense/spoolsense_scanner/issues) with photos and files, or submit a PR to `usermods/`.

---

## Wiring — ESP32-WROOM-32

**PN5180 NFC Module (SPI, right side of ESP32 top to bottom, skipping D12):**

| PN5180 Pin | ESP32 Pin | Direction | Notes |
|------------|-----------|-----------|-------|
| RST        | D13       | Output    | Hardware reset (active low) |
| *(skip)*   | *D12*     | *—*       | *Strapping pin, skip* |
| NSS        | D14       | Output    | SPI chip select (active low) |
| MOSI       | D27       | Output    | SPI data to PN5180 |
| MISO       | D26       | Input     | SPI data from PN5180 |
| SCK        | D25       | Output    | SPI clock |
| BUSY       | D33       | Input     | SPI flow control |
| GPIO       | D32       | Input     | Card detection (future use) |
| IRQ        | D35       | Input     | Interrupt, active HIGH (input-only pin, no pull-up) |
| AUX        | D34       | Input     | Auxiliary monitoring (input-only pin, no pull-up) |
| REQ        | —         | —         | **Not connected.** Only needed for PN5180 firmware updates. |
| VIN        | 5V        | Power     | |
| GND        | GND       | Power     | |

> **Note:** D35 and D34 are input-only pins on the ESP32 (no internal pull-up). D12 is a strapping pin and is skipped.

**16x2 I2C LCD (optional):**

| LCD Pin | ESP32 Pin |
|---------|-----------|
| GND | GND |
| VCC | 5V |
| SDA | GPIO 23 |
| SCL | GPIO 22 |

**SK6812 RGBW Status LED (optional):**

> A single SK6812 RGBW LED module is recommended. Many small breakout boards include the necessary capacitor and resistor already. If using a bare LED, a ~330 resistor on the data line is recommended for signal stability. A common ground between the ESP32 and the LED is required.

| LED Pin | ESP32 Pin |
|---------|-----------|
| VCC | 5V or 3.3V |
| GND | GND |
| DIN | GPIO 4 |

## Wiring — ESP32-S3-Zero

The S3-Zero has a smaller pin count. The PN5180 and LCD (if used) share the same 5V and GND pins — daisy-chain or splice the power wires to connect both devices.

**PN5180 NFC Module (SPI):**

| PN5180 Pin | S3-Zero Pin | Direction | Notes |
|------------|-------------|-----------|-------|
| RST        | GPIO 4      | Output    | Hardware reset (active low) |
| NSS        | GPIO 5      | Output    | SPI chip select (active low) |
| MOSI       | GPIO 6      | Output    | SPI data to PN5180 |
| MISO       | GPIO 7      | Input     | SPI data from PN5180 |
| SCK        | GPIO 8      | Output    | SPI clock |
| BUSY       | GPIO 9      | Input     | SPI flow control |
| GPIO       | GPIO 10     | Input     | Card detection (future use) |
| IRQ        | GPIO 11     | Input     | Interrupt, active HIGH |
| AUX        | GPIO 12     | Input     | Auxiliary monitoring (future use) |
| REQ        | —           | —         | **Not connected.** |
| VIN        | 5V          | Power     | Shared with LCD (daisy-chain) |
| GND        | GND         | Power     | Shared with LCD (daisy-chain) |

**16x2 I2C LCD (optional):**

| LCD Pin | S3-Zero Pin |
|---------|-------------|
| GND | GND (shared with PN5180) |
| VCC | 5V (shared with PN5180) |
| SDA | GPIO 1 |
| SCL | GPIO 2 |

> **Note:** The LCD module needs 5V on VCC for display contrast. The I2C SDA/SCL lines run at 3.3V logic, which the PCF8574 backpack accepts without a level shifter.

**Status LED:** The S3-Zero, C6 DevKitC-1, and C5 DevKitC-1 have onboard addressable RGB LEDs (GPIO 21, 8, and 27 respectively) — no external LED or wiring needed. The XIAO ESP32-C6 has an onboard single-color active-low user LED on GPIO15. If compiling from source, enable it with `#define ENABLE_STATUS_LED 1` in `UserConfig.h`. The installer enables it by default.

**C5/C6 display support:** both targets support a shared-SPI TFT (write-only display plus either NFC reader on one bus). C6 is hardware-validated with a 3.5" ILI9488 landscape dashboard; C5 is compile-enabled via a build-time LovyanGFX patch (`scripts/patch_lovyangfx_c5.py`) and pending hardware validation. Keypad remains disabled on both targets. See `docs/shared-spi-bench-checklist.md` for wiring and validation status.

## Wiring — Seeed Studio XIAO ESP32-C6

The XIAO ESP32-C6 profile is intended for compact NFC-only builds with a PN5180 reader. It uses the XIAO hardware SPI pins and leaves TFT/keypad wiring disabled.

| PN5180 Pin | XIAO ESP32-C6 Pin | ESP32-C6 GPIO | Direction | Notes |
|------------|-------------------|---------------|-----------|-------|
| RST        | D7                | GPIO 17       | Output    | Hardware reset (active low) |
| NSS        | D3                | GPIO 21       | Output    | SPI chip select (active low) |
| MOSI       | D10               | GPIO 18       | Output    | SPI data to PN5180 |
| MISO       | D9                | GPIO 20       | Input     | SPI data from PN5180 |
| SCK        | D8                | GPIO 19       | Output    | SPI clock |
| BUSY       | D6                | GPIO 16       | Input     | SPI flow control |
| VIN        | 5V                | -             | Power     | PN5180 reader power |
| 3.3V       | 3V3               | -             | Power     | PN5180 logic supply |
| GND        | GND               | -             | Power     | Common ground |

> **Note:** Some PN5180 breakout boards require both 5V and 3.3V connected. If the reader self-test fails during initialization, confirm both power rails and common ground before changing pins.

**Serial:** The S3-Zero and Seeed XIAO ESP32-C6 use USB CDC — just plug in a USB-C cable, no external UART adapter needed.

# Configuration

## UserConfig.h (source builds only)
1. Copy the example config: `cp include/UserConfig.example.h include/UserConfig.h`
2. Edit `include/UserConfig.h` and fill in your settings:
   - WiFi SSID and password
   - MQTT broker host, port, and credentials
   - Spoolman URL (optional)
   - Automation mode (`0` = Self Directed, `1` = Controlled by HA)
   - Board selection (normally supplied by the selected PlatformIO environment)
   - Optional hardware: LCD and status LED (see below)
3. Flash the firmware:
   - **WROOM:** `pio run -e esp32dev -t upload`
   - **S3-Zero:** `pio run -e esp32s3zero -t upload`
   - **C6 DevKitC-1:** `pio run -e esp32c6 -t upload`
   - **Seeed XIAO ESP32-C6:** `pio run -e seeed_xiao_esp32c6 -t upload`
   - **C5 DevKitC-1:** `pio run -e esp32c5 -t upload`

> **Note:** If you used the SpoolSense Installer, configuration is stored in NVS and you don't need `UserConfig.h`.

## Optional: LCD (source builds only)

The 16x2 I2C LCD is fully optional. If compiling from source, set in `UserConfig.h`:

```cpp
#define ENABLE_LCD 0
```

When disabled, no I2C bus is initialized, no LCD task is started, and no LCD code is compiled into the binary. Set to `1` if you have the LCD connected. The installer disables it by default.

## Optional: Status LED (source builds only)

The status LED is optional. If compiling from source, set in `UserConfig.h`:

```cpp
#define ENABLE_STATUS_LED 1   // 1 = enabled, 0 = disabled
```

- **ESP32-S3-Zero:** Uses the onboard WS2812 RGB LED on GPIO 21 — no external wiring needed.
- **ESP32-WROOM-32:** Requires an external SK6812 RGBW LED wired to GPIO 4 (see wiring table above).

Pin mapping is automatic via `BoardPins.h` — no need to configure the pin manually.

## Status LED Reference

| Event | LED Behavior |
|------|--------------|
| Booting | White (RGBW W channel on WROOM, RGB white on S3) |
| WiFi connected | Cyan |
| Ready | Blue |
| Tag detected | 3 white flashes |
| Valid spool tag | Filament color (solid); breathing if ≤100g remaining |
| Generic/UID-only tag | 3 white flashes, then off |
| Blank/invalid tag | Red flash, then off |
| Tag removed | Color persists — stays at last scanned filament color |
| Write success | Green flash, then restores filament color |
| Write failure | Red flash, then restores filament color |

## Community

[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.gg/JYFQQQR5F)

Join the [SpoolSense Discord](https://discord.gg/JYFQQQR5F) for real-time help, build photos, and community discussion.

## Contributing & Help Wanted

> **This project is currently in Alpha.** Testing is being done by myself and one other person. There will be bugs — please help!

**How you can help:**

- **Beta testers** — If you build one, I'd love to hear how it goes. Try it out and [open an issue](https://github.com/SpoolSense/spoolsense_scanner/issues) with any bugs or feedback you find.
- **Case design** — More enclosure options are welcome. Submit designs to `usermods/` via PR. See [spoolsense.org/builds/community-mods](https://spoolsense.org/builds/community-mods/) for existing designs.
- **Bug reports & feedback** — Even general impressions are helpful. If something doesn't work the way you'd expect, please open an issue.

## Credits

This project is derived from and inspired by the original **openprinttag_scanner** project created by Ryan C.

* Original project: https://github.com/ryanch/openprinttag_scanner

SpoolSense Scanner builds on that foundation while adapting the firmware for the **SpoolSense ecosystem**, adding features such as multi-format NFC support, a web-based tag writer and reader, TigerTag support, OTA firmware updates, and hardware profile support for multiple ESP32 variants.

Many thanks to the original author and contributors for the work that made this project possible.

### Libraries Used

* **PN5180 Library** by Andreas Trappmann — ISO15693 and ISO14443A NFC driver for the PN5180 module: https://github.com/ATrappmann/PN5180-Library/

## Specs Referenced
*   OpenPrintTag: https://openprinttag.org/generator/
*   TigerTag RFID Guide: https://github.com/TigerTag-Project/TigerTag-RFID-Guide
*   TigerTag SpoolmanDB: https://github.com/TigerTag-Project/TigerTag-RFID-Guide/tree/main/SpoolmanDB
*   OpenTag3D: https://opentag3d.com/
*   Spoolman API: https://donkie.github.io/Spoolman/#tag/filament/operation/Find_filaments_filament_get
