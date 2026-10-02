#pragma once
#include "esp_err.h"
#include <cstddef>
#include <cstdint>

// Telegram Bot API notifications sent by a background task, so HTTPS never delays sampling.
// Messages wait for Wi-Fi and are retried with backoff; credentials are never echoed.
esp_err_t notify_init();
esp_err_t notify_configure(const char *token, const char *chat_id);
bool notify_configured();
// Copies text and optional JPEG; false when the queue is full or memory is short.
bool notify_send(const char *text, const uint8_t *jpeg = nullptr, size_t jpeg_len = 0);
int notify_pending();
int notify_last_status(); // last HTTP status (0 = none yet, -1 = transport error)
