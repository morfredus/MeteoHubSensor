#include <Arduino.h>
#include "board_config.h"
#include "config.h"
#include "meteo_packet.h"
#include "modules/sensor_manager.h"
#include "modules/espnow_sender.h"
#include "modules/power_manager.h"
#include "modules/display_manager.h"

// Instance globale des modules
static SensorManager sensorManager;
static EspNowSender espNowSender;
static PowerManager powerManager;
static DisplayManager displayManager; // ecran sur HW-675, no-op sur S3

// Compteur de cycles d'exécution
static uint32_t lastMeasurementTime = 0;

void performMeasurementAndSend() {
    Serial.println("\n----------------------------------------");
    Serial.println("[MAIN] Debut du cycle de mesure");

    MeteoPacket packet;
    memset(&packet, 0, sizeof(MeteoPacket));
    packet.uptime_sec = millis() / 1000;

    // Temoin bleu pendant l'acquisition (plus d'OLED : la LED est le statut)
    powerManager.setLedColor(0, 40, 120);

    sensorManager.readAll(packet);
    powerManager.readBattery(packet);

    bool success = espNowSender.send(packet);

    // Un seul flux Serial : ESP_LOGI se melee a [POWER] et donne l'illusion
    // d'une trame vide. Le paquet est rempli ici (fields 0x0007 = T+H+P).
    Serial.printf(
        "[MAIN] Paquet %u o  seq=%u  fields=0x%04X  T=%.2f  H=%.2f  P=%.2f  bat=%.2fV  ch=%u  ok=%d\n",
        (unsigned)sizeof(packet),
        packet.sequence,
        packet.valid_fields,
        packet.temperature,
        packet.humidity,
        packet.pressure,
        packet.battery_voltage,
        (unsigned)espNowSender.channel(),
        success ? 1 : 0);

    // Report du releve sur l'ecran integre (HW-675). Sans ecran, no-op.
    displayManager.showReading(packet, success, espNowSender.channel());

    if (success) {
        Serial.printf("[MAIN] Mesure transmise avec succes (Seq: %u ch=%u)\n",
                      packet.sequence, (unsigned)espNowSender.channel());
        powerManager.blinkStatus(0, 150, 0, 60);
    } else {
        Serial.printf("[MAIN] [WARN] Echec d'envoi (Seq: %u ch=%u)\n",
                      packet.sequence, (unsigned)espNowSender.channel());
        powerManager.blinkStatus(150, 0, 0, 60);
    }

    Serial.println("----------------------------------------\n");
}

void setup() {
    Serial.begin(115200);
    // USB CDC du Super Mini : laisser le host enumerer, sans bloquer sans cable.
    uint32_t serialWait = millis();
    while (!Serial && (millis() - serialWait) < 2000) {
        delay(10);
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("   MeteoHubSensor - Sonde Exterieure    ");
    Serial.printf("   %s\n", BOARD_NAME);
#ifdef PROJECT_VERSION
    Serial.printf("   version %s\n", PROJECT_VERSION);
#endif
    Serial.println("========================================");

    powerManager.begin();
    powerManager.setLedColor(0, 50, 150);

    sensorManager.begin();

    // Ecran integre (HW-675) : initialise APRES l'ouverture du bus I2C par
    // SensorManager, puisqu'il partage GP5/GP6 avec les capteurs. Sans ecran
    // (S3), begin() est un no-op.
    displayManager.begin();
#ifdef PROJECT_VERSION
    displayManager.showSplash(PROJECT_VERSION);
#else
    displayManager.showSplash("");
#endif

    if (!espNowSender.begin()) {
        Serial.println("[MAIN] [FATAL] Impossible d'initialiser ESP-NOW");
        powerManager.setLedColor(150, 0, 0);
    } else {
        Serial.printf("[MAIN] ESP-NOW pret, canal %u\n", (unsigned)espNowSender.channel());
        powerManager.turnOffLed();
    }

    performMeasurementAndSend();
    lastMeasurementTime = millis();

    if (ENABLE_DEEP_SLEEP) {
        delay(200);
        powerManager.enterDeepSleep(MEASUREMENT_INTERVAL_SEC);
    }
}

void loop() {
    if (!ENABLE_DEEP_SLEEP) {
        uint32_t now = millis();
        if (now - lastMeasurementTime >= (MEASUREMENT_INTERVAL_SEC * 1000UL)) {
            lastMeasurementTime = now;
            performMeasurementAndSend();
        }
        delay(10);
    }
}
