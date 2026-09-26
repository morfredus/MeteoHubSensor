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
- Le nœud de prod actuel (S3 Super Mini + module Breadvolt + accu Li-ion **14500 ~500 mAh**) tient plusieurs jours ; l'autonomie exacte reste à mesurer sur la carte réelle (le S3 dort moins efficacement que le C3 à cause du LDO et du PHY USB).
- Avec une cellule plus grosse (14500/18650) ou un petit panneau solaire + module de charge, le système peut viser une autonomie longue durée. Chiffres à confirmer par la mesure terrain, pas par le calcul seul.

---

## 3. Synchronisation du canal Wi-Fi

ESP-NOW n'existe que sur **le canal de l'AP** auquel MeteoHub est associé.

La sonde repère le canal grâce à l'AP `MH-NOW` du hub, puis envoie en unicast.
La MAC du hub vient de la NVS, écrite par l'appairage (appui long sur BOOT, voir
le README). Une fois appairée, la sonde cherche le BSSID exact de l'AP de SON
hub : plusieurs hubs peuvent cohabiter. `ESPNOW_RECEIVER_MAC` (`config.h`) ne
sert plus que de valeur par défaut tant qu'aucun appairage n'est enregistré.
Le statut local est la LED RGB et le moniteur série (plus d'OLED sur la sonde).

- Copier `include/secrets_example.h` vers `include/secrets.h` ici (ignoré par git).
- Si le SSID n'est pas vu : canal RTC, sinon repli `ESPNOW_WIFI_CHANNEL_FALLBACK`.
- Côté station, la page OLED **Net.** affiche canal STA et MAC : les deux doivent coller.

---

## 4. Évolution et extensibilité

Le protocole utilise un champ `valid_fields` (masque binaire 16 bits). Cela permet à MeteoHub S3 de savoir instantanément quelles données sont fiables et présentes :

- Si seule la sonde AHT20 est branchée : seuls les bits `FIELD_TEMPERATURE` et `FIELD_HUMIDITY` sont actifs.
- Lorsqu'un anémomètre est ajouté ultérieurement : le bit `FIELD_WIND_SPEED` est activé, et MeteoHub commence automatiquement à enregistrer et afficher le vent sans casser la compatibilité des anciennes trames.
