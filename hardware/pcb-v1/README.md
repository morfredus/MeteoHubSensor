# Carte v1 de MeteoHubSensor (projet KiCad 10)

Platine dessinée pour remplacer la v1 montée sur platine à bandes (uPesy) : même schéma, mais gravée,
avec l'ESP32-S3 Super Mini sur supports pour pouvoir l'échanger sans dessoudage.

Ouvrir `meteohubsensor-v1.kicad_pro` avec KiCad 10.0.x. Fichiers : schéma, PCB routé (66 x 50 mm, deux
couches, plan de masse GND sur les deux faces), bibliothèque `morfsensor` (symboles et empreintes propres au projet).

## Ce que porte la carte

| Réf. | Rôle | Reliée à |
| :--- | :--- | :--- |
| U1 | ESP32-S3 Super Mini N4R2 sur 2 supports femelles 1x9 | voir ci-dessous |
| U2 | Module abaisseur 3,3 V (Youmi, SOT-223) | VIN = + accu, VOUT = rail 3V3 |
| J1 | Accu (JST-XH 2 broches : + / -) | VBAT, GND |
| J2 | BMP280 (JST-XH 4 broches : 3V3, SCL, SDA, GND) | GP9 (SCL), GP8 (SDA) |
| J3 | DHT22 (JST-XH 3 broches : 3V3, DATA, GND) | GP1 |
| J4 | Anémomètre, futur (3V3, IMPULSION, GND) | GP5 |
| J5 | Pluviomètre, futur (3V3, IMPULSION, GND) | GP7 |
| J6 | Girouette, future (3V3, ADC, GND) | GP2 |
| R1, R2 | Pont 100 kohm / 100 kohm sur le + accu, avant le régulateur | milieu = GP4 |
| C3 | 100 nF sur le noeud du pont (stabilise la lecture ADC) | GP4 / GND |
| C1, C2 | 100 nF céramique + 220 uF électrolytique, collés aux broches 3V3/GND du S3 | rail 3V3 |
| R3 | 10 kohm de rappel du DHT22 (omettre avec un module 3 broches qui l'embarque) | DATA / 3V3 |
| H1 à H4 | Trous de fixation M3 | |

Le régulateur alimente la broche **3V3** du S3 (pas la 5V), comme sur la v1 actuelle. Broches du S3 laissées
libres : TX, RX, GP3, GP6, 5V, GP10 à GP13.

## Masse et modèles 3D

La masse (GND) est un plan de cuivre sur les deux faces : toutes les broches GND des connecteurs et des
composants s'y raccordent par leurs pastilles traversantes, sans piste dédiée. Seules l'alimentation et les signaux
sont tracés. Des pistes GND explicites ont été
essayées puis retirées : elles alourdissaient le dessin sans rien apporter, le plan relie déjà tout.
Les empreintes `ESP32-S3_SuperMini_Socket` et `Regulator_Module_3V3` embarquent un modèle 3D simplifié
(`morfsensor.3dshapes/`) : supports et module avec USB-C et antenne, platine du régulateur avec SOT-223.

## Brochage des connecteurs JST-XH

Broche 1 à gauche (côté de l'ergot carré de l'embase), vue de dessus, détrompeur vers le bas.

| Connecteur | Broche 1 | Broche 2 | Broche 3 | Broche 4 | Référence suivie |
| :--- | :--- | :--- | :--- | :--- | :--- |
| J1 accu | + accu | - accu | | | rouge (+) puis noir (-), usage courant des accus Li-ion |
| J2 BMP280 | 3V3 | SCL | SDA | GND | + puis signaux puis GND, comme les autres ; SCL et SDA se relient sans croisement |
| J3 DHT22 | 3V3 | DATA | GND | | ordre de la fiche technique du DHT22 / AM2302 (VDD, DATA, GND) |
| J4 anémomètre | 3V3 | IMPULSION | GND | | même ordre que J3 |
| J5 pluviomètre | 3V3 | IMPULSION | GND | | même ordre que J3 |
| J6 girouette | 3V3 | ADC | GND | | même ordre que J3 |

Les modules BMP280 courants sont étiquetés dans un autre ordre (VCC, GND, SCL, SDA) : les câbles se sertissent à
l'ordre du connecteur de la carte, pas à celui du module.

## Orientation du module régulateur

Vue de dessus, broches vers le bas (soudées sur la carte), IN à gauche et OUT à droite : sur chaque paire,
VIN (gauche) et VOUT (droite) sont sur la broche du bas, GND sur la broche du haut.

## Repères des broches du S3

Les numéros de pastilles de U1 sont les repères imprimés sur le module, vue de dessus, USB en haut : à
gauche de haut en bas TX, RX, 1, 2, 3, 4, 5, 6, 7 ; à droite de haut en bas 5V, GND, 3V3, 13, 12, 11, 10, 9, 8.
Pastille 8 = GPIO8 = SDA, pastille 9 = GPIO9 = SCL, et ainsi de suite : aucune table de correspondance à tenir.
J6 (girouette) ne porte que le signal brut sur GP2 : le pont diviseur propre à la girouette reste à définir
avec le firmware.

## Antenne

Le S3 est implanté **USB vers l'intérieur** : l'antenne céramique se retrouve au **bord bas** de la carte.
Une zone d'exclusion (cuivre, pistes, vias, plan de masse, sur les deux faces) couvre l'espace sous l'antenne
et jusqu'au bord. Aucune piste n'approche l'antenne : les signaux des deux rangées partent vers l'extérieur.

Limite connue : l'USB du module est tourné vers l'intérieur de la carte. Pour flasher, enlever le module de
ses supports (c'est l'intérêt de la prise).

## Cotes relevées sur les modules

- **ESP32-S3 Super Mini** : 15,9 mm hors tout entre les deux rangées de broches, soit 15,24 mm entre axes (broche de
  0,64 mm), pas de 2,54 mm comme sur une platine d'essai. Empreinte `ESP32-S3_SuperMini_Socket`.
- **Module régulateur** : circuit de 25,6 x 11,3 mm. Broches standard (0,64 mm) sur la grille de 2,54 mm, comme les
  deux modules qui entrent dans une platine d'essai : deux paires espacées de 22,86 mm entre axes (9 pas) dans la
  longueur, pas de 2,54 mm dans chaque paire (3 mm hors tout dans la largeur). Empreinte `Regulator_Module_3V3`.
  La position des paires dans la largeur du circuit est supposée centrée : elle ne change que le contour dessiné,
  pas le perçage.

## À vérifier avant fabrication

1. **Module régulateur** : vérifier que son orientation réelle (IN à gauche, VIN sur la broche du bas) correspond à
   l'empreinte, en le posant sur la carte imprimée ou sur un gabarit.
2. **Connecteurs** : les câbles existants ne suivent pas forcément cet ordre ; les recréer au brochage du tableau.

Rappel du point de vigilance de `docs/cablage.md` : la plage d'entrée annoncée du module (4,5 à 7 V) est
au-dessus de celle d'un accu Li-ion. À mesurer sur banc avant de figer la carte.

## Contrôles passés (KiCad 10.0.7, `kicad-cli`)

- ERC : 0 violation.
- DRC avec parité schéma/PCB : 0 erreur, 0 piste non connectée. Restent des avertissements de sérigraphie
  (textes proches des contours de connecteurs).

Les fichiers ont été générés par script puis vérifiés par ces commandes ; les ouvrir une fois dans KiCad et
lancer `Fichier > Enregistrer` les remet au format exact de l'éditeur.
