#include "storage.hpp"
#include "nvs.h"
#include <cstring>

namespace {
constexpr uint32_t VERSION=2; // 2: late_s and enabled
// Same-build blob layout guarded by version, size and checksum. Bump on schema change.
struct Record { uint32_t version; auditor::Settings settings; auditor::Calibration calibration; uint32_t crc; };
uint32_t checksum(const void *ptr, size_t n) {
    auto *p=static_cast<const uint8_t*>(ptr); uint32_t h=2166136261u;
    while(n--) { h^=*p++; h*=16777619u; } return h;
}
}
esp_err_t save_settings(const auditor::Settings &s, const auditor::Calibration &c) {
    if(!auditor::valid(s)) return ESP_ERR_INVALID_ARG;
    Record r{}; r.version=VERSION; r.settings=s; r.calibration=c;
    r.crc=checksum(&r,offsetof(Record,crc));
    nvs_handle_t h; esp_err_t e=nvs_open("auditor",NVS_READWRITE,&h);
    if(e!=ESP_OK) return e;
    e=nvs_set_blob(h,"config",&r,sizeof(r));
    if(e==ESP_OK) e=nvs_commit(h);
    nvs_close(h); return e;
}
esp_err_t load_settings(auditor::Settings &s, auditor::Calibration &c) {
    nvs_handle_t h; esp_err_t e=nvs_open("auditor",NVS_READONLY,&h);
    if(e!=ESP_OK) return e;
    Record r{}; size_t n=sizeof(r); e=nvs_get_blob(h,"config",&r,&n); nvs_close(h);
    if(e!=ESP_OK) return e;
    if(n!=sizeof(r) || r.version!=VERSION || r.crc!=checksum(&r,offsetof(Record,crc)) || !auditor::valid(r.settings)) return ESP_ERR_INVALID_STATE;
    s=r.settings; c=r.calibration; return ESP_OK;
}
