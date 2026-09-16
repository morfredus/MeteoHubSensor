#pragma once
#include <Arduino.h>

// ============================================================================
// Brochage MeteoHubSensor - ESP32-S3 Super Mini
// ============================================================================
// Cible unique (env supermini, define SENSOR_BOARD_S3). Le reste du firmware ne
// connait que les alias PIN_* et les drapeaux SENSOR_HAS_OLED /
// SENSOR_NEEDS_TX_LIMIT : ajouter une autre carte se ferait ici, sans toucher
// aux modules. (La cible C3 HW-675 a ete retiree : on se concentre sur l'ESP32-S3.)
// ============================================================================

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
// (BATTERY_DIVIDER_RATIO = 2,0 dans config.h). Le pont draine en continu
// ~4,0 V / 200 k = 20 uA, negligeable devant les reveils TX. La plage Li-ion
// 2,6-4,2 ci-dessous convertit en pourcentage.
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
// S3, lire via Serial et poser VREF = Vmultimetre / Vlu.
constexpr float BATTERY_VREF_CALIBRATION = 1.0f;

// --- Reserves meteo ---
constexpr uint8_t PIN_ANEMOMETER_PULSE = 1;
constexpr uint8_t PIN_WIND_VANE_ADC = 2;
constexpr uint8_t PIN_RAIN_GAUGE_PULSE = 7;
constexpr uint8_t PIN_AUX_ADC = 10;
constexpr uint8_t PIN_FREE_GP5 = 5;
constexpr uint8_t PIN_FREE_GP6 = 6;

// Pas d'ecran gere sur le S3 : le statut passe par la LED RGB seule. Un ecran
// jamais allume reste dans son etat reset basse conso. Pour piloter un ecran sur
// le S3 : definir SENSOR_HAS_OLED + SENSOR_OLED_* et OLED_I2C_ADDRESS ici.

// Plafond TX ESP-NOW sur le S3 (2026-09-14). Preuve terrain (logs USB, firmware
// 0.13.0 unicast) : a pleine puissance, le S3 emet mais le hub ne renvoie PAS
// d'ACK (NACK a tous les essais). L'antenne PCB du S3 Super Mini est mal adaptee :
// trop de puissance = signal trop degrade pour etre acquitte. On plafonne donc,
// valeur reglee dans config.h (SENSOR_TX_POWER_LEVEL), a remonter progressivement
// (menu config.h) pour retrouver le maximum de portee qui reste stable.
#define SENSOR_NEEDS_TX_LIMIT 1

// Alias I2C (meme bus, memes GPIO).
constexpr uint8_t PIN_I2C_SDA = PIN_SENSOR_SDA;
constexpr uint8_t PIN_I2C_SCL = PIN_SENSOR_SCL;
