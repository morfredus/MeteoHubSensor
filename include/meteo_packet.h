#pragma once
// Arduino pour le firmware ; types standards seuls pour les tests natifs (hote),
// qui compilent ce protocole sans framework.
#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <cstddef>
#endif

// ============================================================================
// Protocole de transmission ESP-NOW - MeteoHub Packet
// Structure partagée entre l'émetteur (sonde ESP32-S3) et le récepteur (MeteoHub S3)
// ============================================================================

constexpr uint8_t METEO_PACKET_MAGIC_0 = 'M';
constexpr uint8_t METEO_PACKET_MAGIC_1 = 'H';
// v2 : ajout des champs de diagnostic reset_reason + wake_count. Le recepteur
// rejette une version differente, donc sonde ET hub doivent etre en v2 ensemble.
// v3 : synchronisation fiable tolerante aux pertes. Ajout de sensor_ts (horloge
// relative monotone du capteur, pour reconstruire l'heure de MESURE et non
// d'arrivee) et frame_type (live vs retransmission d'une mesure bufferisee). Le
// hub renvoie un SyncControl (voie inverse) pour l'accuse cumulatif + demande de
// trou. Sonde ET hub doivent etre reflashes ensemble (version rejetee sinon).
constexpr uint8_t METEO_PROTOCOL_VERSION = 3;

// Version des trames de DONNEES seules (MeteoPacket). v4 : ajout de fw_version
// (version du firmware de la sonde, pour l'afficher sur le hub). Les trames
// d'appairage et de controle restent en METEO_PROTOCOL_VERSION (3) : les faire
// evoluer ensemble aurait impose de reflasher sonde et hub au meme instant.
// Le hub accepte [METEO_DATA_VERSION_MIN, METEO_DATA_VERSION] : on flashe le hub
// d'abord, la sonde ensuite, sans rupture. Une trame v4 refusee par un vieux hub
// n'est pas accusee : la sonde la garde et la renvoie apres sa mise a jour.
constexpr uint8_t METEO_DATA_VERSION = 4;
constexpr uint8_t METEO_DATA_VERSION_MIN = 3;

// Type de trame de donnees (champ frame_type). Une retransmission porte la MEME
// sequence et le MEME sensor_ts que l'acquisition d'origine : seule frame_type
// change, ce qui permet au hub d'ancrer son horloge sur les trames LIVE
// uniquement (une retransmission peut etre vieille de plusieurs heures).
enum MeteoFrameType : uint8_t {
    FRAME_LIVE       = 0, // acquisition du cycle courant (heure ~= maintenant)
    FRAME_RETRANSMIT = 1, // mesure historique rejouee depuis le buffer local
};

// Masque binaire des métriques présentes / valides dans le paquet
enum MeteoFieldFlags : uint16_t {
    FIELD_NONE             = 0,
    FIELD_TEMPERATURE      = 1 << 0,  // Température valide (°C)
    FIELD_HUMIDITY         = 1 << 1,  // Humidité relative valide (%)
    FIELD_PRESSURE         = 1 << 2,  // Pression atmosphérique valide (hPa)
    FIELD_WIND_SPEED       = 1 << 3,  // Vitesse du vent valide (km/h)
    FIELD_WIND_GUST        = 1 << 4,  // Rafale de vent (km/h)
    FIELD_WIND_DIR         = 1 << 5,  // Direction du vent (degrés 0-359)
    FIELD_RAIN_RATE        = 1 << 6,  // Intensité de pluie (mm/h)
    FIELD_RAIN_TOTAL       = 1 << 7,  // Pluie cumulée (mm)
    FIELD_SOLAR_LUX        = 1 << 8,  // Éclairement (Lux)
    FIELD_UV_INDEX         = 1 << 9,  // Indice UV
    FIELD_BATTERY          = 1 << 10, // Mesure de batterie valide
};

// Structure du paquet émis par ESP-NOW (taille fixe compacte, alignée)
struct __attribute__((packed)) MeteoPacket {
    uint8_t magic[2];              // 'M', 'H' pour validation rapide
    uint8_t protocol_version;      // Version du format (1)
    uint8_t node_id;               // Identifiant de la sonde (ex: 1 = Outdoor)
    uint32_t sequence;             // Compteur de trame incrémental (détection perte)
    
    uint16_t valid_fields;         // Masque binaire MeteoFieldFlags
    
    // Métriques météorologiques brutes
    float temperature;             // °C
    float humidity;                // % HR
    float pressure;                // hPa
    
    // Métriques vent / pluie (extensions futures)
    float wind_speed;              // km/h (moyenne sur intervalle)
    float wind_gust;               // km/h (pic sur intervalle)
    uint16_t wind_direction_deg;   // 0-359° (0=Nord, 90=Est, 180=Sud, 270=Ouest)
    float rain_rate;               // mm/h
    float rain_accumulated;        // mm (depuis démarrage ou remise à zéro)
    
    // Métriques d'environnement & alimentation
    float battery_voltage;         // Volts (ex: 3.85V)
    uint8_t battery_percent;       // 0-100%
    uint32_t uptime_sec;           // Uptime en secondes ou boot count

    // Diagnostic d'alimentation / réveil (v2). Lus au RETOUR d'une trame après un
    // trou, ils disent POURQUOI la sonde a décroché, sans avoir à la brancher :
    //   reset_reason : esp_reset_reason() du dernier boot (POWERON, BROWNOUT,
    //                  DEEPSLEEP, PANIC, SW…). BROWNOUT/POWERON après un trou =
    //                  coupure d'alimentation ; DEEPSLEEP = réveil normal.
    //   wake_count   : compteur de réveils en RTC. Survit au deep sleep, repart de
    //                  0 à un power-cycle. Un saut > au nombre de trames reçues
    //                  révèle des réveils qui n'ont pas abouti à un envoi.
    uint8_t reset_reason;
    uint16_t wake_count;

    // --- Synchronisation fiable (v3) ---------------------------------------
    // sensor_ts : secondes ecoulees sur l'horloge RELATIVE du capteur au moment
    // de l'ACQUISITION (accumulee en NVS, survit deep sleep ET power-cycle). Le
    // capteur n'a ni RTC ni NTP : il ne connait pas l'heure murale. Le hub, lui,
    // ancre cette horloge sur l'heure reelle a chaque trame LIVE et reconstruit
    // l'heure de mesure d'une trame retransmise (heure d'arrivee != heure de
    // mesure). Porte par la trame d'origine ET par ses retransmissions.
    uint32_t sensor_ts;
    // frame_type : FRAME_LIVE ou FRAME_RETRANSMIT (voir MeteoFrameType).
    uint8_t frame_type;
    // oldest_seq : plus petit seq encore present dans le buffer local de la sonde
    // (0 si buffer vide). Il dit au hub ce que la sonde peut ENCORE fournir : si
    // oldest_seq depasse le prochain trou attendu par le hub, ce trou est
    // definitivement perdu (mesure sortie du buffer de 30 j) et le hub avance son
    // accuse cumulatif au-dela plutot que de reclamer sans fin une mesure
    // introuvable. C'est le garde-fou anti-blocage du cas-limite « buffer plein ».
    uint32_t oldest_seq;

    // Version du firmware de la sonde (v4) : (majeur << 16) | (mineur << 8) |
    // correctif, voir encodeFwVersion. 0 = inconnue (trame v3).
    uint32_t fw_version;

    uint16_t crc16;                // CRC16 de contrôle d'intégrité
};

static_assert(sizeof(MeteoPacket) == 67, "MeteoPacket must stay packed at 67 bytes");
// Taille d'une trame v3 (sans fw_version) : 63 octets, CRC en fin.
constexpr size_t METEO_PACKET_V3_SIZE = 63;

// ============================================================================
// Voie inverse hub -> sonde : accuse de reception cumulatif + demande de trou
// ============================================================================
// Emise par MeteoHub vers la sonde APRES reception d'une trame, pendant la
// courte fenetre d'eveil de la sonde. Magic 'M','C' (MeteoControl) pour la
// distinguer d'une trame de donnees 'M','H'. Modele « TCP-like » : ack_seq est
// le plus grand numero tel que le hub possede TOUT jusqu'a lui (accuse
// cumulatif, idempotent, robuste aux doublons et aux pertes). want_from/want_count
// est un indice borne du plus bas trou que le hub aimerait combler ensuite.
constexpr uint8_t METEO_CONTROL_MAGIC_0 = 'M';
constexpr uint8_t METEO_CONTROL_MAGIC_1 = 'C';

struct __attribute__((packed)) SyncControl {
    uint8_t magic[2];              // 'M', 'C'
    uint8_t protocol_version;      // METEO_PROTOCOL_VERSION
    uint8_t node_id;               // sonde visee par cet accuse
    uint32_t ack_seq;              // le hub possede TOUT seq <= ack_seq (0 = rien)
    uint32_t want_from_seq;        // plus bas seq manquant souhaite (0 = aucun)
    uint16_t want_count;           // nb de mesures souhaitees a partir de want_from_seq
    // hub_epoch : heure Unix reelle du hub (synchronisee NTP), 0 s'il n'est pas
    // encore synchronise. La sonde n'a ni RTC ni NTP : elle recale son horloge
    // systeme dessus (settimeofday) et l'ESP32 la maintient a travers le deep
    // sleep. Chaque mesure est alors horodatee en heure ABSOLUE des l'acquisition,
    // ce qui reste exact meme apres une coupure d'alimentation. Aucun cout radio :
    // ce champ voyage dans la reponse deja emise, la sonde ne fait qu'un
    // settimeofday (quelques µs), sans scan ni echange supplementaire.
    uint32_t hub_epoch;
    uint16_t crc16;                // CRC16 de controle d'integrite
};

static_assert(sizeof(SyncControl) == 20, "SyncControl must stay packed at 20 bytes");

// Bit de poids fort de want_count : « une OtaOffer suit ». want_count reste un petit
// nombre (une poignee de mesures), le bit 15 est donc libre ; une sonde sans OTA ignore
// want_* de toute facon. Evite de faire attendre CHAQUE reveil une offre qui ne vient pas.
constexpr uint16_t SYNC_FLAG_OTA_OFFER = 0x8000;

// ============================================================================
// Offre de mise a jour OTA hub -> sonde
// ============================================================================
// Emise par le hub (unicast, ~15 ms apres le SyncControl qui porte SYNC_FLAG_OTA_OFFER)
// quand il detient un firmware de sonde different de celui qu'elle declare (fw_version).
// ESP-NOW ne transporte PAS le binaire (250 o par trame) : l'offre dit seulement QUOI
// (version, taille, md5) ; la sonde se connecte alors au SoftAP du hub (« MH-NOW ») et
// telecharge /sensor-fw.bin en HTTP. Magic 'M','O' (MeteoOta).
constexpr uint8_t METEO_OTA_MAGIC_0 = 'M';
constexpr uint8_t METEO_OTA_MAGIC_1 = 'O';

struct __attribute__((packed)) OtaOffer {
    uint8_t magic[2];              // 'M', 'O'
    uint8_t protocol_version;      // METEO_PROTOCOL_VERSION
    uint8_t node_id;               // sonde visee
    uint32_t fw_version;           // version proposee, encodeFwVersion()
    uint32_t size;                 // taille du binaire (octets)
    char md5[33];                  // MD5 du binaire, hexadecimal minuscule, NUL final
    uint16_t crc16;                // CRC16 de controle d'integrite
};

static_assert(sizeof(OtaOffer) == 47, "OtaOffer must stay packed at 47 bytes");

// ============================================================================
// Appairage sonde <-> hub (volontaire, declenche par un appui long sur BOOT)
// ============================================================================
// La sonde ne parle qu'a UN hub, en unicast, dont la MAC est memorisee en NVS
// (plus besoin de la connaitre a la compilation). Pour changer de hub :
//   1. REQUEST  sonde -> broadcast, sur chaque canal 1..13 : « qui est hub ? »
//               (sta_mac = MAC de la sonde, nonce tire au hasard) ;
//   2. RESPONSE hub -> sonde, unicast : identite du hub (sta_mac = MAC STA du
//               hub, ap_mac = BSSID de son SoftAP « MH-NOW », channel = son
//               canal, name = son nom) ; le nonce de la demande est renvoye ;
//   3. CONFIRM  sonde -> nouveau hub, unicast AVEC ACK 802.11 : prouve la liaison
//               dans les deux sens avant que la sonde n'ecrive quoi que ce soit.
//               base_seq = dernier seq accuse par l'ANCIEN hub : le nouveau hub
//               reprend de la, et ne reclame que les mesures encore en attente
//               (pas 30 jours d'historique deja livres ailleurs).
// Le broadcast est limite a cette phase ; mesures et synchro restent en unicast.
// Magic 'M','P' (MeteoPair), distinct de 'M','H' (donnees) et 'M','C' (controle).
constexpr uint8_t METEO_PAIR_MAGIC_0 = 'M';
constexpr uint8_t METEO_PAIR_MAGIC_1 = 'P';

enum MeteoPairType : uint8_t {
    PAIR_REQUEST  = 1,
    PAIR_RESPONSE = 2,
    PAIR_CONFIRM  = 3,
};

struct __attribute__((packed)) MeteoPairFrame {
    uint8_t magic[2];              // 'M', 'P'
    uint8_t protocol_version;      // METEO_PROTOCOL_VERSION
    uint8_t type;                  // MeteoPairType
    uint8_t node_id;               // sonde concernee (REQUEST / CONFIRM)
    uint32_t nonce;                // tire par la sonde, renvoye tel quel par le hub
    uint32_t base_seq;             // CONFIRM : dernier seq accuse par l'ancien hub
    uint8_t sta_mac[6];            // MAC STA de l'EMETTEUR (sonde ou hub)
    uint8_t ap_mac[6];             // RESPONSE : BSSID du SoftAP du hub
    uint8_t channel;               // RESPONSE : canal radio du hub
    char name[16];                 // RESPONSE : nom du hub (termine par NUL)
    uint16_t crc16;                // CRC16 de controle d'integrite
};

static_assert(sizeof(MeteoPairFrame) == 44, "MeteoPairFrame must stay packed at 44 bytes");

// Calcul rapide de CRC16 CCITT
inline uint16_t calculateCrc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// Version de firmware "A.B.C" -> (A << 16) | (B << 8) | C, 0 si illisible.
// Chaque composante est bornee a 255 (0.26.0 -> 0x001A00).
inline uint32_t encodeFwVersion(const char* s) {
    if (s == nullptr) return 0;
    uint32_t part[3] = {0, 0, 0};
    int k = 0;
    bool digit = false;
    for (; *s && k < 3; ++s) {
        if (*s >= '0' && *s <= '9') {
            part[k] = part[k] * 10 + (uint32_t)(*s - '0');
            if (part[k] > 255) return 0;
            digit = true;
        } else if (*s == '.' && digit) {
            ++k; digit = false;
        } else {
            break;
        }
    }
    if (k < 2 || (k == 2 && !digit)) return 0;
    return (part[0] << 16) | (part[1] << 8) | part[2];
}

// Valide une trame de DONNEES recue (`len` = taille reelle) et la normalise :
// v4 telle quelle ; v3 (63 octets, sans fw_version) replacee dans la structure
// v4, fw_version = 0. Magic, version et CRC verifies. `p` contient les octets
// recus, copies depuis le debut de la structure.
inline bool normalizeMeteoPacket(MeteoPacket& p, size_t len) {
    if (p.magic[0] != METEO_PACKET_MAGIC_0 || p.magic[1] != METEO_PACKET_MAGIC_1) return false;
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&p);
    if (p.protocol_version == 3) {
        if (len != METEO_PACKET_V3_SIZE) return false;
        const size_t body = METEO_PACKET_V3_SIZE - sizeof(uint16_t);
        const uint16_t crc = (uint16_t)raw[body] | ((uint16_t)raw[body + 1] << 8);
        if (calculateCrc16(raw, body) != crc) return false;
        p.fw_version = 0;
        p.crc16 = crc;
        return true;
    }
    if (p.protocol_version >= METEO_DATA_VERSION_MIN && p.protocol_version <= METEO_DATA_VERSION) {
        if (len != sizeof(MeteoPacket)) return false;
        return calculateCrc16(raw, sizeof(MeteoPacket) - sizeof(uint16_t)) == p.crc16;
    }
    return false;
}

// Finalise une trame d'appairage (magic, version, CRC).
inline void sealPairFrame(MeteoPairFrame& f) {
    f.magic[0] = METEO_PAIR_MAGIC_0;
    f.magic[1] = METEO_PAIR_MAGIC_1;
    f.protocol_version = METEO_PROTOCOL_VERSION;
    f.crc16 = calculateCrc16(reinterpret_cast<const uint8_t*>(&f),
                             sizeof(MeteoPairFrame) - sizeof(uint16_t));
}

// Verifie la forme d'une trame d'appairage recue (taille, magic, version, CRC).
inline bool isValidPairFrame(const uint8_t* data, int len, MeteoPairFrame* out) {
    if (data == nullptr || len != (int)sizeof(MeteoPairFrame)) return false;
    MeteoPairFrame f;
    for (size_t i = 0; i < sizeof(f); i++) reinterpret_cast<uint8_t*>(&f)[i] = data[i];
    if (f.magic[0] != METEO_PAIR_MAGIC_0 || f.magic[1] != METEO_PAIR_MAGIC_1) return false;
    if (f.protocol_version != METEO_PROTOCOL_VERSION) return false;
    if (calculateCrc16(reinterpret_cast<const uint8_t*>(&f),
                       sizeof(MeteoPairFrame) - sizeof(uint16_t)) != f.crc16) return false;
    if (out) *out = f;
    return true;
}

// Finalise une offre OTA (magic, version, CRC).
inline void sealOtaOffer(OtaOffer& o) {
    o.magic[0] = METEO_OTA_MAGIC_0;
    o.magic[1] = METEO_OTA_MAGIC_1;
    o.protocol_version = METEO_PROTOCOL_VERSION;
    o.md5[sizeof(o.md5) - 1] = ' ';
    o.crc16 = calculateCrc16(reinterpret_cast<const uint8_t*>(&o), sizeof(OtaOffer) - sizeof(uint16_t));
}

// Verifie la forme d'une offre OTA recue (taille, magic, version, CRC).
inline bool isValidOtaOffer(const uint8_t* data, int len, OtaOffer* out) {
    if (data == nullptr || len != (int)sizeof(OtaOffer)) return false;
    OtaOffer o;
    for (size_t i = 0; i < sizeof(o); i++) reinterpret_cast<uint8_t*>(&o)[i] = data[i];
    if (o.magic[0] != METEO_OTA_MAGIC_0 || o.magic[1] != METEO_OTA_MAGIC_1) return false;
    if (o.protocol_version != METEO_PROTOCOL_VERSION) return false;
    if (calculateCrc16(reinterpret_cast<const uint8_t*>(&o), sizeof(OtaOffer) - sizeof(uint16_t)) != o.crc16) return false;
    if (out) *out = o;
    return true;
}
