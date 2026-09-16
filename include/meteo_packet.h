#pragma once
#include <Arduino.h>

// ============================================================================
// Protocole de transmission ESP-NOW - MeteoHub Packet
// Structure partagée entre l'émetteur (ESP32-C3) et le récepteur (MeteoHub S3)
// ============================================================================

constexpr uint8_t METEO_PACKET_MAGIC_0 = 'M';
constexpr uint8_t METEO_PACKET_MAGIC_1 = 'H';
// v2 : ajout des champs de diagnostic reset_reason + wake_count. Le recepteur
// rejette une version differente, donc sonde ET hub doivent etre en v2 ensemble.
constexpr uint8_t METEO_PROTOCOL_VERSION = 2;

// Masque binaire des métriques présentes / valides dans le paquet
enum MeteoFieldFlags : uint16_t {
    FIELD_NONE             = 0,
    FIELD_TEMPERATURE      = 1 << 0,  // Température valide (°C)
    FIELD_HUMIDITY         = 1 << 1,  // Humidité relative valide (%)
    FIELD_PRESSURE         = 1 << 2,  // Pression atmosphérique valide (hPa)
    FIELD_WIND_SPEED       = 1 << 3,  // Vitesse du vent valide (km/h)
    FIELD_WIND_GUST        = 1 << 4,  // Rafale de vent (km/h)
    FIELD_WIND_DIR         = 1 << 5,  // Direction du vent (degrés 0-359)
    FIELD_RAIN_RATE        = 1 << 6,  // Intensité de pluie (mm/h)
    FIELD_RAIN_TOTAL       = 1 << 7,  // Pluie cumulée (mm)
    FIELD_SOLAR_LUX        = 1 << 8,  // Éclairement (Lux)
    FIELD_UV_INDEX         = 1 << 9,  // Indice UV
    FIELD_BATTERY          = 1 << 10, // Mesure de batterie valide
};

// Structure du paquet émis par ESP-NOW (taille fixe compacte, alignée)
struct __attribute__((packed)) MeteoPacket {
    uint8_t magic[2];              // 'M', 'H' pour validation rapide
    uint8_t protocol_version;      // Version du format (1)
    uint8_t node_id;               // Identifiant de la sonde (ex: 1 = Outdoor)
    uint32_t sequence;             // Compteur de trame incrémental (détection perte)
    
    uint16_t valid_fields;         // Masque binaire MeteoFieldFlags
    
    // Métriques météorologiques brutes
    float temperature;             // °C
    float humidity;                // % HR
    float pressure;                // hPa
    
    // Métriques vent / pluie (extensions futures)
    float wind_speed;              // km/h (moyenne sur intervalle)
    float wind_gust;               // km/h (pic sur intervalle)
    uint16_t wind_direction_deg;   // 0-359° (0=Nord, 90=Est, 180=Sud, 270=Ouest)
    float rain_rate;               // mm/h
    float rain_accumulated;        // mm (depuis démarrage ou remise à zéro)
    
    // Métriques d'environnement & alimentation
    float battery_voltage;         // Volts (ex: 3.85V)
    uint8_t battery_percent;       // 0-100%
    uint32_t uptime_sec;           // Uptime en secondes ou boot count

    // Diagnostic d'alimentation / réveil (v2). Lus au RETOUR d'une trame après un
    // trou, ils disent POURQUOI la sonde a décroché, sans avoir à la brancher :
    //   reset_reason : esp_reset_reason() du dernier boot (POWERON, BROWNOUT,
    //                  DEEPSLEEP, PANIC, SW…). BROWNOUT/POWERON après un trou =
    //                  coupure d'alimentation ; DEEPSLEEP = réveil normal.
    //   wake_count   : compteur de réveils en RTC. Survit au deep sleep, repart de
    //                  0 à un power-cycle. Un saut > au nombre de trames reçues
    //                  révèle des réveils qui n'ont pas abouti à un envoi.
    uint8_t reset_reason;
    uint16_t wake_count;

    uint16_t crc16;                // CRC16 de contrôle d'intégrité
};

static_assert(sizeof(MeteoPacket) == 54, "MeteoPacket must stay packed at 54 bytes");

// Calcul rapide de CRC16 CCITT
inline uint16_t calculateCrc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}
