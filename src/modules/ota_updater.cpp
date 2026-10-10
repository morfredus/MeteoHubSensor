#include "ota_updater.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

bool otaFromHub(const OtaOffer& offer, uint8_t channel, const uint8_t* apBssid) {
    // ESP-NOW reste actif : il cohabite avec la station Wi-Fi, et on se rendort juste apres.
    // On passe par le canal et le BSSID connus pour s'associer sans scan (quelques centaines de ms).
    WiFi.begin(ESPNOW_HUB_AP_SSID, ESPNOW_HUB_AP_PASS, channel, apBssid);
    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < OTA_WIFI_TIMEOUT_MS) delay(50);
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] SoftAP du hub injoignable");
        return false;
    }

    // Le hub est la passerelle de son propre SoftAP (192.168.4.1 par defaut).
    String url = "http://" + WiFi.gatewayIP().toString() + "/sensor-fw.bin";
    HTTPClient http;
    http.setTimeout(15000);
    http.begin(url);
    bool ok = false;
    const int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] HTTP %d\n", code);
    } else if (http.getSize() != (int)offer.size) {
        // Le binaire servi n'est plus celui de l'offre (re-televerse entre-temps).
        Serial.printf("[OTA] taille %d != offre %u\n", http.getSize(), (unsigned)offer.size);
    } else {
        if (Update.isRunning()) Update.abort();   // session precedente restee ouverte
        if (Update.begin(offer.size)) {
            Update.setMD5(offer.md5);             // Update.end() refuse l'image si le MD5 differe
            const size_t written = Update.writeStream(*http.getStreamPtr());
            ok = (written == offer.size) && Update.end() && Update.isFinished();
            if (!ok) {
                Serial.printf("[OTA] ecriture %u/%u, erreur Update=%u\n",
                              (unsigned)written, (unsigned)offer.size, (unsigned)Update.getError());
                Update.abort();
            }
        } else {
            Serial.printf("[OTA] Update.begin a echoue, erreur=%u\n", (unsigned)Update.getError());
        }
    }
    http.end();
    WiFi.disconnect();
    return ok;
}
