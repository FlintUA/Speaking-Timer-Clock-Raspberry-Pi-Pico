# Release verification

## Native tests

No Pico SDK is needed for these tests. With a C11 compiler and CMake:

```sh
cmake -S firmware/pico2w_c/tests -B build/host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-tests --config Debug
ctest --test-dir build/host-tests -C Debug --output-on-failure
```

GitHub CI runs this suite on Windows and Linux.

The five suites cover:
- Alarm validity, scheduling, simultaneous slots and duplicate suppression.
- Calendar conversion, leap days, Europe/Berlin transitions, NTP era rollover, invalid headers and mismatched request nonces.
- MAC parsing, unique combined catalogs and preservation of useful saved names.
- Settings v1-v7 decoding and migration to v8, Wi-Fi fields, extended alarm tracks, A/B fallback after corruption, failed writes and refusal to erase unknown sectors. Flash is simulated, so this is not a physical Flash endurance test.
- Git-derived versions: no tags, exact lightweight and annotated tags, commits after a tag, tracked changes, unrelated tags and source archives.

## Firmware builds

Default build:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3
```

Bluetooth-only fallback:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3 -NoWifi
```

The script supports -BuildDirectory for an ASCII build path and -HelixPath for an existing external decoder checkout. It records the Git revision and marks tracked uncommitted changes as dirty. Release builds must have a clean source revision.

## Automatic firmware version

Version strings are generated from reachable local Git release tags matching vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-prerelease. Both lightweight and annotated tags are supported. The generator runs at configuration and before each firmware build; no manual version edit or cache reset is required.

- Clean checkout at v1.0.0: USB version is 1.0.0; About shows v1.0.0.
- Three commits after that tag: USB version is 1.0.0+3.gHASH; About shows v1.0.0+.
- Tracked local edits append .dirty to the USB metadata; About also shows +.
- No reachable release tag: 0.0.0+gHASH, with + on About.
- No Git metadata or Git executable, including source archives: 0.0.0+unknown. An archive does not claim the current release version. Use a Git checkout for an identified release build.

About reserves the second line for by FlintUA; version text is bounded to the LCD's 16 columns. USB retains the full version. The existing build ID separately identifies source revision and enabled profiles.

Fetch tags before building if the checkout does not have them: git fetch --tags. A shallow checkout must include a reachable release tag, or fetch full history using git fetch --unshallow --tags. Tag creation alone does not alter firmware already installed on a board; rebuild and flash it.

## Device acceptance before stable release

Follow [network acceptance](NETWORK_TIME.md#hardware-acceptance) and [firmware verification](../firmware/pico2w_c/README.md#on-device-verification). In addition:
- Start discovery, select no new device and exit; the preferred connection must resume.
- Find a saved speaker again; it must appear once and keep its useful name even if discovery reports Unknown.
- Test a music alarm above track 45 with a real matching file.
- Check version after updating and retain the UF2 build-ID sidecar.
- Run the assembled clock overnight with the actual access point, Portable/Nest, RTC, SD and DFPlayer.

Earlier hardware passes are retained in [HARDWARE_TEST_EVIDENCE.md](HARDWARE_TEST_EVIDENCE.md); they do not imply that newly added features have passed device testing.
