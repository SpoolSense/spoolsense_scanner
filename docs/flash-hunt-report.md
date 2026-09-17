# Flash-hunt result — 2026-09-17

The lossless OpenTag3D logo re-encoding saves **17,184 bytes of ESP32-C3 firmware**, exceeding both 10,000 bytes and 10 KiB. All seven firmware builds and their unchanged 97% budget checks passed. All ten native suites passed after a clean build.

Work was performed on `fix/opentag3d-merge-316`, based on `4da9fd0`, as required by the brief. The only firmware file changed is `src/OpenTag3DLogo.h`. No feature, executable code, build flag, platform pin, partition, budget threshold, dependency, writer page, or changelog was changed.

## Measured C3 result

| Measurement | Bytes |
|---|---:|
| Baseline firmware.bin, rebuilt before editing | 1,906,800 |
| Final firmware.bin | 1,889,616 |
| Firmware saving | **17,184** |
| app0 partition | 1,966,080 |
| Unchanged 97% budget, rounded down | 1,907,097 |
| Budget headroom before | 297 |
| Budget headroom after | **17,481** |
| Physical partition space remaining | 76,464 |

The embedded PNG decreased from 31,951 to 14,764 bytes: 17,187 asset bytes. The measured firmware saving is 3 bytes smaller because the final binary layout differs from raw asset length. The final ELF symbol table reports `OPENTAG3D_LOGO_PNG` as 14,764 bytes.

## Candidates evaluated

All asset sizes below were measured from encoded bytes. Only the selected palette encoding was installed and measured in a firmware build; the other rows make no claim about firmware savings.

| Technique | Measured asset result / saving | Firmware saving | Risk and disposition |
|---|---|---:|---|
| OpenTag3D: exact 256-colour palette, per-entry alpha, original metadata | 31,951 → 14,764 / **17,187 bytes** | **17,184 bytes** | Applied. Identical dimensions and decoded RGBA pixels; original sRGB and eXIf chunks retained. |
| OpenTag3D: optimized RGBA PNG, original metadata | 31,951 → 22,364 / 9,587 bytes | Not built | Not applied: below target; exact palette saves more without losing information. |
| OpenSpool: exploratory Pillow PNG conversion | 7,721 → 21,377 / **−13,656 bytes** | Not built | Not applied: increases size; original is JPEG. |
| TigerTag: exploratory Pillow PNG conversion | 7,781 → 23,320 / **−15,539 bytes** | Not built | Not applied: increases size; original is JPEG. |
| OpenPrintTag: exploratory Pillow PNG conversion | 4,238 → 13,806 / **−9,568 bytes** | Not built | Not applied: increases size; original is JPEG. |

The brief described all four arrays as PNGs. Inspection of their signatures and decoded formats showed that three are JPEGs despite their names. Those three headers and all HTTP handlers remain unchanged. The OpenTag3D logo is displayed at 48 CSS pixels high on the landing page; its original 832 × 247 dimensions were retained.

No lossy conversion, resizing, TFT compression, newlib changes, feature gates, compiler tuning, or shared-web-asset restructuring was evaluated. The brief says to stop once the target is exceeded and the required checks pass; no byte estimates are claimed for those unexplored options.

## Lossless verification

Using the existing Pillow 12.2.0 installation, all 256 distinct RGBA tuples were mapped to palette indices without quantization. RGB components are stored in PLTE and alpha components in tRNS. The original sRGB and eXIf chunks were copied byte for byte. The array identifier, PROGMEM storage and `sizeof` length expression are unchanged.

A separate verification extracted the old array directly from `git show 4da9fd0:src/OpenTag3DLogo.h` and compared it with the final header. It checked every PNG chunk CRC, dimensions, all 205,504 decoded RGBA pixels, metadata equality, and the unchanged length expression. All assertions passed. Both decoded RGBA byte streams have SHA-256:

```text
c70d815eafe85ee9f3d2a2bc28f0ca3c222250eceba0dc39f29f50869d872548
```

The conversion script and before/after PNGs are retained locally under `/tmp/flash-hunt/`. The script was run as `python3 /tmp/flash-hunt/palette_logo.py`; it writes a candidate header in that temporary directory, which was then copied to `src/OpenTag3DLogo.h`. It has no firmware build-time dependency.

## Seven-environment build sweep

Executed from the repository root:

```sh
for e in esp32dev esp32s3zero esp32c3 esp32s3devkitc esp32c6 esp32c5 seeed_xiao_esp32c6; do
  pio run -e "$e" > "/tmp/flash-hunt/build-$e.log" 2>&1 &&
    python3 scripts/check_flash_budget.py "$e" || { echo "FAILED: $e"; exit 1; }
done
```

The loop returned exit status 0. Every build reported SUCCESS; every budget command returned 0 with its default 97% threshold.

| Environment | firmware.bin bytes | app0 bytes | Usage | Build / budget |
|---|---:|---:|---:|---|
| esp32dev | 1,808,208 | 1,966,080 | 92.0% | PASS / PASS |
| esp32s3zero | 1,767,504 | 1,966,080 | 89.9% | PASS / PASS |
| esp32c3 | 1,889,616 | 1,966,080 | 96.1% | PASS / PASS |
| esp32s3devkitc | 1,772,816 | 1,966,080 | 90.2% | PASS / PASS |
| esp32c6 | 1,903,616 | 3,145,728 | 60.5% | PASS / PASS |
| esp32c5 | 1,965,760 | 3,145,728 | 62.5% | PASS / PASS |
| seeed_xiao_esp32c6 | 1,763,248 | 1,966,080 | 89.7% | PASS / PASS |

The builds emitted existing warnings, including deprecated ArduinoJson APIs; SUCCESS does not mean warning-free. Full build output is in the log files above. Baseline and first candidate builds also returned 0 using `pio run -e esp32c3` followed by `python3 scripts/check_flash_budget.py esp32c3`. Their logs are `/tmp/flash-hunt-baseline.log` and `/tmp/flash-hunt/esp32c3-candidate.log`.

The first sandboxed baseline attempt failed with PermissionError on PlatformIO's external `platforms.lock`. The subsequent approved build executions could access the existing package cache and succeeded.

## Native tests

The brief's `make clean` would delete an existing untracked test executable in the user's checkout. To honor the stronger preservation constraint, a fresh copy of tracked `test/native`, `src`, `include`, and `lib` was exported with `git archive HEAD` into `/tmp/flash-hunt/native-checkout`, and the changed logo header was copied into it. Tests and the Makefile were not modified.

Executed:

```sh
make -C /tmp/flash-hunt/native-checkout/test/native clean
make -C /tmp/flash-hunt/native-checkout/test/native
make -C /tmp/flash-hunt/native-checkout/test/native test
```

All three commands returned 0. The Makefile's `test` target executes the same ten suites requested by the brief. Actual final output from each suite:

```text
test_printer_manager:       18 tests, 18 passed, 0 failed
test_opt_ndef_bounds:       OK: 0 failure(s)
test_diagnostics:           OK: 0 failure(s)
test_tft_landscape_layout:  OK: 0 failure(s)
test_tigertag_parser:       OK: 0 failure(s)
test_bambutag_parser:       OK: 0 failure(s)
test_write_uid_guard:      All write UID guard tests passed
test_cc_sizing:            All CC sizing tests passed
test_spool_cache:          All spool cache tests passed
test_opentag3d_runner:     OK: 0 failure(s)
```

Full compiler and test output is retained in `/tmp/flash-hunt/native-build.log` and `/tmp/flash-hunt/native-tests.log`. Contrary to the brief, not every suite uses the literal `OK: 0 failure(s)` summary; the actual outputs above all indicate success.

## Repository verification checklist

1. `pio run -e esp32dev`: PASS, exit 0.
2. `pio run -e esp32s3zero`: PASS, exit 0.
3. Remote code review: not run; no PR is being opened. Local asset validation is described above.
4. New heap allocations: none; only a PROGMEM byte array changed.
5. Thread safety: no executable code or shared-state changes.
6. HTTP mutex acquisition: no request-handler changes.
7. Source inventory: no new firmware source files. Ignored `.mex` files were left untouched under the brief's preservation constraint.
8. DisplayI use: no display-code changes.
9. NFCConnectionI use: no NFC-code changes.
10. Queue null guards: no queue-code changes.
11. Task creation checks: no task-code changes.
12. Comment hygiene: one concise comment records the lossless encoding invariant.
13. Function complexity: no functions added or changed.
14. Function length: no functions added or changed.
15. Nesting depth: no functions added or changed.
16. `git diff --check`: PASS, exit 0.
17. Protected files: `git diff --quiet` returned 0 for budget script, CI workflow, both partition tables, platformio.ini, CHANGELOG.md, OpenTag3DWriterHTML.h, and SharedJS.h.

The repository has CLAUDE.md rather than a root AGENTS.md; its router and relevant architecture, conventions, and testing guidance were read. The testing pattern's old `espressif32@6.10.0` instruction conflicts with the task brief and current configuration; the required pioarduino 55.03.39 pin was preserved.

## Commit status and limitations

**No new commit SHA exists.** The requested commit was attempted with:

```sh
git add -- src/OpenTag3DLogo.h &&
  git commit -m 'Re-encode OpenTag3D logo losslessly to save 17,184 bytes on C3'
```

`git add` returned 128 because the sandbox could not create `.git/index.lock` (`Operation not permitted`); the commit command did not run. Per the standing instruction, the completed change is left in the working tree for the dispatcher to commit. Its measured individual C3 delta is **−17,184 bytes**. This report is a new, uncommitted file.

Nothing was pushed, tagged, or submitted as a PR. The pre-existing `.gitignore` edit and untracked user files were left alone. Build-generated artifacts were updated only through the required PlatformIO commands; native cleanup was confined to the new temporary export.

No board was flashed, and no browser or physical-device visual check was performed. Pixel and metadata equality were verified offline. Remote review and hardware checks are not claimed. The #316 data-preservation fix itself remains separate work; this task provides measured flash space for it.
