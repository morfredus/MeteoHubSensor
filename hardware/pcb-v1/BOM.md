# Liste des composants - carte v1 de MeteoHubSensor

Liste tirée du schéma (`meteohubsensor-v1.kicad_sch`). Toutes les pièces de la carte sont traversantes : aucune
soudure CMS, **52 pastilles à souder** au total. Aucune référence de fournisseur (LCSC, Mouser...) n'est donnée :
elles n'ont pas été vérifiées.

## 1. Soudé sur la carte

### Résistances (3)

| Réf. | Qté | Valeur | Détail | Rôle |
| :--- | :-: | :--- | :--- | :--- |
| R1, R2 | 2 | 100 kohm | Axiale 1/4 W, tolérance **1 %** conseillée, boîtier DIN0207 | Pont de mesure de l'accu (GP4) |
| R3 | 1 | 10 kohm | Axiale 1/4 W, boîtier DIN0207 | Rappel du DHT22 (omettre avec un module 3 broches qui l'embarque) |

### Condensateurs (3)

| Réf. | Qté | Valeur | Détail | Rôle |
| :--- | :-: | :--- | :--- | :--- |
| C1 | 1 | 100 nF | Céramique disque, 50 V, pas 2,5 mm | Découplage près des broches 3V3/GND du S3 |
| C3 | 1 | 100 nF | Céramique disque, 50 V, pas 2,5 mm | Filtre du noeud ADC (GP4) |
| C2 | 1 | 220 uF | Électrolytique radial, **10 V ou plus**, diamètre 6,3 mm, pas 2,5 mm | Réserve de courant pour les pointes d'émission |

### Embases JST-XH (6 embases, 18 broches), pas 2,50 mm, verticales

| Réf. | Nom | Broches | Référence JST | Brochage (1 à n) |
| :--- | :--- | :-: | :--- | :--- |
| J1 | BAT | 2 | B2B-XH-A | + accu, - accu |
| J2 | BMP280 | 4 | B4B-XH-A | 3V3, SCL, SDA, GND |
| J3 | DHT22 | 3 | B3B-XH-A | 3V3, DATA, GND |
| J4 | ANEMO | 3 | B3B-XH-A | 3V3, impulsion, GND |
| J5 | PLUVIO | 3 | B3B-XH-A | 3V3, impulsion, GND |
| J6 | GIROUETTE | 3 | B3B-XH-A | 3V3, ADC, GND |

Récapitulatif : 1 x 2 broches, 4 x 3 broches (12 broches), 1 x 4 broches, soit 2 + 12 + 4 = 18 broches.

### Supports et module

| Réf. | Qté | Pièce | Détail |
| :--- | :-: | :--- | :--- |
| U1 | 2 | Barrette femelle 1x9, pas 2,54 mm | Une par rangée du S3 (18 broches au total), entraxe des rangées 15,24 mm |
| U2 | 1 | Module abaisseur 3,3 V (Youmi, SOT-223) | 4 broches mâles soudées : deux paires espacées de 22,86 mm, circuit 25,6 x 11,3 mm |

Trous de fixation H1 à H4 : 4 trous M3 (3,2 mm), aucune pièce sur la carte.

## 2. À fournir, non soudé sur la carte

| Qté | Pièce | Remarque |
| :-: | :--- | :--- |
| 1 | ESP32-S3 Super Mini N4R2 | Se pose sur les supports U1, USB-C vers l'intérieur de la carte |
| 1 | Module BMP280 (avec ou sans AHT20) | Seule la pression est lue |
| 1 | DHT22 (ou module 3 broches) | Température et humidité de référence |
| 1 | Accu Li-ion 3,7 V | 3000 mAh en place ; chargé hors de la sonde, sans protection sur cette carte |
| 1 | Anémomètre, 1 pluviomètre, 1 girouette | Futurs ; seuls les connecteurs J4, J5, J6 sont prévus |
| 1 | Boîtier femelle JST-XHP 2 broches | Pour J1 |
| 4 | Boîtiers femelles JST-XHP 3 broches | Pour J3, J4, J5, J6 |
| 1 | Boîtier femelle JST-XHP 4 broches | Pour J2 |
| 18 | Cosses à sertir JST-XH (SXH) | 2 + 12 + 4, prévoir quelques cosses de plus pour les ratés de sertissage |
| - | Fil souple 22 à 26 AWG | Pour les câbles des capteurs et de l'accu |
| 4 | Vis M3 et entretoises | Pour la fixation, si le boîtier en demande |

## 3. Totaux

| Famille | Pièces | Pastilles à souder |
| :--- | :-: | :-: |
| Résistances | 3 | 6 |
| Condensateurs | 3 | 6 |
| Embases JST-XH | 6 | 18 |
| Barrettes femelles 1x9 | 2 | 18 |
| Module régulateur | 1 | 4 |
| **Total** | **15** | **52** |
