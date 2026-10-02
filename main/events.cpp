#include "events.hpp"
#include "esp_timer.h"
#include <cstdio>
#include <ctime>

int64_t mono() { return esp_timer_get_time()/1000; }
bool wall_clock_set() { return time(nullptr)>=1704067200; } // 2024-01-01
cJSON *event(const char *type) {
    auto *j=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"event",type);
    cJSON_AddNumberToObject(j,"ms",double(mono()));
    cJSON_AddNumberToObject(j,"epoch",wall_clock_set()?double(time(nullptr)):0);
    return j;
}
void emit(cJSON *j) {
    char *text=cJSON_PrintUnformatted(j);
    // A single printf keeps lines from different tasks from interleaving.
    if(text) { printf("%s\n",text); fflush(stdout); cJSON_free(text); }
    cJSON_Delete(j);
}
