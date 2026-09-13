# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
