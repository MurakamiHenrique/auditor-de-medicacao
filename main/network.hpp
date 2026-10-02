#pragma once
#include "esp_err.h"

// Wi-Fi station with automatic reconnection and SNTP (America/Sao_Paulo, UTC-3 without DST).
// Credentials live in their own NVS namespace and are never echoed on the serial port.
esp_err_t network_init();
esp_err_t network_configure(const char *ssid, const char *password);
bool network_configured();
bool network_connected();
bool network_time_synced();
int network_rssi(); // 0 when disconnected
