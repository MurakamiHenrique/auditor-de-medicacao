#include "network.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
std::atomic<bool> configured{false}, connected{false}, synced{false};
std::atomic<int> failures{0};
esp_timer_handle_t retry_timer=nullptr;

void reconnect(void *) { if(configured) esp_wifi_connect(); }
void on_event(void *, esp_event_base_t base, int32_t id, void *) {
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START) { if(configured) esp_wifi_connect(); }
    else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        connected=false;
        if(!configured || !retry_timer) return;
        // Exponential backoff up to one minute: keeps retrying forever without flooding the AP.
        int n=failures++;
        uint64_t delay_s=std::min(60,1<<std::min(n,6));
        esp_timer_stop(retry_timer);
        esp_timer_start_once(retry_timer,delay_s*1000000);
    }
    else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) { connected=true; failures=0; }
}
void on_sync(timeval *) { synced=true; }
bool fill(wifi_config_t &cfg, const char *ssid, const char *password) {
    size_t s=strlen(ssid), p=strlen(password);
    if(s<1 || s>32 || (p && (p<8 || p>63))) return false;
    cfg={};
    memcpy(cfg.sta.ssid,ssid,s); memcpy(cfg.sta.password,password,p);
    cfg.sta.threshold.authmode=p?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    return true;
}
esp_err_t load(wifi_config_t &cfg) {
    nvs_handle_t h; esp_err_t e=nvs_open("wifi",NVS_READONLY,&h);
    if(e!=ESP_OK) return e;
    char ssid[33]{}, password[64]{}; size_t n=sizeof(ssid), m=sizeof(password);
    e=nvs_get_str(h,"ssid",ssid,&n);
    if(e==ESP_OK) e=nvs_get_str(h,"password",password,&m);
    nvs_close(h);
    if(e!=ESP_OK) return e;
    return fill(cfg,ssid,password)?ESP_OK:ESP_ERR_INVALID_STATE;
}
}

esp_err_t network_init() {
    // Fixed Brazilian offset; Brazil has no daylight saving time since 2019.
    setenv("TZ","<-03>3",1); tzset();
    esp_err_t e=esp_netif_init();
    if(e==ESP_OK) e=esp_event_loop_create_default();
    if(e!=ESP_OK) return e;
    if(!esp_netif_create_default_wifi_sta()) return ESP_FAIL;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if((e=esp_wifi_init(&init))!=ESP_OK) return e;
    esp_wifi_set_storage(WIFI_STORAGE_RAM); // credentials are kept in our own namespace
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,on_event,nullptr);
    esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,on_event,nullptr);
    esp_timer_create_args_t timer{}; timer.callback=reconnect; timer.name="wifi_retry";
    if((e=esp_timer_create(&timer,&retry_timer))!=ESP_OK) return e;
    if((e=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK) return e;
    wifi_config_t cfg{};
    if(load(cfg)==ESP_OK && esp_wifi_set_config(WIFI_IF_STA,&cfg)==ESP_OK) configured=true;
    if((e=esp_wifi_start())!=ESP_OK) return e;
    esp_sntp_config_t sntp=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,ESP_SNTP_SERVER_LIST("a.st1.ntp.br","pool.ntp.org"));
    sntp.sync_cb=on_sync;
    return esp_netif_sntp_init(&sntp); // lwIP keeps retrying and resyncs periodically.
}
esp_err_t network_configure(const char *ssid, const char *password) {
    wifi_config_t cfg{};
    if(!fill(cfg,ssid,password)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t e=nvs_open("wifi",NVS_READWRITE,&h);
    if(e!=ESP_OK) return e;
    e=nvs_set_str(h,"ssid",ssid);
    if(e==ESP_OK) e=nvs_set_str(h,"password",password);
    if(e==ESP_OK) e=nvs_commit(h);
    nvs_close(h);
    if(e!=ESP_OK) return e;
    configured=false; esp_wifi_disconnect(); // stale retries must not race the new config
    if((e=esp_wifi_set_config(WIFI_IF_STA,&cfg))!=ESP_OK) return e;
    configured=true; failures=0;
    return esp_wifi_connect();
}
bool network_configured() { return configured; }
bool network_connected() { return connected; }
bool network_time_synced() { return synced; }
int network_rssi() {
    wifi_ap_record_t ap{};
    return connected && esp_wifi_sta_get_ap_info(&ap)==ESP_OK?ap.rssi:0;
}
