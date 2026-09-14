#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "meteo_packet.h"
#include "config.h"

// ============================================================================
// EspNowSender - liaison ESP-NOW vers le hub MeteoHub
// ============================================================================
// Choix de conception (2026-09-14, reecriture propre) :
//   - UNICAST vers la MAC du hub (ESPNOW_RECEIVER_MAC), PAS de broadcast. Un
//     unicast obtient l'ACK 802.11 materiel du hub + retransmission MAC : bien
//     plus fiable, surtout dehors. Et le callback onDataSent devient une VRAIE
//     confirmation de livraison (SUCCESS = le hub a bien accuse reception),
//     alors qu'en broadcast il renvoyait toujours SUCCESS meme si rien n'arrivait.
//   - Canal decouvert en scannant le SoftAP du hub (ESPNOW_HUB_AP_SSID) : le hub
//     suit le canal de la box, non fixe. En cas d'echec d'envoi, on RE-SCANNE et
//     on retente dans le cycle meme (auto-guerison), sans attendre le recheck.
// ============================================================================

class EspNowSender {
public:
    EspNowSender();

    // Initialise le Wi-Fi STA, decouvre le canal du hub et enregistre le peer
    // unicast. Renvoie false si ESP-NOW ne peut pas demarrer.
    bool begin();

    // Emet le paquet en unicast vers le hub. Renvoie true seulement si le hub a
    // accuse reception (ACK). Retente sur le canal courant, puis re-scanne le
    // canal et retente si le hub semble avoir migre.
    bool send(MeteoPacket& packet);

    // Rescanne le SoftAP du hub (« MH-NOW ») et bascule sur son canal s'il a
    // change. Appele periodiquement en mode continu. Renvoie true si change.
    bool refreshChannel();

    uint32_t getNextSequence();
    uint8_t channel() const { return _channel; }

    static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

private:
    uint8_t _channel;
    uint8_t _destMac[6]{}; // MAC du hub (unicast), copiee depuis ESPNOW_RECEIVER_MAC
    static volatile bool _sendComplete;
    static volatile bool _lastDeliverySuccess;

    // Scanne les reseaux et renvoie le canal du SoftAP du hub, ou 0 si absent.
    uint8_t scanHubChannel();
    // Fixe le canal radio courant (et met a jour _channel).
    void applyChannel(uint8_t ch);
    // (Re)enregistre le peer unicast du hub (suit le canal radio courant).
    bool registerHubPeer();
    // Une salve d'envois unicast sur le canal courant. Renvoie true des qu'un
    // ACK arrive ; false si tous les essais restent sans ACK.
    bool trySendUnicast(const MeteoPacket& packet, uint8_t attempts);
};
