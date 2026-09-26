#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>
#include "measurement_store.h" // StoredRecord (format d'enregistrement inchange, 32 o)

// ============================================================================
// SegmentStore - buffer de securite de la sonde en JOURNAL SEGMENTE (0.24.0)
// ============================================================================
// Pourquoi ce remplacement du ring a fichier unique (MeasurementStore) : LittleFS
// ne modifie jamais un fichier sur place (copie a l'ecriture). Reecrire l'en-tete
// au debut d'un fichier de 276 Ko, a chaque mesure et a chaque accuse, forcait la
// recopie du fichier entier : 4,5 s mesurees par mesure, radio allumee, soit ~5x
// l'energie utile d'un reveil. Ici :
//   - les mesures sont AJOUTEES en fin de petits fichiers (segments d'une
//     journee, 288 x 32 o = 9 Ko) : un ajout ne recopie au pire qu'un bloc ;
//   - le filigrane d'accuse (et le compteur de pertes) n'est PLUS dans un fichier :
//     c'est l'appelant qui le persiste (NVS), le store ne fait que le tenir ;
//   - un segment entierement accuse est SUPPRIME : il ne sert plus a rien (la
//     sonde ne renvoie jamais une mesure deja accusee), la Flash reste presque vide.
//
// Semantique inchangee pour le reste du firmware :
//   SYNCED si seq <= filigrane, PENDING sinon ; une PENDING n'est perdue que si la
//   retention (kMaxSegments jours) est depassee, et c'est alors COMPTE ;
//   selection de retransmission bornee, en ordre de seq croissant.
//
// Un segment est nomme par le seq de sa premiere mesure ; ses mesures sont
// consecutives (seq, seq+1, ...), ce qui permet de connaitre son contenu par sa
// seule taille, sans le relire. Une numerotation qui repart en arriere (NVS de
// la sonde effacee) rend l'ancien contenu inexploitable : on repart a vide.
namespace mhs {

// Stockage de segments : un segment = un fichier en ajout seul, identifie par
// un entier (le seq de sa premiere mesure).
class SegmentFs {
public:
    virtual ~SegmentFs() = default;
    virtual bool list(std::vector<uint32_t>& ids) = 0;              // tous les segments
    virtual uint32_t size(uint32_t id) = 0;                          // octets
    virtual bool append(uint32_t id, const void* src, uint32_t len) = 0; // cree si absent
    virtual bool read(uint32_t id, uint32_t off, void* dst, uint32_t len) = 0;
    virtual bool remove(uint32_t id) = 0;
};

// Implementation memoire (tests hote). Le map partage simule la Flash a travers
// un « redemarrage » (on reconstruit un store sur les memes fichiers).
class MemorySegmentFs : public SegmentFs {
public:
    std::map<uint32_t, std::vector<uint8_t>> files;
    bool failAppend = false; // simule une Flash pleine / en defaut

    bool list(std::vector<uint32_t>& ids) override {
        ids.clear();
        for (const auto& kv : files) ids.push_back(kv.first);
        return true;
    }
    uint32_t size(uint32_t id) override {
        auto it = files.find(id);
        return it == files.end() ? 0 : (uint32_t)it->second.size();
    }
    bool append(uint32_t id, const void* src, uint32_t len) override {
        if (failAppend) return false;
        auto& f = files[id];
        const uint8_t* p = static_cast<const uint8_t*>(src);
        f.insert(f.end(), p, p + len);
        return true;
    }
    bool read(uint32_t id, uint32_t off, void* dst, uint32_t len) override {
        auto it = files.find(id);
        if (it == files.end() || (uint64_t)off + len > it->second.size()) return false;
        memcpy(dst, it->second.data() + off, len);
        return true;
    }
    bool remove(uint32_t id) override { return files.erase(id) > 0; }
};

class SegmentStore {
public:
    static constexpr uint32_t kRecSize = sizeof(StoredRecord);

    // Monte le store. `watermark` / `dropped` viennent de la NVS (persistance de
    // l'appelant). Les segments deja entierement accuses sont purges.
    bool begin(SegmentFs* fs, uint32_t recordsPerSegment, uint32_t maxSegments,
               uint32_t watermark, uint32_t dropped) {
        _fs = fs;
        _perSeg = recordsPerSegment ? recordsPerSegment : 1;
        _maxSeg = maxSegments ? maxSegments : 1;
        _watermark = watermark;
        _dropped = dropped;
        _lastDroppedSeq = 0;
        _segs.clear();
        if (!_fs) return false;
        std::vector<uint32_t> ids;
        if (!_fs->list(ids)) return false;
        std::sort(ids.begin(), ids.end());
        for (uint32_t id : ids) {
            const uint32_t bytes = _fs->size(id);
            Seg s{id, bytes / kRecSize, (bytes % kRecSize) != 0};
            if (id == 0 || s.count == 0) { _fs->remove(id); continue; } // vide / invalide
            _segs.push_back(s);
        }
        purgeSynced();
        return true;
    }

    // Ajoute une mesure (seq attribue par l'appelant, strictement croissant).
    bool append(const StoredRecord& rec) {
        _lastDroppedSeq = 0;
        if (!_fs || rec.seq == 0) return false;
        // Numerotation repartie en arriere : l'ancien contenu appartient a une
        // AUTRE serie, inutilisable (les seq se chevauchent). On repart a vide.
        if (!_segs.empty() && rec.seq <= newestSeq()) {
            wipe();
            _restarted = true;
        }
        const bool needNew = _segs.empty()
            || _segs.back().sealed
            || _segs.back().count >= _perSeg
            || rec.seq != _segs.back().first + _segs.back().count;
        if (needNew) {
            if (!_fs->append(rec.seq, &rec, kRecSize)) return false;
            _segs.push_back(Seg{rec.seq, 1, false});
            enforceRetention();
            return true;
        }
        if (!_fs->append(_segs.back().first, &rec, kRecSize)) {
            _segs.back().sealed = true; // ne plus ajouter a un fichier douteux
            return false;
        }
        _segs.back().count++;
        return true;
    }

    // Accuse cumulatif du hub. Refuse un accuse au-dela de notre plus grande
    // mesure (hub qui suit une AUTRE serie) : l'accepter marquerait « livrees »
    // des mesures jamais archivees. Renvoie true si le filigrane a avance (a
    // persister par l'appelant).
    bool markSyncedUpTo(uint32_t ackSeq) {
        if (ackSeq <= _watermark) return false;
        if (ackSeq > newestSeq()) return false;
        _watermark = ackSeq;
        purgeSynced();
        return true;
    }

    // Filigrane incoherent (au-dela de toute mesure presente) herite d'un ancien
    // firmware : on le ramene a 0, les mesures presentes redeviennent PENDING.
    bool repairWatermark() {
        if (_segs.empty() || _watermark <= newestSeq()) return false;
        _watermark = 0;
        return true;
    }

    // Jusqu'a maxN mesures PENDING de seq >= fromSeq, en ordre croissant.
    uint32_t selectRetransmit(uint32_t fromSeq, uint32_t maxN, StoredRecord* out) {
        uint32_t floorSeq = _watermark;
        if (fromSeq > 0 && fromSeq - 1 > floorSeq) floorSeq = fromSeq - 1;
        uint32_t n = 0;
        for (const Seg& s : _segs) {
            if (n >= maxN) break;
            const uint32_t last = s.first + s.count - 1;
            if (last <= floorSeq) continue;
            uint32_t idx = (floorSeq >= s.first) ? (floorSeq - s.first + 1) : 0;
            for (; idx < s.count && n < maxN; idx++) {
                StoredRecord r;
                if (!_fs->read(s.first, idx * kRecSize, &r, kRecSize)) break;
                if (r.seq > floorSeq) out[n++] = r;
            }
        }
        return n;
    }

    uint32_t ackWatermark() const { return _watermark; }
    uint32_t droppedPending() const { return _dropped; }
    uint32_t lastDroppedSeq() const { return _lastDroppedSeq; }
    // true une fois apres une numerotation repartie en arriere (a journaliser).
    bool takeRestarted() { const bool r = _restarted; _restarted = false; return r; }

    // Plus petite / plus grande mesure presente (0 si vide).
    uint32_t oldestSeq() const { return _segs.empty() ? 0 : _segs.front().first; }

    // Plus ancienne mesure que la sonde peut ENCORE fournir au hub : la plus
    // ancienne PENDING (une mesure accusee n'est jamais renvoyee). C'est cette
    // valeur qui voyage dans oldest_seq. Annoncer une mesure accusee mais encore
    // stockee ferait attendre au hub une mesure qui ne viendra jamais (impasse
    // vue sur le terrain : hub bloque a « want=1089 »). 0 si rien en attente.
    uint32_t oldestProvidableSeq() const {
        const uint32_t newest = newestSeq();
        if (newest == 0 || newest <= _watermark) return 0;
        const uint32_t oldest = oldestSeq();
        return (_watermark + 1 > oldest) ? _watermark + 1 : oldest;
    }
    uint32_t newestSeq() const {
        return _segs.empty() ? 0 : _segs.back().first + _segs.back().count - 1;
    }

    // Mesures presentes / en attente (calcul par plages, sans lecture Flash).
    uint32_t count() const {
        uint32_t c = 0;
        for (const Seg& s : _segs) c += s.count;
        return c;
    }
    uint32_t unsyncedCount() const {
        uint32_t c = 0;
        for (const Seg& s : _segs) {
            const uint32_t last = s.first + s.count - 1;
            if (last <= _watermark) continue;
            c += (s.first > _watermark) ? s.count : (last - _watermark);
        }
        return c;
    }
    uint32_t segmentCount() const { return (uint32_t)_segs.size(); }

private:
    struct Seg { uint32_t first; uint32_t count; bool sealed; };

    // Supprime les segments entierement accuses, SAUF le dernier (celui ou l'on
    // ajoute) : le supprimer ferait creer puis effacer un fichier a chaque cycle.
    void purgeSynced() {
        while (_segs.size() > 1) {
            const Seg& s = _segs.front();
            if (s.first + s.count - 1 > _watermark) break;
            _fs->remove(s.first);
            _segs.erase(_segs.begin());
        }
    }

    // Retention depassee : le plus vieux segment part, et ses PENDING sont
    // COMPTEES (jamais de perte silencieuse).
    void enforceRetention() {
        while (_segs.size() > _maxSeg) {
            const Seg s = _segs.front();
            const uint32_t last = s.first + s.count - 1;
            if (last > _watermark) {
                const uint32_t lost = (s.first > _watermark) ? s.count : (last - _watermark);
                _dropped += lost;
                _lastDroppedSeq = last;
            }
            _fs->remove(s.first);
            _segs.erase(_segs.begin());
        }
    }

    void wipe() {
        for (const Seg& s : _segs) _fs->remove(s.first);
        _segs.clear();
        _watermark = 0;
    }

    SegmentFs* _fs = nullptr;
    std::vector<Seg> _segs;   // tries par seq croissant
    uint32_t _perSeg = 288;
    uint32_t _maxSeg = 30;
    uint32_t _watermark = 0;
    uint32_t _dropped = 0;
    uint32_t _lastDroppedSeq = 0;
    bool _restarted = false;
};

} // namespace mhs
