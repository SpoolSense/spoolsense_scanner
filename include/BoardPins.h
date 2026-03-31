#pragma once

#include "UserConfig.h"

#if defined(BOARD_ESP32_S3)
  // --- ESP32-S3-Zero pin mapping ---
  // PN5180 SPI
  #define PIN_PN5180_RST   4   // Hardware reset (active low)
  #define PIN_PN5180_NSS   5   // SPI chip select (active low)
  #define PIN_PN5180_MOSI  6   // SPI master-out slave-in
  #define PIN_PN5180_MISO  7   // SPI master-in slave-out
  #define PIN_PN5180_SCK   8   // SPI clock
  #define PIN_PN5180_BUSY  9   // Busy signal (input)
  #define PIN_PN5180_GPIO  10  // General purpose I/O (card detection)
  #define PIN_PN5180_IRQ   11  // Interrupt request (active HIGH)
  #define PIN_PN5180_AUX   12  // Auxiliary monitoring (future use)
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
  // NOTE: GPIO 19/20 are USB D-/D+ on S3 and unavailable as GPIO.
  // NOTE: If using LCD + keypad simultaneously on S3, pin conflicts may occur.
  //       For LCD + keypad builds, the ESP32-WROOM is strongly recommended
  //       due to its larger number of freely available GPIO pins.
  #define PIN_KEYPAD_ROW1  38
  #define PIN_KEYPAD_ROW2  39
  #define PIN_KEYPAD_ROW3  40
  #define PIN_KEYPAD_ROW4  41
  #define PIN_KEYPAD_COL1  17
  #define PIN_KEYPAD_COL2  18
  #define PIN_KEYPAD_COL3  42
  // TFT SPI display — S3-Zero + PN532 only (GPIO 1-13 are side headers)
  // PN532 uses GPIO 4-8,11. TFT uses the remaining header pins.
  // Not compatible with PN5180 (not enough free header pins).
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
  #define PIN_PN5180_RST   13  // Hardware reset (active low)
  #define PIN_PN5180_NSS   14  // SPI chip select (active low)
  #define PIN_PN5180_MOSI  27  // SPI master-out slave-in
  #define PIN_PN5180_MISO  26  // SPI master-in slave-out
  #define PIN_PN5180_SCK   25  // SPI clock
  #define PIN_PN5180_BUSY  33  // Busy signal (input)
  #define PIN_PN5180_GPIO  32  // General purpose I/O (card detection)
  #define PIN_PN5180_IRQ   35  // Interrupt request (active HIGH, input-only pin)
  #define PIN_PN5180_AUX   34  // Auxiliary monitoring (input-only pin)
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
  // Uses VSPI. Pins 23/22 freed from LCD when TFT replaces it.
  #define PIN_TFT_MOSI     23  // VSPI MOSI (shared with LCD SDA when LCD enabled)
  #define PIN_TFT_SCLK     22  // VSPI SCLK (shared with LCD SCL when LCD enabled)
  #define PIN_TFT_MISO     -1  // Not needed for write-only TFT
  #define PIN_TFT_CS        2  // Free GPIO
  #define PIN_TFT_DC        4  // Freed from LED when TFT is enabled
  #define PIN_TFT_RST      -1  // Software reset via LovyanGFX
  #define PIN_TFT_BL       -1  // No backlight control (always on), or pick a free pin
#endif
