# MeteoHubSensor

[![Version](https://img.shields.io/badge/version-0.33.0-blue.svg)](VERSION)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)](https://www.espressif.com/)

> 🇫🇷 Version française : [README.fr.md](README.fr.md)

Standalone outdoor weather probe, linked over **ESP-NOW** to the **MeteoHub** station. Target board: **ESP32-S3 Super Mini** (RGB LED, no screen).

---

## 1. Responsibility

**MeteoHubSensor** has a single responsibility: **measure the outdoor environment and transmit its raw metrics over ESP-NOW**.

This node is **not a new morfSystem component**. No web server, no local history.

### Roles:
- **Super Mini (Outdoor)**: *"I measure, I blink the status, I transmit."*
- **MeteoHub**: *"I receive, validate, store, display and expose."*

---

## 2. ESP-NOW architecture

```
              ┌─────────────────┐
              │  ESP32-S3       │
              │  MeteoHub       │
              └────────┬────────┘
                       │ ESP-NOW
              ┌────────▼────────┐
              │  Super Mini     │
              │  RGB LED        │
              │  AHT20 / BMP280 │
              └────────┬────────┘
                       │
              ┌────────┴────────┐
              ▼                 ▼
         Temperature        Humidity
```

### Changing hub: pairing with a long press on BOOT

The probe talks to **a single** MeteoHub, in unicast. That hub's MAC is stored in
the probe's NVS: there is no need to know it at build time, nor to reflash to
change hub.

1. Leave **only** the MeteoHub to pair powered on within radio range
   (MeteoHub **≥ 1.46.0**).
2. With the probe running, hold **BOOT for about 3 s**, until the LED turns
   **solid blue**, then **release**: pairing starts on release. A press longer
   than 10 s is ignored (button considered stuck). Do not hold BOOT while
   plugging the probe in: that is the ESP32 flashing mode.
3. The probe sweeps channels 1 to 13 asking "who is a hub?". The hub answers with
   its identity and MAC; the probe confirms in unicast and waits for the hub's
   acknowledgement.
4. **3 green flashes**: paired, the new MAC is in NVS and measurements go to this
   hub in unicast. **3 red flashes**: failure, the previous association is **kept
   as it was**.

Rules:

- **Deliberate only.** A hub that is off or out of range never triggers a change
  of receiver: the probe keeps its measurements pending and keeps aiming at its
  hub.
- **A failure erases nothing.** Until a new hub has answered AND acknowledged the
  confirmation, the probe keeps the old MAC (in NVS and in memory).
- **Several hubs in range.** If two different hubs answer during the sweep, the
  probe refuses (red flashes) rather than pick one at random. Once paired, the
  other hubs can be switched back on: the probe finds ITS hub's channel through
  the exact BSSID of its "MH-NOW" access point, no longer through the first
  "MH-NOW" it sees.
- **No duplicate, no gap.** The confirmation carries the last measurement number
  acknowledged by the old hub: the new hub resumes from there and only receives
  the measurements still pending.
- **Stuck button = no effect.** A permanent press (an enclosure pressing the
  button, moisture on GPIO0) is never released: it starts no pairing, and the
  button wake-up is not even armed while GPIO0 stays low. Measurements carry on
  normally.
- Search bounded to **60 s**. A short press is ignored.
- The hub (≥ 1.47.0) **only archives the probe that chose it**: a bench probe
  powered on next to it can no longer mix its readings with the outdoor ones. The
  press wakes the probe from sleep; it then goes back to sleep for the remaining
  time, without shifting the measurement cadence.

Without a stored pairing (new probe, erased flash), the probe **sends nothing**:
`ESPNOW_RECEIVER_MAC` (`include/config.h`) is zero since 0.25.0. It keeps its
measurements pending and no longer scans channels until a long press on BOOT has
paired it. A hard-coded MAC would have targeted one specific hub, and after
swapping hubs (production ↔ test bench) an erased probe would have sent its
readings to the wrong hub.

### Local buffer: no measurement lost, without draining the battery

Each measurement is written to flash **before** it is sent, then kept until the
hub acknowledges it. If the hub is off or out of range, the probe later resends
what is missing, in order, at most 10 measurements per wake-up. Retention: 30
days of pending measurements.

Since 0.24.0 this buffer is a **segmented journal**: one small file per day of
measurements (`/mhs/<seq>.seg`), written by appending only. The acknowledgement
watermark lives in NVS, and a fully acknowledged segment is deleted. The former
buffer, a single 276 KB file, was copied in full by LittleFS at every
measurement: 4.5 s measured, radio on. On the first boot in 0.24.0, the
measurements still pending in the old file are taken over, then it is erased.

---

## 3. Hardware

The pinout lives in `include/board_config.h`, selected by the `SENSOR_BOARD_S3`
define set by the PlatformIO environment.

**ESP32-S3 Super Mini** (env `sonde-supermini`)
- 4 MB flash + 2 MB quad PSRAM (unused), native USB CDC. **No screen** (status on the LED).
- Status = RGB LED on GPIO 48 (blue acquisition, green OK, red error).
- **I2C**: SDA = GP8, SCL = GP9. Battery: Li-ion, 3.3 V regulator module, 100k/100k divider on GP4.
- ESP-NOW capped at **11 dBm** (hardware constraint confirmed by testing: full power = no ACK).
- **Sensors**: DHT22 on GP1 (reference for temperature and humidity, no fallback), BMP280 (pressure only; the AHT20 of the module is no longer read). Anemometer reserve on GP5.
- Details (French): [`docs/cablage.md`](docs/cablage.md).

---

## 4. RGB LED

- **Blue**: boot and acquisition.
- **Green**: delivery confirmed (the hub acknowledged, unicast ACK).
- **Red**: not delivered (no ACK from the hub).
- **Solid blue** (after ~3 s holding BOOT): pairing in progress.
- **3 green / red flashes**: pairing succeeded / failed (association unchanged).

---

## 5. Build and flash

1. Copy `include/secrets_example.h` to `include/secrets.h` **in this project**.

```bash
# ESP32-S3 Super Mini
pio run -e sonde-supermini
pio run -e sonde-supermini -t upload
pio run -e sonde-supermini -t monitor
```

If the serial port does not show up: hold BOOT, tap RESET, release BOOT.

---

## 6. Author & License

Developed by **morfredus** for the **MeteoHub** weather station.
License: **GPL v3**.
