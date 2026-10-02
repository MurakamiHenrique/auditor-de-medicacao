#pragma once
#include "auditor.hpp"
#include "esp_err.h"

esp_err_t camera_start(int profile);
void camera_stop();
bool camera_gray(uint8_t *out); // out: auditor::W * auditor::H bytes
// JPEG of the last successful camera_gray() and its monotonic ms; nullptr if none.
const uint8_t *camera_last_jpeg(size_t &len, int64_t &ms);
