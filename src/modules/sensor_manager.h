#pragma once
#include <Arduino.h>
#include "meteo_packet.h"
#include "board_config.h"
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

class SensorManager {
public:
    SensorManager();
    
    // Initialise le bus I2C et détecte les capteurs présents
    bool begin();
    
    // Effectue une lecture complète des capteurs et remplit le paquet
    void readAll(MeteoPacket& packet);
    
    // Accesseurs d'état
    bool hasAht() const { return _ahtFound; }
    bool hasBmp() const { return _bmpFound; }

    // Handlers d'interruptions pour futures extensions (anémomètre, pluie)
    static void onAnemometerPulse();
    static void onRainPulse();

private:
    Adafruit_AHTX0 _aht;
    Adafruit_BMP280 _bmp;
    
    bool _ahtFound = false;
    bool _bmpFound = false;
    uint8_t _bmpAddr = 0x76;

    // Compteurs d'impulsions (en mémoire RTC ou volatile)
    static volatile uint32_t _anemometerPulses;
    static volatile uint32_t _rainPulses;
    
    // Détection / Récupération I2C
    bool initI2C();
    bool initAht();
    bool initBmp();
    
    // Lectures individuelles
    bool readTemperatureHumidity(float& temp, float& hum);
    bool readPressure(float& pres);
    
    // Extensions futures
    void readWind(MeteoPacket& packet);
    void readRain(MeteoPacket& packet);
};
