#pragma once

// Le SSID sert au scan du canal 2.4 GHz. Le mot de passe n'est pas utilise
// (pas d'association STA : ESP-NOW n'en a pas besoin).

struct WifiCredential {
    const char* ssid;
    const char* password;
};

const WifiCredential WIFI_CREDENTIALS[] = {
    {"MonSSID_Maison", "MonMotDePasse"}
};
