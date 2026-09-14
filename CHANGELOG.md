# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.15.2] - 2026-09-15

### Changed

- **Battery fine-calibration factor is now per board.** `BATTERY_VREF_CALIBRATION`
  moved from `config.h` (shared) to `board_config.h` (per board): it corrects the
  ADC eFuse of a specific chip and the actual resistor tolerance, which differ
  between the C3 and the S3. The divider ratio (2.0) stays common. The C3 factor
  is set to 1.0098 (measured 2026-09-15: multimeter 4.11 V vs 4.070 V read, ~1 %);
  the S3 keeps 1.0 pending a measurement on the S3 itself.

## [0.15.1] - 2026-09-15

### Changed

- **S3 battery 0 % lowered from 3.0 V to 2.6 V.** The Breadvolt module cuts at
  2.4 V (over-discharge protection); 0 % now sits 0.2 V above that so the probe
  runs to the end instead of pinning at 0 % from 3.0 V. The plausibility guard
  (reject below MIN - 0.5 V = 2.1 V) still accepts a real near-empty cell. Note:
  below ~3.0 V the Li-ion curve collapses, so the last few percent drain fast (the
  gauge is honest, it does not extend runtime). S3 only; the C3 alkaline scale is
  unchanged.

## [0.15.0] - 2026-09-15

### Added

- **Battery reading enabled on the ESP32-S3 board.** The Breadvolt module outputs
  a constant regulated 3.3 V, so the cell is now tapped before the regulator: a
  100k/100k divider from battery+ to GP4 (`PIN_BATTERY_ADC = 4`), GP4 reading the
  midpoint (~2.0 V for 4.0 V). The firmware already had the full path; only the
  pin was flipped from -1. The shared divider ratio (2.0) and the Li-ion 3.0-4.2 V
  scale apply. The 4.28 V full-charge cell never reaches the pin directly (ADC
  limit). Wiring and calibration documented in `docs/cablage.md`.

## [0.14.2] - 2026-09-14

### Documentation

- **Wiring and notes brought in line with the current firmware.** Per-board TX
  power (C3 8.5 dBm, S3 11 dBm), unicast delivery with hardware ACK (green LED =
  frame acknowledged by the hub), S3 battery reading disabled, and the 5 min
  measurement cadence. No behaviour change.

## [0.14.1] - 2026-09-14

### Added

- **Packaging contract (`morfproject.json`)** so the probe is published in the
  parc releases like the other projects: a firmware project with two PlatformIO
  targets, `supermini` (ESP32-S3) and `c3oled` (ESP32-C3), each producing a
  renamed `firmware.bin`. Picked up by `package-all.py` as a companion repo
  ("extra"), without ever entering `morf doctor` scope.

## [0.14.0] - 2026-09-14

### Changed

- **Per-board TX power.** The two PCB antennas do not behave alike, so the level is
  now set per board in config.h (guarded by `SENSOR_BOARD_*`), each with its own
  commented menu: **S3 = 11 dBm**, **C3 = 8.5 dBm** (both proven delivered on
  field logs; the S3 at full power got no ACK at all).
- **Deep sleep re-enabled** (`ENABLE_DEEP_SLEEP = true`) now that the unicast link
  is confirmed on battery, outdoors. Continuous mode drains the 14500 in hours;
  deep sleep is the autonomous mode. The unicast chain already handles the wake
  path (waits for the ACK before sleeping, keeps the channel in RTC), and the
  on-failure channel re-scan covers a hub channel migration between wakes.

## [0.13.1] - 2026-09-14

### Fixed

- **S3 delivered nothing to the hub** (unicast NACK on every attempt) while the C3
  was delivered first try on the same channel and hub MAC. USB serial logs proved
  the frame left the S3 but the hub sent no ACK, at full TX power only. Root cause:
  the ESP32-S3 Super Mini's onboard PCB antenna is poorly matched, so full power
  degrades the signal past the point the hub can acknowledge it. **TX power is now
  capped on the S3 too** (`SENSOR_NEEDS_TX_LIMIT` in board_config.h), using the
  same `SENSOR_TX_POWER_LEVEL` knob as the C3 (8.5 dBm, proven-good). Raise it via
  the config.h menu to recover range while it stays delivered.

## [0.13.0] - 2026-09-14

### Changed

- **Transmission rewritten to unicast** (clean rebuild of the whole chain). The
  sonde now sends ESP-NOW **unicast to the hub MAC** (`ESPNOW_RECEIVER_MAC`)
  instead of broadcast. This gets the hub's 802.11 hardware ACK and MAC-layer
  retransmission, far more reliable outdoors — and makes `onDataSent` a *real*
  delivery signal: `SUCCESS` now means the hub acknowledged the frame. In
  broadcast mode the callback always returned success, so the sonde blinked green
  ("sent") while the hub received nothing.
- **Self-healing channel on send failure**: if a send gets no ACK after several
  attempts, the sonde re-scans the hub's SoftAP (`MH-NOW`), switches channel and
  retries within the same cycle, instead of emitting into the void until the next
  periodic recheck. Fixes the "chaotic at 300 s interval" behaviour (a channel
  migration between sparse sends was silently dropping whole samples).

### Notes

- The green status LED now means **delivered (ACK received)**, red means **not
  delivered**. Requires that `ESPNOW_RECEIVER_MAC` is exactly the hub's **STA**
  MAC (shown on the hub's Net page / serial `sta=` line).

## [0.12.2] - 2026-09-14

### Changed

- **Deep sleep temporarily disabled** (`ENABLE_DEEP_SLEEP = false`) to isolate a
  battery-only reception issue: on USB the hub receives every cycle, on the 14500
  cell nothing reaches it. In continuous mode the radio stays initialised and the
  cold-start current spikes of each wake are avoided; if data then flows on
  battery, the fault is specific to the deep-sleep wake path (likely a brownout on
  the wake Wi-Fi burst). Field diagnostic step, to be re-enabled once resolved.

## [0.12.1] - 2026-09-14

### Fixed

- No data reaching MeteoHub in deep-sleep mode (sonde sent "ok" but the hub
  received nothing). Two deep-sleep pitfalls:
  - **TX not completing before sleep**: `esp_now_send` (broadcast) only queues the
    frame; the radio then slept before it was transmitted. Now the sonde waits for
    the send callback (`onDataSent`, up to `ACK_WAIT_MS`) before returning.
  - **Fallback channel poisoning the RTC cache**: a failed first scan cached the
    default channel (6) in RTC, so every wake emitted on the wrong channel. The
    RTC cache is now updated only on a *confirmed* scan; a failed scan is a
    one-shot fallback and the next wake re-scans.

## [0.12.0] - 2026-09-14

### Added

- **Deep sleep enabled** (`ENABLE_DEEP_SLEEP = true`): wake → measure → send →
  deep sleep, every `SENSOR_MEASUREMENT_INTERVAL_SECONDS` (5 min). Fits the small
  14500 cell — continuous mode drained it in hours.
- **Channel kept in RTC memory** across sleeps and reused on wake, so the ~2 s
  Wi-Fi scan is not paid on every wake. It is still re-scanned periodically
  (`ESPNOW_RESCAN_EVERY_N_WAKES`, ~1 h at 5 min) to catch a hub channel change;
  `applyChannel` updates the RTC cache so any switch persists across sleeps.

## [0.11.0] - 2026-09-14

### Changed

- S3 Super Mini power is now a **Breadvolt module + 14500 Li-ion** (3.7 V,
  500 mAh) delivering a **regulated 3.3 V**. Since the board only sees the
  regulated rail (not the cell), **battery sensing is disabled on the S3**
  (`PIN_BATTERY_ADC = -1`): no false static percentage or low-battery alert. The
  module handles protection (2.4 V cutoff / 4.28 V) and shows charge via its
  CHG/PWR LEDs; when the cell empties the sonde simply stops (MeteoHub shows OUT
  absent). The Li-ion range (3.0-4.2 V) is kept for the case where the raw cell
  is tapped to GP4. The C3 is unchanged (alkaline).

## [0.10.0] - 2026-09-14

### Changed

- Measurement cadence is now `SENSOR_MEASUREMENT_INTERVAL_SECONDS` in config.h,
  **default 300 s (5 min)** instead of 30 s — weather changes slowly, and 30 s
  over-samples. Single source of truth (drives the continuous loop and the future
  deep-sleep cycle), easy to change for tests (30 / 120 / 300 / 600). The indoor
  side (MeteoHub) uses the same cadence so IN and OUT history stay homogeneous.

## [0.9.2] - 2026-09-14

### Docs

- Rewrite `docs/cablage.md` for both boards (C3 HW-675 and S3 Super Mini): full
  pinout tables, shared I2C sensors, alkaline battery wiring/range, and a
  dedicated status-LED (WS2812/NeoPixel) section explaining when it lights, why,
  and what each colour means (blue = working, green = frame sent, red = send
  failure, off = idle), including the broadcast "sent ≠ received" nuance.

## [0.9.1] - 2026-09-13

### Fixed

- S3 Super Mini battery range set to 2x alkaline (2.0-3.2 V) like the C3 (was
  Li-ion): both boards run on alkaline cells for now. A 0 % at 3.26 V seen
  earlier was on USB power (no cell on the divider), not a real reading.

## [0.9.0] - 2026-09-13

### Added

- **Adaptive ESP-NOW channel.** The hub follows the Livebox channel (not fixed),
  so the sensor now discovers it by scanning the hub's "MH-NOW" SoftAP beacon at
  startup (falling back to `ESPNOW_HUB_CHANNEL` if not found). In continuous mode
  it re-scans every `ESPNOW_CHANNEL_RECHECK_SEC` (5 min) and switches if the
  channel changed; the re-scan is skipped when the battery is below
  `ESPNOW_RESCAN_SKIP_BELOW_PCT` (a Wi-Fi scan costs power, and a battery-caused
  silence is not a channel problem). Deep-sleep wake re-scans via begin(). The
  broadcast peer now uses channel 0 (follows the radio), so switching channel
  needs no peer re-registration.

## [0.8.0] - 2026-09-13

### Changed

- Drop OLED management on the ESP32-S3 Super Mini to lower power consumption:
  `SENSOR_HAS_OLED` is no longer defined for the S3, so `DisplayManager` is a
  no-op and the screen is never powered on (it stays in its low-power reset
  state). U8g2 is dropped from the S3 build. The S3 is LED-only again; the C3
  keeps its integrated OLED.

## [0.7.3] - 2026-09-13

### Fixed

- C3 battery range corrected to **2x alkaline 1.5 V** (2.0-3.2 V), not LiFePO4:
  0.7.2 mislabelled the chemistry, so a nominal 3.0 V pack still read ~14 %. Now
  ~3.0 V reads ~83 %.
- The battery plausibility guard is now derived from the board's voltage range
  (±0.5 V) instead of a hard-coded Li-ion window, so a genuinely low pack (down
  toward MIN) is still reported instead of being dropped as "no divider".

## [0.7.2] - 2026-09-13

### Fixed

- Battery percentage used a Li-ion scale (3.3-4.2 V) for every board. The C3 runs
  on LiFePO4 (~2.9-3.6 V, ~3.3 V nominal), so a healthy cell read as 0 % and made
  MeteoHub raise a false low-battery alert. The 0 %/100 % voltage range is now
  per board in board_config.h: C3 = LiFePO4 (2.9-3.6 V), S3 = Li-ion (3.3-4.2 V).
  The divider ratio stays shared (same wiring).

## [0.7.1] - 2026-09-13

### Changed

- Move the ESP-NOW TX power level (`SENSOR_TX_POWER_LEVEL`) from board_config.h
  to config.h: it is a tunable setting, not board wiring. board_config.h keeps
  only the board trait `SENSOR_NEEDS_TX_LIMIT`.

## [0.7.0] - 2026-09-13

### Changed

- C3 ESP-NOW TX cap is now configurable via `SENSOR_TX_POWER_LEVEL`
  (board_config.h). Under test at **15 dBm** (was 8.5 dBm) for better range with
  the MeteoHub S3; easy to revert if the link degrades.

### Added

- OLED status line now shows the battery **voltage** (e.g. `3.95V`) alongside
  the percentage (72x40 shows voltage compactly).

## [0.6.1] - 2026-09-13

### Added

- OLED support on the ESP32-S3 Super Mini too: an external 0.96" SSD1306
  128x64 wired on the sensor I2C bus (GP8 SDA / GP9 SCL) now shows the reading.
  `DisplayManager` picks the U8g2 driver and layout by board (72x40 on the C3
  HW-675, 128x64 on the S3), driven by `SENSOR_OLED_*` in `board_config.h`.

## [0.6.0] - 2026-09-13

### Added

- Dual board target: the sensor now builds for both the ESP32-S3 Super Mini
  (env `supermini`, LED-only) and the ESP32-C3 HW-675 (env `c3oled`), selected
  by a `SENSOR_BOARD_*` define. Board pinout lives in `board_config.h`.
- C3 HW-675 integrated 0.42" OLED (SSD1306 72x40) support via U8g2, in a new
  `DisplayManager` module. Shows T/H/P, channel, TX status and battery. The
  module is a no-op on the S3 (no screen), so `main.cpp` calls it unconditionally.

### Changed

- C3 uses the board default I2C pins GP5 (SDA) / GP6 (SCL), the bus shared with
  the onboard OLED; the AHT20/BMP280 sit on that same bus.
- C3 caps ESP-NOW TX power at 8.5 dBm (`WiFi.setTxPower(WIFI_POWER_8_5dBm)`),
  guarded by `SENSOR_NEEDS_TX_LIMIT`. At full power the C3 -> S3 hub link is
  unreliable; this is a hardware constraint validated by test, do not raise it
  without re-testing on that board pair. The S3 keeps full power.

## [0.5.6] - 2026-09-12

### Changed

- Stop using the NeoPixel as a power-on glow. Status blinks then LED off.

## [0.5.5] - 2026-09-12

### Changed

- Keep a dim red power glow on the onboard LED so the node looks powered
  at a glance. Still off before deep sleep.

## [0.5.4] - 2026-09-12

### Changed

- Name I2C pins `PIN_SENSOR_SDA` (GP8) and `PIN_SENSOR_SCL` (GP9) so board
  config matches the module wiring at a glance.

## [0.5.3] - 2026-09-12

### Changed

- Single Serial dump of the full ESP-NOW payload after send (T/H/P/fields/seq).

## [0.5.2] - 2026-09-12

### Fixed

- I2C locked to SDA=GP8 / SCL=GP9 (scan on this Super Mini).
- Drop the boot pin probe that restarted the bus and confused Adafruit.
- Ignore battery ADC when GP4 is floating (0.3-1.5 V garbage).
- Log the real STA MAC bytes, not characters from the String.

## [0.5.1] - 2026-09-12

### Fixed

- Probe I2C pin pairs at boot (GP9/GP8, GP5/GP6, and swapped) so AHT/BMP
  are found if the C3 wiring was reused.
- Fill magic, node id and sequence before ESP-NOW send (hub was getting Seq 0).
- Accept BME280 chip id 0x60 on the BMP library path.

## [0.5.0] - 2026-09-12

### Changed

- Target board is now ESP32-S3 Super Mini (4 MB flash, no PSRAM, USB CDC).
- Dropped the 0.42" OLED and U8g2; status is RGB LED plus serial logs.
- I2C is opened explicitly on GP9 (SDA) / GP8 (SCL).
- Battery ADC moved to GP4. Rain pulse on GP7 (avoid GPIO 3 strapping).
- RGB LED on GPIO 48 (GPIO 46 is input-only on ESP32-S3).

## [0.4.1] - 2026-09-12

### Changed

- Reverted to classic ESP-NOW mode with AP connection.
- Sensor now connects to MH-NOW AP for reliable communication.
- Broadcast ESP-NOW mode restored with AP-based channel sync.

## [0.4.0] - 2026-09-12

### Changed

- Simplified ESP-NOW sender to use unicast direct mode instead of broadcast.
- Removed complex AP connection logic and channel scanning.
- Added direct peer registration for MeteoHub STA MAC (20:6E:F1:85:58:68).
- Improved send logic with channel stabilization delay before ESP-NOW init.

## [0.3.16] - 2026-09-12

### Fixed
- `join=0` forever after a failed boot join: retry `MH-NOW` on every send,
  scan and log RSSI/absent. Channel lock + broadcast is no longer the
  happy path while the probe is not associated.

## [0.3.15] - 2026-09-12

### Changed
- Same channel was not enough: the S3 STA never ACKs ESP-NOW from a
  disconnected C3. The probe now joins the hub AP `MH-NOW` (WPA2) and sends
  unicast to that BSSID. OLED `STA ch6` / log `join=1`.

## [0.3.14] - 2026-09-12

### Fixed
- `want=6 radio=6` but hub `rx=0`: TX ran with promiscuous ON, so frames were
  not valid ESP-NOW for the S3 STA. Channel lock is a short pulse, then
  promiscuous off before `esp_now_send`. HT20 11b/g/n.

## [0.3.13] - 2026-09-12

### Fixed
- Sweep 1-13 was not the missing piece: payload is valid and ch=6 was already
  first. Unicast never ACKs, then TX hops to 12/13. Send only on
  `ESPNOW_HUB_CHANNEL` (6) and log `radio=` vs `want=` at `esp_now_send`.

## [0.3.12] - 2026-09-12

### Fixed
- Serial capture that only keeps ESP_LOG lines (`[I]/[W]/[E]`) hid every
  `Serial.printf` ESP-NOW message, so it looked like nothing was sent after
  the I2C/ADC HAL lines. Send path now also uses `ESP_LOGI("meteo", ...)`.
  Missing AHT/BMP still transmits (empty T/H/P).

## [0.3.11] - 2026-09-12

### Added
- Serial dump of the full ESP-NOW payload (T/H/P, flags, CRC, hex) so an empty
  meteo packet (no AHT/BMP) is obvious. OLED shows `pas T/H/P` instead of a
  fake "Sensors OK".

### Fixed
- After `Bcst x13` the hub still had `rx=0`: keep promiscuous on during TX so
  the disconnected STA does not hop off channel 6 before `esp_now_send`.
  Default PHY rate (drop forced 1 Mbps). Peer channel 0 = current radio.

## [0.3.10] - 2026-09-12

### Fixed
- Hub log `ch=6 rx=0`: the probe scanned another 2.4 GHz channel and kept
  promiscuous ON, so the S3 never saw ESP-NOW. Unicast starts on
  `ESPNOW_HUB_CHANNEL` (6). Channel lock is a short promiscuous pulse, then off.

## [0.3.9] - 2026-09-12

### Fixed
- `Bcst x1` after cutting repeaters still missed the hub: unicast ACK failed
  and broadcast went only to the one scanned 2.4 GHz channel. If the S3 is on
  another 2.4 GHz channel, sweep broadcast 1-13. OLED `Bcst x13` means that
  sweep ran.

## [0.3.8] - 2026-09-12

### Fixed
- `Bcst ch11` did not mean the hub heard anything: broadcast was sent only on
  the last tried channel. Broadcast now goes out on every SSID channel seen
  (box vs repeater).

## [0.3.7] - 2026-09-12

### Fixed
- `ERR ch2` after a correct scan: unicast ACK to a stale hub STA MAC fails.
  After unicast fails, send the same frame as STA broadcast (the hub already
  listens). OLED shows `Bcst chN` vs `OK chN`. Keep promiscuous on so the C3
  does not hop off the locked channel.

## [0.3.6] - 2026-09-12

### Fixed
- `ERR ch11` with a valid scan: wait longer for the ESP-NOW ACK, retry, and
  try every 2.4 GHz channel where the SSID was seen (repeater vs box). Send
  callback no longer prints on the Wi-Fi task.

## [0.3.5] - 2026-09-12

### Changed
- Channel comes from a SSID scan only (this project's `secrets.h`). No STA
  association, no WiFiMulti, no AUTH_EXPIRE loop.

## [0.3.4] - 2026-09-12

### Fixed
- After AUTH_EXPIRE, stop the STA handshake so it does not keep hopping the
  radio. ESP-NOW uses the scanned beacon channel instead of config fallback,
  which caused `Peer channel is not equal to the home channel`.

## [0.3.3] - 2026-09-12

### Changed
- STA join uses Arduino `WiFiMulti` on this project's `include/secrets.h`, then
  `WiFi.channel()` for ESP-NOW. Boot OLED no longer prefixes the wait with
  ERREUR.

## [0.3.2] - 2026-09-12

### Changed
- Real STA association from this project's `include/secrets.h` only (`sizeof`
  the credential array). Scan, then `WiFi.begin` with channel and BSSID; stay
  associated so ESP-NOW follows the AP. Fallback to RTC / config channel only
  if WPA fails.

## [0.3.1] - 2026-09-12

### Changed
- Channel discovery uses a 2.4 GHz scan of SSIDs in `include/secrets.h` instead
  of WPA association. ESP32-C3 was failing with AUTH_EXPIRE / AUTH_FAIL; the
  beacon already carries the channel.

### Fixed
- Skip a second `Wire.begin()` after the OLED has already opened I2C.

## [0.3.0] - 2026-09-12

### Added
- At cold boot the probe associates briefly using `include/secrets.h` (same Wi-Fi
  list as the station), reads the AP channel, then drops DHCP and locks ESP-NOW
  on that channel. Deep-sleep wake reuses the RTC-cached channel.

## [0.2.2] - 2026-09-12

### Fixed
- ESP-NOW actually unicasts to the MeteoHub S3 STA MAC (`20:6E:F1:85:58:68`). The
  0.2.1 changelog claimed this, but the firmware still sent to broadcast
  `FF:FF:FF:FF:FF:FF`, so `NOW: OK` meant "frame left the radio", not "hub ACKed".
- Peer is registered on `WIFI_IF_STA`.

## [0.2.1] - 2026-09-11


### Changed
- Configured unicast ESP-NOW receiver MAC address for MeteoHub S3 (`20:6E:F1:85:58:68`).
- Automated version injection via `scripts/version.py`: the root `VERSION` file is now the single source of truth dynamically rendered on the OLED splash screen.

## [0.2.0] - 2026-09-11

### Added
- Added HW-675 ESP32-C3 board target with integrated 0.42" OLED display (SSD1306 72x40) and WS2812B RGB status LED.
- Implemented `DisplayManager` module using U8g2 library with splash screen and real-time status rendering (Node ID, sequence number, battery percentage, live sensor readings, ESP-NOW TX status).
- Implemented RGB status feedback in `PowerManager` (blue on boot/measure, green on successful transmission, red on error).
- Added low-power screen sleep/wake management for deep-sleep cycles.
- Updated board configuration and wiring documentation for HW-675 pinout.

## [0.1.0] - 2026-09-11

### Added
- Initial release of MeteoHubSensor (outdoor weather node on ESP32-C3).
- Direct ESP-NOW wireless transmission to MeteoHub ESP32-S3 station.
- Modular sensor architecture (I2C AHT20 / BMP280 with pluggable sensor slots).
- Extensibility hooks for future sensors: anemometer (pulses), wind vane (ADC), rain gauge (pulses), light.
- Battery monitoring via ADC with calibrated divider support.
- Deep-sleep and periodic measurement cycles for ultra-low power consumption.
- Structured binary packet format (`MeteoPacket`) with node ID, sequence counter, metrics, sensor validity bitmask, and CRC.
- Detailed wiring, hardware, and integration documentation in `docs/`.
