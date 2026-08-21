#pragma once

#include "UserConfig.h"

#if defined(BOARD_S3_DEVKITC)
  // --- ESP32-S3-DevKitC-1-N16R8 pin mapping ---
  // PN5180 SPI (SPI2/FSPI — hardware SPI for speed)
  #define PIN_PN5180_RST   7
  #define PIN_PN5180_NSS   10
  #define PIN_PN5180_MOSI  11  // FSPID
  #define PIN_PN5180_MISO  9   // FSPIQ
  #define PIN_PN5180_SCK   12  // FSPICLK
  #define PIN_PN5180_BUSY  6
  #define PIN_PN5180_GPIO  4
  #define PIN_PN5180_IRQ   5
  #define PIN_PN5180_AUX   2
  // PN532 SPI (shares pins with PN5180; only one active at runtime)
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  // LCD I2C
  #define PIN_LCD_SDA      17
  #define PIN_LCD_SCL      18
  // Status LED — onboard WS2812 RGB on GPIO 48
  #define PIN_STATUS_LED   48
  // 3x4 Matrix Keypad (JTAG pins repurposed — safe after boot)
  #define PIN_KEYPAD_ROW1  39
  #define PIN_KEYPAD_ROW2  40
  #define PIN_KEYPAD_ROW3  41
  #define PIN_KEYPAD_ROW4  42
  #define PIN_KEYPAD_COL1  47
  #define PIN_KEYPAD_COL2  1
  #define PIN_KEYPAD_COL3  8
  // TFT SPI display (SPI3 — separate bus from PN5180)
  #define PIN_TFT_MOSI     14
  #define PIN_TFT_SCLK     13
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS       15
  #define PIN_TFT_DC       16
  #define PIN_TFT_RST      -1  // Software reset via LovyanGFX
  #define PIN_TFT_BL       -1

#elif defined(BOARD_S3_ZERO)
  // --- ESP32-S3-Zero pin mapping ---
  // PN5180 SPI
  #define PIN_PN5180_RST   4
  #define PIN_PN5180_NSS   5
  #define PIN_PN5180_MOSI  6
  #define PIN_PN5180_MISO  7
  #define PIN_PN5180_SCK   8
  #define PIN_PN5180_BUSY  9
  #define PIN_PN5180_GPIO  10
  #define PIN_PN5180_IRQ   11
  #define PIN_PN5180_AUX   12
  // PN532 SPI (shares physical pins with PN5180; only one active at runtime)
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  // LCD I2C
  #define PIN_LCD_SDA      1
  #define PIN_LCD_SCL      2
  // Status LED — onboard WS2812 RGB (always available, no external wiring)
  #define PIN_STATUS_LED   21
  // 3x4 Matrix Keypad (ENABLE_KEYPAD)
  #define PIN_KEYPAD_ROW1  38
  #define PIN_KEYPAD_ROW2  39
  #define PIN_KEYPAD_ROW3  40
  #define PIN_KEYPAD_ROW4  41
  #define PIN_KEYPAD_COL1  17
  #define PIN_KEYPAD_COL2  18
  #define PIN_KEYPAD_COL3  42
  // TFT SPI display — S3-Zero + PN532 only (GPIO 1-13 are side headers)
  #define PIN_TFT_MOSI     13
  #define PIN_TFT_SCLK     12
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS        10
  #define PIN_TFT_DC         3
  #define PIN_TFT_RST        9
  #define PIN_TFT_BL        -1

#elif defined(BOARD_SEEED_XIAO_ESP32_C6)
  // --- Seeed Studio XIAO ESP32-C6 pin mapping ---
  // Uses the XIAO D8/D9/D10 hardware SPI pins plus three free GPIO control lines.
  #define PIN_PN5180_SCK   19  // D8
  #define PIN_PN5180_MISO  20  // D9
  #define PIN_PN5180_MOSI  18  // D10
  #define PIN_PN5180_NSS   21  // D3
  #define PIN_PN5180_BUSY  16  // D6
  #define PIN_PN5180_RST   17  // D7
  #define PIN_PN5180_GPIO  -1
  #define PIN_PN5180_IRQ   -1
  #define PIN_PN5180_AUX   -1
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  #define PIN_LCD_SDA      22  // D4
  #define PIN_LCD_SCL      23  // D5
  // Status LED — onboard single-color user LED on GPIO15, active low
  #define PIN_STATUS_LED   15
  #define PIN_KEYPAD_ROW1  -1
  #define PIN_KEYPAD_ROW2  -1
  #define PIN_KEYPAD_ROW3  -1
  #define PIN_KEYPAD_ROW4  -1
  #define PIN_KEYPAD_COL1  -1
  #define PIN_KEYPAD_COL2  -1
  #define PIN_KEYPAD_COL3  -1
  #define PIN_TFT_MOSI     PIN_PN5180_MOSI
  #define PIN_TFT_SCLK     PIN_PN5180_SCK
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS       -1
  #define PIN_TFT_DC       -1
  #define PIN_TFT_RST      -1
  #define PIN_TFT_BL       -1

#elif defined(BOARD_ESP32_C6)
  // --- ESP32-C6-DevKitC-1 pin mapping ---
  // NFC uses the sole general-purpose SPI controller. GPIO 12/13 remain free
  // for native USB; straps 4/5/9/15 are avoided. TFT shares SCK/MOSI with NFC.
  #define PIN_PN5180_SCK   6
  #define PIN_PN5180_MOSI  7
  #define PIN_PN5180_MISO  2
  #define PIN_PN5180_NSS   10
  #define PIN_PN5180_BUSY  1
  #define PIN_PN5180_RST   0
  #define PIN_PN5180_GPIO  -1
  #define PIN_PN5180_IRQ   -1
  #define PIN_PN5180_AUX   -1
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  #define PIN_LCD_SDA      20
  #define PIN_LCD_SCL      21
  // Onboard WS2812. GPIO8 is a strap, but Espressif's board circuit is safe
  // and the firmware does not drive it until after reset sampling completes.
  #define PIN_STATUS_LED   8
  #define PIN_KEYPAD_ROW1  -1
  #define PIN_KEYPAD_ROW2  -1
  #define PIN_KEYPAD_ROW3  -1
  #define PIN_KEYPAD_ROW4  -1
  #define PIN_KEYPAD_COL1  -1
  #define PIN_KEYPAD_COL2  -1
  #define PIN_KEYPAD_COL3  -1
  #define PIN_TFT_MOSI     PIN_PN5180_MOSI
  #define PIN_TFT_SCLK     PIN_PN5180_SCK
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS       3
  #define PIN_TFT_DC       11
  #define PIN_TFT_RST      18
  #define PIN_TFT_BL       -1

#elif defined(BOARD_ESP32_C5)
  // --- ESP32-C5-DevKitC-1 v1.2 pin mapping ---
  // NFC uses the sole general-purpose SPI controller. GPIO 13/14 remain free
  // for native USB; straps 2/3/7/25/26/28 are avoided. GPIO27 is the board LED.
  #define PIN_PN5180_SCK   6
  #define PIN_PN5180_MOSI  8
  #define PIN_PN5180_MISO  9
  #define PIN_PN5180_NSS   10
  #define PIN_PN5180_BUSY  1
  #define PIN_PN5180_RST   0
  #define PIN_PN5180_GPIO  -1
  #define PIN_PN5180_IRQ   -1
  #define PIN_PN5180_AUX   -1
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  #define PIN_LCD_SDA      24
  #define PIN_LCD_SCL      11
  #define PIN_STATUS_LED   27
  #define PIN_KEYPAD_ROW1  -1
  #define PIN_KEYPAD_ROW2  -1
  #define PIN_KEYPAD_ROW3  -1
  #define PIN_KEYPAD_ROW4  -1
  #define PIN_KEYPAD_COL1  -1
  #define PIN_KEYPAD_COL2  -1
  #define PIN_KEYPAD_COL3  -1
  // Shared-bus map. LovyanGFX 1.2.21 gains C5 support via the fail-closed
  // build-time patch in scripts/patch_lovyangfx_c5.py (upstream issue #700).
  #define PIN_TFT_MOSI     PIN_PN5180_MOSI
  #define PIN_TFT_SCLK     PIN_PN5180_SCK
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS       4
  #define PIN_TFT_DC       5
  #define PIN_TFT_RST      23  // GPIO12 is the header's UART RX; 23 is free on all revisions
  #define PIN_TFT_BL       -1

#elif defined(BOARD_ESP32_C3)
  // --- ESP32-C3 SuperMini pin mapping (HORNAXYS, Waveshare, etc.) ---
  // Scoped variant: NFC SPI reader + I2C LCD + WS2812 only. No TFT, no keypad.
  // Only one usable SPI controller, so TFT sharing is explicitly disabled here.
  // Exposed GPIO: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21.
  // Strap pins on C3: GPIO 2, 8, 9 — these sample their state at POR to select
  // boot mode. GPIO 2 must be HIGH, GPIO 9 HIGH for SPI-flash boot (LOW =
  // download), GPIO 8 controls boot-log output. GPIO 2 is intentionally left
  // unused so the board's own pull-up handles it — do not wire anything that
  // could drive GPIO 2 LOW at POR.
  // PN5180 SPI
  #define PIN_PN5180_SCK   4
  #define PIN_PN5180_MISO  5
  #define PIN_PN5180_MOSI  6
  #define PIN_PN5180_NSS   7
  #define PIN_PN5180_BUSY  3
  #define PIN_PN5180_RST   0   // Non-strap GPIO — avoids GPIO 2 strap-pin risk if module drives RST low at POR
  #define PIN_PN5180_IRQ   10
  // GPIO 20/21 are the C3's UART0 default pins. With ARDUINO_USB_CDC_ON_BOOT=1
  // (set in platformio.ini), Serial routes to USB-CDC and UART0 is free for
  // reuse as plain GPIO. Do not re-init UART0 or change the USB-CDC flag
  // without reassigning these pins first.
  #define PIN_PN5180_GPIO  21
  #define PIN_PN5180_AUX   20
  // PN532 SPI (shares physical pins with PN5180; only one active at runtime)
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  // LCD I2C — GPIO 8/9 are strap pins. I2C backpacks (PCF8574) provide the
  // required external pull-ups and idle SDA/SCL HIGH, which satisfies the POR
  // strap condition. Do not wire devices that can drive these lines LOW before
  // firmware releases the bus.
  #define PIN_LCD_SDA      8
  #define PIN_LCD_SCL      9
  // Status LED — external WS2812 (SuperMini onboard LED is single-color blue, not RGB)
  #define PIN_STATUS_LED   1
  // Keypad — not supported on C3 (pin budget too tight); sentinels keep build working
  #define PIN_KEYPAD_ROW1  -1
  #define PIN_KEYPAD_ROW2  -1
  #define PIN_KEYPAD_ROW3  -1
  #define PIN_KEYPAD_ROW4  -1
  #define PIN_KEYPAD_COL1  -1
  #define PIN_KEYPAD_COL2  -1
  #define PIN_KEYPAD_COL3  -1
  // TFT — not supported on C3 (single usable SPI bus is dedicated to NFC reader)
  #define PIN_TFT_MOSI     -1
  #define PIN_TFT_SCLK     -1
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS       -1
  #define PIN_TFT_DC       -1
  #define PIN_TFT_RST      -1
  #define PIN_TFT_BL       -1

#elif defined(BOARD_ESP32_S3)
  // --- Generic ESP32-S3 fallback (same as S3-Zero) ---
  #define PIN_PN5180_RST   4
  #define PIN_PN5180_NSS   5
  #define PIN_PN5180_MOSI  6
  #define PIN_PN5180_MISO  7
  #define PIN_PN5180_SCK   8
  #define PIN_PN5180_BUSY  9
  #define PIN_PN5180_GPIO  10
  #define PIN_PN5180_IRQ   11
  #define PIN_PN5180_AUX   12
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  #define PIN_LCD_SDA      1
  #define PIN_LCD_SCL      2
  #define PIN_STATUS_LED   21
  #define PIN_KEYPAD_ROW1  38
  #define PIN_KEYPAD_ROW2  39
  #define PIN_KEYPAD_ROW3  40
  #define PIN_KEYPAD_ROW4  41
  #define PIN_KEYPAD_COL1  17
  #define PIN_KEYPAD_COL2  18
  #define PIN_KEYPAD_COL3  42
  #define PIN_TFT_MOSI     13
  #define PIN_TFT_SCLK     12
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS        10
  #define PIN_TFT_DC         3
  #define PIN_TFT_RST        9
  #define PIN_TFT_BL        -1

#else
  // --- ESP32-WROOM-32 pin mapping (default) ---
  // PN5180 SPI
  #define PIN_PN5180_RST   13
  #define PIN_PN5180_NSS   14
  #define PIN_PN5180_MOSI  27
  #define PIN_PN5180_MISO  26
  #define PIN_PN5180_SCK   25
  #define PIN_PN5180_BUSY  33
  #define PIN_PN5180_GPIO  32
  #define PIN_PN5180_IRQ   35  // Input-only pin
  #define PIN_PN5180_AUX   34  // Input-only pin
  // PN532 SPI (shares physical pins with PN5180; only one active at runtime)
  #define PIN_PN532_SCK    PIN_PN5180_SCK
  #define PIN_PN532_MOSI   PIN_PN5180_MOSI
  #define PIN_PN532_MISO   PIN_PN5180_MISO
  #define PIN_PN532_SS     PIN_PN5180_NSS
  #define PIN_PN532_IRQ    PIN_PN5180_IRQ
  #define PIN_PN532_RST    PIN_PN5180_RST
  // LCD I2C
  #define PIN_LCD_SDA      23
  #define PIN_LCD_SCL      22
  // Status LED — external SK6812 RGBW (optional, requires wiring)
  #define PIN_STATUS_LED   4
  // 3x4 Matrix Keypad (ENABLE_KEYPAD)
  #define PIN_KEYPAD_ROW1  19
  #define PIN_KEYPAD_ROW2  15
  #define PIN_KEYPAD_ROW3  16
  #define PIN_KEYPAD_ROW4  5
  #define PIN_KEYPAD_COL1  18
  #define PIN_KEYPAD_COL2  21
  #define PIN_KEYPAD_COL3  17
  // TFT SPI display (ENABLE_TFT — mutually exclusive with LCD I2C)
  #define PIN_TFT_MOSI     23
  #define PIN_TFT_SCLK     22
  #define PIN_TFT_MISO     -1
  #define PIN_TFT_CS        2
  #define PIN_TFT_DC        4
  #define PIN_TFT_RST      -1
  #define PIN_TFT_BL       -1
#endif
