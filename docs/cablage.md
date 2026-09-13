# Schéma de câblage - MeteoHubSensor (ESP32-S3 Super Mini)

Raccordement des capteurs et de l'alimentation sur **ESP32-S3 Super Mini**. Pas d'écran OLED : le statut passe par la LED RGB onboard et le log USB CDC.

Les GPIO 19/20 sont le USB natif. GPIO 0 est le bouton BOOT (non sorti). GPIO 45 et 46 sont des straps ; GPIO 46 est input-only.

---

## 1. Brochage utilisé

| Broche | GPIO | Usage firmware |
| :--- | :--- | :--- |
| **5V** | -- | USB / charge |
| **3V3** | -- | Alim capteurs I2C |
| **GND** | -- | Masse |
| **GP8** | **8** | `PIN_SENSOR_SDA` - fil SDA AHT/BMP |
| **GP9** | **9** | `PIN_SENSOR_SCL` - fil SCL AHT/BMP |
| **GP48** | **48** | LED RGB WS2812 onboard |
| **GP4** | **4** | ADC batterie (pont 100k/100k) |
| **GP1** | **1** | Anémomètre (reed, réserve) |
| **GP2** | **2** | Girouette ADC (réserve) |
| **GP7** | **7** | Pluviomètre reed (réserve) |
| **GP10** | **10** | ADC auxiliaire (réserve) |

Le pinout constructeur annote parfois **DIN WS2812 = GP46**. Sur ESP32-S3, GPIO 46 ne peut pas piloter une LED. Le firmware utilise **GPIO 48**. Si la LED reste éteinte après flash, tester GPIO 47 (certaines clones Lolin).

```
Face USB (haut du pinout)
                +---------------+
                |     USB-C     |
                +------| |------+
         5V  --| TX         RX |-- GND
        3V3  --|               |-- 5V3 OUT
   (ADC) GP1 --|               |-- GP13
         GP2 --|               |-- GP12
         GP3 --|   Super Mini  |-- GP11
   (BAT) GP4 --|               |-- GP10  (aux ADC)
   (libre) GP5 --|              |-- GP9   (SCL)
   (libre) GP6 --|              |-- GP8   (SDA)
   (pluie) GP7 --|             |
                +---------------+
LED RGB onboard = GPIO 48 (pas GP46)
```

---

## 2. Capteurs I2C

```
ESP32-S3 Super Mini      Capteur AHT20 / BMP280
┌──────────┐             ┌─────────────────────┐
│      3V3 ├────────────►│ VCC (3.3V)          │
│      GND ├────────────►│ GND                 │
│ GP8 SDA  ├────────────►│ SDA                 │
│ GP9 SCL  ├────────────►│ SCL                 │
└──────────┘             └─────────────────────┘
```

Pull-up I2C : beaucoup de modules AHT/BMP les ont déjà. Sinon 4.7 kΩ vers 3V3.

---

## 3. Tension batterie sur GP4

Pont 100 kΩ / 100 kΩ (ratio 2.0 dans `config.h`). GPIO 0 n'est pas utilisé : c'est BOOT.

```
             BAT+ (3.0V - 4.2V)
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

Sans pont sur GP4, le firmware ignore la lecture (broche flottante).

Le pad BATTERY+ / BATTERY- de la carte alimente le chargeur. Le pont ADC est un circuit séparé si on veut le pourcentage dans le paquet.

Ne pas activer le mode BOOST (100 mA -> 300 mA) si l'accu fait moins de 500 mAh.
