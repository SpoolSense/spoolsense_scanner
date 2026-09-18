# Contributing to SpoolSense Scanner

This guide covers setting up a development environment, compiling and uploading
the firmware, running tests, and contributing changes.

## Dependencies

The firmware uses the Arduino framework for the ESP32 family and is built with
PlatformIO. Its platform, framework version, libraries, build flags, and board
environments are declared in `platformio.ini`. PlatformIO downloads and manages
those dependencies automatically; do not install the Arduino core or individual
libraries by hand.

For normal firmware development you need:

- [Visual Studio Code](https://code.visualstudio.com/)
- The official PlatformIO IDE extension
- Git, for source control and repository-based dependencies
- A data-capable USB cable for uploading and serial monitoring
- The appropriate ESP32 and NFC hardware for hardware tests

The PlatformIO extension includes PlatformIO Core and manages its own portable
Python runtime. You do not need to install Python or PlatformIO separately.

Running the native automated tests also requires `make` and a C/C++ compiler.
On macOS, these are provided by the Xcode Command Line Tools:

```text
xcode-select --install
```

## Install PlatformIO in Visual Studio Code

Use [Visual Studio Code](https://code.visualstudio.com/) with the official
[PlatformIO IDE extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide).

1. Open the Extensions view in Visual Studio Code.
2. Search for **PlatformIO IDE** published by PlatformIO.
3. Select **Install**, then restart Visual Studio Code if requested.
4. Allow the extension to finish installing PlatformIO Core and its toolchains.

## Open the project

1. Clone this repository if you have not already done so.
2. In Visual Studio Code, choose **File > Open Folder**.
3. Open the repository root—the folder containing `platformio.ini`.
4. Allow PlatformIO to finish installing the project dependencies before
   building for the first time.

Open the repository folder directly rather than creating a new PlatformIO
project. The checked-in `platformio.ini` is the source of truth for platforms,
dependencies, build flags, and board environments.

## Create the local configuration

Copy `include/UserConfig.example.h` to `include/UserConfig.h`. You can do this
in the Visual Studio Code Explorer or from a terminal:

```text
cp include/UserConfig.example.h include/UserConfig.h
```

Edit `include/UserConfig.h` and configure:

- Wi-Fi SSID and password
- MQTT broker host, port, and credentials
- Spoolman URL, if used
- Automation mode
- Optional LCD, status LED, keypad, and dashboard features

`include/UserConfig.h` is ignored by Git because it may contain credentials.
Board selection is normally supplied by the PlatformIO environment rather than
changed in this file.

### Optional hardware settings

The 16x2 I2C LCD is controlled with:

```cpp
#define ENABLE_LCD 0  // 1 = enabled, 0 = disabled
```

When disabled, the I2C bus and LCD task are not initialized.

The status LED is controlled with:

```cpp
#define ENABLE_STATUS_LED 1  // 1 = enabled, 0 = disabled
```

The S3-Zero, C5 DevKitC-1, and C6 DevKitC-1 use their onboard addressable LED.
The XIAO ESP32-C6 uses its onboard single-color active-low user LED. The
ESP32-WROOM requires an external SK6812 RGBW LED wired as described in the main
README. Pin selection is handled by `include/BoardPins.h`.

## Select a build environment

Select the environment from the PlatformIO environment switcher in the Visual
Studio Code status bar.

| Board | PlatformIO environment |
|---|---|
| ESP32-WROOM / ESP32 DevKit | `esp32dev` |
| ESP32-S3-Zero / S3-Zero-M | `esp32s3zero` |
| ESP32-S3-DevKitC-1-N16R8 | `esp32s3devkitc` |
| ESP32-C3 SuperMini | `esp32c3` |
| ESP32-C6-DevKitC-1 | `esp32c6` |
| Seeed Studio XIAO ESP32-C6 | `seeed_xiao_esp32c6` |
| ESP32-C5-DevKitC-1 | `esp32c5` |

## Compile the firmware

Use the PlatformIO **Build** button in the status bar to compile the selected
environment.

PlatformIO's built-in terminal is available from **PlatformIO > Quick Access >
Miscellaneous > PlatformIO Core CLI**. It provides the `pio` command without a
separate command-line installation. To build a specific environment:

```text
pio run -e esp32dev
```

Replace `esp32dev` with the environment for your board. To compile every
supported environment, matching the firmware matrix used by CI:

```text
pio run
```

Build output is written beneath `.pio/build/<environment>/`. The application
image is `firmware.bin`.

## Upload to an ESP32

Connect the board with a data-capable USB cable, select its environment, and
use PlatformIO's **Upload** button. The equivalent command is:

```text
pio run -e esp32dev -t upload
```

PlatformIO normally detects the serial port automatically. If more than one
serial device is connected, list the ports and specify one explicitly:

```text
pio device list
pio run -e esp32dev -t upload --upload-port /dev/cu.usbmodemXXXX
```

Windows ports use names such as `COM3`; Linux ports commonly use
`/dev/ttyUSB0` or `/dev/ttyACM0`.

Some boards require holding **BOOT**, briefly pressing **RESET**, and releasing
**BOOT** when the upload begins. This is only necessary if automatic bootloader
entry fails.

### Configuration precedence and OTA updates

`UserConfig.h` provides the firmware's compiled defaults. At startup, the
scanner loads those defaults first and then applies any matching values stored
in NVS, one key at a time. A normal PlatformIO upload replaces the firmware but
does not erase NVS.

Consequently, changing and recompiling `UserConfig.h` does affect settings that
have never been saved to NVS. It does not replace an existing NVS value for the
same setting. Compile-time-only values still come from the newly built firmware.

There are three ways to manage runtime configuration:

1. **Web configuration (recommended for normal changes):** Open
   `http://spoolsense.local/config`, edit the settings, and select **Save &
   Reboot**. This writes directly to NVS; the installer is not required. If the
   configured Wi-Fi connection fails, connect to the `SpoolSense-XXXX` access
   point and open `http://192.168.4.1/config`.
2. **SpoolSense installer:** Run the installer and select **Config only (source
   builds)** to provision NVS without replacing your source-built firmware:

   ```text
   curl -sL https://raw.githubusercontent.com/SpoolSense/spoolsense-installer/main/install.sh -o /tmp/install.sh && bash /tmp/install.sh
   ```

3. **Return to compiled defaults:** Erase the board's flash, then upload the
   firmware again. This removes all NVS configuration as well as the installed
   firmware, so use it only when you intentionally want a full reset. In Visual
   Studio Code, open the PlatformIO view, expand **Project Tasks**, expand your
   board environment, and select **Platform > Erase Flash**. When it finishes,
   select **General > Upload** for the same environment.

   The equivalent PlatformIO Core CLI commands are:

   ```text
   pio run -e esp32dev -t erase
   pio run -e esp32dev -t upload
   ```

Replace `esp32dev` with the environment for your board. After the erase, the
scanner uses `UserConfig.h` defaults until settings are saved to NVS again.

NVS is useful for development because settings changed through the web UI or
installer survive both USB uploads and OTA updates.

## Serial monitor

Use PlatformIO's **Serial Monitor** button after uploading, or run:

```text
pio device monitor -b 115200
```

If necessary, add `-p` followed by the serial port. Exit the terminal monitor
with **Ctrl+]**.

Serial output is the primary source of boot, Wi-Fi, NFC, and runtime diagnostic
information. Include relevant logs when reporting hardware failures.

## Automated tests

### Native tests

The native test suite exercises parsers and hardware-independent logic without
an ESP32:

```text
make -C test/native test
```

Clean its generated binaries and objects with:

```text
make -C test/native clean
```

The suite covers PrinterManager behavior, OpenPrintTag bounds, diagnostics,
display layout, TigerTag and Bambu parsing, UID write guards, NTAG capability
container sizing, and OpenTag3D parsing.

### Firmware compilation tests

At minimum, compile the environment affected by a change. Before submitting a
pull request, compile all supported environments with `pio run`; CI performs
the same seven-environment matrix.

Compilation proves that each target builds, but it does not validate NFC timing,
SPI wiring, RF reliability, display behavior, or flash operations.

### Hardware testing

NFC and hardware changes must be tested on physical hardware. As applicable:

1. Cold-boot the board and inspect the complete serial log.
2. Confirm Wi-Fi connection and open `http://spoolsense.local`.
3. Confirm the configured NFC reader appears in diagnostics.
4. Scan each affected tag type repeatedly, including tag removal and
   re-presentation.
5. Exercise both read and write paths when the change can affect writes.
6. Verify written data by removing and rescanning the tag.
7. Test optional displays or shared-SPI devices affected by the change.
8. Leave the scanner running long enough to detect resets, watchdog failures,
   or intermittent communication errors.

Record the board, reader, tag type, wiring, PlatformIO environment, and relevant
serial output with the test results.

## Submitting pull requests

1. Fork the repository and create a branch from `dev`.
2. Make your changes.
3. Run the native tests and compile the affected environment.
4. Test hardware-dependent changes on physical hardware.
5. Run `pio run` to compile all supported environments before submission.
6. Open a pull request targeting `dev`, not `main`.

### Branch workflow

- `dev` — active development; all pull requests target this branch
- `main` — production releases only; merged from `dev` when stable

### Code guidelines

- All user-facing config must live in `include/UserConfig.h`.
- Avoid unnecessary heap allocations; runtime memory is limited.
- Tag format parsers belong in `lib/`, such as `lib/opentag3d/` and
  `lib/tigertag/`.
- Consider thread safety because multiple FreeRTOS tasks share state.

## Reporting bugs and suggesting features

Use the appropriate GitHub issue template. Bug reports should include exact
reproduction steps, hardware details, firmware version, and relevant serial
logs. Feature requests should explain the use case and expected behavior.

## CI reference

GitHub Actions runs the native test suite and compiles all environments. The
workflow in `.github/workflows/ci.yml` is authoritative when local instructions
and CI behavior differ.

## Questions?

Open an issue using the Question template or start a discussion.
