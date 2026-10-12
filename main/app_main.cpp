#include "auditor.hpp"
#include "camera.hpp"
#include "events.hpp"
#include "network.hpp"
#include "notify.hpp"
#include "storage.hpp"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "nvs_flash.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

using namespace auditor;
namespace {
Settings settings;
Calibration calibration;
Cycle cycle;
enum class Clock { None, Manual, Sntp };
Clock clock_source=Clock::None;
bool camera_ready=false, storage_ready=false, network_ready=false;
bool resume_today=true; // boot/config may resume today's open window; a finished cycle may not
bool demo=false, resumed=false, boot_notified=false, was_connected=false;
State last_result=State::Idle;
int64_t last_result_epoch=0, last_camera_retry=0;
QueueHandle_t commands;
struct Command { char text[512]; };
static uint8_t gray[W*H];

bool clock_valid() { return clock_source!=Clock::None; }
const char *clock_name() { return clock_source==Clock::Sntp?"sntp":clock_source==Clock::Manual?"manual":"none"; }
bool ready() { return clock_valid() && camera_ready && calibrated(calibration,settings); }
int64_t local_seconds() {
    time_t t=time(nullptr); tm local{}; localtime_r(&t,&local);
    return local.tm_hour*3600+local.tm_min*60+local.tm_sec;
}
// Wall-clock epoch for a monotonic instant; 0 without valid time.
int64_t epoch_at(int64_t ms) { return clock_valid()?int64_t(time(nullptr))+(ms-mono())/1000:0; }
void clock_text(char *out, size_t n, int64_t epoch, const char *format) {
    time_t t=time_t(epoch); tm local{}; localtime_r(&t,&local); strftime(out,n,format,&local);
}
const char *reset_text() {
    switch(esp_reset_reason()) {
        case ESP_RST_POWERON: return "energia ligada";
        case ESP_RST_BROWNOUT: return "queda de tensão (brownout): verifique a fonte";
        case ESP_RST_PANIC: return "erro de software";
        case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
        case ESP_RST_SW: return "reinício por software";
        case ESP_RST_EXT: return "pino de reset";
        default: return "outro";
    }
}
void reply(cJSON *request, bool ok, const char *message) {
    auto *j=event("reply"); auto *id=cJSON_GetObjectItemCaseSensitive(request,"id");
    if(cJSON_IsNumber(id)) cJSON_AddNumberToObject(j,"id",id->valuedouble);
    cJSON_AddBoolToObject(j,"ok",ok); cJSON_AddStringToObject(j,"message",message); emit(j);
}
void status() {
    auto *j=event("status");
    int64_t now=mono();
    cJSON_AddStringToObject(j,"state",name(cycle.state));
    cJSON_AddStringToObject(j,"last_result",name(last_result));
    cJSON_AddNumberToObject(j,"last_result_epoch",double(last_result_epoch));
    cJSON_AddBoolToObject(j,"demo",demo);
    cJSON_AddBoolToObject(j,"clock_valid",clock_valid());
    cJSON_AddStringToObject(j,"clock_source",clock_name());
    cJSON_AddBoolToObject(j,"camera_ready",camera_ready);
    cJSON_AddBoolToObject(j,"storage_ready",storage_ready);
    cJSON_AddBoolToObject(j,"calibrated",calibrated(calibration,settings));
    cJSON_AddBoolToObject(j,"has_empty",calibration.has_empty);
    cJSON_AddNumberToObject(j,"noise",calibration.noise);
    cJSON_AddNumberToObject(j,"threshold",cell_threshold(calibration,settings));
    cJSON_AddBoolToObject(j,"wifi_configured",network_configured());
    cJSON_AddBoolToObject(j,"wifi_connected",network_connected());
    cJSON_AddNumberToObject(j,"rssi",network_rssi());
    cJSON_AddBoolToObject(j,"telegram_configured",notify_configured());
    cJSON_AddNumberToObject(j,"notify_pending",notify_pending());
    cJSON_AddNumberToObject(j,"notify_last_status",notify_last_status());
    cJSON_AddNumberToObject(j,"heap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(j,"min_heap",esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(j,"internal_heap",heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(j,"internal_block",heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    bool pending=cycle.state==State::Armed || cycle.sampling(now);
    cJSON_AddNumberToObject(j,"start_ms",pending?double(cycle.start()):0);
    cJSON_AddNumberToObject(j,"deadline_ms",pending?double(cycle.deadline()):0);
    cJSON_AddNumberToObject(j,"late_deadline_ms",pending?double(cycle.late_deadline()):0);
    cJSON_AddNumberToObject(j,"next_epoch",cycle.state==State::Armed?double(epoch_at(cycle.start())):0);
    auto *s=cJSON_AddObjectToObject(j,"settings");
    cJSON_AddNumberToObject(s,"profile",settings.profile);
    cJSON_AddBoolToObject(s,"enabled",settings.enabled);
    cJSON_AddNumberToObject(s,"hour",settings.hour); cJSON_AddNumberToObject(s,"minute",settings.minute);
    cJSON_AddNumberToObject(s,"window_s",settings.window_s); cJSON_AddNumberToObject(s,"interval_s",settings.interval_s);
    cJSON_AddNumberToObject(s,"late_s",settings.late_s);
    cJSON_AddNumberToObject(s,"confirmations",settings.confirmations);
    cJSON_AddNumberToObject(s,"change",settings.change); cJSON_AddNumberToObject(s,"margin",settings.margin);
    cJSON_AddNumberToObject(s,"min_cells",settings.min_cells);
    cJSON_AddNumberToObject(s,"context_distance",settings.context_distance); cJSON_AddNumberToObject(s,"stability",settings.stability);
    int roi[]={settings.roi.x,settings.roi.y,settings.roi.w,settings.roi.h};
    cJSON_AddItemToObject(s,"roi",cJSON_CreateIntArray(roi,4)); emit(j);
}
// Weekday and date in Portuguese ("sáb 12/10"); newlib only knows English names.
void day_text(char *out, size_t n, int64_t epoch) {
    static const char *days[]={"dom","seg","ter","qua","qui","sex","sáb"};
    time_t t=time_t(epoch); tm local{}; localtime_r(&t,&local);
    snprintf(out,n,"%s %02d/%02d",days[local.tm_wday%7],local.tm_mday,local.tm_mon+1);
}
// Messages use Telegram HTML (<b>, <i>) and only text generated here, so nothing needs escaping.
// Photo only for results the caregiver must check; never older than the current window.
void send_result(State state) {
    char dose[8], at[8], text[500];
    clock_text(dose,sizeof(dose),epoch_at(cycle.start()),"%H:%M");
    if(!resumed && !demo) snprintf(dose,sizeof(dose),"%02d:%02d",settings.hour,settings.minute);
    clock_text(at,sizeof(at),epoch_at(mono()),"%H:%M");
    const char *test=demo?"\U0001f9ea <i>Teste de demonstração</i>\n":"";
    const char *note=resumed?"\n↩️ <i>Janela retomada após reinício do aparelho.</i>":"";
    bool photo=false;
    switch(state) {
        case State::Removed:
            snprintf(text,sizeof(text),"%s✅ <b>Dose tomada</b>\n\U0001f48a Dose das %s\n\U0001f552 Retirada confirmada às %s%s",
                     test,dose,at,note); break;
        case State::LateRemoved:
            snprintf(text,sizeof(text),"%s✅ <b>Dose tomada com atraso</b>\n\U0001f48a Dose das %s\n\U0001f552 Retirada confirmada às %s",
                     test,dose,at); break;
        case State::NotLoaded:
            snprintf(text,sizeof(text),"%s⚠️ <b>Compartimento vazio no horário</b>\n\U0001f48a Dose das %s\n"
                "\U0001f4ed A dose não foi reposta ou foi retirada antes da hora. Confira a foto.%s",test,dose,note);
            photo=true; break;
        case State::Alert: {
            char later[120]="";
            if(settings.late_s) snprintf(later,sizeof(later),"\n\U0001f440 Continuo observando por mais %d min e aviso se ela for retirada.",
                                         std::max(1,settings.late_s/60));
            snprintf(text,sizeof(text),"%s\U0001f514 <b>Dose NÃO tomada</b>\n\U0001f48a Dose das %s\n⏰ Ainda no compartimento às %s%s%s",
                     test,dose,at,later,note);
            photo=true; break;
        }
        default:
            snprintf(text,sizeof(text),"%s❌ <b>Não foi possível verificar</b>\n\U0001f48a Dose das %s\n"
                "\U0001f4f7 Sem imagem confiável (mão na frente, luz ou caixa fora do lugar). Confira a foto.%s",test,dose,note);
            photo=true; break;
    }
    size_t len=0; int64_t taken=-1;
    const uint8_t *jpeg=photo?camera_last_jpeg(len,taken):nullptr;
    if(taken<cycle.start()) jpeg=nullptr;
    if(!notify_send(text,jpeg,len)) { auto *j=event("notify"); cJSON_AddStringToObject(j,"error","Telegram nao configurado ou fila cheia"); emit(j); }
}
// The window opened: the caregiver sees that the schedule fired at the programmed time.
void send_start() {
    char dose[8], length[16], text[300];
    clock_text(dose,sizeof(dose),epoch_at(cycle.start()),"%H:%M");
    if(settings.window_s<120) snprintf(length,sizeof(length),"%d s",settings.window_s);
    else snprintf(length,sizeof(length),"%d min",settings.window_s/60);
    snprintf(text,sizeof(text),"%s⏰ <b>Hora da dose: %s</b>\n\U0001f4f7 Verificando o compartimento por até %s.%s",
             demo?"\U0001f9ea <i>Teste de demonstração</i>\n":"",dose,length,
             resumed?"\n↩️ <i>Janela retomada após reinício do aparelho.</i>":"");
    notify_send(text);
}
void send_boot_notice() {
    char text[500], clock[48]="sem hora", next[48]="—", day[16], hm[8];
    if(clock_valid()) {
        int64_t now=epoch_at(mono());
        day_text(day,sizeof(day),now); clock_text(hm,sizeof(hm),now,"%H:%M");
        snprintf(clock,sizeof(clock),"%s, %s (%s)",day,hm,clock_source==Clock::Sntp?"internet":"ajuste manual");
    }
    if(cycle.state==State::Armed) {
        int64_t at=epoch_at(cycle.start());
        day_text(day,sizeof(day),at); clock_text(hm,sizeof(hm),at,"%H:%M");
        snprintf(next,sizeof(next),"%s às %s",day,hm);
    }
    const char *missing=!clock_valid()?"sem hora (confira o Wi-Fi)":!camera_ready?"câmera indisponível":
        !calibrated(calibration,settings)?"falta calibrar o compartimento VAZIO":!settings.enabled?"agenda desativada":nullptr;
    if(missing) snprintf(text,sizeof(text),"\U0001f50c <b>Auditor ligado</b>\n\U0001f552 Relógio: %s\n\U0001f501 Motivo: %s\n⚠️ <b>Atenção:</b> %s",
                         clock,reset_text(),missing);
    else snprintf(text,sizeof(text),"\U0001f50c <b>Auditor ligado</b>\n\U0001f552 Relógio: %s\n\U0001f501 Motivo: %s\n"
                  "✅ Pronto. Próxima verificação: <b>%s</b>",clock,reset_text(),next);
    notify_send(text);
}
void report_transition(State old) {
    if(old==cycle.state) return;
    auto *j=event("transition"); cJSON_AddStringToObject(j,"from",name(old));
    cJSON_AddStringToObject(j,"state",name(cycle.state));
    cJSON_AddBoolToObject(j,"demo",demo); cJSON_AddBoolToObject(j,"resumed",resumed); emit(j);
    if(cycle.state==State::Monitoring) send_start();
    if(notifiable(cycle.state)) {
        last_result=cycle.state; last_result_epoch=epoch_at(mono());
        send_result(cycle.state);
    }
    status();
}
// Arms the next daily window whenever nothing is pending.
void schedule() {
    int64_t now=mono();
    if(!settings.enabled || !ready() || !cycle.finished(now)) return;
    int64_t offset=next_window_offset(local_seconds(),settings);
    if(offset<0 && !resume_today) offset+=DAY_S;
    int64_t start=now+offset*1000, end=start+int64_t(settings.window_s)*1000;
    if(cycle.arm(now,std::max(start,now),end,settings)) {
        resumed=offset<0; demo=false; resume_today=false; status();
    }
}
// Configuration changed: drop a waiting window so schedule() recomputes it.
void reschedule() { if(!cycle.sampling(mono())) { cycle.cancel(); resume_today=true; demo=false; } }
bool take(Sample &sample) {
    return camera_ready && camera_gray(gray) && extract(gray,sizeof(gray),settings.roi,sample);
}
Result observation() {
    int64_t start=mono(); Sample a,b; Result r;
    bool captured=take(a) && take(b);
    float motion_roi=captured?distance(a.roi.data(),b.roi.data(),N):255;
    float motion_context=captured?context_distance(a,b):255;
    bool stable=captured && motion_roi<=settings.stability && motion_context<=settings.stability;
    if(stable) r=classify(b,calibration,settings);
    auto *j=event("observation");
    cJSON_AddStringToObject(j,"label",name(r.label));
    cJSON_AddBoolToObject(j,"captured",captured); cJSON_AddBoolToObject(j,"stable",stable);
    cJSON_AddNumberToObject(j,"blob",r.change.blob); cJSON_AddNumberToObject(j,"cells",r.change.cells);
    cJSON_AddNumberToObject(j,"min_cells",settings.min_cells);
    cJSON_AddNumberToObject(j,"change",r.change.top); cJSON_AddNumberToObject(j,"threshold",r.threshold);
    cJSON_AddNumberToObject(j,"offset",r.change.offset); cJSON_AddNumberToObject(j,"distance_context",r.context);
    cJSON_AddNumberToObject(j,"motion_roi",motion_roi); cJSON_AddNumberToObject(j,"motion_context",motion_context);
    cJSON_AddNumberToObject(j,"duration_ms",double(mono()-start)); emit(j); return r;
}
// Fills `why` with the failed check and its measured values.
// Test reading sent to the caregiver chat, with the photo that was just analysed.
bool send_sample(const Result &r, int64_t since) {
    char at[8], text[400];
    clock_text(at,sizeof(at),epoch_at(mono()),"%H:%M");
    const char *what=r.label==Observation::Present?"\U0001f48a <b>Dose PRESENTE</b> no compartimento":
        r.label==Observation::Empty?"\U0001f4ed <b>Compartimento VAZIO</b>":
        "❔ <b>INCONCLUSIVA</b> (imagem instável, escura, cena ou luz diferente da calibração)";
    snprintf(text,sizeof(text),"\U0001f50e <b>Leitura de teste</b> · %s\n%s\n"
             "<i>Mancha de %d células (mínimo %d) · diferença %.0f (limite %.0f)</i>",
             at,what,r.change.blob,settings.min_cells,r.change.top,r.threshold);
    size_t len=0; int64_t taken=-1;
    const uint8_t *jpeg=camera_last_jpeg(len,taken);
    return notify_send(text,taken>=since?jpeg:nullptr,len);
}
// Averages five stable frames. `noise` gets the worst per-frame noise against that average.
bool collect(Sample &out, float &noise, char *why, size_t n) {
    Sample first,current;
    static Feature frames[5];
    if(!take(first)) { snprintf(why,n,"Camera sem imagem: confira perfil e cabo"); return false; }
    if(!first.quality) { snprintf(why,n,"Imagem escura ou saturada: ajuste a iluminacao"); return false; }
    uint32_t sums[N]{}, guard[GUARD]{};
    for(int k=0;k<5;++k) {
        esp_task_wdt_reset();
        if(!take(current)) { snprintf(why,n,"Camera sem imagem: confira perfil e cabo"); return false; }
        if(!current.quality) { snprintf(why,n,"Imagem escura ou saturada: ajuste a iluminacao"); return false; }
        float roi=distance(current.roi.data(),first.roi.data(),N), around=context_distance(current,first);
        if(roi>settings.stability || around>settings.stability) {
            snprintf(why,n,"Cena mudou durante a calibracao (ROI %.1f, entorno %.1f; limite %.0f): fixe camera e caixa e afaste as maos",
                     roi,around,settings.stability);
            return false;
        }
        frames[k]=current.roi;
        for(int i=0;i<N;++i) sums[i]+=current.roi[i];
        for(int i=0;i<GUARD;++i) guard[i]+=current.context[i];
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    for(int i=0;i<N;++i) out.roi[i]=uint8_t(sums[i]/5);
    for(int i=0;i<GUARD;++i) out.context[i]=uint8_t(guard[i]/5);
    out.context_count=first.context_count;
    noise=0;
    for(auto &frame:frames) noise=std::max(noise,noise_level(frame,out.roi));
    out.quality=true; return true;
}
bool number(cJSON *j,const char *key,int &out) {
    auto *v=cJSON_GetObjectItemCaseSensitive(j,key); if(!v) return true;
    if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<-100000 || v->valuedouble>100000 || std::floor(v->valuedouble)!=v->valuedouble) return false;
    out=int(v->valuedouble); return true;
}
bool number(cJSON *j,const char *key,float &out) {
    auto *v=cJSON_GetObjectItemCaseSensitive(j,key); if(!v) return true;
    if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble)) return false;
    out=float(v->valuedouble); return true;
}
bool flag(cJSON *j,const char *key,bool &out) {
    auto *v=cJSON_GetObjectItemCaseSensitive(j,key); if(!v) return true;
    if(!cJSON_IsBool(v)) return false;
    out=cJSON_IsTrue(v); return true;
}
const char *text(cJSON *j,const char *key) {
    auto *v=cJSON_GetObjectItemCaseSensitive(j,key);
    return cJSON_IsString(v)?v->valuestring:nullptr;
}
bool update(cJSON *j,Settings &s) {
    if(!number(j,"profile",s.profile) || !number(j,"hour",s.hour) || !number(j,"minute",s.minute) ||
       !number(j,"window_s",s.window_s) || !number(j,"interval_s",s.interval_s) || !number(j,"confirmations",s.confirmations) ||
       !number(j,"late_s",s.late_s) || !flag(j,"enabled",s.enabled) ||
       !number(j,"change",s.change) || !number(j,"min_cells",s.min_cells) || !number(j,"margin",s.margin) ||
       !number(j,"context_distance",s.context_distance) || !number(j,"stability",s.stability)) return false;
    auto *roi=cJSON_GetObjectItemCaseSensitive(j,"roi");
    if(roi) {
        if(!cJSON_IsArray(roi) || cJSON_GetArraySize(roi)!=4) return false;
        int a[4];
        for(int i=0;i<4;++i) { auto *v=cJSON_GetArrayItem(roi,i);
            if(!cJSON_IsNumber(v) || v->valuedouble<0 || v->valuedouble>W || std::floor(v->valuedouble)!=v->valuedouble) return false;
            a[i]=int(v->valuedouble);
        }
        s.roi={a[0],a[1],a[2],a[3]};
    }
    return valid(s);
}
void snapshot(cJSON *req) {
    if(!camera_ready || !camera_gray(gray)) { reply(req,false,"Falha de captura"); return; }
    size_t len=0;
    auto *encoded=static_cast<unsigned char*>(malloc(sizeof(gray)*4/3+8));
    if(!encoded) { reply(req,false,"Sem memoria"); return; }
    int rc=mbedtls_base64_encode(encoded,sizeof(gray)*4/3+8,&len,gray,sizeof(gray));
    if(rc==0) {
        // Base64 contains no JSON metacharacters; avoid two extra 25 KiB copies.
        printf("{\"event\":\"image\",\"ms\":%lld,\"epoch\":%lld,\"width\":%d,\"height\":%d,\"gray_base64\":\"%s\"}\n",
               static_cast<long long>(mono()),static_cast<long long>(wall_clock_set()?time(nullptr):0),W,H,
               reinterpret_cast<char*>(encoded));
        fflush(stdout);
    }
    free(encoded); reply(req,rc==0,rc==0?"Imagem USB enviada":"Erro base64");
}
void handle(const char *input) {
    const char *end=nullptr;
    auto *j=cJSON_ParseWithOpts(input,&end,true);
    auto *cmd=cJSON_GetObjectItemCaseSensitive(j,"cmd");
    if(!cJSON_IsObject(j) || !cJSON_IsString(cmd)) { reply(j,false,"JSON invalido: use cmd"); cJSON_Delete(j); return; }
    const char *op=cmd->valuestring;
    if(!strcmp(op,"status")) { status(); reply(j,true,"Estado enviado"); }
    else if(!strcmp(op,"cancel")) {
        cycle.cancel(); demo=false; resume_today=false; status();
        reply(j,true,"Verificacao cancelada; a agenda diaria continua (use enabled=false para pausar)");
    }
    else if(cycle.sampling(mono())) reply(j,false,"Verificacao em andamento: apenas status ou cancel");
    else if(!strcmp(op,"time")) {
        auto *v=cJSON_GetObjectItemCaseSensitive(j,"epoch");
        if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<1704067200 || v->valuedouble>4102444800 || std::floor(v->valuedouble)!=v->valuedouble)
            reply(j,false,"epoch deve ser inteiro Unix UTC entre 2024 e 2100");
        else if(clock_source==Clock::Sntp) reply(j,true,"Hora ja sincronizada por SNTP; ajuste manual ignorado");
        else {
            timeval tv{time_t(v->valuedouble),0};
            bool ok=settimeofday(&tv,nullptr)==0;
            if(ok) { clock_source=Clock::Manual; reschedule(); }
            reply(j,ok,"Relogio ajustado manualmente; SNTP substitui quando houver Wi-Fi");
        }
    }
    else if(!strcmp(op,"set")) {
        Settings next=settings;
        if(!update(j,next)) reply(j,false,"Parametros invalidos");
        else {
            Calibration refs=calibration;
            bool changed=next.profile!=settings.profile || !same_roi(next.roi,settings.roi);
            if(changed) refs=Calibration{};
            esp_err_t e=storage_ready?save_settings(next,refs):ESP_ERR_INVALID_STATE;
            if(e!=ESP_OK) reply(j,false,"Falha NVS; alteracoes nao aplicadas");
            else {
                bool restart=next.profile!=settings.profile || !camera_ready;
                settings=next; calibration=refs; reschedule();
                if(restart) camera_ready=camera_start(settings.profile)==ESP_OK;
                reply(j,true,camera_ready?"Salvo":"Salvo; camera indisponivel: confira perfil e cabo"); status();
            }
        }
    }
    else if(!strcmp(op,"calibrate")) {
        auto *label=text(j,"label");
        if(!label || (strcmp(label,"present") && strcmp(label,"empty"))) reply(j,false,"label: present ou empty");
        else {
            Sample sample; char why[240]; float noise=0;
            if(!collect(sample,noise,why,sizeof(why))) reply(j,false,why);
            else if(!strcmp(label,"empty")) {
                Calibration next; next.empty=sample; next.noise=noise; next.has_empty=true;
                if(!storage_ready || save_settings(settings,next)!=ESP_OK) reply(j,false,"Falha ao salvar calibracao");
                else {
                    calibration=next; reschedule();
                    snprintf(why,sizeof(why),"Vazio salvo: ruido %.0f, uma celula muda acima de %.0f niveis",
                             noise,cell_threshold(calibration,settings));
                    reply(j,true,why); status();
                }
            }
            // A dose only checks the contrast: nothing is saved, any dose in any position counts.
            else if(!calibrated(calibration,settings)) reply(j,false,"Calibre o compartimento VAZIO primeiro");
            else {
                Result r=classify(sample,calibration,settings);
                if(r.label==Observation::Present)
                    snprintf(why,sizeof(why),"Comprimido detectado: mancha de %d celulas (minimo %d), diferenca %.0f (limite %.0f)",
                             r.change.blob,settings.min_cells,r.change.top,r.threshold);
                else if(r.context>settings.context_distance || std::abs(r.change.offset)>settings.context_distance)
                    snprintf(why,sizeof(why),"Cena ou luz mudou desde o VAZIO (entorno %.1f, brilho %+.0f; limite %.0f): fixe camera e caixa e recalibre o VAZIO",
                             r.context,r.change.offset,settings.context_distance);
                else snprintf(why,sizeof(why),"Comprimido nao se destaca: maior mancha de %d celulas (minimo %d). Aproxime a camera, reduza a ROI ou use fundo que contraste",
                              r.change.blob,settings.min_cells);
                reply(j,r.label==Observation::Present,why);
            }
        }
    }
    else if(!strcmp(op,"clear_calibration")) {
        Calibration next{};
        if(!storage_ready || save_settings(settings,next)!=ESP_OK) reply(j,false,"Falha NVS");
        else { calibration=next; cycle.cancel(); reply(j,true,"Referencias removidas; agenda parada"); status(); }
    }
    else if(!strcmp(op,"snapshot")) snapshot(j);
    else if(!strcmp(op,"sample")) {
        bool telegram=false;
        if(!flag(j,"notify",telegram)) reply(j,false,"notify deve ser true ou false");
        else {
            int64_t since=mono(); Result r=observation();
            if(!telegram) reply(j,true,"Leitura registrada; nao altera a agenda");
            else if(send_sample(r,since)) reply(j,true,"Leitura registrada e enviada ao Telegram");
            else reply(j,false,"Leitura registrada, mas o Telegram nao esta configurado ou a fila esta cheia");
        }
    }
    else if(!strcmp(op,"arm")) {
        int delay_s=0;
        if(!number(j,"delay_s",delay_s) || delay_s<0 || delay_s>86400) reply(j,false,"delay_s entre 0 e 86400");
        else if(!delay_s) { reschedule(); schedule(); reply(j,cycle.state==State::Armed,
            cycle.state==State::Armed?"Agenda diaria recalculada":"Configure hora, camera, calibracao e agenda ativa"); }
        else if(!camera_ready || !calibrated(calibration,settings)) reply(j,false,"Configure a camera e calibre o VAZIO antes do teste");
        else {
            int64_t now=mono(), start=now+int64_t(delay_s)*1000;
            cycle.cancel();
            bool ok=cycle.arm(now,start,start+int64_t(settings.window_s)*1000,settings);
            demo=ok; resumed=false;
            reply(j,ok,ok?"Teste armado; depois dele a agenda diaria volta sozinha":"Falha ao armar teste"); status();
        }
    }
    else if(!strcmp(op,"wifi")) {
        const char *ssid=text(j,"ssid"), *password=text(j,"password");
        esp_err_t e=!network_ready?ESP_ERR_INVALID_STATE:ssid?network_configure(ssid,password?password:""):ESP_ERR_INVALID_ARG;
        reply(j,e==ESP_OK,e==ESP_OK?"Wi-Fi salvo; conectando":e==ESP_ERR_INVALID_ARG?
            "ssid 1-32 caracteres; senha vazia ou 8-63 caracteres":"Falha ao salvar Wi-Fi");
    }
    else if(!strcmp(op,"telegram")) {
        const char *token=text(j,"token"), *chat=text(j,"chat_id");
        esp_err_t e=token && chat?notify_configure(token,chat):ESP_ERR_INVALID_ARG;
        reply(j,e==ESP_OK,e==ESP_OK?"Telegram salvo":e==ESP_ERR_INVALID_ARG?
            "token do BotFather e chat_id numerico (texto)":"Falha ao salvar Telegram");
    }
    else if(!strcmp(op,"test_notify")) {
        size_t len=0; int64_t taken=-1;
        bool photo=camera_ready && camera_gray(gray);
        const uint8_t *jpeg=photo?camera_last_jpeg(len,taken):nullptr;
        bool ok=notify_configured() && notify_send("\U0001f9ea <b>Teste do Auditor de Medicação</b>\n"
            "✅ Notificações funcionando.\n\U0001f4f7 Foto atual da caixa em anexo.",jpeg,len);
        reply(j,ok,ok?(network_connected()?"Teste enfileirado; acompanhe o evento notify":"Teste enfileirado; aguardando Wi-Fi"):
            "Configure o Telegram primeiro");
    }
    else reply(j,false,"Comando desconhecido");
    cJSON_Delete(j);
}
void serial_task(void *) {
    Command cmd{}; size_t n=0; bool overflow=false;
    while(true) {
        uint8_t ch;
        if(uart_read_bytes(UART_NUM_0,&ch,1,pdMS_TO_TICKS(100))!=1) continue;
        if(ch=='\r') continue;
        if(ch=='\n') {
            if(overflow) snprintf(cmd.text,sizeof(cmd.text),"{\"cmd\":\"line_too_long\"}");
            else cmd.text[n]='\0';
            if(n || overflow) xQueueSend(commands,&cmd,portMAX_DELAY);
            n=0; overflow=false;
        } else if(n<sizeof(cmd.text)-1 && !overflow) cmd.text[n++]=char(ch);
        else overflow=true;
    }
}
// Housekeeping that does not depend on the cycle: clock source, Wi-Fi, camera recovery.
void supervise() {
    int64_t now=mono();
    if(network_time_synced() && clock_source!=Clock::Sntp) {
        clock_source=Clock::Sntp; reschedule(); // a manual clock may have been off
        auto *j=event("clock"); cJSON_AddStringToObject(j,"source","sntp"); emit(j);
    }
    bool connected=network_connected();
    if(connected!=was_connected) {
        was_connected=connected;
        auto *j=event("wifi"); cJSON_AddBoolToObject(j,"connected",connected);
        cJSON_AddNumberToObject(j,"rssi",network_rssi()); emit(j);
    }
    // Retry a failed camera outside a window; a loose cable must not need a power cycle.
    if(!camera_ready && settings.profile && !cycle.sampling(now) && now-last_camera_retry>=60000) {
        last_camera_retry=now;
        camera_ready=camera_start(settings.profile)==ESP_OK;
        if(camera_ready) status();
    }
    // Tell the caregiver the device is back, once the schedule is known or after 2 min.
    if(!boot_notified && (cycle.state==State::Armed || now>120000)) { boot_notified=true; send_boot_notice(); }
}
} // namespace

extern "C" void app_main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    // Never erase NVS automatically: schema/full-storage errors remain visible.
    storage_ready=nvs_flash_init()==ESP_OK;
    esp_err_t stored=storage_ready?load_settings(settings,calibration):ESP_FAIL;
    if(stored!=ESP_OK) { settings=Settings{}; calibration=Calibration{}; }
    uart_config_t uart{};
    uart.baud_rate=115200; uart.data_bits=UART_DATA_8_BITS; uart.parity=UART_PARITY_DISABLE;
    uart.stop_bits=UART_STOP_BITS_1; uart.flow_ctrl=UART_HW_FLOWCTRL_DISABLE; uart.source_clk=UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0,&uart));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,2048,0,0,nullptr,0));
    commands=xQueueCreate(8,sizeof(Command));
    if(!commands || xTaskCreate(serial_task,"usb_commands",4096,nullptr,4,nullptr)!=pdPASS) abort();
    // Camera first: its DMA buffers need contiguous internal RAM before Wi-Fi fragments the heap.
    camera_ready=camera_start(settings.profile)==ESP_OK;
    esp_err_t net=storage_ready?network_init():ESP_ERR_INVALID_STATE;
    network_ready=net==ESP_OK;
    esp_err_t notifier=notify_init();
    auto *boot=event("boot"); cJSON_AddStringToObject(boot,"version","2.0.0");
    cJSON_AddStringToObject(boot,"storage_load",esp_err_to_name(stored));
    cJSON_AddStringToObject(boot,"network",esp_err_to_name(net));
    cJSON_AddStringToObject(boot,"notify",esp_err_to_name(notifier));
    cJSON_AddStringToObject(boot,"reset_reason",reset_text());
    cJSON_AddNumberToObject(boot,"psram_bytes",esp_psram_is_initialized()?double(esp_psram_get_size()):0);
    cJSON_AddStringToObject(boot,"message","Autonomo: hora por SNTP, agenda diaria e avisos pelo Telegram"); emit(boot); status();
    esp_task_wdt_add(nullptr); // a hung camera/driver call reboots the device (CONFIG_ESP_TASK_WDT_PANIC)
    int64_t heartbeat=mono();
    while(true) {
        esp_task_wdt_reset();
        supervise();
        schedule();
        State previous=cycle.state;
        // Sample only when still inside the window; tick() closes it otherwise.
        if(cycle.needs_sample(mono())) { Result r=observation(); cycle.observe(mono(),r.label); }
        cycle.tick(mono()); report_transition(previous);
        Command cmd;
        if(xQueueReceive(commands,&cmd,pdMS_TO_TICKS(20))==pdTRUE) handle(cmd.text);
        if(mono()-heartbeat>=10000) { status(); heartbeat=mono(); }
    }
}
