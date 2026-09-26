#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <cstdlib>
#include "sync/segment_store.h"

// ============================================================================
// LittleFsSegmentFs - segments du journal de la sonde sur LittleFS (firmware)
// ============================================================================
// Un segment = un petit fichier « <dir>/<premier_seq>.seg » ecrit en AJOUT SEUL.
// C'est tout l'interet : LittleFS copie un fichier modifie a partir du point
// d'ecriture ; en ajoutant en fin de fichier, on ne touche qu'au dernier bloc,
// la ou l'ancien fichier unique de 276 Ko etait recopie en entier a chaque mesure.
// Chaque operation ouvre/ferme son fichier : la fermeture valide l'ecriture de
// facon atomique (LittleFS), une coupure ne laisse jamais de mesure a moitie ecrite.
namespace mhs {

class LittleFsSegmentFs : public SegmentFs {
public:
    // Monte LittleFS (formate si vierge) et cree le dossier des segments.
    bool begin(const char* dir) {
        _dir = dir;
        if (!LittleFS.begin(/*formatOnFail=*/true)) return false;
        if (!LittleFS.exists(_dir)) LittleFS.mkdir(_dir);
        return true;
    }

    bool list(std::vector<uint32_t>& ids) override {
        ids.clear();
        File d = LittleFS.open(_dir);
        if (!d || !d.isDirectory()) return false;
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            // name() peut rendre le chemin complet ou le seul nom selon la version
            // du core : on ne garde que ce qui suit le dernier '/'.
            const char* n = f.name();
            const char* slash = strrchr(n, '/');
            const char* base = slash ? slash + 1 : n;
            char* end = nullptr;
            const unsigned long id = strtoul(base, &end, 10);
            if (end && strcmp(end, ".seg") == 0 && id > 0) ids.push_back((uint32_t)id);
        }
        return true;
    }

    uint32_t size(uint32_t id) override {
        File f = LittleFS.open(path(id), "r");
        if (!f) return 0;
        const uint32_t s = (uint32_t)f.size();
        f.close();
        return s;
    }

    bool append(uint32_t id, const void* src, uint32_t len) override {
        File f = LittleFS.open(path(id), "a");
        if (!f) return false;
        const size_t w = f.write(static_cast<const uint8_t*>(src), len);
        f.close();
        return w == len;
    }

    bool read(uint32_t id, uint32_t off, void* dst, uint32_t len) override {
        File f = LittleFS.open(path(id), "r");
        if (!f) return false;
        const bool ok = f.seek(off) && f.read(static_cast<uint8_t*>(dst), len) == (int)len;
        f.close();
        return ok;
    }

    bool remove(uint32_t id) override { return LittleFS.remove(path(id)); }

private:
    String path(uint32_t id) const { return String(_dir) + "/" + String(id) + ".seg"; }
    const char* _dir = "/mhs";
};

} // namespace mhs
