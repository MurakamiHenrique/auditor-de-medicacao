#include "notify.hpp"
#include "events.hpp"
#include "network.hpp"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
struct Item { char text[512]; uint8_t *jpeg; size_t len; };
struct Part { const void *data; size_t len; };
constexpr int ATTEMPTS=20;
constexpr char BOUNDARY[]="auditor7d3f9b2e";
QueueHandle_t queue=nullptr;
SemaphoreHandle_t lock=nullptr;
char token[64]{}, chat[24]{};
std::atomic<int> pending{0}, last_status{0};

bool valid_token(const char *t) {
    const char *colon=strchr(t,':'); size_t n=strlen(t);
    if(!colon || colon==t || n>=sizeof(token) || n-(colon-t)<20) return false;
    // "<bot id digits>:<secret of letters, digits, _ or ->"
    for(const char *p=t;p<colon;++p) if(!isdigit((unsigned char)*p)) return false;
    for(const char *p=colon+1;*p;++p) if(!isalnum((unsigned char)*p) && *p!='_' && *p!='-') return false;
    return true;
}
bool valid_chat(const char *c) {
    size_t n=strlen(c), i=c[0]=='-';
    if(n<=i || n>=sizeof(chat)) return false;
    for(;i<n;++i) if(!isdigit((unsigned char)c[i])) return false;
    return true;
}
void report(int status, int attempt, bool photo, bool done) {
    auto *j=event("notify");
    cJSON_AddNumberToObject(j,"status",status); cJSON_AddNumberToObject(j,"attempt",attempt);
    cJSON_AddBoolToObject(j,"photo",photo); cJSON_AddBoolToObject(j,"delivered",status==200);
    cJSON_AddBoolToObject(j,"final",done); cJSON_AddNumberToObject(j,"pending",pending.load());
    emit(j);
}
// POST the parts as one body. Returns the HTTP status or -1 on transport failure.
int post(const char *tok, const char *method, const char *type, const Part *parts, int count) {
    char url[128]; snprintf(url,sizeof(url),"https://api.telegram.org/bot%s/%s",tok,method);
    esp_http_client_config_t cfg{};
    cfg.url=url; cfg.method=HTTP_METHOD_POST; cfg.timeout_ms=20000;
    cfg.crt_bundle_attach=esp_crt_bundle_attach;
    auto client=esp_http_client_init(&cfg);
    if(!client) return -1;
    esp_http_client_set_header(client,"Content-Type",type);
    size_t total=0; for(int i=0;i<count;++i) total+=parts[i].len;
    int status=-1;
    if(esp_http_client_open(client,int(total))==ESP_OK) {
        bool ok=true;
        for(int i=0;ok && i<count;++i) {
            auto *p=static_cast<const char*>(parts[i].data); size_t left=parts[i].len;
            while(ok && left) { int n=esp_http_client_write(client,p,int(left)); ok=n>0; if(ok) { p+=n; left-=size_t(n); } }
        }
        if(ok && esp_http_client_fetch_headers(client)>=0) {
            status=esp_http_client_get_status_code(client);
            char sink[64]; while(esp_http_client_read(client,sink,sizeof(sink))>0) {}
        }
    }
    esp_http_client_close(client); esp_http_client_cleanup(client);
    return status;
}
int deliver(const Item &item, const char *tok, const char *to) {
    if(!item.jpeg) {
        auto *j=cJSON_CreateObject();
        cJSON_AddStringToObject(j,"chat_id",to); cJSON_AddStringToObject(j,"text",item.text);
        cJSON_AddStringToObject(j,"parse_mode","HTML");
        char *body=cJSON_PrintUnformatted(j); cJSON_Delete(j);
        if(!body) return -1;
        Part part{body,strlen(body)};
        int status=post(tok,"sendMessage","application/json",&part,1);
        cJSON_free(body); return status;
    }
    static char head[1024];
    int n=snprintf(head,sizeof(head),
        "--%s\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"parse_mode\"\r\n\r\nHTML\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n%s\r\n"
        "--%s\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"compartimento.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n",BOUNDARY,to,BOUNDARY,BOUNDARY,item.text,BOUNDARY);
    if(n<0 || n>=int(sizeof(head))) return -1;
    char tail[32]; int t=snprintf(tail,sizeof(tail),"\r\n--%s--\r\n",BOUNDARY);
    char type[64]; snprintf(type,sizeof(type),"multipart/form-data; boundary=%s",BOUNDARY);
    Part parts[]={{head,size_t(n)},{item.jpeg,item.len},{tail,size_t(t)}};
    return post(tok,"sendPhoto",type,parts,3);
}
void task(void *) {
    Item item;
    while(true) {
        if(xQueueReceive(queue,&item,portMAX_DELAY)!=pdTRUE) continue;
        for(int attempt=1;;++attempt) {
            // Wait for Wi-Fi without consuming attempts: an alert is still useful when late.
            while(!network_connected()) vTaskDelay(pdMS_TO_TICKS(2000));
            char tok[sizeof(token)], to[sizeof(chat)];
            xSemaphoreTake(lock,portMAX_DELAY); strcpy(tok,token); strcpy(to,chat); xSemaphoreGive(lock);
            int status=*tok && *to?deliver(item,tok,to):401;
            last_status=status;
            // 4xx other than rate limiting means wrong token/chat: retrying cannot help.
            bool done=status==200 || (status>=400 && status<500 && status!=429) || attempt>=ATTEMPTS;
            if(done) --pending;
            report(status,attempt,item.jpeg!=nullptr,done);
            if(done) break;
            vTaskDelay(pdMS_TO_TICKS(1000*std::min(300,5<<std::min(attempt,6))));
        }
        free(item.jpeg);
    }
}
}

esp_err_t notify_init() {
    lock=xSemaphoreCreateMutex();
    queue=xQueueCreate(8,sizeof(Item));
    if(!lock || !queue) return ESP_ERR_NO_MEM;
    nvs_handle_t h;
    if(nvs_open("telegram",NVS_READONLY,&h)==ESP_OK) {
        char tok[sizeof(token)]{}, to[sizeof(chat)]{}; size_t n=sizeof(tok), m=sizeof(to);
        if(nvs_get_str(h,"token",tok,&n)==ESP_OK && nvs_get_str(h,"chat",to,&m)==ESP_OK &&
           valid_token(tok) && valid_chat(to)) { strcpy(token,tok); strcpy(chat,to); }
        nvs_close(h);
    }
    // TLS handshakes need a large stack; lower priority than sampling.
    return xTaskCreate(task,"telegram",10240,nullptr,3,nullptr)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t notify_configure(const char *tok, const char *to) {
    if(!lock || !valid_token(tok) || !valid_chat(to)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t e=nvs_open("telegram",NVS_READWRITE,&h);
    if(e!=ESP_OK) return e;
    e=nvs_set_str(h,"token",tok);
    if(e==ESP_OK) e=nvs_set_str(h,"chat",to);
    if(e==ESP_OK) e=nvs_commit(h);
    nvs_close(h);
    if(e!=ESP_OK) return e;
    xSemaphoreTake(lock,portMAX_DELAY); strcpy(token,tok); strcpy(chat,to); xSemaphoreGive(lock);
    return ESP_OK;
}
bool notify_configured() {
    if(!lock) return false;
    xSemaphoreTake(lock,portMAX_DELAY); bool ok=*token && *chat; xSemaphoreGive(lock);
    return ok;
}
bool notify_send(const char *text, const uint8_t *jpeg, size_t jpeg_len) {
    if(!queue || !notify_configured()) return false;
    Item item{}; snprintf(item.text,sizeof(item.text),"%s",text);
    if(jpeg && jpeg_len) {
        item.jpeg=static_cast<uint8_t*>(malloc(jpeg_len));
        if(item.jpeg) { memcpy(item.jpeg,jpeg,jpeg_len); item.len=jpeg_len; } // else text only
    }
    ++pending;
    if(xQueueSend(queue,&item,0)==pdTRUE) return true;
    --pending; free(item.jpeg); return false;
}
int notify_pending() { return pending; }
int notify_last_status() { return last_status; }
