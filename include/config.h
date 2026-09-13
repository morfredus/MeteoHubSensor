#pragma once
#include <Arduino.h>

// ============================================================================
// Configuration générale du nœud météo extérieur (ESP32-S3 Super Mini)
// ============================================================================

// --- Identifiant de la sonde ---
constexpr uint8_t SENSOR_NODE_ID = 1; // ID unique du nœud (pour distinguer plusieurs sondes si besoin)

// --- Mode d'alimentation et Fréquence de mesure ---
// Si TRUE : mesure, émission ESP-NOW immédiate puis passage en deep sleep
// Si FALSE : boucle active millis() permanente (idéal pour debug ou alimentation secteur)
constexpr bool ENABLE_DEEP_SLEEP = false; 

// Intervalle de mesure et d'émission (en secondes)
// En mode deep sleep, l'ESP se réveille toutes les X secondes.
// En mode continu, loop() émet toutes les X secondes.
constexpr uint32_t MEASUREMENT_INTERVAL_SEC = 30;

// --- ESP-NOW Configuration ---
// SSID/mot de passe de l'AP du hub (identiques a ESPNOW_SOFTAP_* cote station).
// La sonde s'associe ici : plus de scan Livebox ni de lock canal promiscuous.
constexpr char ESPNOW_HUB_AP_SSID[] = "MH-NOW";
constexpr char ESPNOW_HUB_AP_PASS[] = "espnowap";

// Repli uniquement si le scan du SSID (secrets.h) n'a pas donne le canal AP.
constexpr uint8_t ESPNOW_WIFI_CHANNEL_FALLBACK = 1;

// Canal STA du hub (page Net. / log "receiver ready ch="). Le scan SSID
// n'est pas celui du S3 : rx=0 tant qu'on n'envoie pas sur CE canal.
constexpr uint8_t ESPNOW_HUB_CHANNEL = 6;

// Copier la MAC STA affichee page Net. du hub (pas la MAC AP).
constexpr uint8_t ESPNOW_RECEIVER_MAC[6] = {0x20, 0x6E, 0xF1, 0x85, 0x58, 0x68};

// Nombre maximal de tentatives d'émission avant abandon si ACK requis
constexpr uint8_t ESPNOW_MAX_RETRIES = 3;

// --- Paramètres Batterie ---
// Ratio du diviseur de tension (R1 + R2) / R2
// Ex: deux résistances de 100k -> ratio = (100 + 100) / 100 = 2.0f
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float BATTERY_VREF_CALIBRATION = 1.0f; // Facteur d'ajustement fin ADC

// Tension min/max pour calcul du pourcentage Li-ion/LiFePO4
constexpr float BATTERY_VOLTAGE_MIN = 3.3f;
constexpr float BATTERY_VOLTAGE_MAX = 4.2f;
