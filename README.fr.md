# MeteoHubSensor

[![Version](https://img.shields.io/badge/version-0.23.0-blue.svg)](VERSION)
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

### Changer de hub : appairage par appui long sur BOOT

La sonde parle à **un seul** MeteoHub, en unicast. La MAC de ce hub est
mémorisée dans la NVS de la sonde : pas besoin de la connaître à la compilation,
ni de reflasher pour changer de hub.

1. Ne laisser allumé, à portée radio, **que** le MeteoHub à associer
   (MeteoHub **≥ 1.46.0**).
2. Sonde en marche, maintenir **BOOT environ 3 s**, jusqu'à ce que la LED passe
   au **bleu fixe**, puis relâcher. (Ne pas maintenir BOOT en branchant la
   sonde : c'est le mode de flashage de l'ESP32.)
3. La sonde balaie les canaux 1 à 13 en demandant « qui est hub ? ». Le hub
   répond avec son identité et sa MAC ; la sonde confirme en unicast et attend
   l'accusé de réception du hub.
4. **3 éclairs verts** : appairée, la nouvelle MAC est en NVS, les mesures
   repartent en unicast vers ce hub. **3 éclairs rouges** : échec, l'ancienne
   association est **conservée telle quelle**.

Règles :

- **Volontaire uniquement.** Un hub éteint ou hors de portée ne déclenche jamais
  de changement de récepteur : la sonde garde ses mesures en attente et
  continue de viser son hub.
- **Un échec n'efface rien.** Tant qu'un nouveau hub n'a pas répondu ET accusé la
  confirmation, la sonde garde l'ancienne MAC (en NVS comme en mémoire).
- **Plusieurs hubs à portée.** Si deux hubs différents répondent pendant le
  balayage, la sonde refuse (éclairs rouges) plutôt que de choisir au hasard.
  Une fois l'appairage fait, les autres hubs peuvent être rallumés : la sonde
  retrouve le canal de SON hub par le BSSID exact de son point d'accès
  « MH-NOW », et non plus par le premier « MH-NOW » venu.
- **Pas de doublon, pas de trou.** La confirmation porte le dernier numéro de
  mesure accusé par l'ancien hub : le nouveau hub reprend de là et ne reçoit
  que les mesures encore en attente.
- Recherche bornée à **60 s**. Un appui court est ignoré. L'appui réveille la
  sonde de son sommeil ; elle se rendort ensuite pour le temps restant, sans
  décaler la cadence des mesures.

Sans appairage enregistré, la sonde utilise `ESPNOW_RECEIVER_MAC` de
`include/config.h` (une sonde déjà en service garde donc son hub après mise à
jour). Mettre cette constante à zéro pour une sonde neuve, qui attend alors
d'être appairée en gardant ses mesures.

---

## 3. Matériel

Le brochage vit dans `include/board_config.h`, sélectionné par le define
`SENSOR_BOARD_S3` posé par l'environnement PlatformIO.

**ESP32-S3 Super Mini** (env `esp32-s3`)
- 4 Mo flash + 2 Mo PSRAM quad (inutilisée), USB CDC natif. **Pas d'écran** (statut par LED).
- Statut = LED RGB GPIO 48 (bleu acquisition, vert OK, rouge erreur).
- **I2C** : SDA = GP8, SCL = GP9. Batterie : Li-ion 14500 (Breadvolt), pont 100k/100k sur GP4.
- ESP-NOW plafonné à **11 dBm** (contrainte matérielle validée par test : pleine puissance = pas d'ACK).

- **Capteurs** (commun) : AHT20 (T/H), BMP280 (pression).
- Détail : `docs/cablage.md`.

---

## 4. LED RGB

- **Bleu** : boot et acquisition.
- **Vert** : livraison confirmée (le hub a accusé réception, ACK unicast).
- **Rouge** : non livré (pas d'ACK du hub).
- **Bleu fixe** (après ~3 s d'appui sur BOOT) : appairage en cours.
- **3 éclairs verts / rouges** : appairage réussi / échoué (association inchangée).

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
