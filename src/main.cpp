#include <Arduino.h>
#include <esp_system.h>   // esp_reset_reason() pour le diagnostic (paquet v2)
#include <esp_sleep.h>    // esp_light_sleep_start() (mode light sleep experimental)
#include "board_config.h"
#include "config.h"
#include "meteo_packet.h"
#include "modules/sensor_manager.h"
#include "modules/espnow_sender.h"
#include "modules/power_manager.h"
#include "modules/display_manager.h"
#include "modules/sync_manager.h"

// Instance globale des modules
static SensorManager sensorManager;
static EspNowSender espNowSender;
static PowerManager powerManager;
static DisplayManager displayManager; // ecran sur HW-675, no-op sur S3
static mhs::SyncManager syncManager;   // buffer local + identite + horloge (v3)

// Compteur de cycles d'exécution
static uint32_t lastMeasurementTime = 0;
static uint32_t lastChannelCheck = 0;

// Dernier état batterie connu (pour sauter la revérification de canal quand la
// pile est presque vide : ne pas gaspiller le peu qui reste en scans Wi-Fi).
static bool g_lastBatteryValid = false;
static uint8_t g_lastBatteryPct = 100;

// Diagnostic (paquet v2) : compteur de reveils qui SURVIT au deep sleep (RTC) et
// repart de 0 a un power-cycle, plus la raison du dernier reset captee au boot.
// Emportes dans chaque trame, ils disent au hub POURQUOI la sonde a decroche.
RTC_DATA_ATTR static uint32_t g_wakeCount = 0;
static uint8_t g_resetReason = 0;

// Construit une trame de RETRANSMISSION a partir d'une mesure bufferisee : elle
// rejoue le seq et le sensor_ts D'ORIGINE (le hub reconstruira la bonne heure de
// mesure), avec les diagnostics et l'oldest_seq COURANTS.
static MeteoPacket buildRetransmitPacket(const mhs::StoredRecord& rec) {
    MeteoPacket p;
    memset(&p, 0, sizeof(p));
    p.temperature = rec.t;
    p.humidity    = rec.h;
    p.pressure    = rec.p;
    p.battery_voltage = rec.battery_v;
    p.battery_percent = rec.battery_pct;
    p.valid_fields = rec.valid_fields;
    p.sequence   = rec.seq;
    p.sensor_ts  = rec.sensor_ts;
    p.oldest_seq = syncManager.oldestSeq();
    p.reset_reason = g_resetReason;
    p.wake_count = (uint16_t)g_wakeCount;
    p.uptime_sec = millis() / 1000;
    return p;
}

// Phase de synchronisation differee, ouverte APRES l'envoi live REUSSI (le hub a
// donc recu au moins la trame courante et va repondre). Bornee dans le temps ET
// en nombre de mesures : elle ne transforme jamais l'eveil en session illimitee.
static void runDeferredSync() {
    if (!syncManager.ok()) return;

    SyncControl ctrl;
    if (!espNowSender.receiveSyncControl(ctrl, SENSOR_SYNC_RX_WINDOW_MS)) {
        // Pas de reponse du hub dans la fenetre : rien de grave. Les mesures non
        // confirmees restent PENDING dans le buffer et seront rejouees au prochain
        // reveil. On rend la main -> deep sleep.
        Serial.println("[SYNC] Pas de SyncControl (hub muet) : rattrapage differe");
        return;
    }

    // Recale l'horloge sur l'heure reelle du hub (NTP), transportee dans la
    // reponse : la sonde horodate ensuite en absolu, sans le moindre cout radio.
    syncManager.applyHubEpoch(ctrl.hub_epoch);

    // Accuse cumulatif : tout seq <= ack_seq est confirme cote hub.
    if (syncManager.applyAck(ctrl.ack_seq)) {
        Serial.printf("[SYNC] ACK cumulatif seq<=%u (reste %u en attente)\n",
                      ctrl.ack_seq, syncManager.unsyncedCount());
    }

    // Le hub indique le plus bas trou qu'il veut combler (want_from_seq). A defaut,
    // on part de notre plus ancienne mesure encore PENDING.
    const uint32_t fromSeq = (ctrl.want_from_seq != 0) ? ctrl.want_from_seq
                                                        : syncManager.ackWatermark() + 1;

    // Budget de retransmission BORNE : le minimum entre ce que le hub demande, le
    // plafond par cycle et la taille du lot local. Jamais de boucle infinie.
    uint32_t budget = SENSOR_RETX_MAX_PER_CYCLE;
    if (ctrl.want_count != 0 && ctrl.want_count < budget) budget = ctrl.want_count;

    mhs::StoredRecord batch[SENSOR_RETX_MAX_PER_CYCLE];
    const uint32_t n = syncManager.selectRetransmit(fromSeq, budget, batch);
    if (n == 0) return;

    Serial.printf("[SYNC] Retransmission de %u mesure(s) a partir de seq=%u\n", n, fromSeq);
    for (uint32_t i = 0; i < n; i++) {
        MeteoPacket p = buildRetransmitPacket(batch[i]);
        const bool ok = espNowSender.send(p, FRAME_RETRANSMIT);
        Serial.printf("[SYNC] %s seq=%u\n", ok ? "Renvoye" : "Echec", batch[i].seq);
    }

    // Une derniere ecoute courte pour recolter l'accuse cumulatif mis a jour par
    // le hub apres ce lot, et marquer synced ce qui vient d'etre confirme. Bornee.
    if (espNowSender.receiveSyncControl(ctrl, SENSOR_SYNC_RX_WINDOW_MS)) {
        syncManager.applyHubEpoch(ctrl.hub_epoch);
        if (syncManager.applyAck(ctrl.ack_seq)) {
            Serial.printf("[SYNC] ACK cumulatif seq<=%u apres retransmission\n", ctrl.ack_seq);
        }
    }
}

void performCycle() {
    Serial.println("\n----------------------------------------");
    Serial.println("[MAIN] Debut du cycle de mesure");

    // Horloge relative : chaque reveil (hors tout premier boot) represente un
    // intervalle ecoule. On l'avance AVANT d'horodater la nouvelle mesure.
    if (syncManager.lastSeq() > 0) {
        syncManager.advanceClock(SENSOR_MEASUREMENT_INTERVAL_SECONDS);
    }

    MeteoPacket packet;
    memset(&packet, 0, sizeof(MeteoPacket));
    packet.uptime_sec = millis() / 1000;
    packet.reset_reason = g_resetReason;         // diagnostic v2 (voir setup)
    packet.wake_count = (uint16_t)g_wakeCount;

    // Temoin bleu pendant l'acquisition (plus d'OLED : la LED est le statut)
    powerManager.setLedColor(0, 40, 120);

    sensorManager.readAll(packet);
    powerManager.readBattery(packet);
    g_lastBatteryValid = (packet.valid_fields & FIELD_BATTERY) != 0;
    g_lastBatteryPct = packet.battery_percent;

    // STOCKAGE AVANT ENVOI : la mesure entre dans le buffer local (avec seq +
    // sensor_ts) avant toute tentative radio. Une mesure acquise n'est donc jamais
    // perdue parce que l'envoi echoue.
    const uint32_t seq = syncManager.recordAcquisition(packet);

    // Cas-limite journalise : buffer plein d'unacked, une PENDING a ete ecrasee.
    if (syncManager.lastDroppedSeq() != 0) {
        Serial.printf("[SYNC] [WARN] Buffer plein : plus vieille PENDING ecrasee seq=%u "
                      "(total dropped=%u)\n",
                      syncManager.lastDroppedSeq(), syncManager.droppedPending());
    }

    Serial.printf("[SYNC] New measurement seq=%u sensor_ts=%u fields=0x%04X "
                  "T=%.2f H=%.2f P=%.2f\n",
                  seq, packet.sensor_ts, packet.valid_fields,
                  packet.temperature, packet.humidity, packet.pressure);

    // Envoi LIVE de la mesure courante.
    const bool success = espNowSender.send(packet, FRAME_LIVE);

    Serial.printf(
        "[MAIN] Paquet %u o  seq=%u  fields=0x%04X  T=%.2f  H=%.2f  P=%.2f  bat=%.2fV  ch=%u  ok=%d\n",
        (unsigned)sizeof(packet), packet.sequence, packet.valid_fields,
        packet.temperature, packet.humidity, packet.pressure,
        packet.battery_voltage, (unsigned)espNowSender.channel(), success ? 1 : 0);

    displayManager.showReading(packet, success, espNowSender.channel());

    if (success) {
        Serial.printf("[SYNC] Sent seq=%u (ACK MAC ch=%u)\n", seq, (unsigned)espNowSender.channel());
        powerManager.blinkStatus(0, 150, 0, 60);
        // Le hub a recu la trame : il va repondre. On ouvre la phase de synchro
        // differee (accuse cumulatif + retransmission bornee des trous).
        runDeferredSync();
    } else {
        Serial.printf("[MAIN] [WARN] Echec d'envoi (seq=%u ch=%u) : mesure conservee (PENDING)\n",
                      seq, (unsigned)espNowSender.channel());
        powerManager.blinkStatus(150, 0, 0, 60);
        // Hub injoignable : rien a synchroniser maintenant, tout reste PENDING.
    }

    Serial.println("----------------------------------------\n");
}

void setup() {
    // Diagnostic v2, capte AU PLUS TOT : raison du reset de ce boot + incrementation
    // du compteur de reveils (RTC). Un power-cycle a remis g_wakeCount a 0.
    g_resetReason = (uint8_t)esp_reset_reason();
    g_wakeCount++;

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

    // Buffer de securite local (LittleFS + NVS). Degrade mais non bloquant s'il
    // echoue : la sonde emettra quand meme en direct, simplement sans filet.
    if (!syncManager.begin()) {
        Serial.println("[SYNC] [WARN] Buffer local indisponible : emission sans filet");
    } else {
        Serial.printf("[SYNC] Buffer pret (seq=%u, en attente=%u, dropped=%u)\n",
                      syncManager.lastSeq(), syncManager.unsyncedCount(),
                      syncManager.droppedPending());
    }

    if (!espNowSender.begin()) {
        Serial.println("[MAIN] [FATAL] Impossible d'initialiser ESP-NOW");
        powerManager.setLedColor(150, 0, 0);
    } else {
        Serial.printf("[MAIN] ESP-NOW pret, canal %u\n", (unsigned)espNowSender.channel());
        powerManager.turnOffLed();
    }

    performCycle();
    lastMeasurementTime = millis();

    // Deep sleep = ne revient jamais (reboot au reveil). On FERME proprement le
    // buffer (flush LittleFS + NVS) avant de dormir : le retour au deep sleep est
    // garanti meme si la synchro a echoue.
    if (ENABLE_DEEP_SLEEP && !USE_LIGHT_SLEEP) {
        syncManager.end();
        delay(200);
        powerManager.enterDeepSleep(SENSOR_MEASUREMENT_INTERVAL_SECONDS);
    }
}

void loop() {
    if (USE_LIGHT_SLEEP) {
        // Light sleep : on eteint la LED, on arme le timer et on dort. La RAM et le
        // contexte sont conserves (pas de reboot) ; au reveil, l'execution reprend
        // ICI. On incremente wake_count et on refait un cycle complet (mesure +
        // envoi + synchro differee bornee).
        powerManager.turnOffLed();
        esp_sleep_enable_timer_wakeup(
            (uint64_t)SENSOR_MEASUREMENT_INTERVAL_SECONDS * 1000000ULL);
        esp_light_sleep_start();   // bloque jusqu'au timer
        g_wakeCount++;
        performCycle();
        return;
    }

    if (!ENABLE_DEEP_SLEEP) {
        uint32_t now = millis();
        if (now - lastMeasurementTime >= (SENSOR_MEASUREMENT_INTERVAL_SECONDS * 1000UL)) {
            lastMeasurementTime = now;
            performCycle();
        }

        // Revérification périodique du canal : le hub peut migrer de canal. On
        // rescanne « MH-NOW » et on bascule si besoin. On saute si la pile est
        // presque vide (un scan coûte, et un silence dû à la pile n'est pas un
        // problème de canal). En mode deep sleep, chaque réveil rescanne via
        // begin(), donc cette boucle ne concerne que le mode continu.
        if (now - lastChannelCheck >= (ESPNOW_CHANNEL_RECHECK_SEC * 1000UL)) {
            lastChannelCheck = now;
            const bool batteryCritical =
                g_lastBatteryValid && g_lastBatteryPct < ESPNOW_RESCAN_SKIP_BELOW_PCT;
            if (batteryCritical) {
                Serial.println("[MAIN] Revérif canal sautée (batterie critique)");
            } else {
                espNowSender.refreshChannel();
            }
        }
        delay(10);
    }
}
