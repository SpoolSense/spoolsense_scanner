# Changelog

## [1.6.2] - 2026-03-31

### Added

- **WiFi reconnection** — automatic reconnect with exponential backoff (5s→60s) when WiFi drops. Display shows "WiFi Lost" / "WiFi OK" on state change. mDNS re-initializes on reconnect. NFC scanning continues uninterrupted. (#29)
- **Bambu AMS blueprint** — Home Assistant blueprint pushes scanned spool data (material, color, temps) into Bambu Lab AMS trays automatically. Scan a tag, load the tray, done.
- **Bambu → Spoolman deduction blueprint** — after a Bambu print finishes or is canceled, automatically deducts filament weight from Spoolman per tray. Requires spoolman-homeassistant integration.
- **Bambu filament ID map** — 34 material types mapped to Bambu generic filament IDs for accurate AMS tray identification.

### Fixed

- **OTA on TFT builds** — free 57.6KB TFT sprite before OTA download, fixing SSL memory allocation failure. Progress bar shown on TFT during update. (#59)
- **S3-Zero TFT pins** — corrected pin assignments for side headers, PN532 only.

---

## [1.6.1] - 2026-03-30

### Added

- **Link/re-assign NFC+ tags to Spoolman spools** — spool picker on reader page with search. Link unlinked tags or re-assign existing ones. Proxy endpoints avoid CORS. (#54)
- **Tag writer auto-populate from scanned tag** — place a tag on the reader, open any writer page, form fields pre-fill from the tag's data. Works cross-format (scan TigerTag, write as OpenPrintTag). (#57)
- **NFC+ reader shows temps** — extruder and bed temps from Spoolman now displayed on the reader page for NFC+ tags. (#56)

### Fixed

- **HA discovery MQTT traffic reduced ~80%** — only re-publishes when UID changes, not every scan. Legacy openprinttag_ entity cleanup removed. (#55)
- **Spoolman HTTP connection reuse** — persistent TCP connection across API calls, eliminates per-request connection overhead. (#30)
- **NFC+ reader polling** — keeps polling until Spoolman data arrives instead of stopping on first tag detection.
- **Spoolman JSON buffer** — dynamically sized to response (was fixed 16KB, failed on 25KB+ spool lists).
- **Spool picker security** — XSS escaping, hex validation on nfc_id, HTTP timeouts, safe re-assign order (set new before clearing old).
- **TFT display** — removed duplicate filament name line. Fixed breathing animation unsigned underflow. NTP failure now shown on TFT.
- **HA task stack** — bumped 7168 → 8192 to prevent overflow during discovery.
- **Renamed showStatusOnLCD → showStatusScreen** — method now uses DisplayI interface, name reflects that.

---

## [1.6.0] - 2026-03-30

### Added

- **TFT display support (ST7789 240x240)** — color spool graphic with filament color fill, weight bar, tag format icons, and breathing animation for low spools (<100g). Runtime NVS toggle in web config. Mutually exclusive with LCD on WROOM (shared GPIO 22/23). Uses LovyanGFX with 8-bit color sprite for heap efficiency.
- **DisplayI interface** — LCDManager and TFTManager both implement a shared display interface. ApplicationManager works with either display without knowing which is attached.

### Fixed

- **Spoolman color_hex parsing** — nested objects (vendor.extra:{}) broke the JSON streaming parser, causing color to be empty for NFC+ UID lookups. Parser now skips unknown nested objects correctly.
- **NFC+ registration temps** — extruder and bed temperatures now written to Spoolman filament settings. Single temp fields (averaged from material DB min/max).
- **NFC+ weight bar** — initial_weight_g now passed through SpoolmanSyncedPayload so NFC+ tags show the weight bar on TFT and LCD.
- **SPI bus separation** — PN5180 moved to HSPI, TFT on VSPI. Separate SPI peripherals eliminate bus contention.

---

## [1.5.10] - 2026-03-29

### Added

- **AP mode fallback with captive portal** — when WiFi SSID is empty or STA connection fails after 15s, the ESP32 starts an open hotspot (`SpoolSense-XXXX`). A captive portal DNS server auto-redirects phones and laptops to the config page at 192.168.4.1. Enter WiFi credentials, save, device reboots into normal mode. Enables zero-CLI setup for the upcoming web flasher.

### Fixed

- **Tag writer dry temp/time auto-populate** — switched TigerTag and OpenTag3D writer material data from `api.tigertag.io` to canonical GitHub JSON source (`TigerTag-RFID-Guide/database/`). The API was missing dry temp, dry time, and proper nozzle/bed min/max ranges. All writer pages now auto-populate dry temp, dry time, and actual min/max values on material selection. Closes #49.
- **Stale auto-fill values when switching materials** — writer fields now clear when switching to a material without data instead of retaining the previous material's values.

---

## [1.5.9] - 2026-03-28

### Added

- **PN532 NFC reader support** — Adafruit PN532 added as a second NFC reader option (ISO14443A only). NVS key `nfc_reader` selects which reader initializes at boot. Supports GenericUidTag, TigerTag, BambuTag (UID), and OpenPrintTag on NTAG tags. No ISO15693 (ICODE SLIX2) support.
- **3x4 matrix keypad support** — scan a spool, type a tool number, press # to send ASSIGN_SPOOL to Moonraker. Controlled by NVS `keypad_on` flag. Includes LCD feedback during entry.
- **Moonraker URL configuration** — configurable via web UI and installer for keypad tool assignment.
- **NFC reader selection in web UI** — dropdown in Hardware config section with PN5180/PN532 options.

### Fixed

- **LED black spool color** — black (0,0,0) filament color now substitutes dim white (0x33) so the LED is visibly lit instead of appearing off.
- **NFC abstraction leak** — removed `static_cast<HardwareNFCConnection*>` from NFCManager. Reader identification and diagnostics now use virtual methods (`getReaderInfo()`, `logDiagnostics()`) on the NFCConnectionI interface.
- **Troubleshooting page** — NFC reader info is now reader-agnostic (shows "PN5180 v3.4" or "PN532 v1.6" instead of hardcoded PN5180 label).

---

## [1.5.8] - 2026-03-27

### Fixed

- **LCD Type field blank after Spoolman sync** — `material_name` is now carried in `SpoolmanSyncedPayload` instead of being re-read from NFC state after sync completes. Avoids blank Type when the tag briefly loses contact during the Spoolman HTTP request.
- **NFC+ Register button silent failure** — fixed `TypeError` caused by calling `.options[selectedIndex]` on an `<input>` element (not a `<select>`). Registration now works correctly.
- **Generic tag Spoolman lookup** — scanning a plain NFC tag (e.g. NTAG215) now triggers a Spoolman UID lookup via the `nfc_id` extra field. Material, manufacturer, color, and remaining weight are displayed on the LCD and populated in the reader page and `/api/status`.
- **Duplicate UID handling** — when multiple Spoolman spools share the same `nfc_id`, the most recently registered spool (highest ID) is now used instead of the first match.

---

## [1.5.7] - 2026-03-27

### Fixed

- **NVS runtime feature toggling** — LCD and LED are now controlled at runtime via NVS (`lcd_on`, `led_on`) rather than compile-time `#ifdef` flags. A single firmware binary now correctly enables or disables hardware based on what the installer configured. `ConfigurationManager::begin()` moved before peripheral init so NVS values are available before hardware is initialized.

---

## [1.5.6] - 2026-03-27

### Changed

- **Atomic OpenPrintTag write** — /api/write-tag now builds a fresh CBOR tag and writes all fields in a single NFC pass (~5s vs ~25s). Eliminates sequential write drops, scan loop race conditions, and CBOR re-encoding overflow. Existing field values are preserved when not specified.
- **Skip redundant Spoolman syncs** — sync state cache (per UID) skips PATCH requests when filament and weight haven't changed since the last sync. 2-hour TTL with automatic invalidation on tag re-use, archive, and middleware write commands. Reduces unnecessary Spoolman API traffic, especially in AFC setups.

### Fixed

- Buffer overflow in mifareBlockWrite16 — cmd[1] wrote past 1-byte array (undefined behavior)
- Mutex-less tag_data reads — sendSpoolUpdatedMessage and processWriteQueue now hold tagMutex when reading currentSpool
- ESP.restart() during NFC write — scan task is now paused before restart to prevent tag corruption
- NFCScanTask stack bumped 6144→8192 bytes
- Spoolman sync cache weight comparison uses epsilon (0.01g) instead of float == to prevent false misses from floating-point drift
- Spoolman sync cache protected by dedicated FreeRTOS mutex — prevents races between syncSpool() task and ApplicationManager invalidation on middleware writes
- spoolman_id included in sync cache hit check — prevents false cache hits when different spools share the same UID, filament, and weight within the TTL window

---

## [1.5.5] - 2026-03-26

### Added
- **PrusaLink integration (experimental)** — automatic print monitoring and filament weight deduction via PrusaLink API. PrinterManager FreeRTOS task with IDLE/TRACKING state machine. Pre-print validation warns on filament type mismatch and nozzle temp exceeding tag max. Per-tool XL multi-head filament tracking (up to 5 tools). Web config UI for PrusaLink enable toggle, URL, and API key. Looking for testers with Prusa printers.
- **LED set_color command** — scanner LED now shows filament color from Spoolman when scanning UID-only tags. Receives color via MQTT `spoolsense/<id>/cmd/set_color`.

---

## [1.5.4] - 2026-03-25

### Added
- MQTT temp/density/diameter fields — scanner now publishes temperature, density, and diameter data from tags to MQTT

### Fixed
- OpenPrintTag write fix — /api/write-tag no longer overwrites existing tag data with defaults when only specific fields are sent
- PN5180 FreeRTOS yields — replaced blocking delay() with vTaskDelay() in ISO15693 command handler, preventing NFC connection drops after ~5 writes
- PN5180 IRQ cleanup — clear all IRQ flags on every exit path, prevents stale flag accumulation across sequential writes
- PN5180 transceiver reset — cleanupTransceiver() helper forces idle state on all error paths with GENERAL_ERROR_IRQ_STAT detection
- Invalid color skip — don't write zeros when color field is invalid hex
- Nav bar — all links present on all pages, flex-wrap for narrow screens
- Landing page — added missing cards and consistent ordering

---

## [1.5.3] - 2026-03-24 — NFC+ Registration, Material Auto-fill, Web UI Polish

### Added
- NFC+ Registration page at /register/uid — register plain NFC tags in Spoolman using UID as identifier. No data written to the tag.
- Shared material auto-fill — selecting a material auto-fills nozzle temps (±10°C), bed temps (±5°C), and density across all writer pages
- Type-to-search for material and brand fields on all writer pages (replaces dropdowns). TigerTag API expands options when reachable, hardcoded fallback for offline use.
- OpenTag3D logo on landing page card
- NFC+ nav link added to all pages
- NFC+ Registration card on landing page

### Fixed
- Nav bar: all links present on all pages with consistent ordering
- Nav bar: flex-wrap prevents links from being hidden on narrow screens
- Landing page: added missing OpenTag3D, Troubleshooting, and NFC+ cards
- Landing page: real logos for OpenPrintTag, TigerTag, OpenTag3D cards

---

## [1.5.2] - 2026-03-23 — Troubleshooting & TigerTag API

### Added
- Troubleshooting page at /troubleshooting with WiFi, MQTT, Spoolman, NFC, and memory checks
- Scanner Device ID displayed prominently for middleware configuration
- TigerTag writer fetches materials and brands from TigerTag API with hardcoded fallback
- Auto-fill nozzle and bed temperatures when selecting TigerTag material
- API response validation — rejects malformed data, falls back to hardcoded options
- Brand help link for requesting missing brands from TigerTag database

### Changed
- Renamed TagWriterHTML.h to OpenPrintTagWriterHTML.h for consistency
- Added /troubleshooting nav link to all pages
- Memory display shows total, used, free, and usage percentage

### Fixed
- TigerTag dropdown invisible text on some browsers (CSS light-mode fallback)
- PN5180 firmware version read now checks return value before marking reader as ready
- External links use rel="noopener noreferrer" to prevent tabnabbing

## [1.5.1] - 2026-03-21 — Device ID on Web UI

### Added
- Device ID and firmware version displayed on landing page for easy middleware setup
- Device ID and firmware version included in /api/status response

---

## [1.5.0] - 2026-03-21 — OpenTag3D Support

### Added
- OpenTag3D read/write support — full tag detection, parsing, NDEF wrapping,
  Spoolman sync, MQTT publish, and web writer UI
- OpenTag3D parser library (lib/opentag3d/) with encode/decode
- OpenTag3D writer page at /writer/opentag3d with material/modifier dropdowns,
  brand datalist, color picker + hex input, extended fields toggle
- OpenTag3D data in /api/status and reader page
- Descriptive Spoolman filament names from color name (e.g. "Blood Red PLA")

### Changed
- Moved TigerTag parser from src/ to lib/tigertag/ for consistency with other parsers
- Write button text standardized to "Write Tag" across all writer pages
- Tag writers show "safe to remove" after 2s verify hold (prevents premature tag removal)
- Core-only OpenTag3D writes when no extended fields set (32 pages vs 54)

### Fixed
- PN5180 mifareBlockWrite4: poll RX_STATUS instead of blind delay for reliable ACK detection
- PN5180 transceiver reset to Idle after each write ACK for clean state machine
- OpenTag3D temperature decode: fall back to core temps when extended fields are zero
- OpenTag3D density field: use x1000 scale (mg/cm³) to fit uint16 range

---

## [1.4.0] - 2026-03-19 — Web Config, Spoolman Enrichment, Bug Fixes

### Added
- Web-based configuration page at `spoolsense.local/config` — change WiFi, MQTT, Spoolman, automation mode, and hardware settings from the browser. Settings saved to NVS and persist across OTA updates. Device reboots after saving.
- Spoolman filament enrichment — `settings_extruder_temp` and `settings_bed_temp` populated from tag data (averaged from min/max). Custom `material_name` from tag used as filament name.
- Spoolman extra fields — `tag_format` on spool (OpenPrintTag/TigerTag), `aspect`, `dry_temp`, `dry_time_hours` on filament (from TigerTag data). Written opportunistically; no errors if fields don't exist.
- Installer creates Spoolman extra fields (nfc_id, tag_format, aspect, dry_temp, dry_time_hours) when Spoolman is enabled.
- Spoolman sync test suite (11 tests) covering spool UUID lookup with nested JSON, matching edge cases.
- Native test build fixed — all 45+ tests passing.

### Changed
- Filament matching uses vendor + material + color (was vendor + material only). Prevents different colored filaments from the same brand conflating into one entry.
- Spoolman is source of truth — existing filament fields (name, temps) are never overwritten; only blank values are filled from tag data.
- `SpoolmanManager::isConfigured()` now checks `isSpoolmanEnabled()` flag, not just URL length. Respects web config enable/disable toggle.
- Spoolman spool lookup replaced streaming JSON parser with ArduinoJson for reliable nested object handling.

### Fixed
- **setupRF() no longer fails after tag reads** — full PN5180 state machine reset (RF_OFF → Idle → clear IRQs → loadRFConfig → RF_ON → Transceive) between scan cycles.
- **Spoolman spool lookup no longer creates duplicates** — ArduinoJson correctly handles nested filament/vendor `id` fields.
- **Spoolman enable/disable via web config** — fixed NVS type mismatch (putBool/getBool) and isConfigured check.
- **LCD config flag** — uses `#if ENABLE_LCD` (value check) instead of `#ifdef ENABLE_LCD` (existence check).
- **Native test build** — `ENABLE_STATUS_LED` unset in native builds, `pauseScanTask`/`resumeScanTask` gated behind `NATIVE_TEST`, TigerTagParser/ConversionUtils linked into NFC tests.

## [1.3.4] - 2026-03-19 — P0 Bug Fixes

### Fixed
- **setupRF() no longer fails after tag reads** — root cause was the PN5180 state machine not being reset between scan cycles. After any tag read (ISO15693 multi-block or ISO14443A probe), the chip stayed in Transceive/Receive state. `setupRF()` now performs a full state machine reset: RF_OFF → Idle → clear IRQs → loadRFConfig → RF_ON → Transceive. Eliminates repeated SpoolDetected events and Spoolman API spam.
- **Spoolman spool lookup no longer creates duplicates** — replaced the streaming JSON parser (`htcw_json`) with ArduinoJson for spool UUID lookups. The streaming parser could not reliably handle Spoolman's nested JSON responses (filament → vendor → id), causing UUID matching to fail and duplicate spools to be created on every scan. ArduinoJson handles nesting correctly.

## [1.3.3] - 2026-03-18 — Multi-page Web UI, TigerTag, OTA Updates

### Added
- Multi-page web UI at `spoolsense.local` — landing page with tool cards, shared navigation across all pages
- Tag Reader page (`/reader`) — auto-detects tag format (OpenPrintTag, TigerTag, generic UID), displays all data read-only, stops polling on detection with "Scan Again" button
- TigerTag reader support — detect and parse NTAG213/215 TigerTag binary format with embedded material/brand lookup tables
- TigerTag writer page (`/writer/tigertag`) — write filament data to NTAG tags in TigerTag format with material, brand, color, weight, diameter, aspect, and temperature fields
- `writeISO14443Pages()` — NTAG page write support via PN5180 `mifareBlockWrite4` (command 0xA2)
- `WRITE_TIGERTAG` NFC write type with 40-byte binary payload
- OTA firmware update page (`/update`) — check for updates from GitHub releases with release notes display, one-click download and flash with progress tracking, manual `.bin` upload
- Dual OTA partition table (`ota_0` + `ota_1`, 1.88MB each) — enables automatic rollback on failed update
- Async OTA download — background FreeRTOS task streams firmware from GitHub HTTPS, browser polls `/api/ota-status` for progress
- NFC scan task pause/resume during OTA upload
- `GET /api/version` — returns firmware version and board type
- `GET /api/ota-status` — returns OTA download/flash state and progress
- `POST /api/update-from-url` — ESP32 downloads and flashes firmware from a URL
- `POST /api/upload-firmware` — multipart binary upload for manual OTA
- `/api/status` extended with `tag_kind` field and nested `tigertag` object for TigerTag data
- `POST /api/write-tigertag` — assemble and enqueue TigerTag binary write
- OpenPrintTag and TigerTag logos served as PROGMEM PNGs at `/img/openprinttag.png` and `/img/tigertag.png`
- Logos displayed in writer page card headers
- Spool archive on re-tag — automatically archives old Spoolman spool when tag is re-written with different filament, or same filament with weight jump on nearly empty spool (≤100g → >500g)
- `FIRMWARE_VERSION` build flag in `platformio.ini` — single source of truth for version, shown on update page and in HA discovery
- Shared CSS (`/css/shared.css`) and JS (`/js/shared.js`) served as cacheable endpoints
- Firmware Update card on landing page
- Update link in navigation bar across all pages

### Changed
- OpenPrintTag writer moved from `/` to `/writer/openprinttag` — landing page is now the tool hub
- CSS and JS extracted from inline to shared PROGMEM endpoints
- LCD status line shows `NFC+ Wifi+` / `SM+ MQTT+` (replaces BLE indicator with MQTT connection status)
- ESP32-S3 NeoPixel color order fixed from `NEO_GRB` to `NEO_RGB`

### Removed
- BLE stack — `BluetoothManager.cpp/.h` deleted, `CONFIG_BT` build flags removed, BLE init removed from `main.cpp`. Saves ~540KB flash. Configuration moving to web UI

## [1.2.0] - 2026-03-18 — TigerTag Reader, NVS Config, Spoolman Fixes

### Added
- TigerTag reader support — detect and parse NTAG213/215 TigerTag binary format (ISO14443A) with material, brand, color, weight, and temperature data via embedded lookup tables
- NVS config support — `ConfigurationManager` reads from NVS partition first, per-key fallback to compile-time defaults; enables pre-built binary flashing via installer without recompiling
- GitHub Actions release workflow — auto-builds ESP32-WROOM and ESP32-S3-Zero firmware on tag push, attaches bootloader + partitions + firmware to GitHub release
- SpoolSense Installer — interactive CLI (`spoolsense-installer` repo) that downloads firmware, generates NVS config, verifies chip/flash, and flashes via esptool
- SpoolSense GitHub org — both repos transferred to `github.com/SpoolSense`

### Fixed
- Spoolman 400 on spool create — fixed double-escaped UUID, switched to `nfc_id` extra field, wrapped value as JSON string
- Spoolman filament exact match — switched to ArduinoJson for filament lookup; streaming parser couldn't handle nested vendor objects, was creating duplicate filaments
- Material type dropdown — fixed tag writer dropdown order to match OpenPrintTag enum (TPU=2, ABS=3, ASA=4); was writing wrong type codes
- setupRF() after tag re-detection — fixed LED state being overwritten after setupRF failure

## [1.1.1] - 2026-03-17 — ESP32-S3-Zero Support

### Added
- ESP32-S3-Zero as a second supported board alongside ESP32-WROOM-32
- `include/BoardPins.h` — centralized pin definitions for all boards, auto-selected at compile time
- `[env:esp32s3zero]` PlatformIO build environment with USB CDC serial, 4MB flash, and S3-specific BLE init
- S3-Zero onboard WS2812 RGB LED support (GPIO 21, no external wiring needed)
- S3-Zero LCD support on GPIO 1 (SDA) / GPIO 2 (SCL)
- S3-Zero wiring documentation in README with power sharing notes

### Changed
- Unified LED enable flag: `ENABLE_STATUS_LED` in `UserConfig.h` replaces `USE_STATUS_LED` build flag in `platformio.ini`
- LED pin now defined in `BoardPins.h` (was `-DSTATUS_LED_PIN=4` build flag)
- Serial baud rate changed from 9600 to 115200 on WROOM
- `platformio.ini` refactored to use shared base `[env]` section — both board environments inherit common settings
- BLE controller init on S3 handled by `BLEDevice::init()` (WROOM retains manual classic BT memory release)
- LED pixel type auto-selected: NEO_GRB (S3 onboard WS2812) or NEO_GRBW (WROOM external SK6812)

## [1.1.0] - 2026-03-16 — Built-in Tag Writer (Early Beta)

### Added
- Built-in HTTP web server (port 80) — reachable at `http://spoolsense.local` after WiFi connects
- Tag writer UI served directly from the device — write OpenPrintTag fields from any browser on the local network
- REST API for tag operations: `GET /api/status`, `POST /api/write-tag`, `POST /api/format-tag`
- Tag writer supports all OpenPrintTag fields: material type, manufacturer, custom material name,
  full/remaining weight, color, density, diameter, print/bed/preheat temperatures, and Spoolman ID
- CORS headers on all endpoints for local development testing
- mDNS registration as `spoolsense.local`
- NFC read retry logic (up to 3 attempts with RF reset between retries) — fixes ISO15693 read
  failures that previously left the NFC stack in a stuck state
- `setupRF()` failure recovery: clears stuck RF state immediately instead of waiting 30s for watchdog

### Changed
- NFC scan loop now performs a full hardware reset when `setupRF()` fails with a tag present,
  recovering in one cycle instead of ~30 seconds
- LCD is now truly optional — `ENABLE_LCD 0` in `UserConfig.h` fully gates I2C init, LCD task,
  and all LCD calls; no Wire library or LCD code compiled when disabled

## [1.0.0] - 2026-03-14 — Initial Release

### Added
- OpenPrintTag (ISO15693) full CBOR/NDEF read and write via PN5180
- ISO14443A tag detection (NTAG215 and other Type A tags) via PN5180ISO14443
  library (Copyright 2019 Dirk Carstensen, LGPL-2.1)
- `TagProtocol`, `TagKind`, and `TagScanResult` classification model for
  multi-format tag routing
- `GENERIC_TAG_DETECTED` application event for UID-only ISO14443A tags —
  LCD shows "Generic Tag / UID scan only", HA publishes `blank:false`
- `OpenTag3D` reserved in `TagKind` enum for future support
- `UserConfig.h` compile-time configuration — WiFi, MQTT, Spoolman, LCD, LED,
  automation mode, and board selection are now set before flashing
- `AUTOMATION_MODE` define: `0` = Self Directed (scanner auto-deducts filament),
  `1` = Controlled by Home Assistant
- Simplified board selection: `#define BOARD_ESP32_WROOM` or `#define BOARD_ESP32_S3`
- Status LED (SK6812 RGBW) support via FreeRTOS task — non-blocking flash,
  persistent filament color, breathing animation for low spool (≤100g)
- Full SpoolDetected payload dump to serial log for debugging
- Read cache batching (16-block reads) to fix WDT crash on SLIX2 tags with
  per-command block limits

### Changed
- `ConfigurationManager` loads from compile-time config instead of NVS
- LED persists last filament color after tag removal — no longer clears on
  `TAG_REMOVED`; color stays until the next scan
- Tag scan flash changed from single yellow to 3 white flashes

### Removed
- BLE device configuration (WiFi, MQTT, Spoolman settings via BLE app)
- Settings tab from BLE web UI — spool tag operations remain
- PrusaLink / direct printer integration — out of scope for SpoolSense
- `PrinterManager`, `PrusaLinkAPIStrategy`, `BgcodeParser` and related files
