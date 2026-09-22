#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "sync/blob_store.h"

// ============================================================================
// LittleFsBlobStore - backend Flash du buffer de securite (firmware)
// ============================================================================
// Implemente l'abstraction BlobStore (lecture/ecriture a un offset) au-dessus
// d'un unique fichier LittleFS de TAILLE FIXE. Le ring buffer (MeasurementStore)
// ecrit toujours dans ce meme fichier, aux memes offsets : c'est LittleFS qui
// assure le wear leveling physique (systeme de fichiers COW/log-structure), le
// ring ne fait que borner la retention. Pas de PSRAM, pas de NVS-journal.
//
// Le fichier est prealloue a sa taille cible au premier montage, puis ouvert en
// lecture/ecriture pour toute la session d'eveil et referme avant le deep sleep.
namespace mhs {

class LittleFsBlobStore : public BlobStore {
public:
    // Monte LittleFS (formate si vierge) et ouvre/preallouer le fichier a `bytes`.
    // Renvoie false si le montage ou l'allocation echoue.
    bool begin(const char* path, uint32_t bytes) {
        _path = path;
        _size = bytes;
        if (!LittleFS.begin(/*formatOnFail=*/true)) {
            return false;
        }
        // Preallocation si absent ou trop court (le format du ring revalide de
        // toute facon l'en-tete : un fichier neuf sera formate par MeasurementStore).
        if (!LittleFS.exists(_path) || (uint32_t)fileSize() < bytes) {
            if (!preallocate(bytes)) return false;
        }
        _file = LittleFS.open(_path, "r+");
        return (bool)_file;
    }

    void end() {
        if (_file) { _file.flush(); _file.close(); }
    }

    bool read(uint32_t off, void* dst, uint32_t len) override {
        if (!_file || (uint64_t)off + len > _size) return false;
        if (!_file.seek(off)) return false;
        return _file.read(reinterpret_cast<uint8_t*>(dst), len) == (int)len;
    }

    bool write(uint32_t off, const void* src, uint32_t len) override {
        if (!_file || (uint64_t)off + len > _size) return false;
        if (!_file.seek(off)) return false;
        return _file.write(reinterpret_cast<const uint8_t*>(src), len) == len;
    }

    uint32_t size() const override { return _size; }

    bool flush() override {
        if (!_file) return false;
        _file.flush();
        return true;
    }

private:
    uint32_t fileSize() {
        File f = LittleFS.open(_path, "r");
        if (!f) return 0;
        const size_t s = f.size();
        f.close();
        return (uint32_t)s;
    }

    // Cree le fichier a la bonne taille (octets a 0xFF, comme une Flash vierge :
    // MeasurementStore n'y verra pas de magic valide et formatera proprement).
    bool preallocate(uint32_t bytes) {
        File f = LittleFS.open(_path, "w");
        if (!f) return false;
        uint8_t block[256];
        memset(block, 0xFF, sizeof(block));
        uint32_t remaining = bytes;
        while (remaining > 0) {
            const uint32_t chunk = remaining < sizeof(block) ? remaining : sizeof(block);
            if (f.write(block, chunk) != chunk) { f.close(); return false; }
            remaining -= chunk;
        }
        f.flush();
        f.close();
        return true;
    }

    const char* _path = nullptr;
    uint32_t _size = 0;
    File _file;
};

} // namespace mhs
