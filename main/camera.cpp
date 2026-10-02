#include "camera.hpp"
#include "esp_camera.h"
#include "jpeg_decoder.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <cstdlib>
#include <cstring>

static bool ready=false;
static uint8_t *rgb=nullptr;
// Copy of the last analysed frame: evidence attached to caregiver notifications.
constexpr size_t JPEG_MAX=16*1024;
static uint8_t *jpeg_copy=nullptr;
static size_t jpeg_len=0;
static int64_t jpeg_ms=-1;
void camera_stop() {
    if(ready) esp_camera_deinit();
    ready=false; free(rgb); rgb=nullptr; free(jpeg_copy); jpeg_copy=nullptr; jpeg_len=0; jpeg_ms=-1;
}
const uint8_t *camera_last_jpeg(size_t &len, int64_t &ms) {
    len=jpeg_len; ms=jpeg_ms; return jpeg_len?jpeg_copy:nullptr;
}
esp_err_t camera_start(int profile) {
    camera_stop();
    if(profile!=1 && profile!=2) return ESP_ERR_INVALID_ARG;
    camera_config_t c{};
    c.pin_pwdn=profile==1?32:-1; c.pin_reset=-1;
    c.pin_xclk=profile==1?0:21; c.pin_sccb_sda=26; c.pin_sccb_scl=27;
    c.pin_d7=35; c.pin_d6=34; c.pin_d5=39; c.pin_d4=36;
    c.pin_d3=profile==1?21:19; c.pin_d2=profile==1?19:18;
    c.pin_d1=profile==1?18:5; c.pin_d0=profile==1?5:4;
    c.pin_vsync=25; c.pin_href=23; c.pin_pclk=22;
    c.xclk_freq_hz=20000000; c.ledc_timer=LEDC_TIMER_0; c.ledc_channel=LEDC_CHANNEL_0;
    c.pixel_format=PIXFORMAT_JPEG; c.frame_size=FRAMESIZE_QQVGA;
    c.jpeg_quality=6; c.fb_count=1;
    c.fb_location=CAMERA_FB_IN_DRAM; c.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
    esp_err_t err=esp_camera_init(&c);
    if(err!=ESP_OK) return err;
    ready=true;
    // Decoder output and photo copy prefer PSRAM: internal RAM is needed by Wi-Fi and DMA.
    rgb=static_cast<uint8_t*>(heap_caps_malloc_prefer(auditor::W*auditor::H*3,2,MALLOC_CAP_SPIRAM,MALLOC_CAP_8BIT));
    jpeg_copy=static_cast<uint8_t*>(heap_caps_malloc_prefer(JPEG_MAX,2,MALLOC_CAP_SPIRAM,MALLOC_CAP_8BIT));
    if(!rgb || !jpeg_copy) { camera_stop(); return ESP_ERR_NO_MEM; }
    vTaskDelay(pdMS_TO_TICKS(300));
    for(int i=0;i<4;++i) { auto *fb=esp_camera_fb_get(); if(!fb) { camera_stop(); return ESP_FAIL; } esp_camera_fb_return(fb); }
    return ESP_OK;
}
bool camera_gray(uint8_t *out) {
    if(!ready || !out) return false;
    // First buffer can predate the command; discard it before every observation.
    auto *old=esp_camera_fb_get(); if(!old) return false; esp_camera_fb_return(old);
    auto *fb=esp_camera_fb_get(); if(!fb) return false;
    esp_jpeg_image_cfg_t jpeg{};
    jpeg.indata=fb->buf; jpeg.indata_size=fb->len;
    jpeg.outbuf=rgb; jpeg.outbuf_size=auditor::W*auditor::H*3;
    jpeg.out_format=JPEG_IMAGE_FORMAT_RGB888; jpeg.out_scale=JPEG_IMAGE_SCALE_0;
    esp_jpeg_image_output_t info{};
    bool ok=fb->format==PIXFORMAT_JPEG && fb->width==auditor::W && fb->height==auditor::H &&
        esp_jpeg_get_image_info(&jpeg,&info)==ESP_OK && info.width==auditor::W && info.height==auditor::H &&
        info.output_len<=jpeg.outbuf_size && esp_jpeg_decode(&jpeg,&info)==ESP_OK;
    if(ok && fb->len<=JPEG_MAX) { memcpy(jpeg_copy,fb->buf,fb->len); jpeg_len=fb->len; jpeg_ms=esp_timer_get_time()/1000; }
    esp_camera_fb_return(fb);
    if(ok) for(int i=0;i<auditor::W*auditor::H;++i)
        out[i]=(77u*rgb[3*i]+150u*rgb[3*i+1]+29u*rgb[3*i+2])>>8; // esp_jpeg RGB888, no byte swap
    return ok;
}
