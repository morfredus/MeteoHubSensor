#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include "meteo_packet.h"
#include "sync/measurement_store.h"   // ancien format : reprise unique a la migration
#include "sync/segment_store.h"
#include "littlefs_blob_store.h"
#include "littlefs_segment_fs.h"

// ============================================================================
// SyncManager (sonde) - buffer local + identite + horloge relative
// ============================================================================
// Reunit ce qui rend la sonde tolerante aux pertes :
//   - le buffer persistant en Flash : journal SEGMENTE en ajout seul (SegmentStore
//     + LittleFsSegmentFs, 0.24.0), dont le filigrane d'accuse vit en NVS ;
//   - le numero de sequence MONOTONE, persiste en NVS pour survivre au brownout
//     (contrairement a l'ancien compteur RTC remis a 0 a chaque power-cycle) ;
//   - l'horloge RELATIVE du capteur (secondes cumulees), aussi en NVS, qui
//     permet au hub de reconstruire l'heure de MESURE d'une retransmission.
//
// Une ecriture NVS par cycle (seq + horloge), soit ~288/jour : usure negligeable
// (NVS gere son propre wear leveling). Les MESURES, elles, ne vont PAS en NVS :
// elles vont dans le ring LittleFS (cf. cahier des charges : NVS n'est pas un
// journal de mesures).
namespace mhs {

class SyncManager {
public:
    // Monte le buffer et charge seq/horloge depuis NVS. Renvoie false si le
    // stockage local est indisponible (la sonde continuera d'emettre en direct,
    // mais sans filet : c'est degrade, pas bloquant).
    bool begin() {
        // NVS : source d'autorite du seq, de l'horloge, du filigrane d'accuse et
        // du compteur de pertes (petits entiers, frequents : le role de la NVS).
        _prefs.begin("mhs", /*readOnly=*/false);
        _seq = _prefs.getUInt("seq", 0);
        _clock = _prefs.getUInt("clock", 0);
        const bool haveWm = _prefs.isKey("ack");
        uint32_t wm = _prefs.getUInt("ack", 0);
        uint32_t dropped = _prefs.getUInt("drop", 0);

        if (!_fs.begin(SENSOR_SEGMENT_DIR)) {
            _ok = false;
            return false;
        }

        // Ancien buffer (fichier unique de 276 Ko, avant 0.24.0) : lu UNE fois.
        // On en reprend le filigrane (si la NVS ne l'a pas encore) et les pertes,
        // puis les mesures EN ATTENTE, recopiees une a une dans le journal (jamais
        // tout le fichier en RAM : la sonde n'a pas de PSRAM active). Le fichier
        // est ensuite efface.
        const bool legacy = LittleFS.exists(SENSOR_BUFFER_PATH);
        LittleFsBlobStore legacyBlob;
        MeasurementStore old;
        bool legacyOk = false;
        if (legacy) {
            const uint32_t bytes = sizeof(StoreHeader)
                                 + SENSOR_BUFFER_CAPACITY * sizeof(StoredRecord);
            legacyOk = legacyBlob.begin(SENSOR_BUFFER_PATH, bytes)
                    && old.begin(&legacyBlob, SENSOR_BUFFER_CAPACITY);
            if (legacyOk) {
                if (!haveWm) wm = old.ackWatermark();
                if (old.droppedPending() > dropped) dropped = old.droppedPending();
            }
        }

        if (!_store.begin(&_fs, SENSOR_SEGMENT_RECORDS, SENSOR_MAX_SEGMENTS, wm, dropped)) {
            legacyBlob.end();
            _ok = false;
            return false;
        }
        _ok = true;

        if (legacy) {
            uint32_t imported = 0;
            if (legacyOk) {
                // Ordre logique du ring = ordre d'ecriture = seq croissants.
                for (uint32_t i = 0; i < old.count(); i++) {
                    StoredRecord r;
                    if (!old.recordAt(i, r) || r.seq <= wm) continue;
                    if (r.seq > _store.newestSeq() && _store.append(r)) imported++;
                }
            }
            legacyBlob.end();
            LittleFS.remove(SENSOR_BUFFER_PATH);
            persistStoreState();
            Serial.printf("[SYNC] Ancien buffer converti en journal segmente "
                          "(%u mesure(s) en attente reprise(s), filigrane=%u)\n",
                          imported, _store.ackWatermark());
        }

        // Filigrane au-dela de toute mesure du buffer : accuse d'un hub qui
        // suivait une AUTRE serie. Les mesures redeviennent en attente.
        if (_store.repairWatermark()) {
            persistStoreState();
            Serial.println("[SYNC] [WARN] Accuse incoherent (serie precedente) : "
                           "mesures du buffer remises en attente");
        }
        return true;
    }

    bool ok() const { return _ok; }

    void end() {
        _prefs.end();
    }

    // Avance l'horloge relative de `elapsedSec` (duree ecoulee depuis la mesure
    // precedente : intervalle de deep sleep). A appeler une fois par reveil AVANT
    // recordAcquisition, sauf au tout premier boot (elapsed=0).
    void advanceClock(uint32_t elapsedSec) { _clock += elapsedSec; }

    uint32_t sensorNow() const { return _clock; }
    uint32_t lastSeq() const { return _seq; }

    // Seuil de plausibilite d'une heure Unix reelle (~2020-09). En dessous,
    // l'horloge systeme n'a pas encore ete recalee (ou a ete perdue a une coupure
    // d'alimentation) : on retombe sur l'horloge relative.
    static constexpr uint32_t kEpochPlausible = 1600000000u;

    // Vrai si la sonde connait l'heure murale reelle. L'ESP32 maintient l'horloge
    // systeme a travers le deep sleep ; une coupure d'alimentation la remet a zero
    // (on repasse alors en relatif jusqu'au prochain recalage par le hub).
    bool hasRealTime() const { return (uint32_t)time(nullptr) > kEpochPlausible; }

    // Traduit un horodatage stocke en horodatage ABSOLU pour la transmission.
    // Un enregistrement deja absolu (> seuil) part tel quel. Un enregistrement
    // RELATIF (stocke avant que la sonde connaisse l'heure) est converti a la
    // volee, SI la sonde est desormais a l'heure : absolu = relatif + (heure_reelle
    // - horloge_relative_courante). Cela evite que le hub melange des trames
    // relatives (vieux buffer) et absolues (live) avec une seule ancre -> plus de
    // reconstruction aberrante ni de rejet « implausible ts ». Sans heure connue,
    // on laisse le relatif (le hub reconstruira via son ancre relative).
    uint32_t absoluteTs(uint32_t storedTs) const {
        if (storedTs > kEpochPlausible) return storedTs; // deja absolu
        if (!hasRealTime()) return storedTs;             // pas d'heure : reste relatif
        const int64_t offset = (int64_t)time(nullptr) - (int64_t)_clock;
        const int64_t abs = (int64_t)storedTs + offset;
        return abs > (int64_t)kEpochPlausible ? (uint32_t)abs : storedTs;
    }

    // Recale l'horloge systeme sur l'epoch fourni par le hub (recu dans le
    // SyncControl). Aucun cout radio : le champ voyage dans la reponse deja emise.
    // On n'ecrit l'horloge que si l'ecart est significatif, pour ne pas la toucher
    // a chaque cycle sans raison.
    void applyHubEpoch(uint32_t epoch) {
        if (epoch <= kEpochPlausible) return; // hub pas encore synchronise NTP
        const uint32_t now = (uint32_t)time(nullptr);
        const uint32_t diff = (now > epoch) ? (now - epoch) : (epoch - now);
        if (hasRealTime() && diff < 2) return; // deja a l'heure, rien a faire
        struct timeval tv;
        tv.tv_sec = (time_t)epoch;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
    }

    // Enregistre une nouvelle acquisition dans le buffer : attribue le prochain
    // seq, l'horodate (horloge relative), persiste seq+horloge en NVS. Remplit
    // aussi packet.sequence / packet.sensor_ts pour l'envoi live. Renvoie le seq.
    uint32_t recordAcquisition(MeteoPacket& packet) {
        _seq++;
        // Horodatage : heure ABSOLUE si la sonde est a l'heure (recalee par le hub,
        // maintenue par le RTC a travers le deep sleep), sinon horloge relative en
        // repli tant que le premier recalage n'a pas eu lieu. Un timestamp absolu
        // reste exact meme apres une coupure d'alimentation.
        const uint32_t ts = hasRealTime() ? (uint32_t)time(nullptr) : _clock;
        StoredRecord r{};
        r.seq = _seq;
        r.sensor_ts = ts;
        r.t = packet.temperature;
        r.h = packet.humidity;
        r.p = packet.pressure;
        r.battery_v = packet.battery_voltage;
        r.battery_pct = packet.battery_percent;
        r.valid_fields = packet.valid_fields;
        if (_ok) {
            _store.append(r);
            if (_store.takeRestarted()) {
                Serial.println("[SYNC] [WARN] Numerotation repartie en arriere : "
                               "ancien buffer ecarte");
            }
            persistStoreState(); // pertes eventuelles (retention) + filigrane
        }

        // Persiste l'identite/horloge AVANT tout envoi : meme si le cycle est
        // coupe ensuite, on ne reattribuera jamais ce seq (pas de doublon d'id).
        _prefs.putUInt("seq", _seq);
        _prefs.putUInt("clock", _clock);

        packet.sequence = _seq;
        packet.sensor_ts = ts;
        packet.oldest_seq = oldestSeq();
        return _seq;
    }

    // Plus ancienne mesure que la sonde peut encore FOURNIR (oldest_seq des
    // trames) : la plus ancienne en attente, jamais une mesure deja accusee.
    uint32_t oldestSeq() const { return _ok ? _store.oldestProvidableSeq() : 0; }
    uint32_t ackWatermark() const { return _ok ? _store.ackWatermark() : 0; }
    uint32_t unsyncedCount() const { return _ok ? _store.unsyncedCount() : 0; }
    uint32_t droppedPending() const { return _ok ? _store.droppedPending() : 0; }
    uint32_t lastDroppedSeq() const { return _ok ? _store.lastDroppedSeq() : 0; }

    // Applique l'accuse cumulatif du hub (marque synced <= ackSeq). Renvoie true
    // si le filigrane a avance.
    bool applyAck(uint32_t ackSeq) {
        if (!_ok || !_store.markSyncedUpTo(ackSeq)) return false;
        persistStoreState();
        return true;
    }

    // Selectionne jusqu'a maxN mesures a retransmettre (>= fromSeq, PENDING),
    // ordre chronologique. Renvoie le nombre ecrit dans out.
    uint32_t selectRetransmit(uint32_t fromSeq, uint32_t maxN, StoredRecord* out) {
        return _ok ? _store.selectRetransmit(fromSeq, maxN, out) : 0;
    }

private:
    // Filigrane et pertes en NVS, ecrits seulement s'ils ont change.
    void persistStoreState() {
        if (_prefs.getUInt("ack", 0) != _store.ackWatermark() || !_prefs.isKey("ack"))
            _prefs.putUInt("ack", _store.ackWatermark());
        if (_prefs.getUInt("drop", 0) != _store.droppedPending())
            _prefs.putUInt("drop", _store.droppedPending());
    }

    LittleFsSegmentFs _fs;
    SegmentStore _store;
    Preferences _prefs;
    uint32_t _seq = 0;
    uint32_t _clock = 0;
    bool _ok = false;
};

} // namespace mhs
