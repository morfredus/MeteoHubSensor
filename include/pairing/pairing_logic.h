#pragma once
#include <cstdint>
#include <cstring>
#include "meteo_packet.h"

// ============================================================================
// Logique PURE de l'appairage sonde -> hub (sans radio, sans NVS)
// ============================================================================
// Testee sur l'hote (pio test -e native). Le module radio (PairingManager) ne
// fait qu'emettre les demandes, livrer ici les reponses recues, puis appliquer
// la decision.
//
// Regles de la specification, garanties ici :
//   - l'appairage est VOLONTAIRE (appui long sur BOOT), jamais une consequence
//     d'une perte de liaison : rien ici n'est appele hors de cette procedure ;
//   - un echec ne touche JAMAIS l'association existante : la decision ne produit
//     un nouveau record qu'avec EXACTEMENT un hub valide ;
//   - plusieurs hubs a portee : on balaie tous les canaux et, si deux hubs
//     DIFFERENTS repondent, on refuse (ambigu) plutot que de choisir au hasard.
//     La procedure dit de ne laisser allume que le hub voulu : le refus rend
//     visible une consigne non respectee au lieu d'appairer le mauvais hub.
// ============================================================================

namespace mhpair {

// Association memorisee en NVS : UN seul blob, donc une seule ecriture NVS
// (atomique). Une coupure pendant l'enregistrement laisse l'ancien record entier
// ou le nouveau entier, jamais un melange des deux.
struct __attribute__((packed)) PairRecord {
    uint8_t  version;       // kRecordVersion
    uint8_t  sta_mac[6];    // MAC STA du hub : destination unicast des mesures
    uint8_t  ap_mac[6];     // BSSID du SoftAP du hub : retrouve SON canal au scan
    uint8_t  channel;       // dernier canal connu du hub
    char     name[16];      // nom annonce par le hub (affichage / logs)
    uint16_t crc16;
};
constexpr uint8_t kRecordVersion = 1;

inline bool isZeroMac(const uint8_t* m) {
    for (int i = 0; i < 6; i++) if (m[i]) return false;
    return true;
}

inline bool isUnicastMac(const uint8_t* m) {
    // Ni nulle, ni broadcast/multicast (bit de poids faible du 1er octet).
    return !isZeroMac(m) && (m[0] & 0x01) == 0;
}

inline void sealRecord(PairRecord& r) {
    r.version = kRecordVersion;
    r.name[sizeof(r.name) - 1] = '\0';
    r.crc16 = calculateCrc16(reinterpret_cast<const uint8_t*>(&r),
                             sizeof(PairRecord) - sizeof(uint16_t));
}

inline bool isValidRecord(const PairRecord& r) {
    if (r.version != kRecordVersion) return false;
    if (!isUnicastMac(r.sta_mac)) return false;
    return calculateCrc16(reinterpret_cast<const uint8_t*>(&r),
                          sizeof(PairRecord) - sizeof(uint16_t)) == r.crc16;
}

// Construit la demande diffusee par la sonde.
inline MeteoPairFrame makeRequest(uint32_t nonce, uint8_t nodeId, const uint8_t* sensorMac) {
    MeteoPairFrame f;
    memset(&f, 0, sizeof(f));
    f.type = PAIR_REQUEST;
    f.node_id = nodeId;
    f.nonce = nonce;
    memcpy(f.sta_mac, sensorMac, 6);
    sealPairFrame(f);
    return f;
}

// Construit la confirmation unicast envoyee au hub retenu.
inline MeteoPairFrame makeConfirm(uint32_t nonce, uint8_t nodeId, const uint8_t* sensorMac,
                                  uint32_t baseSeq) {
    MeteoPairFrame f;
    memset(&f, 0, sizeof(f));
    f.type = PAIR_CONFIRM;
    f.node_id = nodeId;
    f.nonce = nonce;
    f.base_seq = baseSeq;
    memcpy(f.sta_mac, sensorMac, 6);
    sealPairFrame(f);
    return f;
}

enum class Outcome : uint8_t {
    NONE,       // aucun hub n'a repondu (encore)
    UNIQUE,     // exactement un hub : on peut confirmer
    AMBIGUOUS,  // au moins deux hubs differents : refus
};

// Recueille les reponses d'une procedure d'appairage.
class ResponseCollector {
public:
    static constexpr int kMaxHubs = 4;

    explicit ResponseCollector(uint32_t nonce) : _nonce(nonce) {}

    // Offre une trame recue (deja passee par isValidPairFrame) avec la MAC
    // source vue par la radio. Renvoie true si elle a ete retenue. Rejette :
    // un autre type qu'une reponse, un nonce qui n'est pas celui de CETTE
    // procedure (reponse tardive a une ancienne demande), une MAC annoncee
    // differente de la MAC source (trame incoherente), une MAC non unicast,
    // un canal hors 1..14.
    bool offer(const MeteoPairFrame& f, const uint8_t* srcMac) {
        if (f.type != PAIR_RESPONSE) return false;
        if (f.nonce != _nonce) return false;
        if (memcmp(f.sta_mac, srcMac, 6) != 0) return false;
        if (!isUnicastMac(f.sta_mac)) return false;
        if (f.channel < 1 || f.channel > 14) return false;

        for (int i = 0; i < _count; i++) {
            if (memcmp(_hubs[i].sta_mac, f.sta_mac, 6) == 0) {
                // Meme hub entendu a nouveau (demande repetee, fuite sur un canal
                // voisin) : on garde son canal ANNONCE, qui fait foi.
                _hubs[i].channel = f.channel;
                sealRecord(_hubs[i]);
                return true;
            }
        }
        if (_count >= kMaxHubs) { _overflow = true; return true; }
        PairRecord& r = _hubs[_count++];
        memset(&r, 0, sizeof(r));
        memcpy(r.sta_mac, f.sta_mac, 6);
        memcpy(r.ap_mac, f.ap_mac, 6);
        r.channel = f.channel;
        memcpy(r.name, f.name, sizeof(r.name));
        sealRecord(r);
        return true;
    }

    Outcome outcome() const {
        if (_count == 0) return Outcome::NONE;
        if (_count == 1 && !_overflow) return Outcome::UNIQUE;
        return Outcome::AMBIGUOUS;
    }

    int hubCount() const { return _count; }

    // Le record a enregistrer. N'a de sens que si outcome() == UNIQUE.
    const PairRecord& unique() const { return _hubs[0]; }

private:
    uint32_t _nonce;
    PairRecord _hubs[kMaxHubs]{};
    int _count = 0;
    bool _overflow = false;
};

} // namespace mhpair
