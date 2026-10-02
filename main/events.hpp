#pragma once
#include "cJSON.h"
#include <cstdint>

int64_t mono(); // monotonic milliseconds since boot
bool wall_clock_set(); // civil time plausible (SNTP or manual adjustment)
cJSON *event(const char *type); // JSON event with ms/epoch, freed by emit()
void emit(cJSON *j); // one atomic JSON line on the USB serial
