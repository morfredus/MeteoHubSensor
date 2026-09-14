#pragma once
#include <Arduino.h>

// ============================================================================
// Brochage MeteoHubSensor - deux cibles materielles
// ============================================================================
// La sonde existe en deux variantes de carte, choisies par le define pose dans
// platformio.ini (env supermini -> S3, env c3oled -> C3) :
//
//   SENSOR_BOARD_S3  : ESP32-S3 Super Mini (LED RGB seule, sans ecran).
//   SENSOR_BOARD_C3  : ESP32-C3 HW-675 (0.42" OLED SSD1306 72x40 integre).
//
// Le reste du firmware ne connait que les alias PIN_* et les drapeaux
// SENSOR_HAS_OLED / SENSOR_NEEDS_TX_LIMIT : ajouter une 3e carte se fait ici,
// sans toucher les modules.
// ============================================================================

// Repli defensif : si aucun env n'a pose de define (compilation hors PlatformIO,
// intellisense...), on retombe sur le S3, cible historique. platformio.ini pose
// toujours l'un des deux explicitement.
#if !defined(SENSOR_BOARD_C3) && !defined(SENSOR_BOARD_S3)
#define SENSOR_BOARD_S3
#endif

#if defined(SENSOR_BOARD_C3)
// ---------------------------------------------------------------------------
// ESP32-C3 HW-675 (0.42" OLED integre)
// ---------------------------------------------------------------------------
// Brochage impose par la carte (cf. notice HW-675) : le bus I2C GP5/GP6 est
// deja cable a l'ecran OLED. Les capteurs AHT20/BMP280 se greffent sur CE MEME
// bus (I2C est multi-esclave : OLED 0x3C, AHT20 0x38, BMP280 0x76/0x77).
constexpr char BOARD_NAME[] = "ESP32-C3 HW-675";

// I2C partage OLED + capteurs (GP5 = SDA, GP6 = SCL sur la HW-675).
constexpr uint8_t PIN_SENSOR_SDA = 5;
constexpr uint8_t PIN_SENSOR_SCL = 6;
constexpr int8_t PIN_SENSOR_POWER = -1; // alim 3V3 permanente

// LED RGB WS2812B onboard (GP8, strapping High au boot : pilotee apres init).
constexpr int8_t PIN_RGB_LED = 8;
constexpr uint8_t NUM_PIXELS = 1;

// Bouton BOOT / flash (GP9).
constexpr uint8_t PIN_BOOT_BUTTON = 9;

// Batterie sur GP4 (ADC1_CH4) via pont diviseur. Alimentation : 2 piles alcalines
// 1,5 V en serie. Neuves ~3,2 V (2x1,6), nominal 3,0 V, considerees vides vers
// ~2,0 V (2x1,0). Une echelle Li-ion (3,3-4,2) classerait ces 3,0 V a 0 %.
constexpr uint8_t PIN_BATTERY_ADC = 4;
constexpr float BATTERY_VOLTAGE_MIN = 2.0f; // 0 %  (2 x ~1,0 V)
constexpr float BATTERY_VOLTAGE_MAX = 3.2f; // 100 % (2 x ~1,6 V neuves)
// Facteur de calibration fin propre a cette carte (voir config.h). Calibre le
// 2026-09-15 avec l'outil _calib-batterie : multimetre 4,11 V, ecran 4,070 V ->
// 4,11 / 4,070 = 1,0098 (correction de ~1 %, tolerance des resistances).
constexpr float BATTERY_VREF_CALIBRATION = 1.0098f;

// --- Reserves meteo (GPIO libres de la HW-675) ---
constexpr uint8_t PIN_ANEMOMETER_PULSE = 10; // GP10 (numerique)
constexpr uint8_t PIN_WIND_VANE_ADC = 3;     // GP3 (ADC1_CH3)
constexpr uint8_t PIN_RAIN_GAUGE_PULSE = 7;  // GP7 (numerique)
constexpr uint8_t PIN_AUX_ADC = 2;           // GP2 (ADC1_CH2)
constexpr uint8_t PIN_FREE_GP0 = 0;
constexpr uint8_t PIN_FREE_GP1 = 1;

// La HW-675 porte l'ecran 0.42" (SSD1306 72x40) sur le bus I2C ci-dessus.
#define SENSOR_HAS_OLED 1
#define SENSOR_OLED_72X40 1
// Adresse I2C de l'ecran (SSD1306 par defaut).
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;

// Le C3 Super Mini / HW-675 demande une puissance TX ESP-NOW plafonnee pour
// communiquer de facon stable avec le MeteoHub S3 : a pleine puissance, la
// liaison echoue (constat materiel, cf. CHANGELOG). Voir EspNowSender::begin().
// C'est un TRAIT DE CARTE (ce brochage/cette radio le demande) ; la VALEUR du
// plafond, elle, est un reglage et vit dans config.h (SENSOR_TX_POWER_LEVEL).
#define SENSOR_NEEDS_TX_LIMIT 1

#else
// ---------------------------------------------------------------------------
// ESP32-S3 Super Mini (LED RGB seule, sans ecran)
// ---------------------------------------------------------------------------
// USB natif : GPIO 19/20. BOOT : GPIO 0 (non sorti). Straps : 45/46.
// LED RGB onboard : GPIO 48 (GPIO 46 = input-only, ignore malgre le pinout).
constexpr char BOARD_NAME[] = "ESP32-S3 Super Mini";

// --- Capteurs AHT20 (0x38) et BMP280 (0x77) ---
// Cablage : fil SDA du module -> GP8, fil SCL du module -> GP9, 3V3, GND.
constexpr uint8_t PIN_SENSOR_SDA = 8;
constexpr uint8_t PIN_SENSOR_SCL = 9;
constexpr int8_t PIN_SENSOR_POWER = -1; // -1 = alim 3V3 permanente

// LED RGB onboard.
constexpr int8_t PIN_RGB_LED = 48;
constexpr uint8_t NUM_PIXELS = 1;

// Bouton BOOT.
constexpr uint8_t PIN_BOOT_BUTTON = 0;

// Alimentation : module Breadvolt + accu Li-ion 14500 (3,7 V, 500 mAh). Le
// module fournit un 3,3 V REGULE a la carte et gere lui-meme l'accu (protection
// decharge 2,4 V / charge 4,28 V, LEDs CHG/PWR). Le 3,3 V regule est CONSTANT :
// le mesurer ne dirait rien de l'accu. On tape donc la CELLULE, AVANT le
// regulateur, au + de l'accu (multimetre ~4,0 V en charge).
//
// Cablage batterie (2026-09-15) : + accu --[R1 100k]--(GP4)--[R2 100k]-- GND.
// GP4 lit le point milieu = tension accu / 2 (~2,0 V pour 4,0 V), sous la limite
// ADC du S3 : indispensable, l'accu monte a 4,28 V en pleine charge et ne doit
// JAMAIS arriver brut sur la pin. Le firmware remultiplie par le ratio du pont
// (BATTERY_DIVIDER_RATIO = 2,0 dans config.h, commun aux deux cartes). Le pont
// draine en continu ~4,0 V / 200 k = 20 uA, negligeable devant les reveils TX.
// La plage Li-ion 2,6-4,2 ci-dessous convertit en pourcentage.
constexpr int8_t PIN_BATTERY_ADC = 4;
// 0 % a 2,6 V : le module coupe a 2,4 V (protection decharge), on garde 0,2 V de
// marge au-dessus pour afficher 0 % juste avant la coupure. La sonde tourne donc
// jusqu'au bout au lieu de plafonner a 0 % des 3,0 V. Attention : sous ~3,0 V la
// courbe Li-ion s'effondre, les derniers % defilent vite (peu de capacite reelle
// dans cette queue) ; la jauge reste honnete, elle ne rallonge pas l'autonomie.
constexpr float BATTERY_VOLTAGE_MIN = 2.6f; // 0 %  (0,2 V au-dessus de la coupure 2,4 V)
constexpr float BATTERY_VOLTAGE_MAX = 4.2f; // 100 % (Li-ion pleine charge)
// Facteur de calibration fin propre a CE S3 (voir config.h). Pas encore mesure
// sur le S3 lui-meme : 1.0 (analogReadMilliVolts applique deja l'usine, on part
// a ~1 % pres). Pour l'affiner, flasher l'outil _calib-batterie (env s3) sur le
// S3, lire via Serial et poser VREF = Vmultimetre / Vlu. NB : la valeur du C3
// (1,0098) ne se transpose pas, c'est une autre puce ADC.
constexpr float BATTERY_VREF_CALIBRATION = 1.0f;

// --- Reserves meteo ---
constexpr uint8_t PIN_ANEMOMETER_PULSE = 1;
constexpr uint8_t PIN_WIND_VANE_ADC = 2;
constexpr uint8_t PIN_RAIN_GAUGE_PULSE = 7;
constexpr uint8_t PIN_AUX_ADC = 10;
constexpr uint8_t PIN_FREE_GP5 = 5;
constexpr uint8_t PIN_FREE_GP6 = 6;

// Pas d'ecran gere sur le S3 : le statut passe par la LED RGB seule. L'OLED
// n'est volontairement PAS piloté ici pour reduire la consommation (un ecran
// jamais allume reste dans son etat reset basse conso). Le C3, lui, garde son
// ecran integre. Pour reactiver un ecran sur le S3 : redefinir SENSOR_HAS_OLED
// et SENSOR_OLED_128X64 + OLED_I2C_ADDRESS ici.

// Plafond TX ESP-NOW AUSSI sur le S3 (2026-09-14). Preuve terrain (logs USB,
// firmware 0.13.0 unicast) : a pleine puissance, le S3 emet mais le hub ne
// renvoie PAS d'ACK (NACK a tous les essais), tandis que le C3 plafonne a
// 8,5 dBm est livre du 1er coup sur le meme canal, meme MAC hub. L'antenne PCB du
// S3 Super Mini est mal adaptee : trop de puissance = signal trop degrade pour
// etre acquitte. On plafonne donc, valeur reglee dans config.h
// (SENSOR_TX_POWER_LEVEL). A remonter progressivement (menu config.h) pour
// retrouver le maximum de portee qui reste stable.
#define SENSOR_NEEDS_TX_LIMIT 1

#endif

// Alias I2C (meme bus, memes GPIO) - commun aux deux cartes.
constexpr uint8_t PIN_I2C_SDA = PIN_SENSOR_SDA;
constexpr uint8_t PIN_I2C_SCL = PIN_SENSOR_SCL;
