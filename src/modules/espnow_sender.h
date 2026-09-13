#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "meteo_packet.h"
#include "config.h"

class EspNowSender {
public:
    EspNowSender();

    // Initialisation ESP-NOW simplifiée : broadcast sur canal fixe
    bool begin();

    bool send(MeteoPacket& packet);

    uint32_t getNextSequence();
    uint8_t channel() const { return _channel; }

    static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

private:
    uint8_t _channel;
    uint8_t _destMac[6]{};
    static volatile bool _sendComplete;
    static volatile bool _lastDeliverySuccess;

    bool registerBroadcastPeer();
    bool sendOnCurrentChannel(const MeteoPacket& packet, uint8_t attempts);
};
