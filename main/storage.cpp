#include "storage.hpp"
#include "nvs.h"
#include <cstring>

namespace {
constexpr uint32_t VERSION=3; // 2: late_s and enabled; 3: empty reference only, change/min_cells
// Same-build blob layout guarded by version, size and checksum. Bump on schema change.
struct Record { uint32_t version; auditor::Settings settings; auditor::Calibration calibration; uint32_t crc; };
// Version 2 layout, read once so an update keeps profile, ROI and schedule.
struct SettingsV2 {
    auditor::Roi roi; int hour, minute, window_s, interval_s, confirmations;
    float max_distance, margin, context_distance, stability; int profile, late_s; bool enabled;
};
struct CalibrationV2 { auditor::Sample present, empty; bool has_present, has_empty; };
struct RecordV2 { uint32_t version; SettingsV2 settings; CalibrationV2 calibration; uint32_t crc; };
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
    size_t n=0; e=nvs_get_blob(h,"config",nullptr,&n);
    if(e==ESP_OK && n==sizeof(RecordV2)) {
        static RecordV2 old; e=nvs_get_blob(h,"config",&old,&n); nvs_close(h);
        if(e!=ESP_OK) return e;
        if(old.version!=2 || old.crc!=checksum(&old,offsetof(RecordV2,crc))) return ESP_ERR_INVALID_STATE;
        auditor::Settings next; const auto &o=old.settings;
        next.roi=o.roi; next.hour=o.hour; next.minute=o.minute; next.window_s=o.window_s;
        next.interval_s=o.interval_s; next.confirmations=o.confirmations; next.profile=o.profile;
        next.late_s=o.late_s; next.enabled=o.enabled;
        if(!auditor::valid(next)) return ESP_ERR_INVALID_STATE;
        s=next; c=auditor::Calibration{}; return ESP_OK; // the empty reference must be recalibrated
    }
    static Record r; n=sizeof(r); if(e==ESP_OK) e=nvs_get_blob(h,"config",&r,&n); nvs_close(h);
    if(e!=ESP_OK) return e;
    if(n!=sizeof(r) || r.version!=VERSION || r.crc!=checksum(&r,offsetof(Record,crc)) || !auditor::valid(r.settings)) return ESP_ERR_INVALID_STATE;
    s=r.settings; c=r.calibration; return ESP_OK;
}
