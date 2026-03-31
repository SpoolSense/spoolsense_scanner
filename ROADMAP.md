# SpoolSense Roadmap

Feature roadmap across the SpoolSense ecosystem (scanner, middleware, installer, docs).

## Exploring

| Feature | Repo | Description |
|---------|------|-------------|
| Direct Moonraker mode | Scanner | Scanner talks to Moonraker directly, no middleware needed |
| Bambu Lab AMS via MQTT | Scanner | Direct MQTT integration with Bambu printers |
| Bambu Lab printer support | Middleware | Local MQTT bridge for Bambu spool tracking |
| MIFARE Classic authentication | Scanner | Read Creality CFS and Bambu encrypted tags |
| ST7789 TFT color display | Scanner | Replace 16x2 LCD with 240x240 color TFT |
| Multi-tag detection | Scanner | Read two spools simultaneously (dual antenna) |
| Spoolman write to tag | Middleware | Write spool data fetched from Spoolman directly to a tag |
| Bondtech INDX compatibility | Middleware | Support up to 8 toolheads (retail Q2 2026) |

## Planned

| Feature | Repo | Target | Issue |
|---------|------|--------|-------|
| WiFi reconnection logic | Scanner | — | #29 |
| Tag writer: populate from Spoolman | Scanner | — | #32 |
| NTAG variant detection (GET_VERSION) | Scanner | — | #22 |
| TigerTag partial write (changed fields only) | Scanner | — | #13 |
| PN5180 Phase 2 reliability | Scanner | — | #20 |
| HA publish queue fix (silent drops) | Scanner | — | #28 |
| ISO15693 selected-mode writes | Scanner | — | #21 |
| Nozzle/bed temps to AFC lane_data | Middleware | — | #36 |
| Resync AFC lock state on MQTT reconnect | Middleware | — | #13 |
| Moonraker websocket (replace polling) | Middleware | — | #11 |
| Sync ownership clarification (scanner vs middleware) | Middleware | — | #22 |
| Tag writeback architecture review | Scanner + Middleware | — | — |
| Smarter Spoolman lookups (filter by NFC ID) | Middleware | — | — |
| Low spool push notification (HA) | Middleware | — | — |
| Klipper error alerts via LED (per-toolhead) | Scanner | — | — |
| Wiring photos and assembly guides | Docs | — | — |
| More community enclosure designs | Docs | — | — |

## In Progress

| Feature | Repo | Notes |
|---------|------|-------|
| Prusa PrusaLink integration | Scanner | Experimental, looking for testers |
| Creality rooted printer guide | Docs | Compatible via Moonraker |

## Completed

| Feature | Repo | Version |
|---------|------|---------|
| Link/re-assign NFC+ tags to Spoolman spools | Scanner | v1.6.1 |
| Tag writer auto-populate from scanned tag | Scanner | v1.6.1 |
| NFC+ reader shows temps from Spoolman | Scanner | v1.6.1 |
| HA discovery traffic reduced ~80% | Scanner | v1.6.1 |
| HTTP connection reuse for Spoolman | Scanner | v1.6.1 |
| TFT display support (ST7789 240x240) | Scanner | v1.6.0 |
| DisplayI interface (pluggable displays) | Scanner | v1.6.0 |
| Spoolman color_hex parsing fix | Scanner | v1.6.0 |
| NFC+ registration temps to Spoolman | Scanner | v1.6.0 |
|---------|------|---------|
| AP mode fallback + captive portal | Scanner | v1.5.10 |
| Web flasher (browser-based firmware flash) | Docs | v1.5.10 |
| Tag writer dry temp/time auto-populate | Scanner | v1.5.10 |
| 3x4 matrix keypad (tool assignment) | Scanner | v1.5.9 |
| PN532 NFC reader support | Scanner | v1.5.9 |
| OpenTag3D read/write | Scanner | v1.5.5 |
| TigerTag read/write | Scanner | v1.5.0 |
| NFC+ UID registration (Spoolman) | Scanner | v1.5.0 |
| Klipper/AFC middleware | Middleware | v1.5.0 |
| Status LED (SK6812 / WS2812) | Scanner | v1.5.0 |
| Web-based tag writer (all formats) | Scanner | v1.5.0 |
| Orca Slicer lane data integration | Middleware | v1.5.4 |
| Write loop prevention (per-UID cooldown) | Middleware | v1.5.5 |
| Atomic toolhead activation (rollback) | Middleware | v1.5.5 |
| Tag writeback (remaining weight sync) | Middleware | v1.5.5 |
| 16x2 I2C LCD display | Scanner | v1.4.0 |
| OTA firmware updates | Scanner | v1.3.0 |
| Spoolman auto-sync | Scanner | v1.2.0 |
| PN5180 NFC reader support | Scanner | v1.0.0 |
| OpenPrintTag read/write | Scanner | v1.0.0 |
| Home Assistant MQTT discovery | Scanner | v1.0.0 |
