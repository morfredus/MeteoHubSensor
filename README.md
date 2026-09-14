# MeteoHubSensor

[![Version](https://img.shields.io/badge/version-0.15.1-blue.svg)](VERSION)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: ESP32-S3 / C3](https://img.shields.io/badge/Platform-ESP32--S3%20%2F%20C3-orange.svg)](https://www.espressif.com/)

Sonde météo extérieure autonome, en liaison **ESP-NOW** avec la station **MeteoHub**. Deux cartes possibles : **ESP32-S3 Super Mini** (LED RGB, sans écran) ou **ESP32-C3 HW-675** (OLED 0.42" intégré).

---

## 1. Responsabilité du projet

**MeteoHubSensor** a une responsabilité unique : **mesurer l'environnement extérieur et transmettre ses métriques brutes par ESP-NOW**.

Ce nœud n'est **pas une nouvelle brique de morfSystem**. Pas de serveur web, pas d'historique local.

### Rôles :
- **Super Mini (Outdoor)** : *"Je mesure, je clignote le statut, je transmets."*
- **MeteoHub** : *"Je reçois, je valide, je stocke, j'affiche et j'expose."*

---

## 2. Architecture ESP-NOW

```
              ┌─────────────────┐
              │  ESP32-S3       │
              │  MeteoHub       │
              └────────┬────────┘
                       │ ESP-NOW
              ┌────────▼────────┐
              │  Super Mini     │
              │  LED RGB        │
              │  AHT20 / BMP280 │
              └────────┬────────┘
                       │
              ┌────────┴────────┐
              ▼                 ▼
        Température         Humidité
```

---

## 3. Matériel (deux cartes possibles)

Le brochage de chaque carte vit dans `include/board_config.h`, sélectionné par
le define `SENSOR_BOARD_*` posé par l'environnement PlatformIO.

**ESP32-S3 Super Mini** (env `supermini`)
- 4 Mo flash, **sans PSRAM**, USB CDC natif. **Pas d'écran** (OLED non piloté, pour réduire la consommation).
- Statut = LED RGB GPIO 48 (bleu acquisition, vert OK, rouge erreur).
- **I2C** : SDA = GP8, SCL = GP9. Batterie : Li-ion, pont 100k/100k sur GP4.

**ESP32-C3 HW-675** (env `c3oled`)
- 4 Mo flash. **OLED 0.42" (SSD1306 72x40) intégré**, affiche le dernier relevé.
- **I2C** : SDA = GP5, SCL = GP6 (bus partagé OLED + capteurs). LED RGB sur GP8.
- ESP-NOW plafonné à **8,5 dBm** (contrainte matérielle validée par test).

- **Capteurs** (commun) : AHT20 (T/H), BMP280 (pression).
- Détail : `docs/cablage.md`.

---

## 4. LED RGB

- **Bleu** : boot et acquisition.
- **Vert** : livraison confirmée (le hub a accusé réception, ACK unicast).
- **Rouge** : non livré (pas d'ACK du hub).

---

## 5. Compilation et flash

1. Copier `include/secrets_example.h` vers `include/secrets.h` **dans ce projet**.

```bash
# ESP32-S3 Super Mini
pio run -e supermini
pio run -e supermini -t upload
pio run -e supermini -t monitor
```

```bash
# ESP32-C3 HW-675 (OLED intégré)
pio run -e c3oled
pio run -e c3oled -t upload
pio run -e c3oled -t monitor
```

Si le port série n'apparaît pas : tenir BOOT, tap RESET, relâcher BOOT.

---

## 6. Auteur & Licence

Développé par **morfredus** pour la station météo **MeteoHub**.  
Licence : **GPL v3**.
