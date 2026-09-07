#pragma once

#include <LovyanGFX.hpp>
#include "BoardPins.h"

// ---------------------------------------------------------------------------
// LovyanGFX display config — runtime panel selection (ST7789 or GC9A01).
//
// Both panels are 240x240 SPI. Same pins, same bus, different driver IC.
// The panel type is selected at construction via a string parameter
// read from NVS ("st7789" or "gc9a01").
//
// SPI bus:
//   WROOM  — VSPI (bus 3): pins 22/23 freed from LCD when TFT replaces LCD
//   S3-Zero — SPI2 (FSPI): side header pins
// ---------------------------------------------------------------------------

enum class TFTDriver : uint8_t {
    ST7789 = 0,
    GC9A01 = 1,
    ILI9341 = 2,   // 240x320 — dashboard centered with 40px letterbox
    ILI9488 = 3,   // 320x480 — dashboard centered
    ST7796 = 4     // 320x480 — dashboard centered, like ILI9488
};

// 480x320 panels share the centered-sprite / wide-landscape geometry.
inline bool tftIs480Wide(TFTDriver d) {
    return d == TFTDriver::ILI9488 || d == TFTDriver::ST7796;
}

#if defined(BOARD_ESP32_S3)

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_st7789;
    lgfx::Panel_GC9A01  _panel_gc9a01;
    lgfx::Panel_ILI9341 _panel_ili9341;
    lgfx::Panel_ILI9488 _panel_ili9488;
    lgfx::Panel_ST7796  _panel_st7796;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(TFTDriver driver = TFTDriver::ST7789) {
        // SPI bus — S3-DevKitC uses SPI3 (PN5180 on SPI2), S3-Zero uses SPI2
        {
            auto cfg = _bus_instance.config();
#if defined(BOARD_S3_DEVKITC)
            cfg.spi_host   = SPI3_HOST;  // SPI3 — PN5180 uses SPI2/FSPI
#else
            cfg.spi_host   = SPI2_HOST;  // FSPI on S3-Zero
#endif
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = PIN_TFT_SCLK;
            cfg.pin_mosi   = PIN_TFT_MOSI;
#if defined(BOARD_SHARED_SPI)
            // On single-GP-SPI boards LovyanGFX performs the one and only
            // Arduino SPI.begin() for the whole bus, and Arduino's
            // SPIClass::begin() ignores a second begin() once the bus is
            // started. The shared bus must therefore come up with the
            // PN5180/PN532 MISO routed here — the write-only TFT never reads
            // it, but NFC cannot read tag responses without it.
            cfg.pin_miso   = PIN_PN5180_MISO;
#else
            cfg.pin_miso   = PIN_TFT_MISO;
#endif
            cfg.pin_dc     = PIN_TFT_DC;
            _bus_instance.config(cfg);
        }

        // Select panel and configure
        lgfx::Panel_Device* panel = nullptr;
        if (driver == TFTDriver::GC9A01) {
            panel = &_panel_gc9a01;
        } else if (driver == TFTDriver::ILI9341) {
            panel = &_panel_ili9341;
        } else if (driver == TFTDriver::ILI9488) {
            panel = &_panel_ili9488;
        } else if (driver == TFTDriver::ST7796) {
            panel = &_panel_st7796;
        } else {
            panel = &_panel_st7789;
        }

        panel->setBus(&_bus_instance);

        {
            auto cfg = panel->config();
            cfg.pin_cs   = PIN_TFT_CS;
            cfg.pin_rst  = PIN_TFT_RST;
            cfg.pin_busy = -1;
            // The dashboard renders a 240x240 sprite; larger ILI94xx panels
            // center it by offsetting the addressing window (#180)
            if (driver == TFTDriver::ILI9341) {
                cfg.memory_width  = 240;
                cfg.memory_height = 320;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 40;
            } else if (driver == TFTDriver::ILI9488 || driver == TFTDriver::ST7796) {
                cfg.memory_width  = 320;
                cfg.memory_height = 480;
                cfg.panel_width   = 320;   // full native panel (landscape via rotation)
                cfg.panel_height  = 480;
                cfg.offset_x      = 0;
                cfg.offset_y      = 0;
            } else {
                cfg.memory_width  = 240;
                cfg.memory_height = 240;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 0;
            }
            // ILI9488 3.5" modules ship X-mirrored vs the ST7789 default and are
            // mounted landscape; rotation 5 = 90° + horizontal mirror-correct.
            // ST7796 is the same 320x480 glass mounted landscape but typically
            // unmirrored and non-inverted: rotation 1 = plain 90°, yielding the
            // 480x320 surface the wide renderers expect. Field confirmation
            // pending (#301) — a tester adjusts from here if their module differs.
            cfg.offset_rotation = (driver == TFTDriver::ILI9488) ? 5
                                : (driver == TFTDriver::ST7796) ? 1 : 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable     = false;
            cfg.invert       = (driver == TFTDriver::ILI9341 || driver == TFTDriver::ILI9488 ||
                                driver == TFTDriver::ST7796)
                                   ? false : true;
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;
            cfg.bus_shared   = false;
            panel->config(cfg);
        }

        // Backlight (skip if pin not assigned)
        if (PIN_TFT_BL >= 0) {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            panel->setLight(&_light_instance);
        }

        setPanel(panel);
    }
};

#else // WROOM or single-SPI C3/C5/C6

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_st7789;
    lgfx::Panel_GC9A01  _panel_gc9a01;
    lgfx::Panel_ILI9341 _panel_ili9341;
    lgfx::Panel_ILI9488 _panel_ili9488;
    lgfx::Panel_ST7796  _panel_st7796;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(TFTDriver driver = TFTDriver::ST7789) {
        // SPI bus — WROOM uses VSPI (PN5180 on HSPI). Capability-enabled
        // single-SPI boards share SPI2 with NFC; disabled targets do not link LGFX.
        {
            auto cfg = _bus_instance.config();
#if defined(BOARD_ESP32_C3) || defined(BOARD_ESP32_C5) || defined(BOARD_ESP32_C6)
            cfg.spi_host   = SPI2_HOST;
#else
            cfg.spi_host   = VSPI_HOST;
#endif
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = PIN_TFT_SCLK;
            cfg.pin_mosi   = PIN_TFT_MOSI;
#if defined(BOARD_SHARED_SPI)
            // On single-GP-SPI boards LovyanGFX performs the one and only
            // Arduino SPI.begin() for the whole bus, and Arduino's
            // SPIClass::begin() ignores a second begin() once the bus is
            // started. The shared bus must therefore come up with the
            // PN5180/PN532 MISO routed here — the write-only TFT never reads
            // it, but NFC cannot read tag responses without it.
            cfg.pin_miso   = PIN_PN5180_MISO;
#else
            cfg.pin_miso   = PIN_TFT_MISO;
#endif
            cfg.pin_dc     = PIN_TFT_DC;
            _bus_instance.config(cfg);
        }

        lgfx::Panel_Device* panel = nullptr;
        if (driver == TFTDriver::GC9A01) {
            panel = &_panel_gc9a01;
        } else if (driver == TFTDriver::ILI9341) {
            panel = &_panel_ili9341;
        } else if (driver == TFTDriver::ILI9488) {
            panel = &_panel_ili9488;
        } else if (driver == TFTDriver::ST7796) {
            panel = &_panel_st7796;
        } else {
            panel = &_panel_st7789;
        }

        panel->setBus(&_bus_instance);

        {
            auto cfg = panel->config();
            cfg.pin_cs   = PIN_TFT_CS;
            cfg.pin_rst  = PIN_TFT_RST;
            cfg.pin_busy = -1;
            // The dashboard renders a 240x240 sprite; larger ILI94xx panels
            // center it by offsetting the addressing window (#180)
            if (driver == TFTDriver::ILI9341) {
                cfg.memory_width  = 240;
                cfg.memory_height = 320;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 40;
            } else if (driver == TFTDriver::ILI9488 || driver == TFTDriver::ST7796) {
                cfg.memory_width  = 320;
                cfg.memory_height = 480;
                cfg.panel_width   = 320;   // full native panel (landscape via rotation)
                cfg.panel_height  = 480;
                cfg.offset_x      = 0;
                cfg.offset_y      = 0;
            } else {
                cfg.memory_width  = 240;
                cfg.memory_height = 240;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 0;
            }
            // ILI9488 3.5" modules ship X-mirrored vs the ST7789 default and are
            // mounted landscape; rotation 5 = 90° + horizontal mirror-correct.
            // ST7796 is the same 320x480 glass mounted landscape but typically
            // unmirrored and non-inverted: rotation 1 = plain 90°, yielding the
            // 480x320 surface the wide renderers expect. Field confirmation
            // pending (#301) — a tester adjusts from here if their module differs.
            cfg.offset_rotation = (driver == TFTDriver::ILI9488) ? 5
                                : (driver == TFTDriver::ST7796) ? 1 : 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable     = false;
            cfg.invert       = (driver == TFTDriver::ILI9341 || driver == TFTDriver::ILI9488 ||
                                driver == TFTDriver::ST7796)
                                   ? false : true;
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;
#if defined(BOARD_SHARED_SPI)
            cfg.bus_shared   = true;
#else
            cfg.bus_shared   = false;
#endif
            panel->config(cfg);
        }

        // Backlight (skip if pin not assigned)
        if (PIN_TFT_BL >= 0) {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            panel->setLight(&_light_instance);
        }

        setPanel(panel);
    }
};

#endif // BOARD_ESP32_S3
