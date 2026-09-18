# Third-Party Licenses

SpoolSense Scanner itself is licensed under Apache-2.0 (see [LICENSE](LICENSE)).
The firmware binaries also contain the libraries below, each under its own
license. This file is the notice for them.

## LGPL libraries

These libraries are compiled into every firmware binary and are covered by the
GNU Lesser General Public License. Their use does not change the license of
SpoolSense Scanner's own code.

| Library | Version | License | Source |
|---|---|---|---|
| RFID_MFRC522v2 (RC522 reader) | 2.0.6 | LGPL-2.1 | https://github.com/OSSLibraries/Arduino_MFRC522v2 |
| Keypad | 3.1.1 | LGPL-2.1 | https://github.com/Chris--A/Keypad |
| PN5180 Library (modified, vendored in `lib/PN5180/`) | — | LGPL-2.1-or-later | https://github.com/ATrappmann/PN5180-Library |
| Adafruit NeoPixel | 1.15.4 | LGPL-3.0-or-later | https://github.com/adafruit/Adafruit_NeoPixel |

License texts: [LGPL-2.1](licenses/LGPL-2.1.txt), [LGPL-3.0](licenses/LGPL-3.0.txt),
and [GPL-3.0](licenses/GPL-3.0.txt), which the LGPL-3.0 text builds on.

### Rebuilding with a modified library

The LGPL gives you the right to run the firmware with your own version of
these libraries. Everything needed is in this repository:

- The complete firmware source, including the modified PN5180 library in
  `lib/PN5180/`.
- The exact library versions, pinned in `platformio.ini`. Replace a pinned entry
  with your own copy or fork, or edit the vendored source directly.
- Build and flash steps for every supported board in
  [CONTRIBUTING.md](CONTRIBUTING.md).

Each release is built from the tagged commit of the same version.

## Other libraries

| Library | License |
|---|---|
| ArduinoJson | MIT |
| PubSubClient | MIT |
| Adafruit BusIO | MIT |
| base64 | MIT |
| Adafruit PN532 | BSD |
| LovyanGFX | MIT and BSD-2-Clause |
| htcw_json, htcw_io, htcw_bits (vendored in `lib/`) | MIT |
| LiquidCrystal_I2C | No license stated by its author |
