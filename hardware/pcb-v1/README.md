# Carte v1 de MeteoHubSensor (projet KiCad 10)

Platine dessinée pour remplacer la v1 montée sur platine à bandes (uPesy) : même schéma, mais gravée,
avec l'ESP32-S3 Super Mini sur supports pour pouvoir l'échanger sans dessoudage.

Ouvrir `meteohubsensor-v1.kicad_pro` avec KiCad 10.0.x. Fichiers : schéma, PCB routé (66 x 50 mm, deux
couches, plan de masse GND sur les deux faces reliées par une via de couture), bibliothèque `morfsensor` (symboles et empreintes propres au projet).

## Ce que porte la carte

| Réf. | Rôle | Reliée à |
| :--- | :--- | :--- |
| U1 | ESP32-S3 Super Mini N4R2 sur 2 supports femelles 1x9 | voir ci-dessous |
| U2 | Module abaisseur 3,3 V (Youmi, SOT-223) | VIN = + accu, VOUT = rail 3V3 |
| J1 | Accu (JST-XH 2 broches : + / -) | VBAT, GND |
| J2 | BMP280 (JST-XH 4 broches : 3V3, GND, SCL, SDA) | GP9 (SCL), GP8 (SDA) |
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
composants s'y raccordent par leurs pastilles traversantes. En plus du plan, des pistes GND explicites (0,4 mm)
relient en arbre les broches GND sur la face avant, à partir de la broche GND du S3. Quatre broches (J3, J4, J5, J6)
sont enclavées entre des pistes de signaux : chacune a une courte piste vers une via de masse.
Ces pistes sont du même réseau que le plan, donc elles fusionnent avec lui : aucun souci pour la fabrication.
Elles sont tracées automatiquement (angles libres) : les redresser à la main si une présentation plus propre est
souhaitée, sans conséquence électrique.
Les empreintes `ESP32-S3_SuperMini_Socket` et `Regulator_Module_3V3` embarquent un modèle 3D simplifié
(`morfsensor.3dshapes/`) : supports et module avec USB-C et antenne, platine du régulateur avec SOT-223.

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

## À vérifier avant fabrication

Ces trois cotes viennent de photos et de catalogues, pas d'un pied à coulisse :

1. **Entraxe des deux rangées du S3** : 15,24 mm supposé (`XR - XL` dans l'empreinte
   `morfsensor.pretty/ESP32-S3_SuperMini_Socket`). Mesurer entre les deux rangées de trous du module.
2. **Module régulateur** : paires de broches IN+/GND et OUT+/GND supposées espacées de 22,86 mm
   (empreinte `Regulator_Module_3V3`). Mesurer sur le module, et vérifier le sens IN/OUT de chaque paire.
3. **Connecteurs** : JST-XH, pas 2,50 mm, verticaux (B2B/B3B/B4B-XH-A). Vérifier la polarité des câbles existants
   contre l'ordre des broches ci-dessus.

Rappel du point de vigilance de `docs/cablage.md` : la plage d'entrée annoncée du module (4,5 à 7 V) est
au-dessus de celle d'un accu Li-ion. À mesurer sur banc avant de figer la carte.

## Contrôles passés (KiCad 10.0.7, `kicad-cli`)

- ERC : 0 violation.
- DRC avec parité schéma/PCB : 0 erreur, 0 piste non connectée. Restent des avertissements de sérigraphie
  (textes proches des contours de connecteurs).

Les fichiers ont été générés par script puis vérifiés par ces commandes ; les ouvrir une fois dans KiCad et
lancer `Fichier > Enregistrer` les remet au format exact de l'éditeur.
