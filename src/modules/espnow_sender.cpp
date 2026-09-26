#include "espnow_sender.h"
#include "secrets.h"
#include "board_config.h"
#include <esp_wifi.h>
#include <esp_random.h>
#include <Preferences.h>
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

const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Destination dont on attend l'ACK : le callback d'envoi ignore les statuts
// d'une AUTRE trame (une demande broadcast de l'appairage, toujours « SUCCESS »,
// ne doit pas passer pour l'ACK de la confirmation unicast).
uint8_t s_ackMac[6] = {};

// Reponses d'appairage captees par le callback radio (tache Wi-Fi) et lues par
// pair() (tache principale). Petit anneau protege par une section critique ; la
// capture n'est active que pendant la procedure.
struct RxPair { uint8_t mac[6]; MeteoPairFrame frame; };
constexpr int PAIR_RING = 8;
RxPair s_pairRing[PAIR_RING];
volatile uint32_t s_pairHead = 0;
uint32_t s_pairTail = 0;
volatile bool s_pairCapture = false;
portMUX_TYPE s_pairMux = portMUX_INITIALIZER_UNLOCKED;

// Association en NVS : un seul blob (ecriture atomique), namespace dedie.
constexpr char NVS_NS[]  = "mhpair";
constexpr char NVS_KEY[] = "hub";

bool saveRecord(const mhpair::PairRecord& r) {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, /*readOnly=*/false)) return false;
    const size_t w = prefs.putBytes(NVS_KEY, &r, sizeof(r));
    prefs.end();
    return w == sizeof(r);
}

bool loadRecord(mhpair::PairRecord& r) {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, /*readOnly=*/true)) return false; // namespace absent
    const bool ok = prefs.getBytesLength(NVS_KEY) == sizeof(r)
                 && prefs.getBytes(NVS_KEY, &r, sizeof(r)) == sizeof(r);
    prefs.end();
    return ok && mhpair::isValidRecord(r);
}

void printMac(const char* label, const uint8_t* m) {
    Serial.printf("%s%02X:%02X:%02X:%02X:%02X:%02X", label, m[0], m[1], m[2], m[3], m[4], m[5]);
}
}

void EspNowSender::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (mac_addr != nullptr && memcmp(mac_addr, s_ackMac, 6) != 0) return;
    // En UNICAST, ce statut est fiable : SUCCESS = le hub a accuse reception.
    _lastDeliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
    _sendComplete = true;
}

void EspNowSender::onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    // Appairage en cours : une reponse de hub est mise de cote pour pair().
    if (s_pairCapture && mac_addr != nullptr) {
        MeteoPairFrame f;
        if (isValidPairFrame(data, len, &f)) {
            portENTER_CRITICAL(&s_pairMux);
            RxPair& slot = s_pairRing[s_pairHead % PAIR_RING];
            memcpy(slot.mac, mac_addr, 6);
            slot.frame = f;
            s_pairHead = s_pairHead + 1;
            portEXIT_CRITICAL(&s_pairMux);
            return;
        }
    }
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

void EspNowSender::loadAssociation() {
    mhpair::PairRecord r;
    if (loadRecord(r)) {
        memcpy(_destMac, r.sta_mac, 6);
        memcpy(_apMac, r.ap_mac, 6);
        _haveAp = !mhpair::isZeroMac(r.ap_mac);
        _paired = true;
        printMac("[PAIR] Hub appaire (NVS) sta=", _destMac);
        Serial.printf(" nom=%s canal=%u\n", r.name, (unsigned)r.channel);
        if (r.channel >= 1 && r.channel <= 14) {
            _channel = r.channel; // indice de depart si le cache RTC est vide
        }
        return;
    }
    // Aucun appairage enregistre : MAC par defaut de config.h (sonde deja en
    // service avant l'appairage). Toute a zero = sonde neuve, non appairee.
    memcpy(_destMac, ESPNOW_RECEIVER_MAC, 6);
    _haveAp = false;
    _paired = mhpair::isUnicastMac(_destMac);
    if (_paired) {
        printMac("[PAIR] Pas d'appairage en NVS, MAC par defaut hub=", _destMac);
        Serial.println();
    } else {
        Serial.println("[PAIR] Sonde non appairee : appui long sur BOOT pour choisir un hub");
    }
}

bool EspNowSender::begin() {
    Serial.println("[ESPNOW] Init (unicast vers le hub)");
    loadAssociation();

    // Wi-Fi en STA non associe : ESP-NOW n'a pas besoin d'association. On coupe
    // le sommeil modem, sinon un ACK/une trame tombe pendant une micro-sieste.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

#if defined(SENSOR_NEEDS_TX_LIMIT)
    // Contrainte materielle : a pleine puissance, l'antenne PCB du Super Mini
    // n'obtient aucun ACK du hub. Plafond defini dans board_config.h/config.h.
    WiFi.setTxPower(SENSOR_TX_POWER_LEVEL);
    Serial.printf("[ESPNOW] TX plafonnee (antenne Super Mini), niveau=%d\n",
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
            ch = (rtcChannel != 0) ? rtcChannel : _channel;
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

    // Sonde non appairee : ESP-NOW demarre quand meme (l'appairage en a besoin),
    // les mesures restent simplement en attente dans le buffer local.
    if (_paired && !registerHubPeer()) {
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
    if (!_paired) {
        Serial.println("[ESPNOW] Aucun hub appaire : mesure gardee en attente");
        return false;
    }
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
    memcpy(s_ackMac, _destMac, 6);
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
    // Hub appaire : chercher le BSSID EXACT de SON SoftAP. Tous les hubs
    // s'appellent « MH-NOW » ; se fier au seul SSID ferait suivre le canal d'un
    // AUTRE hub a portee. Sans BSSID connu (MAC par defaut, avant appairage) :
    // premier « MH-NOW » venu, comme avant.
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
    uint8_t found = 0;
    for (int i = 0; i < n; i++) {
        const bool match = _haveAp
            ? (memcmp(WiFi.BSSID(i), _apMac, 6) == 0)
            : (WiFi.SSID(i) == ESPNOW_HUB_AP_SSID);
        if (match) {
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
    // + _ctrlReceived des qu'un SyncControl valide arrive. On NE remet PAS le
    // drapeau a zero ici : c'est resetSyncControl(), appele AVANT l'envoi live, qui
    // arme la capture. Ainsi une reponse arrivee juste apres l'ACK (avant meme
    // d'entrer ici) n'est pas perdue par une course de timing. On consomme le
    // drapeau a la lecture. Retour garanti a la fin de la fenetre (jamais bloquant).
    const uint32_t t0 = millis();
    while ((millis() - t0) < windowMs) {
        if (_ctrlReceived) {
            out = _lastCtrl;
            _ctrlReceived = false; // consomme : la prochaine ecoute attend une nouvelle reponse
            return true;
        }
        delay(2);
    }
    return false;
}

void EspNowSender::resetSyncControl() {
    // Arme la capture d'un SyncControl : a appeler juste avant l'envoi live, pour
    // que toute reponse du hub a partir de cet instant soit retenue.
    _ctrlReceived = false;
}

// ----------------------------------------------------------------------------
// Appairage (appui long sur BOOT)
// ----------------------------------------------------------------------------
PairingReport EspNowSender::pair(uint32_t baseSeq, uint32_t timeoutMs) {
    PairingReport report;
    uint8_t myMac[6] = {};
    WiFi.macAddress(myMac);

    // Etat a restaurer si l'appairage echoue : l'association existante ne doit
    // JAMAIS etre perdue (ni en NVS, ni en RAM, ni dans la table des peers).
    const uint8_t oldChannel = _channel;

    const uint32_t nonce = esp_random();
    const MeteoPairFrame req = mhpair::makeRequest(nonce, SENSOR_NODE_ID, myMac);

    if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_peer_info_t peer{};
        memcpy(peer.peer_addr, BROADCAST_MAC, 6);
        peer.channel = 0;
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    portENTER_CRITICAL(&s_pairMux);
    s_pairTail = s_pairHead;
    portEXIT_CRITICAL(&s_pairMux);
    s_pairCapture = true;

    Serial.printf("[PAIR] Recherche d'un hub (canaux 1..%u, %u s max)\n",
                  (unsigned)PAIRING_MAX_CHANNEL, (unsigned)(timeoutMs / 1000));

    bool haveCandidate = false;
    mhpair::PairRecord candidate{};
    const uint32_t t0 = millis();
    uint32_t sweep = 0;

    // Un balayage COMPLET avant de decider : c'est ce qui permet de voir deux
    // hubs sur deux canaux differents, et donc de refuser l'ambiguite.
    while (millis() - t0 < timeoutMs) {
        sweep++;
        mhpair::ResponseCollector collector(nonce);
        auto drain = [&]() {
            while (true) {
                RxPair rx;
                portENTER_CRITICAL(&s_pairMux);
                const bool has = (s_pairTail != s_pairHead);
                if (has) { rx = s_pairRing[s_pairTail % PAIR_RING]; s_pairTail++; }
                portEXIT_CRITICAL(&s_pairMux);
                if (!has) break;
                collector.offer(rx.frame, rx.mac);
            }
        };

        for (uint8_t ch = 1; ch <= PAIRING_MAX_CHANNEL; ch++) {
            applyChannel(ch);
            for (uint8_t k = 0; k < PAIRING_REQUESTS_PER_CHANNEL; k++) {
                esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&req), sizeof(req));
                const uint32_t w0 = millis();
                while (millis() - w0 < PAIRING_REQUEST_GAP_MS) { drain(); delay(5); }
            }
        }
        drain();

        report.hubsHeard = collector.hubCount();
        const mhpair::Outcome out = collector.outcome();
        Serial.printf("[PAIR] Balayage %u : %d hub(s) entendu(s)\n",
                      (unsigned)sweep, collector.hubCount());
        if (out == mhpair::Outcome::AMBIGUOUS) {
            report.result = PairingReport::AMBIGUOUS;
            break;
        }
        if (out == mhpair::Outcome::UNIQUE) {
            candidate = collector.unique();
            haveCandidate = true;
            break;
        }
    }

    s_pairCapture = false;
    esp_now_del_peer(BROADCAST_MAC);

    if (haveCandidate) {
        // Confirmation unicast AVEC ACK sur le canal annonce par le hub : prouve la
        // liaison dans les deux sens avant d'ecrire quoi que ce soit.
        applyChannel(candidate.channel);
        delay(20); // laisse passer les derniers statuts d'envoi broadcast
        const bool sameAsOld = (memcmp(candidate.sta_mac, _destMac, 6) == 0);
        if (!esp_now_is_peer_exist(candidate.sta_mac)) {
            esp_now_peer_info_t peer{};
            memcpy(peer.peer_addr, candidate.sta_mac, 6);
            peer.channel = 0;
            peer.ifidx = WIFI_IF_STA;
            peer.encrypt = false;
            esp_now_add_peer(&peer);
        }

        const MeteoPairFrame cf = mhpair::makeConfirm(nonce, SENSOR_NODE_ID, myMac, baseSeq);
        memcpy(s_ackMac, candidate.sta_mac, 6);
        bool acked = false;
        for (uint8_t i = 0; i < 5 && !acked; i++) {
            _sendComplete = false;
            _lastDeliverySuccess = false;
            if (esp_now_send(candidate.sta_mac, reinterpret_cast<const uint8_t*>(&cf),
                             sizeof(cf)) != ESP_OK) { delay(20); continue; }
            const uint32_t w0 = millis();
            while (!_sendComplete && (millis() - w0) < ACK_WAIT_MS) delay(1);
            acked = _sendComplete && _lastDeliverySuccess;
            if (!acked) delay(20);
        }

        if (!acked) {
            report.result = PairingReport::CONFIRM_FAILED;
            if (!sameAsOld) esp_now_del_peer(candidate.sta_mac);
        } else if (!saveRecord(candidate)) {
            report.result = PairingReport::SAVE_FAILED;
            if (!sameAsOld) esp_now_del_peer(candidate.sta_mac);
        } else {
            // Bascule : l'ancien peer est retire, le nouveau hub devient LA cible.
            if (!sameAsOld && _paired) esp_now_del_peer(_destMac);
            memcpy(_destMac, candidate.sta_mac, 6);
            memcpy(_apMac, candidate.ap_mac, 6);
            _haveAp = !mhpair::isZeroMac(candidate.ap_mac);
            _paired = true;
            rtcChannel = candidate.channel;
            report.result = PairingReport::PAIRED;
            report.hub = candidate;
            memcpy(s_ackMac, _destMac, 6);
            printMac("[PAIR] Appaire au hub sta=", _destMac);
            Serial.printf(" nom=%s canal=%u reprise apres seq=%u\n",
                          candidate.name, (unsigned)candidate.channel, (unsigned)baseSeq);
            return report;
        }
    }

    // Echec : tout revient a l'etat d'avant (canal, peer, cible des ACK).
    applyChannel(oldChannel);
    if (_paired) registerHubPeer();
    memcpy(s_ackMac, _destMac, 6);
    switch (report.result) {
        case PairingReport::AMBIGUOUS:
            Serial.printf("[PAIR] Refus : %d hubs differents repondent. N'en laisser qu'un "
                          "allume et recommencer. Association inchangee.\n", report.hubsHeard);
            break;
        case PairingReport::CONFIRM_FAILED:
            Serial.println("[PAIR] Echec : le hub n'a pas accuse la confirmation. Association inchangee.");
            break;
        case PairingReport::SAVE_FAILED:
            Serial.println("[PAIR] Echec : ecriture NVS impossible. Association inchangee.");
            break;
        default:
            Serial.println("[PAIR] Aucun hub n'a repondu. Association inchangee.");
            break;
    }
    return report;
}
