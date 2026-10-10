#pragma once
// Decision pure (testable sur PC) : la sonde accepte-t-elle une offre OTA du hub ?
// Aucun acces radio ni flash ici, voir src/modules/ota_updater.cpp pour le transfert.
#include <cstdint>
#include "meteo_packet.h"

namespace mhota {

// Plafond de tentatives par version proposee. Une version qui ne s'installe pas (Wi-Fi
// du hub injoignable, binaire refuse) ne doit pas reveiller le Wi-Fi a chaque cycle
// jusqu'a vider la pile ; on s'arrete et on attend une autre version (ou un power-cycle).
constexpr uint8_t MAX_TRIES_PER_TARGET = 5;
// Garde-fou taille : la partition d'application de la sonde fait 1,25 Mo (default.csv).
constexpr uint32_t MAX_IMAGE_SIZE = 0x140000;
// Sous cette tension, un transfert Wi-Fi de plusieurs dizaines de secondes n'est pas
// raisonnable. Sans mesure de batterie valide (alimentation secteur / banc), on laisse faire.
constexpr float MIN_BATTERY_V = 3.5f;

enum class Verdict : uint8_t { ACCEPT, SAME_VERSION, BAD_OFFER, LOW_BATTERY, TOO_MANY_TRIES };

// `lastTarget` / `tries` : version deja tentee et nombre d'essais (conserves en RTC).
inline Verdict decide(const OtaOffer& o, uint32_t currentFw, bool batteryValid, float batteryV,
                      uint32_t lastTarget, uint8_t tries) {
    if (o.fw_version == 0 || o.size == 0 || o.size > MAX_IMAGE_SIZE || o.md5[0] == '\0')
        return Verdict::BAD_OFFER;
    if (o.fw_version == currentFw) return Verdict::SAME_VERSION;
    if (batteryValid && batteryV < MIN_BATTERY_V) return Verdict::LOW_BATTERY;
    if (o.fw_version == lastTarget && tries >= MAX_TRIES_PER_TARGET) return Verdict::TOO_MANY_TRIES;
    return Verdict::ACCEPT;
}

}  // namespace mhota
