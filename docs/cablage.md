# Câblage et fonctionnement - MeteoHubSensor

Sonde météo extérieure autonome, reliée à la station **MeteoHub** par **ESP-NOW**.
Carte cible : **ESP32-S3 Super Mini** (`esp32-s3`) - LED RGB seule (pas d'écran,
pour réduire la consommation), TX ESP-NOW plafonnée (11 dBm).

> La liaison est un **unicast** vers la MAC du hub : chaque envoi reçoit un accusé
> de réception matériel (ACK). La carte plafonne sa puissance d'émission (l'antenne
> PCB du Super Mini est mal adaptée : à pleine puissance, le hub n'acquitte pas).
> Le niveau se règle dans `include/config.h`.

Le brochage vit dans `include/board_config.h` (sélectionné par `SENSOR_BOARD_S3`) et
les réglages dans `include/config.h`.

---

## 1. Brochage - ESP32-S3 Super Mini

USB natif sur GPIO 19/20. BOOT sur GPIO 0. Straps 45/46 (GP46 input-only).

| Broche | GPIO | Usage firmware |
| :--- | :--- | :--- |
| **3V3** | -- | Alim capteurs I2C |
| **GND** | -- | Masse |
| **GP8** | 8 | I2C **SDA** (AHT20 / BMP280) |
| **GP9** | 9 | I2C **SCL** |
| **GP48** | 48 | LED RGB **WS2812** onboard (pas GP46) |
| **GP0** | 0 | Bouton **BOOT** |
| **GP4** | 4 | ADC batterie (`PIN_BATTERY_ADC = 4`) : pont 100k/100k depuis le **+ accu** (avant le régulateur 3,3 V) |
| **GP1** | 1 | Anémomètre (réserve) |
| **GP2** | 2 | Girouette ADC (réserve) |
| **GP7** | 7 | Pluviomètre (réserve) |
| **GP10** | 10 | ADC auxiliaire (réserve) |
| **GP5 / GP6** | 5 / 6 | Libres |

- **Pas d'écran** : le statut passe par la LED RGB et le log USB CDC.
- **TX ESP-NOW plafonnée à 11 dBm** : à pleine puissance, le hub n'acquitte aucune
  trame (antenne PCB du Super Mini mal adaptée). Réglable via le menu de `config.h`
  (`SENSOR_TX_POWER_LEVEL` ; la carte le demande via `SENSOR_NEEDS_TX_LIMIT`).
- La LED RGB onboard est sur **GPIO 48**. Le pinout constructeur annote parfois
  DIN WS2812 = GP46 : sur ESP32-S3, GP46 ne peut pas la piloter. Si la LED reste
  éteinte après flash, tester GPIO 47 (certains clones Lolin).

---

## 2. Capteurs I2C

```
Carte                     Capteur AHT20 / BMP280
┌──────────┐              ┌─────────────────────┐
│      3V3 ├─────────────►│ VCC (3.3V)          │
│      GND ├─────────────►│ GND                 │
│  SDA     ├─────────────►│ SDA                 │
│  SCL     ├─────────────►│ SCL                 │
└──────────┘              └─────────────────────┘
```

- SDA/SCL : **GP8/GP9**.
- Adresses : AHT20 = 0x38, BMP280/BME280 = 0x76 ou 0x77.
- Pull-up I2C : la plupart des modules AHT/BMP les intègrent ; sinon 4,7 kΩ vers 3V3.

---

## 3. Alimentation batterie

**Module Breadvolt + accu Li-ion 14500** (3,7 V, 500 mAh). Le module sort un
**3,3 V régulé** vers la carte et **gère lui-même l'accu** (protection décharge
2,4 V, charge 4,28 V, LEDs CHG/PWR).

- Le 3,3 V régulé est **constant** : le mesurer ne dirait rien de l'accu. On tape donc
  la **cellule**, **avant** le régulateur, au **+ accu** (multimètre ~4,0 V en charge).
- Cet accu monte à **4,28 V** en pleine charge : il ne doit **jamais** arriver brut sur
  la pin (limite ADC du S3 ~3,3 V). Un **pont 100 kΩ / 100 kΩ** divise la tension par
  deux et GP4 lit le point milieu :

  ```
  + accu ──[R1 100k]──┬── GP4 (ADC1)
                      │
                   [R2 100k]
                      │
                     GND
  ```

  GP4 voit ~2,0 V pour 4,0 V accu ; le firmware remultiplie par le ratio du pont
  (`BATTERY_DIVIDER_RATIO = 2,0`) et convertit sur la plage Li-ion 2,6-4,2 V.
  `PIN_BATTERY_ADC = 4` (activé le 2026-09-15).
- Le pont draine en continu ~4,0 V / 200 kΩ ≈ **20 µA**, négligeable devant les
  réveils d'émission.
- **Calibration fine (optionnelle)** : si la tension rapportée par la sonde diffère de
  ton multimètre (tolérance des résistances + offset ADC), poser
  `BATTERY_VREF_CALIBRATION = tension_multimètre / tension_rapportée` dans `config.h`.
- Filet de sécurité : quand l'accu se vide, le pourcentage tombe à 0 % (0 % calé à
  2,6 V, 0,2 V au-dessus de la coupure 2,4 V du module), puis la protection coupe et la
  sonde s'arrête (MeteoHub voit **OUT absent**).

---

## 4. LED de statut (WS2812 / NeoPixel)

La carte porte **une LED RGB adressable WS2812** (NeoPixel), pas une simple LED. Le
firmware l'utilise comme **témoin d'activité et de résultat d'émission**, en luminosité
douce (pour ne pas éblouir de nuit ni trop consommer). Il n'y a **pas d'écran** : cette
LED est le seul retour visuel local.

| Couleur | Quand | Signification |
| :--- | :--- | :--- |
| 🔵 **Bleu** | Au démarrage, puis pendant **chaque acquisition** de mesure | « Je travaille » : lecture des capteurs et préparation de la trame en cours |
| 🟢 **Vert** (bref) | Juste après l'envoi ESP-NOW, si le hub a **accusé réception** | La trame est **livrée** (ACK reçu du hub) |
| 🔴 **Rouge** (bref) | Juste après l'envoi ESP-NOW, si **aucun ACK** n'est revenu | La trame **n'a pas été livrée** (hub hors de portée / éteint) |
| 🔴 **Rouge fixe** | Au boot, si l'init ESP-NOW échoue | Radio ESP-NOW indisponible (défaut au démarrage) |
| 🔵 **Bleu fixe** | Après ~3 s d'appui sur **BOOT** | Appairage en cours : recherche d'un hub (60 s max) |
| 🟢 **3 éclairs verts** | Fin d'appairage | Nouveau hub enregistré, mesures redirigées vers lui |
| 🔴 **3 éclairs rouges** | Fin d'appairage | Aucun hub, plusieurs hubs, ou pas d'accusé : **association inchangée** |
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
