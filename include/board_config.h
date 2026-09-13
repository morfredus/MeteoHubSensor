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

// Batterie : GP4 (ADC1_CH4) libre. Ignoree par PowerManager si aucun pont n'est
// cable (lecture hors plage filtree).
constexpr uint8_t PIN_BATTERY_ADC = 4;

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

// Le C3 Super Mini / HW-675 exige une puissance TX ESP-NOW reduite pour
// communiquer de facon stable avec le MeteoHub S3 : a pleine puissance, la
// liaison echoue (constat materiel valide par test, cf. CHANGELOG). Voir
// EspNowSender::begin().
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

// Batterie : pont 100k/100k sur GP4 (ADC1).
constexpr uint8_t PIN_BATTERY_ADC = 4;

// --- Reserves meteo ---
constexpr uint8_t PIN_ANEMOMETER_PULSE = 1;
constexpr uint8_t PIN_WIND_VANE_ADC = 2;
constexpr uint8_t PIN_RAIN_GAUGE_PULSE = 7;
constexpr uint8_t PIN_AUX_ADC = 10;
constexpr uint8_t PIN_FREE_GP5 = 5;
constexpr uint8_t PIN_FREE_GP6 = 6;

// Ecran externe 0.96" (SSD1306 128x64) cable sur le meme bus I2C que les
// capteurs (GP8 SDA / GP9 SCL). Adresse 0x3C, ne gene pas AHT20 (0x38) /
// BMP280 (0x76/0x77).
#define SENSOR_HAS_OLED 1
#define SENSOR_OLED_128X64 1
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;

// Pas de plafond TX sur le S3 (liaison ESP-NOW stable a pleine puissance).

#endif

// Alias I2C (meme bus, memes GPIO) - commun aux deux cartes.
constexpr uint8_t PIN_I2C_SDA = PIN_SENSOR_SDA;
constexpr uint8_t PIN_I2C_SCL = PIN_SENSOR_SCL;
