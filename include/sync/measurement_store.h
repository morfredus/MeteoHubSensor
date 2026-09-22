#pragma once
#include <cstdint>
#include <cstring>
#include "blob_store.h"

// ============================================================================
// MeasurementStore - buffer/historique de securite de la sonde (30 j vises)
// ============================================================================
// Ring buffer persistant en Flash : chaque mesure acquise y est ecrite AVANT
// toute tentative d'envoi. Une mesure n'est consideree SYNCED que lorsque le hub
// confirme l'avoir recue (accuse cumulatif de la voie inverse), jamais parce
// qu'elle a « juste ete envoyee ».
//
// Etat de synchronisation par FILIGRANE (watermark), pas par octet-par-mesure :
// l'accuse du hub etant CUMULATIF (« j'ai tout jusqu'a seq K »), on ne persiste
// qu'un seul entier ack_watermark. Une mesure est :
//     SYNCED  si seq <= ack_watermark
//     PENDING si seq >  ack_watermark
// Marquer 200 mesures confirmees = UNE ecriture d'en-tete, pas 200 reecritures
// de slots : c'est le comportement le plus doux pour l'usure Flash. (Le wear
// leveling physique, lui, est assure par LittleFS cote firmware ; ce ring fournit
// la RETENTION bornee, LittleFS repartit les ecritures.)
//
// Invariants (cahier des charges) :
//   - survit au deep sleep, au reboot et a la coupure (backend persistant) ;
//   - une PENDING n'est jamais supprimee « juste parce qu'elle est ancienne » :
//     on n'ecrase que quand le buffer est PLEIN, et ce cas-limite est explicite
//     et compte (dropped_pending), jamais silencieux ;
//   - selection de retransmission BORNEE (jamais de boucle infinie) ;
//   - pas de corruption : ecritures a offset fixe, en-tete revalidee au montage.
//
// Toute la logique vit ici, sur une BlobStore abstraite -> testable sans LittleFS.
namespace mhs {

// Enregistrement stocke (taille fixe 32 o, aligne). Le pas fixe fige la geometrie
// du ring et laisse de la marge pour de futurs champs sans re-migrer.
struct __attribute__((packed)) StoredRecord {
    uint32_t seq;            // numero de sequence monotone (identite de la mesure)
    uint32_t sensor_ts;      // horloge relative du capteur a l'acquisition (s)
    float    t;              // temperature (°C)
    float    h;              // humidite (%)
    float    p;              // pression (hPa)
    float    battery_v;      // tension batterie (V)
    uint8_t  battery_pct;    // batterie (%)
    uint16_t valid_fields;   // masque MeteoFieldFlags au moment de l'acquisition
    uint8_t  _pad[5];        // alignement -> 32 octets
};
static_assert(sizeof(StoredRecord) == 32, "StoredRecord doit rester a 32 octets");

// En-tete du fichier ring. Relu au montage : si magic/version/geometrie ne
// correspondent pas (premier boot, changement de capacite, corruption), on
// (re)formate proprement plutot que d'interpreter des octets douteux.
struct __attribute__((packed)) StoreHeader {
    uint32_t magic;          // 'M','H','S','B'
    uint16_t version;        // format de l'en-tete
    uint16_t record_size;    // = sizeof(StoredRecord)
    uint32_t capacity;       // nombre de creneaux
    uint32_t head;           // index du PROCHAIN creneau a ecrire [0, capacity)
    uint32_t count;          // nombre de creneaux occupes [0, capacity]
    uint32_t ack_watermark;  // plus grand seq confirme par le hub (cumulatif)
    uint32_t dropped_pending;// compteur : PENDING ecrasees (buffer plein > retention)
};
static_assert(sizeof(StoreHeader) == 28, "StoreHeader doit rester a 28 octets");

constexpr uint32_t MHS_STORE_MAGIC   = 0x4253484D; // 'MHSB' en little-endian
constexpr uint16_t MHS_STORE_VERSION = 2;          // 2 = modele watermark

class MeasurementStore {
public:
    // Monte le store sur `backend`, en visant `capacity` creneaux. Si l'en-tete
    // existant est valide et de meme geometrie, on le REPREND (les mesures et le
    // filigrane survivent au reboot). Sinon on formate. Renvoie false si le
    // backend est trop petit pour l'en-tete + au moins un creneau.
    bool begin(BlobStore* backend, uint32_t capacity) {
        _backend = backend;
        _lastDroppedSeq = 0;
        if (!_backend) return false;

        const uint64_t need = static_cast<uint64_t>(sizeof(StoreHeader))
                            + static_cast<uint64_t>(capacity) * sizeof(StoredRecord);
        if (capacity == 0 || _backend->size() < need) return false;

        StoreHeader h{};
        if (_backend->read(0, &h, sizeof(h)) && headerValid(h, capacity)) {
            _hdr = h; // reprise a chaud : head/count/watermark d'avant le reboot
            return true;
        }
        return format(capacity);
    }

    // (Re)formate un ring vide de `capacity` creneaux.
    bool format(uint32_t capacity) {
        _hdr.magic = MHS_STORE_MAGIC;
        _hdr.version = MHS_STORE_VERSION;
        _hdr.record_size = sizeof(StoredRecord);
        _hdr.capacity = capacity;
        _hdr.head = 0;
        _hdr.count = 0;
        _hdr.ack_watermark = 0;
        _hdr.dropped_pending = 0;
        return persistHeader();
    }

    uint32_t capacity() const { return _hdr.capacity; }
    uint32_t count() const { return _hdr.count; }
    bool full() const { return _hdr.count == _hdr.capacity; }
    uint32_t ackWatermark() const { return _hdr.ack_watermark; }
    uint32_t droppedPending() const { return _hdr.dropped_pending; }
    // seq de la derniere PENDING ecrasee par le dernier append (0 = aucune). Le
    // firmware s'en sert pour journaliser explicitement le cas-limite « buffer
    // plein d'unacked ».
    uint32_t lastDroppedSeq() const { return _lastDroppedSeq; }

    bool isSynced(const StoredRecord& r) const { return r.seq <= _hdr.ack_watermark; }

    // Ajoute une mesure. Le ring ecrase la plus ancienne s'il est plein. seq et
    // sensor_ts doivent deja etre renseignes par l'appelant.
    bool append(const StoredRecord& rec) {
        if (!_backend || _hdr.capacity == 0) return false;
        _lastDroppedSeq = 0;

        // Cas-limite : buffer plein. Le creneau que l'on s'apprete a ecraser est
        // le plus ancien. S'il est PENDING (comm coupee sur toute la retention),
        // on le note explicitement (compteur + seq) plutot que de le perdre en
        // silence. On ecrase quand meme, sequentiellement (le plus doux pour la
        // Flash) : garder une PENDING vieille de 30 j au prix de la mesure fraiche
        // serait pire, et le comportement reste borne et documente.
        if (full()) {
            StoredRecord old{};
            if (readSlot(_hdr.head, old) && old.seq != 0 && old.seq > _hdr.ack_watermark) {
                _hdr.dropped_pending++;
                _lastDroppedSeq = old.seq;
            }
        }

        if (!writeSlot(_hdr.head, rec)) return false;
        _hdr.head = (_hdr.head + 1) % _hdr.capacity;
        if (_hdr.count < _hdr.capacity) _hdr.count++;
        return persistHeader();
    }

    // Applique l'accuse cumulatif du hub : tout seq <= ackSeq devient SYNCED.
    // Idempotent (un seul filigrane, jamais de recul). Renvoie true si le
    // filigrane a avance.
    bool markSyncedUpTo(uint32_t ackSeq) {
        if (ackSeq <= _hdr.ack_watermark) return false;
        _hdr.ack_watermark = ackSeq;
        return persistHeader();
    }

    // Plus petit seq encore PENDING (0 si tout est synchronise / buffer vide).
    uint32_t oldestUnsyncedSeq() const {
        uint32_t best = 0;
        for (uint32_t i = 0; i < _hdr.count; i++) {
            StoredRecord r;
            if (!readSlot(physicalSlot(i), r)) continue;
            if (r.seq != 0 && r.seq > _hdr.ack_watermark) {
                if (best == 0 || r.seq < best) best = r.seq;
            }
        }
        return best;
    }

    // Nombre de creneaux PENDING (borne l'ampleur du rattrapage).
    uint32_t unsyncedCount() const {
        uint32_t n = 0;
        for (uint32_t i = 0; i < _hdr.count; i++) {
            StoredRecord r;
            if (readSlot(physicalSlot(i), r) && r.seq != 0 && r.seq > _hdr.ack_watermark) n++;
        }
        return n;
    }

    // Selectionne jusqu'a `maxN` mesures PENDING a retransmettre, en ordre de seq
    // CROISSANT (chronologie), a partir de `fromSeq` (0 = depuis la plus ancienne
    // PENDING). BORNE par maxN : jamais de rattrapage sans limite, l'autonomie
    // prime, le reste attend le reveil suivant. Ecrit dans out[0..return-1].
    uint32_t selectRetransmit(uint32_t fromSeq, uint32_t maxN, StoredRecord* out) const {
        uint32_t n = 0;
        // On part juste au-dessus du plus grand seq deja retenu. Le plancher est
        // le filigrane (rien en dessous n'est PENDING) ET fromSeq-1 si demande.
        uint32_t floorSeq = _hdr.ack_watermark;
        if (fromSeq > 0 && fromSeq - 1 > floorSeq) floorSeq = fromSeq - 1;
        uint32_t lastTaken = floorSeq;
        while (n < maxN) {
            uint32_t bestSeq = 0;
            StoredRecord bestRec{};
            bool found = false;
            for (uint32_t i = 0; i < _hdr.count; i++) {
                StoredRecord r;
                if (!readSlot(physicalSlot(i), r)) continue;
                if (r.seq == 0 || r.seq <= lastTaken) continue;
                if (!found || r.seq < bestSeq) { bestSeq = r.seq; bestRec = r; found = true; }
            }
            if (!found) break;
            out[n++] = bestRec;
            lastTaken = bestSeq;
        }
        return n;
    }

    // Lecture d'un creneau logique i (0 = plus ancien). Utilitaire de test/diag.
    bool recordAt(uint32_t logicalIndex, StoredRecord& out) const {
        if (logicalIndex >= _hdr.count) return false;
        return readSlot(physicalSlot(logicalIndex), out);
    }

private:
    static bool headerValid(const StoreHeader& h, uint32_t capacity) {
        return h.magic == MHS_STORE_MAGIC
            && h.version == MHS_STORE_VERSION
            && h.record_size == sizeof(StoredRecord)
            && h.capacity == capacity
            && h.head < capacity
            && h.count <= capacity;
    }

    // Traduit un index LOGIQUE (0 = plus ancien) en index PHYSIQUE dans le ring.
    uint32_t physicalSlot(uint32_t logicalIndex) const {
        const uint32_t oldest = (_hdr.head + _hdr.capacity - _hdr.count) % _hdr.capacity;
        return (oldest + logicalIndex) % _hdr.capacity;
    }

    uint32_t slotOffset(uint32_t slot) const {
        return sizeof(StoreHeader) + slot * sizeof(StoredRecord);
    }

    bool readSlot(uint32_t slot, StoredRecord& out) const {
        return _backend && _backend->read(slotOffset(slot), &out, sizeof(out));
    }
    bool writeSlot(uint32_t slot, const StoredRecord& in) {
        return _backend && _backend->write(slotOffset(slot), &in, sizeof(in))
            && _backend->flush();
    }
    bool persistHeader() {
        return _backend && _backend->write(0, &_hdr, sizeof(_hdr)) && _backend->flush();
    }

    BlobStore* _backend = nullptr;
    StoreHeader _hdr{};
    uint32_t _lastDroppedSeq = 0;
};

} // namespace mhs
