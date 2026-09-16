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
// TRUE = mode autonome sur batterie : le mode continu vide le 14500 en quelques
// heures. La chaîne unicast gère le réveil (attente de l'ACK avant sommeil, canal
// mémorisé en RAM RTC). Passer à FALSE seulement pour un debug sur USB.
constexpr bool ENABLE_DEEP_SLEEP = true;

// Cadence de MESURE et d'émission, en secondes. NE PAS coder en dur ailleurs :
// c'est la seule source de vérité. IN (côté MeteoHub) doit utiliser la même
// cadence, pour que les deux séries de l'historique soient homogènes.
// La météo évolue lentement : 5 min est la valeur nominale. Facile à changer
// pour les tests : 30 / 120 / 300 / 600 (30 s / 2 / 5 / 10 min).
// En deep sleep (futur capteur autonome), c'est aussi la base du cycle
// énergétique : réveil -> mesure -> émission -> deep sleep -> réveil suivant.
#define SENSOR_MEASUREMENT_INTERVAL_SECONDS 300

// --- ESP-NOW Configuration ---
// SSID/mot de passe de l'AP du hub (identiques a ESPNOW_SOFTAP_* cote station).
// La sonde s'associe ici : plus de scan Livebox ni de lock canal promiscuous.
constexpr char ESPNOW_HUB_AP_SSID[] = "MH-NOW";
constexpr char ESPNOW_HUB_AP_PASS[] = "espnowap";

// Repli uniquement si le scan du SSID (secrets.h) n'a pas donne le canal AP.
constexpr uint8_t ESPNOW_WIFI_CHANNEL_FALLBACK = 1;

// Canal PAR DÉFAUT / repli si le scan du SoftAP « MH-NOW » n'aboutit pas. Le
// canal réel n'est PAS fixe : le hub suit le canal de la Livebox, qui peut
// changer. La sonde découvre donc le canal en scannant le SoftAP du hub
// (ESPNOW_HUB_AP_SSID) au démarrage, et le revérifie périodiquement en usage.
constexpr uint8_t ESPNOW_HUB_CHANNEL = 6;

// Revérification périodique du canal (mode continu). Le hub peut migrer de canal :
// on rescanne « MH-NOW » à cet intervalle et on bascule si le canal a changé.
constexpr uint32_t ESPNOW_CHANNEL_RECHECK_SEC = 300; // 5 min

// En DEEP SLEEP, le canal découvert est mémorisé en RAM RTC (survit au sommeil) et
// réutilisé directement au réveil : on évite un scan Wi-Fi (~2 s, coûteux) à chaque
// réveil. On ne rescanne que toutes les N réveils (au cas où le hub a migré). À
// 5 min/réveil, 12 = ~1 h.
constexpr uint32_t ESPNOW_RESCAN_EVERY_N_WAKES = 12;

// Sous ce niveau de batterie, on SAUTE la revérification de canal : un scan Wi-Fi
// coûte de l'énergie, et une pile en fin de vie ne doit pas être gaspillée à
// chasser un canal (une silence radio due à la pile n'est pas un souci de canal).
constexpr uint8_t ESPNOW_RESCAN_SKIP_BELOW_PCT = 10;

// Copier la MAC STA affichee page Net. du hub (pas la MAC AP).
constexpr uint8_t ESPNOW_RECEIVER_MAC[6] = {0x20, 0x6E, 0xF1, 0x85, 0x58, 0x68};

// Nombre maximal de tentatives d'émission avant abandon si ACK requis
constexpr uint8_t ESPNOW_MAX_RETRIES = 3;

// --- Puissance d'émission ESP-NOW ---
// Appliquée quand la carte le demande (SENSOR_NEEDS_TX_LIMIT, trait de carte
// défini dans board_config.h). Valeur d'un wifi_power_t : la macro n'est
// développée que dans espnow_sender.cpp (WiFi.h).
//
// Menu S3 (Super Mini) : stable à 11 dBm. Prouvé terrain (logs USB unicast) : à
// pleine puissance le S3 n'obtient AUCUN ACK ; 11 dBm est livré (testé sur
// batterie, dehors). Pour tester une autre valeur, décommenter la ligne voulue et
// reflasher, en gardant le maximum de portée qui reste livré (ACK).
//#define SENSOR_TX_POWER_LEVEL WIFI_POWER_8_5dBm
#define SENSOR_TX_POWER_LEVEL WIFI_POWER_11dBm
//#define SENSOR_TX_POWER_LEVEL WIFI_POWER_15dBm
//#define SENSOR_TX_POWER_LEVEL WIFI_POWER_17dBm
//#define SENSOR_TX_POWER_LEVEL WIFI_POWER_19dBm

// --- Paramètres Batterie ---
// Ratio du diviseur de tension (R1 + R2) / R2
// Ex: deux résistances de 100k -> ratio = (100 + 100) / 100 = 2.0f
// COMMUN aux deux cartes : même montage 100k/100k.
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;

// Tension min/max (0 % / 100 %) et FACTEUR DE CALIBRATION FIN
// (BATTERY_VREF_CALIBRATION) : définis PAR CARTE dans board_config.h.
// - min/max dépendent de la CHIMIE (une LiFePO4 ~2,9-3,6 V lue sur une échelle
//   Li-ion 3,3-4,2 apparaîtrait à 0 % alors qu'elle est pleine).
// - le facteur fin dépend de la PUCE (calibration ADC eFuse propre à chaque
//   ESP32) et de la tolérance réelle des résistances de la carte : il se mesure
//   carte par carte (outil _calib-batterie : VREF = Vmultimètre / Vlu).
// Le ratio du pont, lui, reste commun (ci-dessus).
