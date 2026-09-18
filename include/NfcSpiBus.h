#pragma once

// SPI peripheral reserved for the NFC reader on boards where the reader and the
// TFT sit on separate buses. Shared by every reader backend that owns its own
// SPIClass, so none of them can land on the TFT's host by accident.

// Use HSPI for the reader so VSPI is free for the TFT display
// Bus choice is per-target: HSPI is the SPI2 peripheral on classic ESP32 but
// bus 1 = the SPI3 peripheral on ESP32-S3 — the same host LovyanGFX claims for
// the TFT (SPI3_HOST) on the S3-DevKitC. Two drivers arbitrating one SPI
// peripheral corrupts transactions (garbage transceiver states) and can hold
// the bus indefinitely, starving the scan task into a task_wdt reboot. Boards
// whose TFT owns SPI2 instead (S3-Zero) override PN5180_SPI_BUS in build flags.
#ifndef PN5180_SPI_BUS
  #if CONFIG_IDF_TARGET_ESP32S3
    #define PN5180_SPI_BUS FSPI  // bus 0 = SPI2 — matches the FSPI pin naming in BoardPins.h
  #elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C6
    #define PN5180_SPI_BUS FSPI  // the C3/C5/C6 expose one general-purpose SPI host
  #else
    #define PN5180_SPI_BUS HSPI  // SPI2 on classic ESP32
  #endif
#endif
