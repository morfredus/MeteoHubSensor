#include "espnow_sender.h"
#include "secrets.h"
#include "board_config.h"
#include <esp_wifi.h>
#include <cstring>

// Le numero de sequence n'est PLUS en RTC (remis a 0 au power-cycle) : il vit
// desormais en NVS via SyncManager, monotone meme apres un brownout.

// Canal du hub memorise en RAM RTC : survit au deep sleep, reutilise au reveil
// sans refaire un scan Wi-Fi. 0 = pas encore decouvert (premier boot).
RTC_DATA_ATTR static uint8_t rtcChannel = 0;
// Compteur de reveils, pour ne rescanner que toutes les N fois (deep sleep).
RTC_DATA_ATTR static uint32_t rtcWakeCount = 0;

volatile bool EspNowSender::_sendComplete = false;
volatile bool EspNowSender::_lastDeliverySuccess = false;
volatile bool EspNowSender::_ctrlReceived = false;
SyncControl EspNowSender::_lastCtrl{};

namespace {
// Delai d'attente de l'ACK apres un esp_now_send unicast. L'ACK 802.11 revient
// en quelques ms ; 60 ms couvre large, retransmissions MAC incluses.
constexpr uint32_t ACK_WAIT_MS = 60;
// Essais sur le canal courant avant de suspecter une migration de canal du hub.
constexpr uint8_t ATTEMPTS_PER_CHANNEL = 3;
}

void EspNowSender::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    (void)mac_addr;
    // En UNICAST, ce statut est fiable : SUCCESS = le hub a accuse reception.
    _lastDeliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
    _sendComplete = true;
}

void EspNowSender::onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    (void)mac_addr;
    // Voie inverse : on ne s'interesse qu'au SyncControl du hub (magic 'M','C').
    // Trame trop courte / mauvais magic / mauvaise version / CRC KO -> ignoree.
    if (data == nullptr || len < (int)sizeof(SyncControl)) return;
    SyncControl ctrl;
    memcpy(&ctrl, data, sizeof(SyncControl));
    if (ctrl.magic[0] != METEO_CONTROL_MAGIC_0 || ctrl.magic[1] != METEO_CONTROL_MAGIC_1) return;
    if (ctrl.protocol_version != METEO_PROTOCOL_VERSION) return;
    const size_t lenForCrc = sizeof(SyncControl) - sizeof(uint16_t);
    if (calculateCrc16(reinterpret_cast<const uint8_t*>(&ctrl), lenForCrc) != ctrl.crc16) return;
    _lastCtrl = ctrl;
    _ctrlReceived = true;
}

EspNowSender::EspNowSender() : _channel(ESPNOW_HUB_CHANNEL) {
    memcpy(_destMac, ESPNOW_RECEIVER_MAC, 6);
}

bool EspNowSender::begin() {
    Serial.println("[ESPNOW] Init (unicast vers le hub)");

    // Wi-Fi en STA non associe : ESP-NOW n'a pas besoin d'association. On coupe
    // le sommeil modem, sinon un ACK/une trame tombe pendant une micro-sieste.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

#if defined(SENSOR_NEEDS_TX_LIMIT)
    // Contrainte materielle (C3) : a pleine puissance, la liaison C3 -> hub S3
    // echoue de facon intermittente. Plafond defini dans board_config.h/config.h.
    WiFi.setTxPower(SENSOR_TX_POWER_LEVEL);
    Serial.printf("[ESPNOW] TX plafonnee (contrainte C3), niveau=%d\n",
                  (int)SENSOR_TX_POWER_LEVEL);
#endif

    // Decouverte du canal du hub. En deep sleep on reutilise le canal memorise en
    // RTC et on ne rescanne que toutes les ESPNOW_RESCAN_EVERY_N_WAKES reveils :
    // un scan (~2 s) a chaque reveil ruinerait l'autonomie.
    const bool needScan = (rtcChannel == 0)
        || (rtcWakeCount % ESPNOW_RESCAN_EVERY_N_WAKES == 0);
    rtcWakeCount++;

    uint8_t ch;
    if (needScan) {
        const uint8_t scanned = scanHubChannel();
        if (scanned != 0) {
            rtcChannel = ch = scanned; // resultat confirme : on le cache en RTC
            Serial.printf("[ESPNOW] Canal decouvert=%d (scan MH-NOW)\n", ch);
        } else {
            // Scan infructueux : repli PONCTUEL sans polluer le cache RTC. Si rien
            // n'est encore confirme, on laisse rtcChannel=0 pour rescanner au
            // prochain reveil plutot que figer un mauvais canal.
            ch = (rtcChannel != 0) ? rtcChannel : ESPNOW_HUB_CHANNEL;
            Serial.printf("[ESPNOW] MH-NOW non trouve, repli ponctuel=%d\n", ch);
        }
    } else {
        ch = rtcChannel;
        Serial.printf("[ESPNOW] Canal repris du cache RTC=%d\n", ch);
    }
    applyChannel(ch);
    delay(50); // laisse le canal se stabiliser

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Init failed");
        return false;
    }
    esp_now_register_send_cb(onDataSent);
    // Voie inverse (v3) : le hub peut renvoyer un SyncControl pendant la fenetre
    // d'eveil. On enregistre le callback de reception des maintenant.
    esp_now_register_recv_cb(onDataRecv);

    if (!registerHubPeer()) {
        return false;
    }

    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    Serial.printf("[ESPNOW] Pret ch=%d src=%02X:%02X:%02X:%02X:%02X:%02X"
                  " -> hub=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  _channel, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  _destMac[0], _destMac[1], _destMac[2],
                  _destMac[3], _destMac[4], _destMac[5]);
    return true;
}

bool EspNowSender::registerHubPeer() {
    // Peer unicast = MAC du hub. channel = 0 : le peer suit le canal radio
    // courant, donc un changement de canal (applyChannel) ne force pas a
    // reenregistrer le peer. ifidx STA : le hub ecoute sur son interface STA.
    if (esp_now_is_peer_exist(_destMac)) {
        esp_now_del_peer(_destMac);
    }

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, _destMac, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW] Ajout du peer hub echoue");
        return false;
    }
    return true;
}

bool EspNowSender::send(MeteoPacket& packet, uint8_t frameType) {
    // En-tete + integrite. Le seq / sensor_ts / oldest_seq sont deja renseignes
    // par le SyncManager (propriétaire de l'identite et de l'horloge, persistees
    // en NVS) : send() ne fait que finaliser l'en-tete et transmettre. Les retries
    // reemetten la MEME trame (le hub deduplique par seq).
    packet.magic[0] = METEO_PACKET_MAGIC_0;
    packet.magic[1] = METEO_PACKET_MAGIC_1;
    packet.protocol_version = METEO_PROTOCOL_VERSION;
    packet.node_id = SENSOR_NODE_ID;
    packet.frame_type = frameType;

    const size_t dataLenForCrc = sizeof(MeteoPacket) - sizeof(uint16_t);
    packet.crc16 = calculateCrc16(reinterpret_cast<const uint8_t*>(&packet), dataLenForCrc);

    // 1er jet : plusieurs essais sur le canal courant.
    if (trySendUnicast(packet, ATTEMPTS_PER_CHANNEL)) {
        return true;
    }

    // Aucun ACK : le hub a peut-etre migre de canal. On rescanne et on retente.
    Serial.println("[ESPNOW] Pas d'ACK : re-scan du canal du hub...");
    const uint8_t scanned = scanHubChannel();
    if (scanned != 0 && scanned != _channel) {
        rtcChannel = scanned;
        applyChannel(scanned);
        registerHubPeer(); // le scan a pu bousculer la radio : on refixe le peer
        Serial.printf("[ESPNOW] Nouveau canal du hub=%d, nouvelle tentative\n", scanned);
        return trySendUnicast(packet, ATTEMPTS_PER_CHANNEL);
    }

    Serial.println("[ESPNOW] Toujours pas d'ACK (hub hors de portee ?)");
    return false;
}

bool EspNowSender::trySendUnicast(const MeteoPacket& packet, uint8_t attempts) {
    for (uint8_t i = 0; i < attempts; i++) {
        _sendComplete = false;
        _lastDeliverySuccess = false;

        esp_err_t result = esp_now_send(_destMac, (const uint8_t*)&packet, sizeof(MeteoPacket));
        if (result != ESP_OK) {
            Serial.printf("[ESPNOW] esp_now_send erreur attempt %d: %d\n", i + 1, result);
            delay(20);
            continue;
        }

        // Attendre l'ACK reel (callback). CRITIQUE : sans cette attente la radio
        // pourrait s'eteindre (deep sleep) avant la fin de la transmission.
        const uint32_t t0 = millis();
        while (!_sendComplete && (millis() - t0) < ACK_WAIT_MS) {
            delay(1);
        }

        if (_sendComplete && _lastDeliverySuccess) {
            Serial.printf("[ESPNOW] Livre (ACK) attempt %d ch=%d\n", i + 1, _channel);
            return true;
        }
        Serial.printf("[ESPNOW] Pas d'ACK attempt %d (%s)\n",
                      i + 1, _sendComplete ? "NACK" : "timeout");
        delay(20);
    }
    return false;
}

uint8_t EspNowSender::scanHubChannel() {
    // Scan actif bloquant (~1-2 s) : on cherche le SoftAP du hub et on lit son
    // canal. WiFi.scanNetworks fonctionne en STA non associe.
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
    uint8_t found = 0;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ESPNOW_HUB_AP_SSID) {
            found = static_cast<uint8_t>(WiFi.channel(i));
            break;
        }
    }
    WiFi.scanDelete();
    return found;
}

void EspNowSender::applyChannel(uint8_t ch) {
    // Fixe uniquement le canal RADIO ; le cache RTC (rtcChannel) n'est mis a jour
    // qu'apres un scan CONFIRME, jamais sur un repli.
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    _channel = ch;
}

bool EspNowSender::refreshChannel() {
    const uint8_t ch = scanHubChannel();
    if (ch == 0) {
        applyChannel(_channel); // le scan a bouscule la radio : on la refixe
        registerHubPeer();
        Serial.println("[ESPNOW] Rescan : MH-NOW non vu, canal inchange");
        return false;
    }
    const bool changed = (ch != _channel);
    rtcChannel = ch; // scan confirme
    applyChannel(ch);
    registerHubPeer();
    if (changed) {
        Serial.printf("[ESPNOW] Canal du hub change -> %d\n", ch);
    }
    return changed;
}

bool EspNowSender::receiveSyncControl(SyncControl& out, uint32_t windowMs) {
    // Ecoute BORNEE de la voie inverse. Le callback onDataRecv remplit _lastCtrl
    // + _ctrlReceived des qu'un SyncControl valide arrive. On sonde le drapeau
    // jusqu'a la fenetre, puis on rend la main quoi qu'il arrive : jamais de
    // blocage du retour au deep sleep.
    _ctrlReceived = false;
    const uint32_t t0 = millis();
    while (!_ctrlReceived && (millis() - t0) < windowMs) {
        delay(2);
    }
    if (!_ctrlReceived) return false;
    out = _lastCtrl;
    return true;
}
