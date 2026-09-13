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

### Autonomie théorique (sur batterie Li-ion 18650 de 2500 mAh) :
- Pour une mesure toutes les **60 secondes** :
  - Consommation moyenne globale : ~35 µA.
  - Autonomie théorique : **plusieurs mois à plus d'un an** sans recharge.
  - Avec un petit panneau solaire 5V 1W et un module de charge TP4056/CN3791, le système devient **100% autonome à l'année**.

---

## 3. Synchronisation du canal Wi-Fi

ESP-NOW n'existe que sur **le canal de l'AP** auquel MeteoHub est associé.

La sonde rejoint l'AP `MH-NOW` du hub pour caler le canal, puis envoie en
unicast. Recopier la MAC STA de la page **Net.** du hub dans `config.h`.
Le statut local est la LED RGB et le moniteur série (plus d'OLED sur la sonde).

- Copier `include/secrets_example.h` vers `include/secrets.h` ici (ignoré par git).
- Si le SSID n'est pas vu : canal RTC, sinon repli `ESPNOW_WIFI_CHANNEL_FALLBACK`.
- Côté station, la page OLED **Net.** affiche canal STA et MAC : les deux doivent coller.

---

## 4. Évolution et extensibilité

Le protocole utilise un champ `valid_fields` (masque binaire 16 bits). Cela permet à MeteoHub S3 de savoir instantanément quelles données sont fiables et présentes :

- Si seule la sonde AHT20 est branchée : seuls les bits `FIELD_TEMPERATURE` et `FIELD_HUMIDITY` sont actifs.
- Lorsqu'un anémomètre est ajouté ultérieurement : le bit `FIELD_WIND_SPEED` est activé, et MeteoHub commence automatiquement à enregistrer et afficher le vent sans casser la compatibilité des anciennes trames.
