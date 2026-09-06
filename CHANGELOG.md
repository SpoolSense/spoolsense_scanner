# Changelog

## [1.10.1] - 2026-09-06

### Added

- **NFC reader boot and bus-timeout lines now appear in the web log (`/logs`)** — the PN5180 startup sequence (pin map, SPI clock, reset handshake, firmware version, RF setup) and the BUSY-handshake timeout lines are mirrored into the web log, so a reader that fails in the field can be diagnosed from the browser without a USB cable. Timeout lines are rate-limited in the web log (first and every 20th, with a running count) so a failing reader cannot scroll away the rest of the log; USB serial keeps every line. (#293)

### Fixed

- **Self-test no longer reports a dead reader as healthy** — register health now fails with its own recommendation when the status registers read all-ones (nothing driving the SPI bus: a dead, unpowered, or miswired module). The bus-wedge result also points at `/logs` to show which handshake stalled.

### Build

- **The JSON streaming library is now bundled with the firmware source** — the package registry deleted the exact pinned release the parsers are validated against, which broke every fresh build. Building from source no longer depends on that registry for it.

## [1.10.0] - 2026-08-07

Major memory recovery for boards without PSRAM using a TFT display. No
configuration changes; upgrade over OTA as usual.

### Fixed

- **Out-of-memory on ESP32-WROOM and other no-PSRAM boards with the TFT enabled** — the display held a permanent 57.6 KB framebuffer in internal RAM, the firmware's largest single allocation, leaving as little as 7 KB free once WiFi, Spoolman sync, and the web UI were running together; the scanner crashed within minutes and disabling the display was the only workaround. Frames now render through a 15.4 KB strip that exists only while drawing, returning the full 57.6 KB between frames. Field-validated on a WROOM + PN5180 + TFT setup: internal free RAM with the display on went from 13 KB to 74 KB. (#287)
- **A firmware update can no longer strand the display task's resources** — OTA stopped the render task with a hard kill that could land mid-frame and leave the shared SPI bus locked: a blank update screen, and on ESP32-C5/C6 a dead NFC reader after a failed update. The render task now parks at a safe point before the update proceeds.

### Changed

- **240x240 and 320x240 displays (ST7789, GC9A01, ILI9341) on no-PSRAM boards upgrade from 256-colour to full 16-bit colour** — removes the banding on the tinted spool graphic. Boards with PSRAM keep their existing full-framebuffer rendering, unchanged.

## [1.9.3] - 2026-08-06

### Fixed

- **Tag writes from the web pages are now always bound to the tag being written** — if the page detected a tag but could not read its ID, the write went out unbound and the firmware applied it to whichever tag was on the scanner at that moment. The writer pages now stop with an explanation, and the scanner rejects any write request that arrives without a tag ID. Writing still needs no extra steps: the page detects the tag when you press Write, exactly as before. (#283)

## [1.9.2] - 2026-08-03

Write-path safety and OTA reliability fixes. No configuration changes;
upgrade over OTA as usual.

### Fixed

- **Any OTA upload longer than 30 seconds was aborted by the task watchdog** — pausing NFC for the update suspended the scan task with `vTaskSuspend` without unsubscribing it from the task watchdog. That task is the firmware's only watchdog subscriber, and a suspended task can never feed its entry, so the watchdog panicked deterministically 30 seconds into any upload that held the pause. Fast transfers finished inside the window and masked the bug; slower clients hit it every time, and with no rollback the device reset mid-flash and quietly kept running the old firmware. The scan task is now unsubscribed from the watchdog across the pause and re-subscribed on resume. (#280)
- **Writes are now validated against the tag actually selected on the reader this scan cycle** — the 3-second scan-cooldown dedup, which suppresses PN5180 partial-UID re-reads by matching only the first three UID bytes, could skip tag processing and leave the write guard comparing against the previous tag — letting a quickly-swapped, prefix-sharing tag receive another spool's payload. Bound writes now fail closed when no tag is selected, and atomic/field-update writes — which build their payload from the previously-read tag data — additionally require that data to belong to the bound tag. Affects all write formats. (#276)

### Changed

- **Shared-SPI boards (ESP32-C6, ESP32-C5) now run the PN5180 at 3.5 MHz instead of the 7 MHz datasheet maximum** — with a TFT loading the same SCK/MOSI lines, 7 MHz corrupted a double-digit percentage of UID reads on a C6 bench rig (FF-truncated and bit-flipped UIDs, some sharing enough prefix bytes to slip past the scan-cooldown dedup) while 3.5 MHz read 84/84 clean. The clock is now a build-time knob (`PN5180_SPI_HZ`, guarded to the 7 MHz datasheet ceiling), and the reader boot log prints the requested clock. Dedicated-bus boards keep 7 MHz.

## [1.9.1] - 2026-08-01

Correctness and responsiveness fixes from a full code review. No configuration
changes; upgrade over OTA as usual.

### Fixed

- **TigerTag tags with newer version IDs were misread as plain UID tags** — the forward-compatibility fallback compared the diameter byte against the type constants, so any tag whose version ID was not yet in the public database fell through to generic-UID handling instead of being parsed. (#256)
- **A partly-read Bambu tag could publish invented values** — an authentication failure mid-read left that block uninitialized and the parser consumed it anyway, so marginal coupling could report a random color, weight, diameter, or nozzle/bed temperature to Spoolman and the printer. Reads that miss an identity or print-critical block are now discarded, and a zeroed identity block is no longer published as a spool. (#257)
- **Recent-spool history never updated, and every scan paid 50 ms for it** — the history helper tried to take a lock its callers already held, which cannot succeed on a non-recursive mutex, so each scan and write waited out the full timeout and then skipped the update. Scans get that time back. (#258)
- **OpenSpool writes were not bound to the scanned tag** — unlike the other formats, the handler did not record which tag the write was for, so a tag swapped in after detection could receive the payload. (#259)
- **Opening the troubleshooting page could stall the scanner** — its diagnostics request ran a live Spoolman check outside the lock that serializes outbound HTTP, colliding with a sync or printer poll in progress. The check is now serialized, with a short budget: when the connection is busy the page reports that instead of falsely claiming Spoolman is unreachable. (#262)
- **Build warning on every target** — the PN5180 library defined `FIRMWARE_VERSION` as an EEPROM address, colliding with the firmware version string and burying real warnings. (#263)

## [1.9.0] - 2026-07-17

### Upgrade notes — read before updating

- **OTA upgrading from 1.8.x works normally** and keeps all settings (validated on hardware: the 1.8.x bootloader boots this release across repeated updates on both OTA slots).
- **Downgrading is not supported.** This release moves to a new system framework (Arduino-ESP32 3.x / ESP-IDF 5.5). There is no OTA path backward; going back to 1.8.x requires a full USB reflash, and in our testing the downgraded firmware could not read settings written by 1.9.0 — expect to reconfigure the device from scratch.
- ESP32-C6 and ESP32-C5 use a new 8MB partition layout with larger update slots. Flash them with the full images from this release via USB (`esptool`/PlatformIO); web-flasher support for these boards follows separately. They cannot be provisioned from older instructions.

### Added

- **ESP32-C6 and ESP32-C5 DevKitC-1 targets** — firmware environments, validated NFC board pin maps, onboard RGB LED support, CI builds, and release artifacts. Both targets support PN5180/PN532 NFC, I2C LCD, and the full network/web feature set; keypad remains disabled.
- **Capability-gated shared SPI on ESP32-C6** — a write-only TFT and either PN5180 or PN532 use one initialized GP-SPI host, distinct fail-safe chip selects, and a recursive bounded transaction guard. Validated on C6 hardware with an ILI9488.
- **Landscape dashboard for the 3.5" ILI9488 (480×320)** — full-screen landscape scanned-spool view with a tinted 3D spool image (the coil takes the scanned filament colour, the reel stays neutral), FreeSans typography, a persistent WiFi/Home-Assistant/printer status bar that stays live while idle, and landscape scan-progress/status screens. Renders via a ~30 KB strip pipeline on no-PSRAM boards or a full 16-bit framebuffer with PSRAM.
- **Refreshed 240×240 screens (ST7789/GC9A01)** — the scanned and idle views use the same tinted 3D spool image as the large display, and the header-based status screens show a live WiFi signal indicator.
- **ESP32-C5 shared-SPI TFT (compile-enabled)** — `scripts/patch_lovyangfx_c5.py` applies LovyanGFX upstream's sanctioned ESP32-C5 aliasing (issue #700) to the pinned 1.2.21 release at build time, fail-closed against version or text drift, plus a C5 AHB-DMA register mapping and the `GPIO_PIN_MUX_REG` table the C5 SDK omits. Hardware validation pending.

### Fixed

- `/api/version` now reports the correct board on every target (S3-DevKitC, C3, C6, and C5 previously misreported as `esp32s3zero`/`esp32dev`).

### Changed

- **Arduino-ESP32 3.3.9 / ESP-IDF 5.5.4 migration** — PlatformIO now uses the exact pioarduino `55.03.39` release. Task creation is unicore-safe on C3/C5/C6, the NFC watchdog uses the IDF 5 reconfiguration API with checked errors, and WiFi dependencies are included explicitly for Arduino 3.x.
- **LovyanGFX 1.2.21** — updated the exact pin for Arduino 3.x compatibility; the esp32c5 environment applies the build-time C5 enablement patch above.

## [1.8.3] - 2026-07-11

### Security

- **NDEF parser rejects payloads larger than the data actually read** — a truncated read or hostile tag could declare up to a 64KB payload against a 44-byte buffer, and the region parsers inherited an end pointer far past the allocation (out-of-bounds reads, ASan-verified). The declared payload must now fit the held data; a native regression suite guards it. (#239)

### Added

- **Runtime-configurable NFC reader pins** — a new "NFC Reader Pins (advanced)" section on the config page remaps the reader wiring (RST, NSS/SS, BUSY, SCK, MOSI, MISO) without a custom firmware build, for boards whose pinout differs from the supported variants (#201; also the pin-map half of #197's C3-OLED board). Blank means board default; overrides are validated at the NVS boundary — board-invalid pins revert individually, and any conflict within the set or against the status LED or enabled feature pins reverts the whole set with a serial warning, so a half-applied remap can't wedge the reader. Applies on the post-save restart. PN532 shares the same six slots (BUSY unused).
- **ILI9341 and ILI9488 TFT support** — both ILI94xx panels join the TFT driver dropdown. The 240×240 dashboard renders centered on the larger panels. (#180)
- **Release assets ship `.sha256` checksums** — the installer already verifies downloads against sidecar checksums and fails closed; scanner releases now publish them, so a corrupted download gets caught before it flashes instead of boot-looping the board. (#227)
- **Self-test wizard on the Troubleshooting page** — a guided, read-only diagnostic that turns wiring/RF/power/config/network problems into plain-language recommendations and a **sanitized support report** you can paste straight into a GitHub issue (no WiFi/MQTT passwords, API keys, or full tag UIDs). It checks device/reset/heap/task-stack health; the NFC reader init, firmware, and — for the PN5180 — the live `RF_STATUS`/`IRQ_STATUS`/`SYSTEM_STATUS` registers and bus-wedged latch; and WiFi/MQTT/Spoolman/printer reachability. An optional 100-cycle **NFC stability test** (place a tag when prompted) measures detection/read success, UID consistency, and latency and reports an experimental 0–100 stability score. The stability stage cooperatively pauses the scan task at a safe boundary so it never suspends mid-SPI, and normal scanning resumes on completion, cancel, or timeout. Runs on its own low-priority task so it never blocks the web UI. (#253) _New — the stability score formula is experimental and will be tuned against known-good and marginal hardware; please report results._
- **Integration bench runner** — `test/integration/run_bench.sh` packages the mock-PrusaLink scenarios (full print, cancel proration, filament mismatch, XL multi-tool) into a one-command hardware-in-the-loop bench suite with documented prerequisites. (#225)

### Improved

- **Web UI pages load ~4× smaller** — the HTML, CSS, and JS assets are now pre-compressed at build time and served with `Content-Encoding: gzip`, cutting the total transferred from ~226 KB to ~56 KB (each page 21–35% of its former size). A `pre:` build step gzips the readable source in `src/*HTML.h` / `src/Shared*.h` into generated PROGMEM byte arrays with the firmware version baked in, so page authoring is unchanged. As a side effect the uncompressed copies are no longer compiled in, reclaiming ~174 KB of flash.

### Fixed

- **ISO14443 block reads flagged by the receiver are discarded** — a read the PN5180 itself marked with a general error could previously hand corrupted bytes to the tag parsers if the length looked right. (#111)
- **OpenTag3D density field documented at its real scale** — the `_ugcm3` suffix is misleading (on-tag scale is mg/cm³); the authoritative header now says so, after a stale doc snippet caused a 1000× density bug in a port. (#236)

## [1.8.2] - 2026-07-10

### Improved

- **PN532 tag reads are ~4× faster** — the reader was already receiving 16 bytes (4 pages) per radio exchange but keeping only 4; reads now harvest the full response, cutting round-trips 4× everywhere (classify read 10 → 3 exchanges, full OpenSpool payload 50 → 13, post-write verification the same). Most noticeable on single-core ESP32-C3 builds, where each library wait also stalls WiFi and the web UI. Radio traffic is unchanged — tags see the identical command sequence they always have. Extended payload reads are now also clamped to the tag variant's user memory, so a malformed payload length can no longer read lock/config pages into the payload tail. (#242) _New — please report results: verified against the library source and CI, not yet timed on physical PN532 hardware._

## [1.8.1] - 2026-07-09

### Added

- **"Link tag (no write)" button** — selecting a spool in the Import-from-Spoolman picker now offers linking an already-written tag to that spool without rewriting it. The button arms the link window and guides the scan ("lift it off and set it back down" — links bind on fresh tag arrival), then confirms the link or reports expiry via a new pending-link status endpoint. Previously the link only armed as part of a tag write, and there was no feedback at all.
- **Snapmaker U1 stage mode** — one scanner now serves all four U1 toolheads. In stage mode (Config → Snapmaker U1 → Channel Mode), a scan is held for 30 seconds while you pick the channel: a T0–T3 picker with live countdown appears on the reader page, or type the channel digit + `#` on a keypad if one is fitted (keypad optional — the web picker needs no extra hardware). Spoolman enrichment still applies while the spool is staged, so the assignment posts the augmented data. Fixed-channel mode (one scanner per toolhead) remains the default and is unchanged. Phase 2 of the U1 integration. _New — please report results: validated for stability but not yet confirmed against every U1 hardware revision._
- **U1 motion-sensor auto-pick** — the hands-free path: in stage mode the scanner polls the U1's per-lane filament sensors and assigns the staged spool to the first lane that goes from empty to loaded after the scan. Scan the spool, feed the lane, done — no screen, no button. Lanes already loaded at scan time can never steal the spool (edge detection with a stage-time baseline plus debounce), the web picker and keypad still work as manual overrides, and a "auto-assign when a lane loads" toggle (default on) can disable it. Phase 3 of the U1 integration. _New — the per-lane sensor object names are read from the extended firmware config; if auto-pick doesn't trigger on your unit, please report so we can confirm the naming._

### Changed

- **One identity resolver for tag→spool mapping** (#224) — sync, enrichment saves, tag registration, weight deduction, and the reader lookup all resolve "which spool does this tag denote" through a single function with strict precedence: the user's explicit pick this sync, then the tag's nfc_id claim in Spoolman, then the tag-stored spool id *validated against the live spool*, then the validated cache (with eviction on failure at the same choke point). A tag pointing at a spool already claimed by a different tag is recognized as stale instead of trusted; a tag pointing at a manually-created spool with no nfc_id is adopted instead of duplicated. Transport failures are distinguished from not-found everywhere, and creation happens only on a clean miss. Replaces seven scattered mechanisms (~200 lines net removed) — the structural fix behind the historical duplicate-spool family (#91, #103, #127, #130, #134, #218).

## [1.8.0] - 2026-07-06

### Security

- **Web API is same-origin only** — removed the `Access-Control-Allow-Origin: *` header from every endpoint and the development-era CORS preflight handler. The scanner serves its own UI, so browsers enforce same-origin automatically; the wildcard let any website a browser on your LAN visited call the scanner's API (including config changes and OTA). Verified nothing legitimate depends on cross-origin access: the spoolsense.org web flasher uses WebSerial, the middleware panel calls its own origin, and the mobile app's native HTTP client is unaffected by CORS.
- **OTA-by-URL restricted to SpoolSense releases** — `/api/update-from-url` now rejects any URL outside `https://github.com/SpoolSense/`. Previously anyone on the LAN could point the scanner at an arbitrary firmware binary. (TLS certificate pinning deliberately deferred to the signed-manifest work: pinning GitHub's rotating roots risks bricking field OTA.)

### Added

- **Configurable status LED pin** — new optional `led_pin` setting on the config page (Hardware section). Empty means the board default; overrides are validated against per-board rules (strap pins, flash pins, input-only pins, PSRAM-reserved pins, NFC reader pins, and enabled-feature pins) at the NVS boundary and fall back to the default with a serial warning if invalid. (#203, closes #196)
- **Heap and per-task stack instrumentation** — every FreeRTOS task now self-reports its stack high-water mark, logged with heap stats (free / min-ever / largest block) as `MEM:` lines every 60s on serial and the `/logs` web viewer. `/api/diagnostics` gains `min_free_bytes` and a per-task `stack_hwm_bytes` map. Baseline for the memory-recovery work: heap changes are validated against measured numbers from here on.
- **Tag writes verified by readback** — TigerTag, OpenTag3D, and OpenSpool writes re-read the written pages and compare against the intended data after every reported success. A mismatch triggers one full-rewrite recovery, then fails loudly instead of reporting success on a corrupted write. Matters more now that the ACK fix is nibble-permissive: the 4-bit ACK carries no CRC, so a noise byte can read as success. (#167)

### Improved

- **Spool-by-UID lookup streams instead of buffering** — the enrichment save and UID registration flows resolve a tag to its spool via the streaming nfc_id matcher (no spool-count cap, archived spools excluded), then fetch just the matched spool (~1KB), replacing an 8KB JSON buffer of the full spool list. Transport or parse failures during the lookup now abort the save with a 503 instead of falling through to create a duplicate spool. Second slice of memory-recovery phase 2.
- **Filament dedup search streams** — both the sync path and the enrichment save pull-parse the filament list over the HTTP stream with constant memory, replacing an 8KB JSON document plus a String holding the entire response body on each lookup (the sync copy ran on the stack-tight SpoolmanSync task). Matching semantics unchanged (exact material+color+name tier, material+color fallback, alpha-suffix-safe RGB compares); lookup failures abort instead of creating duplicates.
- **Spool-by-UID matcher is now constant-memory** — the shared nfc_id matcher behind sync, enrichment, registration, and weight deduction pull-parses the spool list over the HTTP stream; the previous filtered-DOM parse still grew with spool count. Malformed or truncated responses are now detected (including the parser's silent-stop failure mode, verified against the JSON library off-target) and abort the sync instead of reading as "spool not found" — closing the last paths where a transient lookup failure could create a duplicate spool.
- **Vendor lookups are constant-memory and duplicate-safe** — one streaming vendor matcher now backs the sync path, tag registration, the enrichment save, and the vendor-search API, replacing four separate DOM/string parses of the vendor list (one of them a 2KB document on the web task's stack). Transient lookup failures return an error instead of a false "not found", so a network blip can no longer create a duplicate vendor.
- **Filament-search API streams** — the enrichment page's filament finder pull-parses the filament list and then fetches only the matched filament, replacing a 4KB list parse. It now prefers the canonical (bare-named) filament for a material+color over the first list-order match, and reports lookup failures as errors instead of "not found".
- **Spool picker proxy is chunked-transfer safe** — the spool list pass-through now requests HTTP/1.0, so a Spoolman behind a reverse proxy that chunks responses no longer risks chunk framing corrupting the relayed JSON.
- **`/api/diagnostics` reports the last reset reason** — `reset_reason` field (e.g. `poweron`, `panic`, `task_wdt`, `brownout`) so field devices can distinguish crashes from power cycles without serial access.

### Fixed

- **TFT and NFC reader no longer share an SPI bus on ESP32-S3** — the PN5180 driver requested `HSPI`, which is the SPI2 peripheral on classic ESP32 but maps to SPI3 on the S3: the same host the TFT claims on the S3-DevKitC. Both drivers arbitrating one peripheral corrupted NFC transactions (spurious `transceiver state` errors, corrupted UID reads) and could hold the bus long enough to starve the scan task into a `task_wdt` reboot every 8–18 minutes with the display enabled. The PN5180 now takes SPI2 on the S3 (the S3-Zero, whose TFT owns SPI2, keeps SPI3 via build flag). Found via new RTC-noinit crash forensics, which also ship in this release: after a watchdog reset the boot log reports exactly which scan phase died.
- **Scanner reboot loop at marginal tag coupling** — the NFC scan task fed its 30-second watchdog only once per scan cycle, but a single cycle can legally grind for minutes when RF operations fail slowly (each bounded at 1–2s) across a multi-page read or write: a wedged transceiver at marginal coupling caused `task_wdt` panic reboots every 8–18 minutes. The watchdog is now fed per block inside the read/write ladders, so it still catches genuine hangs but no longer reboots the scanner for slow RF failure grinds. Root-caused from `reset_reason` + panic dumps on a flapping-coupling bench rig.

- **Spool archive/recreate churn on sync** — three interacting defects (#218): the re-tag check archived spools whenever the resolved filament *id* differed, but dedup can resolve the same physical filament to a different id (user-named filaments, enrichment-created variants, tag formats that coerce the brand — TigerTag has no custom vendors). Explicitly picked spools (writer picker → pending link) were archived seconds after linking. Now: archive only on a real material/color change, a just-picked spool is never archived or re-pointed, filament dedup falls back to material+color when the name doesn't match, and the enrichment save searches for an existing filament even when the vendor is unknown instead of creating vendorless duplicates. Repro'd against a clean Spoolman v0.23.1: 3 seeded spools became 7 spools + 8 filaments in three writes; after the fix the same flow reuses the picked spools. User links are now durable: linking stamps the spool (`nfc_link` extra, auto-registered), and a user-linked spool is never auto-archived or re-pointed by sync — weight still updates; changing identity requires another explicit link. The pending-link PATCH also now merges the spool's existing extras instead of replacing the whole map (it silently wiped `tag_format` and middleware fields before).
- **PN5180 tag writes failing at marginal coupling** — three compounding bugs (#212): the 4-bit NTAG write ACK was exact-matched against `0x0A` so real ACKs with undefined upper bits (e.g. `0x2A`) were treated as failures; `mifareHalt()` left the transceiver parked in WaitReceive, wedging the state machine when the retry path reconfigured RF mid-transceive (`sendData state=3/0` errors); and the retry ladder tore down a still-ACTIVE session instead of simply resending the write. Multi-page writes that previously spiraled for 20+ seconds and failed now complete. (#212, reported in #194)

## [1.7.6] - 2026-07-02

### Added

- **Spoolman links on the reader page** — every Spoolman ID shown after a scan is now a link that opens that spool's page in Spoolman's web UI (new tab). Also fixes the OpenSpool reader view's existing link, which used a hash route that landed on Spoolman's home page instead of the spool. (#179)
- **Prusa MK4 / MMU3 HA blueprints** — two Home Assistant blueprints (`spoolsense_prusa_binding.yaml` + `spoolsense_prusa_lifecycle.yaml`) implementing per-slot Prusa spool tracking via HA orchestration. Default model is one scanner per slot (5 for MMU3). Pre-print material check uses bidirectional substring match; post-print deduction publishes per-tool grams via `cmd/deduct/<UID>` MQTT (reuses v1.6.14 path). Cancel proration by last-known progress %. PrusaLink local API has no slot-binding writes, so HA orchestration is the only path. Setup guide at [spoolsense.org/installation/prusa](https://spoolsense.org/installation/prusa/). Untested on real Prusa hardware. (#187, #195)
- **`tag_format` saved to Spoolman extras on enrichment save** — the TigerTag, OpenTag3D, and OpenSpool writer pages now record the tag's format (`tigertag`, `opentag3d`, `openspool`) in the spool's `Tag Format` extra field, matching what auto-sync already writes. Spool updates merge with existing extras so middleware-managed fields (like Happy Hare's MMU Gate) are preserved. (#185)

### Fixed

- **Tag present/removed flapping with stationary tags** — a single failed read (RF hiccup, marginal coupling) immediately declared the tag removed, and the next 50ms poll re-detected it. Every subscriber flapped with it: MQTT `present`, the HA sensor, displays. Removal is now debounced behind 3 consecutive misses (~150ms). Reported by two users (serial log flapping every ~5s; HA sensor "hopping").
- **WiFi association failures on ESP32-C3 (SuperMini / SuperMini Plus)** — station init now clears stale association state and sets mode + sleep configuration before `WiFi.begin()`. Boards that previously fell back to AP mode on every boot connect normally. Connect window extended from 15s to 30s, and the WiFi status code is logged during the wait and on failure so unsuccessful connects are diagnosable from serial.
- **Bambu deduction blueprint template error** — `tray_index` used `loop.index0`, which is only defined inside Jinja `{% for %}` blocks, not HA `repeat:` actions. Newer Home Assistant versions raise `UndefinedError: 'loop' is undefined` and the deduction never fires. Now uses `repeat.index - 1`. Re-import the blueprint after updating. (#200)

---

## [1.7.5] - 2026-04-29

### Added

- **Snapmaker U1 direct-mode integration** — scanner pushes scan results to a Snapmaker U1's external filament-detection endpoint via HTTP. New **Snapmaker U1 Integration** section on the config page binds a scanner to a toolhead channel (0–3). All 6 tag formats route through this path: smart tags publish on detection, generic UID tags publish after Spoolman lookup. Per-material temperature defaults (PLA/PETG/ABS/ASA/TPU/PVA/PC/PA) fill gaps for tags missing data; Spoolman augment overlays extra fields without overwriting on-tag values. Hostname auto-suggest sets `spoolsense-tN` when a channel is picked, simplifying multi-scanner deployments. Requires [paxx12 Extended Firmware](https://github.com/paxx12/SnapmakerU1-Extended-Firmware) (develop branch) with **Filament Detection: External** enabled in the firmware-config page. (#98, #182)

---

## [1.7.4] - 2026-04-22

### Added

- **ESP32-C3 SuperMini board variant** — new `esp32c3` PlatformIO env targeting HORNAXYS/Waveshare C3 SuperMini boards. Scoped to NFC reader (PN5180 or PN532) + I2C LCD + WS2812 status LED; TFT and keypad intentionally unsupported (single usable SPI controller and tight pin budget). `PIN_PN5180_RST` wired to GPIO 0 rather than a strap pin so the reader cannot force the board into download mode at POR. (#171, #172)
- **`roomonthethird` case** — STEP/STL/F3D files for the SpoolSense NFC reader case organized under `usermods/roomonthethird/`.

---

## [1.7.3] - 2026-04-20

### Added

- **WiFi keep-awake toggle** — new config option disables ESP32 modem sleep so the scanner stays consistently reachable on the network, at the cost of slightly higher idle current. Defaults off. (#163)
- **`roomonthethird` case files** — community-contributed STEP/STL/F3D case models for the NFC reader, under `usermods/roomonthethird/`. (#165)

### Improved

- **TigerTag partial writes** — writes pre-read pages 4–13 and only send pages whose contents actually changed (grouped into contiguous runs). Full-rewrite fallback on any run failure preserves tag consistency. Reduces NFC transaction time and tag wear on field-level edits. (#13, #164)

### Changed

- **`sendSpoolDetectedMessage` renamed to `sendOpenPrintTagMessage`** — original name was misleading; the function only fires for OpenPrintTag detections. Internal rename, no user-facing behavior change. (#159, #162)

---

## [1.7.2] - 2026-04-14

### Added

- **Bambu Lab MIFARE Classic tag reading** — scanner authenticates to Bambu tags using HKDF-derived MIFARE Classic keys and decodes material, color, weight, remaining filament length, nozzle/bed temperature, and dry time. Published via MQTT `tag/state` and `/api/status`, and shown on the reader page alongside other tag formats. (#24)

### Changed

- Added `.superpowers` and `.DS_Store` to `.gitignore`.

---

## [1.7.1] - 2026-04-13

### Added

- **Pre-emptive spool link in writer flow** — when writing a tag after picking an existing Spoolman spool, the picker selection now links the spool's `nfc_id` to the written tag instead of the auto-sync creating a duplicate Spoolman entry. (#130)
- **Writer Read enrichment polling** — writer pages poll briefly after a tag read to let Spoolman enrichment complete before rendering, plus a format-detection grace period. Avoids partially-populated forms. (#101)

### Fixed

- Writer Read now restores all fields for a full round-trip (prior behavior left some fields blank). (#101)
- Clear stale fields and cancel prior enrichment timer when starting a new Read.
- Skip `remaining_g` in enrichment payloads when the value is 0 to avoid wiping valid weight. (#128)
- Default `initial_weight` to 1000g and derive `remaining` when Spoolman has no weight set. (#128)
- Firefox stale-cache `ReferenceError` on writer pages — shared CSS/JS cache now versioned via `FIRMWARE_VERSION`. (#151)
- Timestamp ordering in pending-link flow to prevent a write-vs-sync race condition.

### Changed

- **Pinned `espressif32@6.10.0`** in `platformio.ini` to prevent auto-upgrade to Arduino 3.x (breaking API changes in Arduino 3.x — e.g. `WiFiClient` includes, `esp_task_wdt_init` signature).

---

## [1.7.0] - 2026-04-11

### Added

- **Bambu AMS tray dashboard on TFT** — persistent colored grid showing AMS tray status (material, color, weight) on the 240x240 TFT display. Auto-detects grid layout: 2x2 for 1-4 trays, 2x4 for 5-8, 4x4 for 9-16. (#148)
- **HA dashboard blueprint** — new `spoolsense_bambu_dashboard.yaml` watches Bambu AMS tray entities and publishes tray state to the scanner via MQTT.
- **HA tray assignment** — loading blueprint sends `cmd/tray_assign` to store UID-to-tray mapping on the scanner. No HA input_text helpers needed.
- **HA deduction by tray** — updated deduction blueprint sends `cmd/deduct_tray` with tray index. Scanner resolves tray to UID and deducts from Spoolman.
- **Spoolman weight on dashboard** — after tray assignment, scanner queries Spoolman for remaining weight and displays it on the dashboard.
- **NVS dashboard cache** — tray state persists across reboots for instant display on power-on.
- **Configurable low-spool threshold** — replaces hardcoded 100g, configurable via web config page. (#137)
- **cmd/low_spool MQTT handler** — middleware/HA triggers LED breathing mid-print. (#137)
- **Bambu AMS deduction via scanner** — cmd/deduct with Spoolman-direct fallback for non-writable tags. (#141)

### Improved

- Dashboard reverts to tray view 5 seconds after scanning a spool.
- Boot sequence skips WiFi/ready screens when dashboard is enabled.
- MQTT client buffer increased to 2048 bytes for large tray payloads.
- Millis() wrap-safe timer for dashboard revert.

### Fixed

- Fixed broken input_text helper writes in loading blueprint (silently failed since helpers were never created).
- Fixed cold boot blank screen when Bambu dashboard enabled.
- Fixed material string null termination in tray_update handler.

### Breaking Changes

- Loading blueprint (`spoolsense_bambu_ams.yaml`) now requires `scanner_device_id` input — re-import and recreate automation.
- Deduction blueprint (`spoolsense_bambu_deduction.yaml`) rewritten to use `cmd/deduct_tray` — re-import and recreate automation.

---

## [1.6.17] - 2026-04-10

### Added

- **Web serial log viewer** — live scanner output at spoolsense.local/logs with auto-scroll, copy button, and clear. Captures tag detection, Spoolman sync, MQTT status, write outcomes, and errors. Linked from landing page and troubleshooting page. (#139)

### Fixed

- **Low-spool LED breathing for non-writable tags** — tags without weight data (OpenSpool, GenericUID, TigerTag) were not triggering low-spool LED breathing. Now evaluates Spoolman-provided remaining weight after sync and triggers breathing when below threshold. (#137)
- **Default initial_weight to 1000g** — spools created without an initial weight now default to 1000g instead of omitting it, preventing Spoolman from showing 0g remaining. (#128)
- **Removed debug timing instrumentation** — TIMING printf lines from PN5180 reliability work removed from production builds.

### Changed

- **Web flasher auto-update** — release workflow now automatically updates spoolsense.org firmware binaries and manifests via GitHub Actions dispatch.

---

## [1.6.16] - 2026-04-09

### Fixed

- **Tag write failures on PN5180** — write path now uses the active tag session from detectTag instead of resetting the RF field and failing to reactivate. Fixes "tag reactivation failed" errors when writing via web UI.
- **Spool duplication via web UI** — register-uid endpoint now checks for existing filaments and spools before creating. Previously every tag write created duplicates in Spoolman. (#134, #135, #130)
- **PN5180 sendData reliability** — poll for WaitTransmit state with 10ms timeout instead of single check. Eliminates transceiver state errors on fast tag transitions. (#111)

### Changed

- **Tag session kept alive between reads** — first page read preserves the active session for follow-up NDEF reads. Extended read time reduced from 1080ms to 420ms. Quick-tap OpenTag3D now reliable. (#126)
- **Writer page UX** — status messages now remind users to keep the tag on the scanner during write and verification.

---

## [1.6.15] - 2026-04-09

### Added

- **NTAG variant detection** — sends GET_VERSION (0x60) during tag detection to identify exact NTAG model (213/215/216, Ultralight EV1). Write paths validate tag page capacity before writing. Variant exposed in `/api/status` as `ntag_variant` and `ntag_pages`. (#22)

### Changed

- **PN5180 tag session optimization** — tag stays active between detection and first page read, eliminating redundant setupRF + activateTypeA. First page read: 741ms → 96ms (87% faster). Total detect-to-data: ~2000ms → ~1340ms. (#111)
- **Consolidated MQTT tag/state JSON** — replaced 6 duplicate snprintf JSON builders across ApplicationManager and HomeAssistantManager with shared `TagStateJson.h` using ArduinoJson. One struct, one builder — any field change happens in one place. (#125)

---

## [1.6.14] - 2026-04-08

### Added

- **Filament deduction storage** — middleware sends usage deductions via MQTT (`cmd/deduct/<uid>`) after prints. Scanner stores them in NVS and applies to writable tags on the next scan. Deductions persist across reboots, accumulate between prints, and clamp to zero on apply. (#123)
- **DeductionManager** — new singleton handling NVS storage + tag write-back for OpenPrintTag (aux region partial write) and OpenTag3D (NDEF rebuild)
- **Immediate apply** — if the tag is on the scanner when the deduction arrives, it writes immediately instead of waiting for the next scan

### Fixed

- **NVS clear after enqueue** — pending deduction is only cleared after the NFC write is successfully queued, not before (prevents data loss if write queue is full)
- **Case-insensitive UID match** — MQTT deduct handler uses `strcasecmp` for UID comparison to handle mixed-case UIDs from middleware
- **OpenTag3D weight precision** — uses `lroundf()` instead of truncating cast when converting deduction to uint16_t

---

## [1.6.13] - 2026-04-07

### Added

- **MQTT `tag_format` field** — `spoolsense/<device_id>/tag/state` payload now includes `tag_format` so the middleware knows whether weight writeback is supported. Values: `openprinttag`, `tigertag`, `opentag3d`, `openspool`, `uid_only`, `unknown`. Unblocks middleware filament usage deduction (#122).

---

## [1.6.12] - 2026-04-06

### Changed

- **Shared write flow UX** — extracted duplicated write/read/enrichment logic from all 4 writer pages into SharedJS.h. Net -476 lines, ~19KB flash saved. (#96)
- **Write verification strengthened** — OpenSpool verifies brand/material/color, OpenTag3D verifies material/manufacturer/weight, TigerTag adds weight check. Previously only checked tag_kind.

### Fixed

- **Spoolman enrichment remaining weight** — Spoolman uses `used_weight` (calculated), not `remaining_weight` (read-only). Enrichment save now sends `initial_weight` + `used_weight` so remaining weight actually persists.
- **Spoolman spool lookup by nfc_id** — `extra_field` filter was returning all spools. Now fetches and matches `nfc_id` client-side, skipping archived spools.
- **S3-DevKitC LED color order** — onboard WS2812B uses GRB, not RGB. Red was displaying as green.
- **waitForTag text** — no longer says "NTAG" since OpenPrintTag uses ISO15693 tags.
- **Discord release notifications** — release workflow now sends changelog to Discord.

---

## [1.6.11] - 2026-04-05

### Added

- **PSRAM sprite allocation** — TFT sprite buffer allocated from PSRAM with 16-bit color depth on ESP32-S3-DevKitC-1-N16R8. Falls back to 8-bit internal RAM if PSRAM unavailable. (#115)
- **NTP retry** — if NTP fails at boot, retries every 30 seconds non-blocking until time is obtained. (#109)
- **NFC non-halt** — NFC reader failure no longer halts the device. Web UI remains accessible for debugging.

### Fixed

- **Cross-core thread safety** — `volatile bool` replaced with `std::atomic<bool>` for `stopRequested_` and `connected_` in HomeAssistantManager. (#108)
- **addToRecentSpools mutex guard** — acquires `tagMutex` before reading shared `currentSpool` data to prevent stale reads. (#106)
- **Write queue drain before restart** — config save and OTA restart wait for pending NFC writes to complete before rebooting. (#107)
- **OPI PSRAM config** — `qio_opi` memory type for N16R8 octal PSRAM. Fixes "PSRAM chip not found" error.
- **TFT backlight guard** — skip PWM setup when backlight pin is not assigned. Fixes `ledc_channel_config` error.

---

## [1.6.10] - 2026-04-05

### Added

- **GC9A01 round TFT display** — 240x240 round TFT support as an alternative to the ST7789 square TFT. Runtime selectable via the config page (TFT Driver dropdown). Same pins, same resolution, different panel driver. (#113)
- **ESP32-S3-DevKitC-1-N16R8 board support** — new build environment `esp32s3devkitc` for the 16MB flash / 8MB PSRAM variant. PN5180 on SPI2/FSPI + TFT on SPI3 (separate buses, no conflicts). PSRAM enabled for future use. (#114)

### Fixed

- **Centered SpoolSense header** — "SpoolSense" text centered at top of TFT display on all screens.

---

## [1.6.9] - 2026-04-05

### Fixed

- **Filament deduplication** — fetch all filaments per vendor and match client-side instead of relying on Spoolman's unreliable server-side filter. Case-insensitive matching on material, color, and name prevents duplicate filament creation. (#92)
- **HTTP connection reuse** — disabled TCP connection reuse for Spoolman API calls. Stale keepalive connections caused intermittent 400 errors on every request after the first. Root cause of the filament churn. (#92)
- **Filament lookup failsafe** — when Spoolman API returns an error, skip filament creation instead of creating a duplicate. Retries on next scan. (#92)
- **Filament naming convention** — filament name built from material + modifier (e.g. "PLA Silk", "PETG CF") for consistent deduplication across tag formats. (#103)
- **Temperature averaging** — when a tag stores min/max nozzle or bed temps, average them for Spoolman's single-value field instead of silently dropping one. (#100)
- **Diameter default** — default to 1.75mm when diameter is not specified by the tag or user, at every layer. TigerTag now converts its diameter ID to mm. (#102)

---

## [1.6.8] - 2026-04-04

### Added

- **Spoolman enrichment on reader page** — smart tag scans (OpenTag3D, TigerTag, OpenSpool, OpenPrintTag) now trigger a Spoolman UID lookup. Spoolman-sourced fields (remaining weight, bed temp, spool ID) appear inline with a blue "Spoolman" badge. (#97)
- **Spoolman Enrichment section on writer pages** — OpenSpool, TigerTag, and OpenTag3D writers gain a bottom section for fields Spoolman can store that the tag format cannot. Editable and saved to Spoolman on Write Tag with vendor/filament deduplication and user confirmation. (#97)
- **Read button on all writer pages** — queues the scanner to read an existing tag for re-write scenarios. Fills both the tag form and enrichment section from a Spoolman UID match. (#97)
- **New API endpoints** — `GET /api/spoolman/find-vendor`, `GET /api/spoolman/find-filament`, `POST /api/spoolman/save-enrichment` for the enrichment write flow. (#97)
- **Spool picker UX** — results list hidden until user starts typing in the search box.

### Fixed

- **OpenSpool enrichment bed temp** — collapsed to a single field (Spoolman stores one value, not min/max).
- **Enrichment persistence** — Spoolman enrichment data persists on reader page after tag removal until the next scan.
- **Vendor/filament confirmation** — user's choice to decline reusing an existing vendor or filament is now respected (creates new entry instead of silently reusing).

---

## [1.6.7] - 2026-04-03

### Added

- **OpenSpool tag format support** — reads and writes OpenSpool NFC tags (NTAG215/216 with NDEF JSON payload). Standalone parser detects `application/json` NDEF records with `"protocol":"openspool"`. Writer page at `/writer/openspool` with Spoolman spool picker integration. Reader page shows brand, material, color, and nozzle temps. TFT "OS" label (pink). Logo on landing page. Nav link on all pages. (#93)

### Fixed

- **Duplicate spool prevention** — reuse existing spool when Spoolman archive fails instead of creating a duplicate. Prevents the cascade where intermittent API failures caused new spools on every scan. (#91)
- **Zero weight Spoolman rejection** — omit weight, density, and diameter fields from Spoolman API when zero. Fixes 422 errors on Spoolman 0.23.x which rejects `"weight": 0`. (#91)
- **JSON injection in write API** — OpenSpool write endpoint uses ArduinoJson to build tag payload instead of snprintf with raw user input. (#93)

---

## [1.6.6] - 2026-04-02

### Fixed

- **Spoolman extra fields auto-register** — scanner now checks and creates required Spoolman extra fields (dry_temp, dry_time_hours, aspect, nfc_id, tag_format, active_toolhead) on first sync. Versioned NVS flag skips API calls on subsequent boots. Fixes filament auto-create 400 errors on Spoolman 0.23.x. (#87)
- **Vendor lookup deduplication** — fetch all vendors and match client-side instead of unreliable ?name= filter. Prevents duplicate vendor creation on every scan. (#87)
- **JSON error response escaping** — sendError() now escapes quotes, backslashes, newlines, and control characters. Prevents malformed JSON responses. (#77)
- **Write/OTA return value checks** — format-tag and TigerTag write endpoints return 503 when queue full. OTA task creation failure returns 500 and resets state. (#78)

---

## [1.6.5] - 2026-04-02

### Added

- **Configurable mDNS hostname** — set a custom hostname via the config page for multi-scanner setups (AFC lanes, toolchangers). Default remains `spoolsense.local`. Also sets WiFi DHCP hostname. Server-side and client-side validation. (#82)

### Fixed

- **snprintf buffer overflow in HA JSON** — clamped buffer length before conditional appends and closing brace in `handleSpoolDetected` to prevent memory corruption on long payloads. (#79)
- **Monotonic NFC write request IDs** — replaced `millis()`-based IDs with centralized `NFCManager::generateRequestId()` counter to prevent collisions across batched writes. (#80)
- **processMessages queue cap** — capped message loop at `QUEUE_SIZE` iterations to prevent timer starvation from rapid message bursts. (#81)

---

## [1.6.4] - 2026-04-01

### Added

- **TFT text labels** — replaced unreadable bitmap icons with colored text labels (OPT, TT, OT3D, Bambu, NFC+). Wired BambuTag mapping in ApplicationManager. Saves ~2KB flash. (#66)
- **Scan cooldown** — 3s cooldown suppresses PN5180 partial UID double-scans that caused false "Generic Tag Not in Spoolman" errors. (#70)
- **Keypad pin rearrangement** — WROOM keypad pins reordered to match physical ribbon connector order for easier wiring. Breaking change — existing keypad users must re-wire. Thanks @mrsimicsak. (#69)

### Fixed

- **HTTP mutex on WebServerManager** — Spoolman proxy, UID registration, and spool link handlers now acquire g_httpMutex. Prevents races with background Spoolman sync that caused intermittent HTTP 400 errors. (#75)
- **URL-encode manufacturer** — vendor names with spaces (e.g., "Bambu Lab") no longer break UID registration queries. (#76)
- **ApplicationManager code review fixes** — shadowed variable rename, deduplicated ifdef blocks, NATIVE_TEST guard, HA field rename.

---

## [1.6.3] - 2026-03-31

### Added

- **HA state retention after tag removal** — spool data (material, color, weight, temps) persists in HA sensor after tag is removed, with `present: false`. Dashboards now show last scanned spool instead of going blank. (#60)
- **Tag writer: populate from Spoolman** — searchable spool picker on all 3 writer pages (OpenPrintTag, TigerTag, OpenTag3D). Pick a spool from your Spoolman inventory and the form auto-fills. Shows hint when Spoolman is not configured. (#32)

### Fixed

- **[P1] Spoolman streaming UID lookup** — replaced ArduinoJson bulk parse with streaming HTTP parser for spool lookups. Fixes NoMemory crash on 30+ spools that broke all tag format syncs. Uses ~600 bytes instead of ~60KB. Works with any database size. (#68)
- **HA publish queue** — increased from 6 to 12 items and added drop logging. Previously silently dropped messages when MQTT was disconnected. (#28)
- **TFT null queue guards** — all queue operations now check for null before access. Prevents crash if queue allocation fails.
- **TFT queue depth** — increased from 4 to 8 with drop logging for burst traffic.
- **TFT showText4** — was discarding lines 1-2 on generic tag scans. Now shows "Spool: Unknown Tag" instead of just "Unknown Tag".
- **TFT duplicate struct removed** — eliminated TFTSpoolData (duplicate of DisplaySpoolData with unused name field). Saves 48 bytes per queued message.
- **TFT startTask check** — now verifies task creation succeeded instead of logging success unconditionally.

---

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
