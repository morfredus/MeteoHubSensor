#include "display_manager.h"

#if defined(SENSOR_HAS_OLED)
// ============================================================================
// Implementation reelle : ESP32-C3 HW-675, OLED SSD1306 72x40 via U8g2.
// ============================================================================
#include <U8g2lib.h>
#include <Wire.h>
#include <cstdio>

// Pilote HW I2C : l'ecran partage le bus Wire avec les capteurs. On NE choisit
// PAS un pilote SW I2C (bit-bang) : il reconfigurerait le multiplexage des
// broches I2C a chaque trame et casserait les lectures HW des capteurs sur le
// cycle suivant. Un seul maitre I2C materiel pour tout le bus.
//
// Le format depend de la carte : 0.42" 72x40 sur le C3 HW-675, 0.96" 128x64 sur
// le S3 (ecran externe). Meme API U8g2, seul le constructeur change.
#if defined(SENSOR_OLED_128X64)
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);
#else
static U8G2_SSD1306_72X40_ER_F_HW_I2C s_u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);
#endif

void DisplayManager::begin() {
    // Le bus est deja ouvert par SensorManager (Wire.begin(GP5, GP6)). On le
    // re-affirme avec les MEMES broches : sur ESP32, Wire.begin conserve les
    // broches deja configurees, l'appel est donc sans effet de bord ici, mais
    // garantit un ecran fonctionnel meme si l'ordre d'init changeait.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    s_u8g2.setI2CAddress(OLED_I2C_ADDRESS << 1);
    _ready = s_u8g2.begin();
    if (_ready) {
        s_u8g2.setBusClock(400000);
        s_u8g2.clearBuffer();
        s_u8g2.sendBuffer();
    }
}

void DisplayManager::showSplash(const char* version) {
    if (!_ready) return;
    char ver[20] = "";
    if (version && *version) snprintf(ver, sizeof(ver), "v%s", version);

    s_u8g2.clearBuffer();
#if defined(SENSOR_OLED_128X64)
    s_u8g2.setFont(u8g2_font_9x15B_tf);
    s_u8g2.drawStr(0, 22, "MeteoHub");
    s_u8g2.drawStr(0, 42, "Sensor");
    s_u8g2.setFont(u8g2_font_6x10_tf);
    if (*ver) s_u8g2.drawStr(0, 60, ver);
#else
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr(0, 12, "MeteoHub");
    s_u8g2.drawStr(0, 24, "Sensor");
    s_u8g2.setFont(u8g2_font_5x7_tf);
    if (*ver) s_u8g2.drawStr(0, 38, ver);
#endif
    s_u8g2.sendBuffer();
}

void DisplayManager::showReading(const MeteoPacket& packet, bool txOk, uint8_t channel) {
    if (!_ready) return;

    // Chaines communes aux deux formats : on les prepare une fois, chaque
    // disposition ne fait plus que placer le texte.
    char tStr[16], hStr[16], pStr[16], statusStr[24];
    if (packet.valid_fields & FIELD_TEMPERATURE)
        snprintf(tStr, sizeof(tStr), "T %.1fC", packet.temperature);
    else
        snprintf(tStr, sizeof(tStr), "T --.-C");
    if (packet.valid_fields & FIELD_HUMIDITY)
        snprintf(hStr, sizeof(hStr), "H %.0f%%", packet.humidity);
    else
        snprintf(hStr, sizeof(hStr), "H --%%");
    if (packet.valid_fields & FIELD_PRESSURE)
        snprintf(pStr, sizeof(pStr), "P %.0f hPa", packet.pressure);
    else
        snprintf(pStr, sizeof(pStr), "P ---- hPa");
    if (packet.valid_fields & FIELD_BATTERY)
        snprintf(statusStr, sizeof(statusStr), "ch%u  %s  bat%u%%", channel,
                 txOk ? "TX OK" : "TX XX", packet.battery_percent);
    else
        snprintf(statusStr, sizeof(statusStr), "ch%u  %s", channel,
                 txOk ? "TX OK" : "TX XX");

    s_u8g2.clearBuffer();
#if defined(SENSOR_OLED_128X64)
    // 128x64 : en-tete + T/H en gros, pression et etat en petit.
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr(0, 9, "Sonde ext.");
    s_u8g2.setFont(u8g2_font_9x15_tf);
    s_u8g2.drawStr(0, 27, tStr);
    s_u8g2.drawStr(0, 44, hStr);
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr(0, 55, pStr);
    s_u8g2.drawStr(0, 64, statusStr);
#else
    // 72x40 : disposition compacte, une grandeur par ligne. La ligne d'etat
    // complete deborderait sur 72 px : on la reduit ici.
    char statusSmall[16];
    if (packet.valid_fields & FIELD_BATTERY)
        snprintf(statusSmall, sizeof(statusSmall), "c%u %s b%u", channel,
                 txOk ? "OK" : "XX", packet.battery_percent);
    else
        snprintf(statusSmall, sizeof(statusSmall), "c%u %s", channel,
                 txOk ? "OK" : "XX");
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr(0, 10, tStr);
    s_u8g2.drawStr(0, 20, hStr);
    s_u8g2.setFont(u8g2_font_5x7_tf);
    s_u8g2.drawStr(0, 30, pStr);
    s_u8g2.drawStr(0, 39, statusSmall);
    (void)statusStr;
#endif
    s_u8g2.sendBuffer();
}

void DisplayManager::showMessage(const char* line1, const char* line2) {
    if (!_ready) return;
    s_u8g2.clearBuffer();
    s_u8g2.setFont(u8g2_font_6x10_tf);
    if (line1) s_u8g2.drawStr(0, 16, line1);
    if (line2) s_u8g2.drawStr(0, 30, line2);
    s_u8g2.sendBuffer();
}

#else
// ============================================================================
// Cible sans ecran (ESP32-S3 Super Mini) : no-op. Le firmware appelle
// DisplayManager sans condition ; ici rien ne se passe, available() = false.
// ============================================================================
void DisplayManager::begin() { _ready = false; }
void DisplayManager::showSplash(const char*) {}
void DisplayManager::showReading(const MeteoPacket&, bool, uint8_t) {}
void DisplayManager::showMessage(const char*, const char*) {}

#endif
