#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "meteo_packet.h"
#include "board_config.h"
#include "config.h"

class PowerManager {
public:
    PowerManager();
    
    void begin();
    
    // Mesure la tension de batterie et renseigne le paquet
    void readBattery(MeteoPacket& packet);
    
    // Contrôle de la LED RGB onboard (WS2812 GPIO 48)
    void setLedColor(uint8_t r, uint8_t g, uint8_t b);
    void blinkStatus(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs = 40);
    void turnOffLed();
    
    // Passe l'ESP32-S3 en mode veille profonde pour une durée donnée. Le bouton
    // BOOT réveille aussi la sonde (appairage). L'heure du prochain réveil
    // programmé est retenue en RAM RTC : un réveil par le bouton peut ainsi se
    // rendormir pour le temps RESTANT, sans décaler la cadence des mesures.
    void enterDeepSleep(uint32_t seconds = SENSOR_MEASUREMENT_INTERVAL_SECONDS);

    // Secondes restantes avant le réveil programmé (après un réveil par le
    // bouton). 0 si inconnu ou déjà dépassé.
    uint32_t secondsUntilScheduledWake() const;

private:
    float _lastVoltage = 0.0f;
    uint8_t _lastPercent = 100;
    Adafruit_NeoPixel _pixel;
    bool _pixelReady = false;
};
