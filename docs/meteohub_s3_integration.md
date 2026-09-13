# Intégration côté MeteoHub (ESP32-S3)

Ce guide détaille comment intégrer la réception ESP-NOW sur la station **MeteoHub (ESP32-S3)**.

---

## 1. Structure du paquet

Copier le fichier `include/meteo_packet.h` de `MeteoHubSensor` dans le dossier `include/` de `MeteoHub`.

---

## 2. Initialisation ESP-NOW sur l'ESP32-S3 (Récepteur)

Dans l'ESP32-S3 de MeteoHub, initialiser ESP-NOW après la connexion Wi-Fi (ou après `WiFi.mode(WIFI_STA)`) :

```cpp
#include <esp_now.h>
#include "meteo_packet.h"

// Variable globale pour stocker la dernière mesure reçue
static MeteoPacket lastOutdoorMetrics;
static bool outdoorMetricsAvailable = false;
static uint32_t lastOutdoorPacketTime = 0;

// Callback appelé à chaque réception d'une trame ESP-NOW
void onMeteoDataReceived(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len != sizeof(MeteoPacket)) {
        Serial.printf("[METEO-S3] [WARN] Paquet de taille invalide: %d (attendu: %d)\n", len, sizeof(MeteoPacket));
        return;
    }

    MeteoPacket packet;
    memcpy(&packet, incomingData, sizeof(MeteoPacket));

    // 1. Validation de l'en-tête Magic
    if (packet.magic[0] != METEO_PACKET_MAGIC_0 || packet.magic[1] != METEO_PACKET_MAGIC_1) {
        Serial.println("[METEO-S3] [WARN] Magic packet invalide");
        return;
    }

    // 2. Validation du CRC16
    size_t dataLen = sizeof(MeteoPacket) - sizeof(packet.crc16);
    uint16_t expectedCrc = calculateCrc16((const uint8_t*)&packet, dataLen);
    if (packet.crc16 != expectedCrc) {
        Serial.println("[METEO-S3] [WARN] Echec de verification CRC16");
        return;
    }

    // 3. Traitement des métriques
    lastOutdoorMetrics = packet;
    outdoorMetricsAvailable = true;
    lastOutdoorPacketTime = millis();

    Serial.printf("[METEO-S3] Reception Sonde #%u (Seq: %u) : Temp=%.2f C, Hum=%.2f %%, Bat=%.2f V\n",
                  packet.node_id, packet.sequence, packet.temperature, packet.humidity, packet.battery_voltage);

    // 4. Injection dans le gestionnaire de stockage SD / historique de MeteoHub
    // sensorManager.updateOutdoorData(packet.temperature, packet.humidity, packet.pressure);
}

void initMeteoHubReceiver() {
    // Initialise ESP-NOW
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(onMeteoDataReceived);
        Serial.println("[METEO-S3] Recepteur ESP-NOW initialise avec succes");
    } else {
        Serial.println("[METEO-S3] [ERROR] Echec init ESP-NOW sur S3");
    }
}
```

---

## 3. Détection des pertes de paquets

Grâce au champ `packet.sequence` incrémenté à chaque émission par la sonde, MeteoHub peut détecter facilement les paquets perdus :

```cpp
static uint32_t expectedSequence = 0;

if (expectedSequence != 0 && packet.sequence > expectedSequence) {
    uint32_t lostCount = packet.sequence - expectedSequence;
    Serial.printf("[METEO-S3] [WARN] %u paquet(s) perdu(s) entre la sonde et le Hub\n", lostCount);
}
expectedSequence = packet.sequence + 1;
```
