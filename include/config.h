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

// EXPERIMENTAL (test alim) : dormir en LIGHT SLEEP au lieu de DEEP SLEEP.
//   - Deep sleep : ~10 µA, mais REBOOT au reveil (setup rejoue, RTC conservee).
//   - Light sleep : conserve la RAM/le contexte (pas de reboot), la radio se coupe
//     puis reprend, et tire NETTEMENT plus de courant (~centaines de µA).
// But du test : le module Breadvolt (boost AP2004H) semble decrocher a la charge
// tres faible du deep sleep -> le rail 3,3 V flechit, le domaine RTC brownout, la
// sonde ne se reveille plus (aucune perte en USB, seulement sur Breadvolt). Le
// light sleep charge davantage le boost : s'il tient, on a la reponse sans
// materiel. Prend le pas sur ENABLE_DEEP_SLEEP quand true. Repasser a false pour
// revenir au deep sleep une fois le test conclu.
// Remis a false : on teste d'abord un AUTRE module S3 (cense identique) en DEEP
// SLEEP, pour isoler si le decrochage vient de la carte elle-meme. L'option reste
// disponible : repasser a true pour tester le light sleep si besoin.
constexpr bool USE_LIGHT_SLEEP = false;

// Cadence de MESURE et d'émission, en secondes. NE PAS coder en dur ailleurs :
// c'est la seule source de vérité. IN (côté MeteoHub) doit utiliser la même
// cadence, pour que les deux séries de l'historique soient homogènes.
// La météo évolue lentement : 5 min est la valeur nominale. Facile à changer
// pour les tests : 30 / 120 / 300 / 600 (30 s / 2 / 5 / 10 min).
// En deep sleep (futur capteur autonome), c'est aussi la base du cycle
// énergétique : réveil -> mesure -> émission -> deep sleep -> réveil suivant.
#define SENSOR_MEASUREMENT_INTERVAL_SECONDS 300

// --- Buffer de securite local (synchronisation fiable v3) ------------------
// Historique local en Flash (LittleFS, partition « spiffs » de 1,44 Mo). Ring
// buffer borne : chaque mesure y est ecrite AVANT l'envoi et n'est declaree
// synchronisee qu'apres l'accuse cumulatif du hub. Objectif de retention 30 j.
//   30 j @ 5 min = 8640 mesures ; 8640 * 32 o + en-tete = ~270 Ko (~19 % de la
//   partition) -> les 30 jours tiennent avec une large marge.
//
// Depuis 0.24.0 : journal SEGMENTE en ajout seul (un fichier par jour de mesures
// dans SENSOR_SEGMENT_DIR), filigrane d'accuse en NVS. L'ancien fichier unique
// (SENSOR_BUFFER_PATH) etait recopie en entier par LittleFS a chaque mesure
// (4,5 s mesurees) ; il n'est plus lu qu'une fois, pour reprendre ses mesures
// en attente, puis efface.
constexpr uint32_t SENSOR_SEGMENT_RECORDS = 288;   // 1 jour @ 5 min = 9 Ko par fichier
constexpr uint32_t SENSOR_MAX_SEGMENTS    = 30;    // retention : 30 jours en attente
constexpr char     SENSOR_SEGMENT_DIR[]   = "/mhs";
// Ancien format (lecture unique a la migration).
constexpr uint32_t SENSOR_BUFFER_CAPACITY = 8640;
constexpr char     SENSOR_BUFFER_PATH[]   = "/mhs_buffer.bin";

// Retransmission BORNEE par cycle : on ne rejoue qu'un lot limite de mesures
// historiques par reveil, pour ne pas transformer l'eveil en session illimitee
// (l'autonomie prime). Le reste se rattrape aux reveils suivants.
constexpr uint32_t SENSOR_RETX_MAX_PER_CYCLE = 10;

// Fenetre d'ecoute (ms) ouverte APRES l'envoi live pour recevoir le SyncControl
// du hub (accuse cumulatif + trou a combler). Courte et bornee : elle ne rallonge
// l'eveil que de ce delai au maximum, puis on retransmet et on dort.
constexpr uint32_t SENSOR_SYNC_RX_WINDOW_MS = 300;

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

// MAC STA du hub PAR DEFAUT, utilisee seulement tant qu'aucun appairage n'est
// enregistre en NVS (une sonde deja en service garde ainsi son hub apres une mise
// a jour du firmware). Depuis l'appairage par appui long sur BOOT, la MAC n'a
// plus besoin d'etre connue a la compilation : laisser {0,0,0,0,0,0} pour une
// sonde neuve, qui attend alors d'etre appairee (mesures gardees en attente).
constexpr uint8_t ESPNOW_RECEIVER_MAC[6] = {0x20, 0x6E, 0xF1, 0x85, 0x58, 0x68};

// --- Appairage (appui long sur BOOT) ---
// Appui long = « je veux changer de hub ». Action VOLONTAIRE uniquement : une
// perte de liaison ne declenche jamais d'appairage (un hub simplement eteint ne
// doit pas faire basculer la sonde vers un autre).
constexpr uint32_t PAIRING_LONG_PRESS_MS = 3000;   // appui minimal pour lancer
// Au-dela, le bouton est considere COINCE (boitier, cable, humidite sur GPIO0) :
// aucun appairage. Vu sur le terrain : une sonde dehors lancait seule des
// appairages et sautait ses mesures. L'appairage part au RELACHEMENT d'un appui
// compris entre PAIRING_LONG_PRESS_MS et PAIRING_STUCK_MS : un appui permanent ne
// se relache jamais, il ne peut donc rien declencher.
constexpr uint32_t PAIRING_STUCK_MS = 10000;
constexpr uint32_t PAIRING_TIMEOUT_MS = 60000;     // abandon (association inchangee)
constexpr uint8_t  PAIRING_MAX_CHANNEL = 13;       // canaux Wi-Fi balayes (Europe)
constexpr uint8_t  PAIRING_REQUESTS_PER_CHANNEL = 3; // demandes par canal
constexpr uint32_t PAIRING_REQUEST_GAP_MS = 150;   // ecoute apres chaque demande
// Un balayage complet dure ~13 x 3 x 150 ms = ~6 s ; 60 s en permettent ~10.

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
