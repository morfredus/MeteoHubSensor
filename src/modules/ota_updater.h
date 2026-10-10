#pragma once
#include <Arduino.h>
#include "meteo_packet.h"

// Telecharge le firmware propose par le hub (OtaOffer) depuis son SoftAP « MH-NOW » et le
// flashe dans le slot OTA inactif. Renvoie true si l'image est ecrite ET son MD5 verifie :
// l'appelant doit alors redemarrer. false = echec, l'image courante reste intacte.
// `channel` = canal ESP-NOW courant (= canal du SoftAP), `apBssid` = BSSID du SoftAP ou nullptr.
bool otaFromHub(const OtaOffer& offer, uint8_t channel, const uint8_t* apBssid);
