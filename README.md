# MeteoHubSensor

[![Version](https://img.shields.io/badge/version-0.22.0-blue.svg)](VERSION)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)](https://www.espressif.com/)

Sonde météo extérieure autonome, en liaison **ESP-NOW** avec la station **MeteoHub**. Carte cible : **ESP32-S3 Super Mini** (LED RGB, sans écran).

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

## 3. Matériel

Le brochage vit dans `include/board_config.h`, sélectionné par le define
`SENSOR_BOARD_S3` posé par l'environnement PlatformIO.

**ESP32-S3 Super Mini** (env `esp32-s3`)
- 4 Mo flash + 2 Mo PSRAM quad (inutilisée), USB CDC natif. **Pas d'écran** (statut par LED).
- Statut = LED RGB GPIO 48 (bleu acquisition, vert OK, rouge erreur).
- **I2C** : SDA = GP8, SCL = GP9. Batterie : Li-ion 14500 (Breadvolt), pont 100k/100k sur GP4.
- ESP-NOW plafonné à **11 dBm** (contrainte matérielle validée par test : pleine puissance = pas d'ACK).
- **Capteurs** : AHT20 (T/H), BMP280 (pression).
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
pio run -e esp32-s3
pio run -e esp32-s3 -t upload
pio run -e esp32-s3 -t monitor
```

Si le port série n'apparaît pas : tenir BOOT, tap RESET, relâcher BOOT.

---

## 6. Auteur & Licence

Développé par **morfredus** pour la station météo **MeteoHub**.  
Licence : **GPL v3**.
