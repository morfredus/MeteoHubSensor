#include "sensor_manager.h"
#include <Wire.h>

// Définition des variables statiques d'interruption
volatile uint32_t SensorManager::_anemometerPulses = 0;
volatile uint32_t SensorManager::_rainPulses = 0;

void IRAM_ATTR SensorManager::onAnemometerPulse() {
    _anemometerPulses++;
}

void IRAM_ATTR SensorManager::onRainPulse() {
    _rainPulses++;
}

SensorManager::SensorManager() {}

bool SensorManager::begin() {
    Serial.println("[SENSOR] Initialisation des capteurs...");

    // Si une broche d'alimentation dédiée aux capteurs existe, on l'active
    if (PIN_SENSOR_POWER >= 0) {
        pinMode(PIN_SENSOR_POWER, OUTPUT);
        digitalWrite(PIN_SENSOR_POWER, HIGH);
        delay(50); // Temps de stabilisation alimentation capteurs
    }

    // Initialisation du bus I2C
    if (!initI2C()) {
        Serial.println("[SENSOR] [WARN] Echec d'initialisation du bus I2C");
        return false;
    }

    // Détection des capteurs I2C
    _ahtFound = initAht();
    _bmpFound = initBmp();

    // Configuration des broches pour futures extensions (si configurées)
    if (PIN_ANEMOMETER_PULSE >= 0) {
        pinMode(PIN_ANEMOMETER_PULSE, INPUT_PULLUP);
        // attachInterrupt(digitalPinToInterrupt(PIN_ANEMOMETER_PULSE), onAnemometerPulse, FALLING);
    }

    if (PIN_RAIN_GAUGE_PULSE >= 0) {
        pinMode(PIN_RAIN_GAUGE_PULSE, INPUT_PULLUP);
        // attachInterrupt(digitalPinToInterrupt(PIN_RAIN_GAUGE_PULSE), onRainPulse, FALLING);
    }

    if (PIN_WIND_VANE_ADC >= 0) {
        pinMode(PIN_WIND_VANE_ADC, INPUT);
    }

    Serial.printf("[SENSOR] Bilan detection : AHT20=%s, BMP280=%s\n", 
                  _ahtFound ? "OK" : "ABSENT", 
                  _bmpFound ? "OK" : "ABSENT");

    return (_ahtFound || _bmpFound);
}

bool SensorManager::initI2C() {
    // Un seul Wire.begin : Adafruit rappelle begin() sans broches (defaut S3 = 8/9).
    Wire.begin(PIN_SENSOR_SDA, PIN_SENSOR_SCL);
    Wire.setClock(100000);
    delay(20);
    Serial.printf("[SENSOR] I2C  SDA=GP%u  SCL=GP%u\n", PIN_SENSOR_SDA, PIN_SENSOR_SCL);
    return true;
}

bool SensorManager::initAht() {
    if (_aht.begin(&Wire)) {
        Serial.println("[SENSOR] Capteur AHT20/AHT10 detecte");
        return true;
    }
    Serial.println("[SENSOR] [WARN] Capteur AHT20 non detecte");
    return false;
}

bool SensorManager::initBmp() {
    // 0x58 = BMP280, 0x60 = BME280 (meme lib, pression utilisable)
    const uint8_t addrs[] = {0x76, 0x77};
    const uint8_t ids[] = {0x58, 0x60};
    for (uint8_t addr : addrs) {
        for (uint8_t id : ids) {
            if (_bmp.begin(addr, id)) {
                _bmpAddr = addr;
                Serial.printf("[SENSOR] Capteur BMP/BME detecte 0x%02X id=0x%02X\n", addr, id);
                goto bmp_ok;
            }
        }
    }
    Serial.println("[SENSOR] [WARN] Capteur BMP/BME non detecte (0x76 / 0x77)");
    return false;

bmp_ok:

    // Reglage « weather monitoring » de la fiche technique Bosch : mode force,
    // suréchantillonnage x1, filtre IIR DESACTIVE. Avec une mesure toutes les
    // 5 min, un filtre IIR x16 lissait la pression sur plus d'une heure, ou
    // repartait de zero a un redemarrage (saut). La sonde doit rendre la reponse
    // REELLE du capteur : la qualification des mesures se fait plus haut (hub,
    // analyses), jamais ici. Le suréchantillonnage x1 suffit a la resolution
    // affichee (0,1 hPa) et raccourcit la conversion (moins d'eveil).
    _bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                     Adafruit_BMP280::SAMPLING_X1,    // Temperature
                     Adafruit_BMP280::SAMPLING_X1,    // Pression
                     Adafruit_BMP280::FILTER_OFF,     // Pas de filtre : reponse brute
                     Adafruit_BMP280::STANDBY_MS_500); // sans effet en mode force
    return true;
}

bool SensorManager::readTemperatureHumidity(float& temp, float& hum) {
    if (!_ahtFound) {
        // Tentative de re-detection a chaud
        _ahtFound = initAht();
        if (!_ahtFound) return false;
    }

    sensors_event_t humEvent, tempEvent;
    if (_aht.getEvent(&humEvent, &tempEvent)) {
        temp = tempEvent.temperature;
        hum = humEvent.relative_humidity;
        return true;
    }
    return false;
}

bool SensorManager::readPressure(float& pres) {
    if (!_bmpFound) {
        _bmpFound = initBmp();
        if (!_bmpFound) return false;
    }

    if (_bmp.takeForcedMeasurement()) {
        pres = _bmp.readPressure() / 100.0F; // Conversion Pa -> hPa
        return (pres > 300.0F && pres < 1100.0F);
    }
    return false;
}

void SensorManager::readWind(MeteoPacket& packet) {
    // Emplacement pour extension future Anémomètre / Girouette
    // Ex: calcul de la vitesse selon les impulsions comptées
    // packet.wind_speed = (float)_anemometerPulses * CALIBRATION_FACTOR;
    // packet.valid_fields |= FIELD_WIND_SPEED;
}

void SensorManager::readRain(MeteoPacket& packet) {
    // Emplacement pour extension future Pluviomètre
    // Ex: packet.rain_accumulated = (float)_rainPulses * BUCKET_MM_PER_PULSE;
    // packet.valid_fields |= FIELD_RAIN_TOTAL;
}

void SensorManager::readAll(MeteoPacket& packet) {
    // Remise à zéro des champs par défaut
    packet.temperature = 0.0f;
    packet.humidity = 0.0f;
    packet.pressure = 0.0f;
    packet.wind_speed = 0.0f;
    packet.wind_gust = 0.0f;
    packet.wind_direction_deg = 0;
    packet.rain_rate = 0.0f;
    packet.rain_accumulated = 0.0f;

    // 1. Température et Humidité (AHT20)
    float t = 0, h = 0;
    if (readTemperatureHumidity(t, h)) {
        packet.temperature = t;
        packet.humidity = h;
        packet.valid_fields |= (FIELD_TEMPERATURE | FIELD_HUMIDITY);
        Serial.printf("[SENSOR] Temp: %.2f *C, Hum: %.2f %%\n", t, h);
    } else {
        Serial.println("[SENSOR] [WARN] T/H absents (AHT20 muet) -> 0.0 dans le paquet");
    }

    // 2. Pression atmosphérique (BMP280)
    float p = 0;
    if (readPressure(p)) {
        packet.pressure = p;
        packet.valid_fields |= FIELD_PRESSURE;
        Serial.printf("[SENSOR] Pression: %.2f hPa\n", p);
    } else {
        Serial.println("[SENSOR] [WARN] Pression absente (BMP280 muet) -> 0.0 dans le paquet");
    }

    // 3. Extensions futures (Vent, Pluie, Solaire)
    readWind(packet);
    readRain(packet);
}
