# Câblage et fonctionnement - MeteoHubSensor

Sonde météo extérieure autonome, reliée à la station **MeteoHub** par **ESP-NOW**.
Deux cartes sont prises en charge, choisies par l'environnement PlatformIO :

- **ESP32-C3 HW-675** (`c3oled`) : OLED 0.42" intégré, LED RGB, TX ESP-NOW plafonnée (8,5 dBm).
- **ESP32-S3 Super Mini** (`supermini`) : LED RGB seule (pas d'écran, pour réduire la consommation), TX ESP-NOW plafonnée (11 dBm).

> La liaison est un **unicast** vers la MAC du hub : chaque envoi reçoit un accusé
> de réception matériel (ACK). Les deux cartes plafonnent leur puissance d'émission
> (l'antenne PCB de ces mini-cartes est mal adaptée : à pleine puissance, le hub
> n'acquitte pas). Le niveau se règle **par carte** dans `include/config.h`.

Le brochage vit dans `include/board_config.h` (sélectionné par `SENSOR_BOARD_*`) et les
réglages dans `include/config.h`.

---

## 1. Brochage - ESP32-C3 HW-675

L'écran OLED est câblé d'usine sur le bus I2C GP5/GP6. Les capteurs se greffent sur
**ce même bus** (I2C est multi-esclave : chaque périphérique a son adresse).

| Broche | GPIO | Usage firmware |
| :--- | :--- | :--- |
| **3V3** | -- | Alim capteurs + OLED |
| **GND** | -- | Masse |
| **GP5** | 5 | I2C **SDA** (OLED 0x3C + AHT20 0x38 + BMP280 0x76/0x77) |
| **GP6** | 6 | I2C **SCL** |
| **GP8** | 8 | LED RGB **WS2812B** (interne HW-675) |
| **GP9** | 9 | Bouton **BOOT** |
| **GP4** | 4 | ADC batterie (pont 100k/100k) |
| **GP10** | 10 | Anémomètre (réserve) |
| **GP3** | 3 | Girouette ADC (réserve) |
| **GP7** | 7 | Pluviomètre (réserve) |
| **GP2** | 2 | ADC auxiliaire (réserve) |
| **GP0 / GP1** | 0 / 1 | Libres |

- Écran **OLED 0.42" SSD1306 72x40** intégré (adresse I2C 0x3C).
- **TX ESP-NOW plafonnée à 8,5 dBm** (constat matériel : à pleine puissance la
  liaison est instable). Le niveau vit dans `config.h` (`SENSOR_TX_POWER_LEVEL`,
  un menu commenté par carte), la carte le demande via `SENSOR_NEEDS_TX_LIMIT`.

---

## 2. Brochage - ESP32-S3 Super Mini

USB natif sur GPIO 19/20. BOOT sur GPIO 0. Straps 45/46 (GP46 input-only).

| Broche | GPIO | Usage firmware |
| :--- | :--- | :--- |
| **3V3** | -- | Alim capteurs I2C |
| **GND** | -- | Masse |
| **GP8** | 8 | I2C **SDA** (AHT20 / BMP280) |
| **GP9** | 9 | I2C **SCL** |
| **GP48** | 48 | LED RGB **WS2812** onboard (pas GP46) |
| **GP0** | 0 | Bouton **BOOT** |
| **GP4** | 4 | ADC batterie **désactivée** (`PIN_BATTERY_ADC = -1`) : le module Breadvolt sort un 3,3 V régulé, GP4 ne verrait pas la cellule |
| **GP1** | 1 | Anémomètre (réserve) |
| **GP2** | 2 | Girouette ADC (réserve) |
| **GP7** | 7 | Pluviomètre (réserve) |
| **GP10** | 10 | ADC auxiliaire (réserve) |
| **GP5 / GP6** | 5 / 6 | Libres |

- **Pas d'écran** : le statut passe par la LED RGB et le log USB CDC.
- **TX ESP-NOW plafonnée à 11 dBm** : à pleine puissance, le hub n'acquitte aucune
  trame (antenne PCB du Super Mini mal adaptée). Réglable via le menu de `config.h`.
- La LED RGB onboard est sur **GPIO 48**. Le pinout constructeur annote parfois
  DIN WS2812 = GP46 : sur ESP32-S3, GP46 ne peut pas la piloter. Si la LED reste
  éteinte après flash, tester GPIO 47 (certains clones Lolin).

---

## 3. Capteurs I2C (commun aux deux cartes)

```
Carte                     Capteur AHT20 / BMP280
┌──────────┐              ┌─────────────────────┐
│      3V3 ├─────────────►│ VCC (3.3V)          │
│      GND ├─────────────►│ GND                 │
│  SDA     ├─────────────►│ SDA                 │
│  SCL     ├─────────────►│ SCL                 │
└──────────┘              └─────────────────────┘
```

- SDA/SCL : **GP5/GP6** sur le C3, **GP8/GP9** sur le S3.
- Adresses : AHT20 = 0x38, BMP280/BME280 = 0x76 ou 0x77, OLED (C3) = 0x3C.
- Pull-up I2C : la plupart des modules AHT/BMP les intègrent ; sinon 4,7 kΩ vers 3V3.

---

## 4. Alimentation batterie

L'alimentation et la **mesure de batterie dépendent de la carte** (`board_config.h`) :
la plage 0 % / 100 % (`BATTERY_VOLTAGE_MIN` / `MAX`) suit la chimie, et le ratio du
pont (`BATTERY_DIVIDER_RATIO`, commun) vaut 2,0 pour un pont 100 kΩ / 100 kΩ.

**ESP32-C3 HW-675 : 2 piles alcalines 1,5 V** (~3,0 V nominal), tension lue sur GP4
via un pont 100 kΩ / 100 kΩ.

```
             BAT+ (2 x 1,5 V ~ 2,0 à 3,2 V)
               │
              ┌┴┐
              │ │ R1 (100 kΩ)
              └┬┘
               ├───► Vers GP4 (ADC1)
              ┌┴┐
              │ │ R2 (100 kΩ)
              └┬┘
               │
             BAT- / GND
```

- Plage alcaline **2,0 V → 3,2 V** : ~3,2 V ≈ 100 %, ~3,0 V ≈ 83 %, ~2,0 V ≈ 0 %.
- Le pourcentage et la tension sont transmis dans le paquet ESP-NOW ; MeteoHub lève une
  **alerte pile faible** sous son seuil.

**ESP32-S3 Super Mini : module Breadvolt + accu Li-ion 14500** (3,7 V, 500 mAh). Le
module sort un **3,3 V régulé** vers la carte et **gère lui-même l'accu** (protection
décharge 2,4 V, charge 4,28 V, LEDs CHG/PWR).

- La carte ne voit que le 3,3 V régulé (constant) : GP4 **ne peut pas mesurer la
  cellule**. La mesure batterie est donc **désactivée** (`PIN_BATTERY_ADC = -1`) : pas
  de pourcentage figé ni de fausse alerte. Quand l'accu se vide, la protection coupe et
  la sonde s'arrête (MeteoHub voit **OUT absent**) ; l'état de charge se lit sur les
  LEDs du module.
- Pour retrouver le % : tirer un fil du **+ accu** (avant régulation) vers GP4 via un
  pont 100 kΩ / 100 kΩ, et remettre `PIN_BATTERY_ADC = 4` (la plage Li-ion 3,0-4,2 V
  est déjà prête dans `board_config.h`).

---

## 5. LED de statut (WS2812 / NeoPixel)

Les deux cartes portent **une LED RGB adressable WS2812** (NeoPixel), pas une simple
LED. Le firmware l'utilise comme **témoin d'activité et de résultat d'émission**, en
luminosité douce (pour ne pas éblouir de nuit ni trop consommer). Il n'y a **pas
d'écran sur le S3** : cette LED est le seul retour visuel local.

| Couleur | Quand | Signification |
| :--- | :--- | :--- |
| 🔵 **Bleu** | Au démarrage, puis pendant **chaque acquisition** de mesure | « Je travaille » : lecture des capteurs et préparation de la trame en cours |
| 🟢 **Vert** (bref) | Juste après l'envoi ESP-NOW, si le hub a **accusé réception** | La trame est **livrée** (ACK reçu du hub) |
| 🔴 **Rouge** (bref) | Juste après l'envoi ESP-NOW, si **aucun ACK** n'est revenu | La trame **n'a pas été livrée** (hub hors de portée / éteint) |
| 🔴 **Rouge fixe** | Au boot, si l'init ESP-NOW échoue | Radio ESP-NOW indisponible (défaut au démarrage) |
| ⚫ **Éteinte** | Au repos entre deux mesures (et avant la mise en veille) | Rien à signaler / cycle terminé |

Précisions importantes :

- **Vert = trame réellement livrée.** L'envoi est un **unicast** vers la MAC du hub :
  la couche radio renvoie un ACK matériel. Le vert confirme donc que MeteoHub a bien
  reçu la trame (et non seulement qu'elle est partie) ; le rouge signale l'absence
  d'ACK (hub éteint, hors de portée, ou mauvais canal).
- Le clignotement vert/rouge est **court** (~60 ms) : à chaque cycle de mesure
  (5 min par défaut), on voit un bref éclat, puis la LED s'éteint.
- En **mode veille profonde** (si activé), la LED est éteinte avant le sommeil pour ne
  rien consommer ; chaque réveil rejoue la séquence bleu → vert/rouge.

Le pilotage est dans `src/modules/power_manager.cpp` (`setLedColor`, `blinkStatus`,
`turnOffLed`), déclenché depuis `src/main.cpp`.
