#include "power_manager.h"
#include <esp_sleep.h>
#include <WiFi.h>

PowerManager::PowerManager() 
    : _pixel(NUM_PIXELS, PIN_RGB_LED, NEO_GRB + NEO_KHZ800) {}

void PowerManager::begin() {
    // LED RGB onboard (WS2812 sur GPIO 48)
    if (PIN_RGB_LED >= 0) {
        _pixel.begin();
        _pixel.setBrightness(30); // Luminosité douce pour ne pas éblouir ni trop consommer
        _pixel.clear();
        _pixel.show();
        _pixelReady = true;
    }

    // ADC batterie (GPIO 4, ADC1). Quelques lectures a vide : la 1re
    // analogReadMilliVolts apres boot sort souvent Vref=0.
    if (PIN_BATTERY_ADC >= 0) {
        pinMode(PIN_BATTERY_ADC, INPUT);
        analogReadResolution(12);
        analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
        for (uint8_t i = 0; i < 8; i++) {
            (void)analogReadMilliVolts(PIN_BATTERY_ADC);
            delay(2);
        }
    }
}

void PowerManager::setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!_pixelReady) return;
    _pixel.setPixelColor(0, _pixel.Color(r, g, b));
    _pixel.show();
}

void PowerManager::turnOffLed() {
    if (!_pixelReady) return;
    _pixel.clear();
    _pixel.show();
}

void PowerManager::blinkStatus(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs) {
    if (!_pixelReady) return;
    setLedColor(r, g, b);
    delay(durationMs);
    turnOffLed();
}

void PowerManager::readBattery(MeteoPacket& packet) {
    if (PIN_BATTERY_ADC < 0) {
        packet.battery_voltage = 0.0f;
        packet.battery_percent = 0;
        return;
    }

    // Moyennage sur 16 échantillons pour lisser le bruit ADC
    uint32_t adcSum = 0;
    constexpr uint8_t SAMPLES = 16;
    for (uint8_t i = 0; i < SAMPLES; i++) {
        adcSum += analogReadMilliVolts(PIN_BATTERY_ADC);
        delayMicroseconds(200);
    }
    
    float pinMilliVolts = (float)adcSum / (float)SAMPLES;
    
    // Tension réelle batterie (V) = tension broche (V) * ratio diviseur * calibration
    float rawVoltage = (pinMilliVolts / 1000.0f) * BATTERY_DIVIDER_RATIO * BATTERY_VREF_CALIBRATION;
    
    _lastVoltage = rawVoltage;

    // Calcul du pourcentage estimé
    if (_lastVoltage >= BATTERY_VOLTAGE_MAX) {
        _lastPercent = 100;
    } else if (_lastVoltage <= BATTERY_VOLTAGE_MIN) {
        _lastPercent = 0;
    } else {
        _lastPercent = (uint8_t)(((_lastVoltage - BATTERY_VOLTAGE_MIN) / (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN)) * 100.0f);
    }

    // Garde de plausibilite : rejette une lecture aberrante (pont absent, pin
    // flottant) SANS masquer une pile reellement faible. On borne donc autour de
    // la plage de la CARTE (BATTERY_VOLTAGE_MIN/MAX, chimie-dependante) avec une
    // marge : une pile presque vide (proche de MIN) doit encore etre signalee.
    constexpr float kGuardMargin = 0.5f;
    if (_lastVoltage < (BATTERY_VOLTAGE_MIN - kGuardMargin)
        || _lastVoltage > (BATTERY_VOLTAGE_MAX + kGuardMargin)) {
        packet.battery_voltage = 0.0f;
        packet.battery_percent = 0;
        Serial.printf("[POWER] Batterie ignoree (%.2f V) : hors plage carte [%.1f-%.1f V]\n",
                      _lastVoltage, BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);
        return;
    }

    packet.battery_voltage = _lastVoltage;
    packet.battery_percent = _lastPercent;
    packet.valid_fields |= FIELD_BATTERY;

    Serial.printf("[POWER] Batterie: %.2f V (%d %%)\n", _lastVoltage, _lastPercent);
}

void PowerManager::enterDeepSleep(uint32_t seconds) {
    Serial.printf("[POWER] Mise en veille profonde (Deep Sleep) pour %u secondes...\n", seconds);
    Serial.flush();

    // Extinction complète de la LED RGB avant sommeil
    turnOffLed();

    // Configuration du réveil par timer
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    
    // Extinction du Wi-Fi avant sommeil
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    esp_deep_sleep_start();
}
