#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// ============================================================================
// Abstraction de stockage octet-adressable (buffer local de la sonde)
// ============================================================================
// Le buffer de securite (MeasurementStore) raisonne en lecture/ecriture a un
// offset, sans savoir OU vivent les octets. Sur la sonde, c'est un fichier
// LittleFS de taille fixe ; dans les tests hote, c'est un tableau en memoire.
// Cette separation rend TOUTE la logique de ring buffer testable sans materiel,
// exactement la logique de synchronisation que le cahier des charges demande de
// couvrir par des tests unitaires.
namespace mhs {

class BlobStore {
public:
    virtual ~BlobStore() = default;

    // Lit `len` octets a partir de `off`. Renvoie false si hors bornes.
    virtual bool read(uint32_t off, void* dst, uint32_t len) = 0;
    // Ecrit `len` octets a partir de `off`. Renvoie false si hors bornes.
    virtual bool write(uint32_t off, const void* src, uint32_t len) = 0;
    // Taille totale (octets) de l'espace adressable.
    virtual uint32_t size() const = 0;
    // Force la persistance si le backend bufferise (no-op en memoire).
    virtual bool flush() { return true; }
};

// Backend en memoire, pour les tests hote. Simule aussi la persistance : deux
// MemoryBlobStore construits sur le MEME vecteur partagent les octets, ce qui
// permet de simuler un redemarrage (on reconstruit un store sur les memes
// donnees et on verifie que head/count/synced ont survecu).
class MemoryBlobStore : public BlobStore {
public:
    explicit MemoryBlobStore(uint32_t bytes) : _buf(bytes, 0xFF) {}
    // Partage un tampon existant (simulation de reboot : memes octets sur disque).
    explicit MemoryBlobStore(std::vector<uint8_t>* shared) : _shared(shared) {}

    bool read(uint32_t off, void* dst, uint32_t len) override {
        auto& b = buf();
        if (static_cast<uint64_t>(off) + len > b.size()) return false;
        std::memcpy(dst, b.data() + off, len);
        return true;
    }
    bool write(uint32_t off, const void* src, uint32_t len) override {
        auto& b = buf();
        if (static_cast<uint64_t>(off) + len > b.size()) return false;
        std::memcpy(b.data() + off, src, len);
        return true;
    }
    uint32_t size() const override { return static_cast<uint32_t>(constBuf().size()); }

private:
    std::vector<uint8_t> _buf;
    std::vector<uint8_t>* _shared = nullptr;
    std::vector<uint8_t>& buf() { return _shared ? *_shared : _buf; }
    const std::vector<uint8_t>& constBuf() const { return _shared ? *_shared : _buf; }
};

} // namespace mhs
