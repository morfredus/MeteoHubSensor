#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "meteo_packet.h"
#include "config.h"
#include "pairing/pairing_logic.h"

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
//
// Appairage (2026-09-26) : la MAC du hub vient de la NVS (namespace "mhpair"),
// ecrite par pair() apres un appui long sur BOOT. Tant qu'aucun appairage n'a eu
// lieu, ESPNOW_RECEIVER_MAC (config.h) sert de valeur par defaut : une sonde deja
// en service continue de parler a son hub apres mise a jour du firmware. Une fois
// appairee, le scan de canal cherche le BSSID EXACT du SoftAP de SON hub, et non
// plus le premier « MH-NOW » venu : plusieurs hubs peuvent cohabiter a portee.
// ============================================================================

// Bilan d'une procedure d'appairage, pour le journal et la LED.
struct PairingReport {
    enum Result : uint8_t { PAIRED, NO_HUB, AMBIGUOUS, CONFIRM_FAILED, SAVE_FAILED } result = NO_HUB;
    mhpair::PairRecord hub{};   // hub retenu (valide si PAIRED)
    int hubsHeard = 0;          // hubs distincts entendus au dernier balayage
};

class EspNowSender {
public:
    EspNowSender();

    // Initialise le Wi-Fi STA, decouvre le canal du hub et enregistre le peer
    // unicast. Renvoie false si ESP-NOW ne peut pas demarrer.
    bool begin();

    // Emet le paquet en unicast vers le hub. Renvoie true seulement si le hub a
    // accuse reception (ACK). Retente sur le canal courant, puis re-scanne le
    // canal et retente si le hub semble avoir migre. Le seq/sensor_ts/oldest_seq
    // sont renseignes par l'appelant (SyncManager) ; `frameType` distingue une
    // acquisition LIVE d'une RETRANSMISSION d'une mesure bufferisee.
    bool send(MeteoPacket& packet, uint8_t frameType = FRAME_LIVE);

    // Ecoute BORNEE : attend jusqu'a `windowMs` un SyncControl valide du hub
    // (accuse cumulatif + trou a combler). Renvoie true si recu (dans `out`).
    // Ne rallonge l'eveil que de la fenetre, puis rend la main -> deep sleep sur.
    bool receiveSyncControl(SyncControl& out, uint32_t windowMs);

    // Arme la capture d'un SyncControl (remet le drapeau a zero). A appeler juste
    // avant l'envoi live pour ne pas rater une reponse tres rapide du hub.
    void resetSyncControl();

    // Rescanne le SoftAP du hub (« MH-NOW ») et bascule sur son canal s'il a
    // change. Appele periodiquement en mode continu. Renvoie true si change.
    bool refreshChannel();

    uint8_t channel() const { return _channel; }

    // true si un hub est connu (NVS ou MAC par defaut non nulle).
    bool isPaired() const { return _paired; }
    const uint8_t* hubMac() const { return _destMac; }

    // Procedure d'appairage VOLONTAIRE (appui long sur BOOT). Balaie les canaux
    // 1..13 en diffusant une demande, recueille les reponses, puis confirme en
    // unicast AVEC ACK aupres du hub retenu. N'ecrit la NVS qu'apres cette
    // confirmation ; tout echec (aucun hub, plusieurs hubs, pas d'ACK) laisse
    // l'association existante INTACTE. `baseSeq` = dernier seq accuse par l'ancien
    // hub, transmis au nouveau pour qu'il reprenne de la. Bornee par `timeoutMs`.
    PairingReport pair(uint32_t baseSeq, uint32_t timeoutMs);

    static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len);

private:
    uint8_t _channel;
    uint8_t _destMac[6]{}; // MAC du hub (unicast) : NVS, sinon ESPNOW_RECEIVER_MAC
    uint8_t _apMac[6]{};   // BSSID du SoftAP du hub (connu apres appairage)
    bool _haveAp = false;  // false : scan par SSID seul (comportement historique)
    bool _paired = false;
    static volatile bool _sendComplete;
    static volatile bool _lastDeliverySuccess;
    // Voie inverse : dernier SyncControl recu du hub + drapeau, remplis par le
    // callback de reception (contexte ESP-NOW) et lus dans receiveSyncControl.
    static volatile bool _ctrlReceived;
    static SyncControl _lastCtrl;

    // Scanne les reseaux et renvoie le canal du SoftAP du hub, ou 0 si absent.
    uint8_t scanHubChannel();
    // Charge l'association depuis la NVS (repli : ESPNOW_RECEIVER_MAC).
    void loadAssociation();
    // Fixe le canal radio courant (et met a jour _channel).
    void applyChannel(uint8_t ch);
    // (Re)enregistre le peer unicast du hub (suit le canal radio courant).
    bool registerHubPeer();
    // Une salve d'envois unicast sur le canal courant. Renvoie true des qu'un
    // ACK arrive ; false si tous les essais restent sans ACK.
    bool trySendUnicast(const MeteoPacket& packet, uint8_t attempts);
};
