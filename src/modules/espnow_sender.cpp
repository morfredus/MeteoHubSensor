#include "espnow_sender.h"
#include "secrets.h"
#include "board_config.h"
#include <esp_wifi.h>
#include <esp_log.h>
#include <cstring>

// Compteur de séquence conservé en RTC après deep sleep.
RTC_DATA_ATTR static uint32_t rtcSequenceNumber = 0;

volatile bool EspNowSender::_sendComplete = false;
volatile bool EspNowSender::_lastDeliverySuccess = false;

namespace {
constexpr uint32_t ACK_WAIT_MS = 250;
}

void EspNowSender::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    (void)mac_addr;
    _lastDeliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
    _sendComplete = true;
}

EspNowSender::EspNowSender() : _channel(ESPNOW_HUB_CHANNEL) {
    memcpy(_destMac, ESPNOW_RECEIVER_MAC, 6);
}

bool EspNowSender::begin() {
    // Approche ultra-simple : broadcast pur sur canal fixe, sans connexion AP
    Serial.println("[ESPNOW] Initializing in simple broadcast mode...");
    
    // Initialisation WiFi en mode STA (sans connexion)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Désactiver le WiFi sleep pour ESP-NOW
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

#if defined(SENSOR_NEEDS_TX_LIMIT)
    // Contrainte materielle (C3) : a pleine puissance, ESP-NOW C3 -> hub S3
    // echoue de facon intermittente. Plafonner la TX a 8,5 dBm rend la liaison
    // stable. Constat valide par test sur le C3 Super Mini / HW-675 ; ne PAS
    // relever sans re-tester sur ce couple de cartes.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.println("[ESPNOW] TX plafonnee a 8.5 dBm (contrainte C3)");
#endif

    // Fixer le canal radio AVANT ESP-NOW init
    esp_wifi_set_channel(ESPNOW_HUB_CHANNEL, WIFI_SECOND_CHAN_NONE);
    _channel = ESPNOW_HUB_CHANNEL;
    
    Serial.printf("[ESPNOW] Channel fixed to %d\n", _channel);
    
    // Petit délai pour stabiliser le canal
    delay(100);
    
    // Initialisation ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] Init failed");
        return false;
    }
    
    esp_now_register_send_cb(onDataSent);
    
    if (!registerBroadcastPeer()) {
        Serial.println("[ESPNOW] Peer registration failed");
        return false;
    }
    
    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    Serial.printf("[ESPNOW] Ready ch=%d mac=%02X:%02X:%02X:%02X:%02X:%02X\n",
                 _channel, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return true;
}

bool EspNowSender::registerBroadcastPeer() {
    // Approche classique : broadcast vers le hub AP
    const uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, broadcastMac, 6);
    peer.channel = _channel;
    peer.encrypt = false;
    
    if (esp_now_is_peer_exist(broadcastMac)) {
        esp_now_del_peer(broadcastMac);
    }

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESPNOW] Failed to add broadcast peer");
        return false;
    }
    
    Serial.println("[ESPNOW] Broadcast peer added");
    return true;
}

bool EspNowSender::send(MeteoPacket& packet) {
    packet.magic[0] = METEO_PACKET_MAGIC_0;
    packet.magic[1] = METEO_PACKET_MAGIC_1;
    packet.protocol_version = METEO_PROTOCOL_VERSION;
    packet.node_id = SENSOR_NODE_ID;
    packet.sequence = getNextSequence();

    const size_t dataLenForCrc = sizeof(MeteoPacket) - sizeof(uint16_t);
    packet.crc16 = calculateCrc16(reinterpret_cast<const uint8_t*>(&packet), dataLenForCrc);
    
    // Tentative d'envoi broadcast
    return sendOnCurrentChannel(packet, 3);
}

bool EspNowSender::sendOnCurrentChannel(const MeteoPacket& packet, uint8_t attempts) {
    // Approche ultra-simple : broadcast sans attente d'ACK
    const uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    for (uint8_t i = 0; i < attempts; i++) {
        esp_err_t result = esp_now_send(broadcastMac, (const uint8_t*)&packet, sizeof(MeteoPacket));
        if (result == ESP_OK) {
            Serial.printf("[ESPNOW] Broadcast sent attempt %d\n", i + 1);
            delay(50); // Petit délai pour laisser la transmission partir
            return true;
        }
        
        Serial.printf("[ESPNOW] Broadcast failed attempt %d: %d\n", i + 1, result);
        delay(20);
    }
    
    Serial.println("[ESPNOW] All broadcast attempts failed");
    return false;
}

uint32_t EspNowSender::getNextSequence() {
    rtcSequenceNumber++;
    return rtcSequenceNumber;
}
