#pragma once
#include <Arduino.h>
#include "board_config.h"
#include "meteo_packet.h"

// ============================================================================
// DisplayManager : petit ecran de statut de la sonde (HW-675 uniquement).
//
// Sur la carte C3 HW-675, un OLED 0.42" (SSD1306 72x40) est cable sur le meme
// bus I2C que les capteurs (GP5/GP6). Ce module l'utilise pour montrer, sans
// PC connecte, ce que la sonde vient de mesurer et d'emettre.
//
// Sur l'ESP32-S3 Super Mini (pas d'ecran), toutes les methodes sont des
// no-op : le firmware appelle DisplayManager sans condition, la separation
// materielle reste dans board_config.h. `available()` renvoie alors false.
// ============================================================================
class DisplayManager {
public:
    // Initialise l'ecran. A appeler APRES l'ouverture du bus I2C (Wire), car
    // l'ecran partage GP5/GP6 avec les capteurs.
    void begin();

    // Ecran de demarrage (nom du projet + version).
    void showSplash(const char* version);

    // Affiche le dernier releve et l'etat d'emission ESP-NOW.
    void showReading(const MeteoPacket& packet, bool txOk, uint8_t channel);

    // Message court (2 lignes) pour un etat exceptionnel (ex : ESP-NOW KO).
    void showMessage(const char* line1, const char* line2 = nullptr);

    // true si un ecran est reellement present et initialise.
    bool available() const { return _ready; }

private:
    bool _ready = false;
};
