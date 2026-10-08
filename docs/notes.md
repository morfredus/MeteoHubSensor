# Notes d'architecture et choix techniques - MeteoHubSensor

## 1. Pourquoi ESP-NOW ?

L'objectif principal de ce projet est de déporter l'acquisition météo dans le jardin ou sur un balcon sans créer de fragilité sur le réseau :

- **Indépendance vis-à-vis du réseau local** : Si la box internet plante, redémarre ou change de mot de passe, l'acquisition météo ne s'arrête pas une seule seconde.
- **Ultra-faible consommation** : L'association Wi-Fi complète (DHCP, négociation WPA2, échange de clés) prend généralement entre 1.5 et 3 secondes. Avec ESP-NOW, la trame est émise en moins de 15 millisecondes dès le réveil.
- **Simplicité opérationnelle** : Aucun serveur web ou pile réseau complexe sur la sonde extérieure.

---

## 2. Consommation et autonomie sur batterie

### Bilan de consommation estimé :
1. **Phase de mesure et émission** (durée ~60 ms) :
   - Réveil + initialisation I2C : ~20 ms @ 20 mA
   - Mesure AHT20/BMP280 : ~15 ms @ 5 mA
   - Émission radio ESP-NOW : ~15 ms @ 120 mA
   - Énergie par cycle : environ \(0.002 \text{ mAh}\) par mesure.

2. **Phase de veille profonde (Deep Sleep)** :
   - ESP32-S3 Super Mini en deep sleep : plus gourmand que le C3 (LDO + USB
     PHY). Compter quelques dizaines de µA, à mesurer sur la carte réelle.
   - S'ajoute le **pont de mesure batterie** (100k/100k sur le + accu) qui draine
     ~20 µA en continu. Négligeable ici, mais présent aussi pendant le deep sleep :
     pour le supprimer, on pourrait commander le pont par un GPIO (MOSFET) ; jugé
     inutile vu l'ordre de grandeur.

### Autonomie théorique :
- La cadence réelle est de **5 minutes** (`SENSOR_MEASUREMENT_INTERVAL_SECONDS`, configurable) : bien plus économe qu'une mesure par minute, l'essentiel du temps étant passé en deep sleep.
- Le nœud de prod actuel (S3 Super Mini + module régulateur 3,3 V Youmi + accu Li-ion ; 500 mAh au départ, **3000 mAh** depuis le 2026-10-03 ; deux accus à permuter, chargés dans un chargeur de bureau) tient plusieurs jours ; l'autonomie exacte reste à mesurer sur la carte réelle (le S3 dort moins efficacement que le C3 à cause du LDO et du PHY USB).
- Avec une cellule plus grosse (14500/18650) ou un petit panneau solaire + module de charge, le système peut viser une autonomie longue durée. Chiffres à confirmer par la mesure terrain, pas par le calcul seul.

---

## 3. Synchronisation du canal Wi-Fi

ESP-NOW n'existe que sur **le canal de l'AP** auquel MeteoHub est associé.

La sonde repère le canal grâce à l'AP `MH-NOW` du hub, puis envoie en unicast.
La MAC du hub vient de la NVS, écrite par l'appairage (appui long sur BOOT, voir
le README). Une fois appairée, la sonde cherche le BSSID exact de l'AP de SON
hub : plusieurs hubs peuvent cohabiter. `ESPNOW_RECEIVER_MAC` (`config.h`) vaut
zéro : sans appairage enregistré, la sonde n'envoie rien et attend un appui long.
Le statut local est la LED RGB et le moniteur série (plus d'OLED sur la sonde).

- Copier `include/secrets_example.h` vers `include/secrets.h` ici (ignoré par git).
- Si le SSID n'est pas vu : canal RTC, sinon repli `ESPNOW_WIFI_CHANNEL_FALLBACK`.
- Côté station, la page OLED **Net.** affiche canal STA et MAC : les deux doivent coller.

---

## 4. Évolution et extensibilité

Le protocole utilise un champ `valid_fields` (masque binaire 16 bits). Cela permet à MeteoHub S3 de savoir instantanément quelles données sont fiables et présentes :

- Si seule la sonde AHT20 est branchée : seuls les bits `FIELD_TEMPERATURE` et `FIELD_HUMIDITY` sont actifs.
- Lorsqu'un anémomètre est ajouté ultérieurement : le bit `FIELD_WIND_SPEED` est activé, et MeteoHub commence automatiquement à enregistrer et afficher le vent sans casser la compatibilité des anciennes trames.

---

## 5. Retour terrain : sonde de prod remplacée par une carte avec antenne filaire (2026-10-06)

**Info importante : matériel.** La sonde de prod est une **ESP32-S3 Super Mini N4R2** (4 Mo flash,
PSRAM 2 Mo quad). L'ancienne sonde (MAC 3C:0F:02:E2:C7:D4, vue du hub `E2:C7:D4`) et la nouvelle
(AC:27:6E:CC:B2:DC, vue du hub `CC:B2:DC`) sont **le même modèle de carte** : la comparaison ne
mélange pas deux puces. Le C3 (HW-675) n'existe que sur le banc, jamais en prod. Ne pas déduire
le modèle d'une MAC (le préfixe ne distingue pas S3 et C3).

**Attention au mot « C3 » : l'inscription « C3 » sur la puce céramique rouge de la carte est le
marquage de l'antenne céramique, commune aux Super Mini ESP32-C3 et ESP32-S3. Ce n'est pas le
SoC : une carte S3 porte bien ce « C3 » sur son antenne.**

- Nouvelle sonde : carte avec une antenne filaire ajoutée autour de l'antenne céramique. Fil rigide 20 AWG
  (0,5 mm), 35 mm au total : 15 mm en vertical, le reste forme la boucle autour des deux côtés de
  l'antenne céramique. Photo du montage : `docs/antenne-filaire-g2.jpg` (mesures faites avec le banc d'essai Wi-Fi). Géométrie très
  sensible : 1 mm de boucle a valu ~9 dB sur le banc.
- Banc Wi-Fi (même carte, même endroit) : RSSI moyen -53,2 dBm sans antenne, -42,2 avec la 1re
  géométrie (34 mm), -33,4 avec la 2e (35 mm), 0 % de perte dans tous les cas. Ces mesures
  démontrent une amélioration importante du RSSI, mais pas encore une augmentation mesurable de la
  fiabilité de la liaison : le taux de perte était déjà de 0 % sans antenne dans ces conditions
  de test.
- Appairage du 2026-10-06 à 00:37 : 5 demandes en ~1 s, réponse du hub canal 11, sonde associée ;
  1re trame vivante à 00:42, `seq=1` sans mesures valides (ignorée par le hub), `seq=2` et `3`
  valides, rattrapage complet (`want=0/0`). Réveil `up=2s`, comme l'ancienne sonde.
- **Référence à comparer** : dernière ligne ESP-NOW du hub avec l'ancienne sonde : `rx=107 ok=107
  bad=0 rssi=-37` (canal 11). Comparer avec la ligne `src=...CC:B2:DC` suivante.
- Impression (non chiffrée, observation subjective à confirmer par une mesure) : association Wi-Fi plus rapide qu'avec la carte nue.
- TX ESP-NOW toujours plafonnée à 11 dBm par le firmware (`SENSOR_NEEDS_TX_LIMIT`) : à pleine
  puissance le hub n'acquittait pas avec l'antenne PCB d'origine. Non retesté avec l'antenne
  filaire ; sur le banc, une des deux cartes ne s'associait pas à 19,5 dBm (avec ou sans fil),
  l'autre oui : le comportement à pleine puissance dépend de la carte individuelle.
- À suivre : RSSI/ACK vus par le hub, courbe de batterie du hub (>= 1.59.0), tenue du fil à la
  pluie et au vent.

![Antenne filaire G2](antenne-filaire-g2.jpg)

**Sources de l'idée.** Fiche de la carte sur espboards.dev (encart « Good to know » : sur certaines
S3 Super Mini, l'antenne céramique est montée à l'envers, la piste d'alimentation ne touche alors
pas l'élément ; le point d'alimentation est du côté de la barre blanche) et l'article Hackaday
[Simple antenna makes for better ESP32-C3 WiFi](https://hackaday.com/2025/04/07/simple-antenna-makes-for-better-esp32-c3-wifi/)
(2025-04-07), qui décrit un fil quart d'onde de 31 mm. L'idée est cohérente avec une antenne quart d'onde à
2,4 GHz, soit environ 31 mm dans l'air. Le montage réellement retenu fait 35 mm de fil, mais sa
géométrie autour de l'antenne céramique fait partie intégrante du montage. Piste à vérifier si une carte
s'associe mal : l'orientation de l'antenne avant d'accuser la puce.

**Comment on en est arrivé là.** Aucune antenne n'était prévue. L'information est venue d'une
lecture sur espboards.dev, puis de l'article Hackaday : « quart d'onde, 2,4 GHz, environ 31 mm »
a rappelé les antennes de la CB. Un bout de fil 20 AWG a suivi, puis un banc d'essai Wi-Fi pour
vérifier que l'idée tenait.

## 6. DHT22 : référence température et humidité (0.30.0, étendu en 0.31.0)

- **Pourquoi le DHT22 en priorité** : dans l'installation réelle, le DHT22 est mieux isolé que le module
  AHT20/BMP280. Il donne un relevé hygrométrique plus réaliste et ne se bloque pas à 100 % (l'AHT20, lui,
  sature). Le critère est donc le comportement sur le terrain, pas la précision de la fiche technique ni l'écart du
  banc (voir le point suivant).
- **Principe (0.31.0)** : température et humidité viennent du DHT22 (GP1) seul. Le module BMP280+AHT20 ne sert plus
  qu'à la pression atmosphérique ; l'AHT20 n'est plus lu (bibliothèque retirée). Plus de repli : si le DHT22 est
  muet ou renvoie une trame invalide, température et humidité restent absentes du paquet (bits de validité à 0)
  plutôt que d'être remplacées par une valeur jugée moins fiable. Avant 0.31.0 : humidité DHT22 avec repli AHT20,
  température AHT20. Pas de détection à part : chaque cycle retente, un DHT22 absent échoue en quelques ms.
- **Pourquoi le DHT22 devient aussi la référence de température** : mêmes constats que pour l'humidité. Le relevé
  est plus stable et plus réaliste que celui de l'AHT20 du module, et un capteur unique pour les deux grandeurs
  évite un couple température/humidité venant de deux endroits physiques différents (l'humidité relative dépend
  directement de la température au point de mesure).
- **Calibration de l'humidité (2026-10-08)** : la sonde a été placée dans une atmosphère de solution saline saturée
  (NaCl), qui impose naturellement environ 75 % d'humidité relative. La sonde a rendu 75,0 à 75,1 % pendant 2 heures,
  stable et sans dérive : elle est considérée comme bien calibrée, aucun correctif nécessaire. Les constantes
  `DHT22_HUM_OFFSET` et `DHT22_TEMP_OFFSET` (`board_config.h`, à 0) restent disponibles pour une future recalibration.
- **Banc** (projet `40-test/dht22-aht-compare`, 813 mesures sur 42 min, 2026-10-07) : températures BMP280 - AHT20 =
  +0,06 °C (σ 0,02), DHT22 - AHT20 = -0,17 °C (σ 0,18) ; humidité DHT22 - AHT20 = -2,2 points (de -1,1 à -4,0,
  corrélation 0,92, écart en réduction au fil de la mesure) ; pression 1012,2 à 1013,0 hPa. Sans hygromètre de
  référence, on ne sait pas lequel est le plus juste ; offset d'affichage possible dans l'UI du banc.
- **Piège** : le type doit être DHT22. Avec DHT11 codé, la valeur lue ressemble à la température.
- **Conso estimée** (non mesurée) : environ 15 µA de repos (capteur alimenté en permanence), soit +4 % sur un cycle
  de 300 s ; lecture négligeable (quelques ms à 1,5 mA). Couper l'alimentation coûterait plus (1 à 2 s d'éveil).
  À confirmer au multimètre, DHT22 branché puis débranché.

## 7. Essai de la carte v2 en conditions réelles : aucune trame reçue (2026-10-07)

- **Contexte** : 0.30.0 en prod sur la v1 (DHT22 prioritaire pour l'humidité, repli AHT20), communication ok ;
  l'origine de l'humidité (DHT22) est transparente côté hub. Essai ensuite de la carte v2 (Electrocookie).
- **Constat** : avec la v2, **aucune trame n'arrive sur le hub**. Deux essais : un ESP32-S3 Super Mini équipé de
  l'antenne filaire, puis un autre sans antenne. Même résultat. La v2 mesurait et enregistrait pourtant bien (buffer
  de synchronisation).
- **Hypothèse (non démontrée)** : perturbations radioélectriques dues aux liaisons faites sous la carte (fil isolé,
  ponts de soudure sur les bandes), proches de l'antenne ou de la puce.
- **Pistes écartées** : sortir le S3 de la carte ne résout pas le problème. Dégager la zone d'antenne est impossible
  sans abandonner l'objectif de la v2 (réduire la taille). Même posée à côté du hub, la carte ne communiquait pas.
  La cause exacte reste donc inconnue.
- **Conclusion** : la v2 est un échec pour son objectif (carte plus compacte) ; la v1 reste la carte de production.
- **Retour** : carte v1 remise en place, modifiée pour accueillir le DHT22 ; fonctionnement ok dès le premier
  démarrage.
- **Rattrapage** : la sonde est revenue après 35 min de silence. Le hub a reçu le live seq=562 puis 6 retransmissions
  (seq 557 à 561), toutes archivées sans trou, puis un cycle normal (seq=563, réveil DEEPSLEEP). La synchronisation
  avec accusé cumulatif joue donc son rôle sur une coupure de 35 min.
- **Démarrage à froid** : la 1re mesure (reset POWERON, up=8s, t=20,0 °C) est jugée non conforme, archivée avec la
  marque `cold_boot` et exclue de l'affichage ; la carte venait d'être alimentée, donc encore chaude. Les
  retransmissions gardent `reset=POWERON` puisque ces mesures datent du démarrage.
