/* Adapted from FalkoWing/muse-passport, Apache-2.0; see main/muse/LICENSE.
 * Original speech protocol/buffering; BSP audio and shared volume adapter. */
#include "muse_speech.h"
#include "passport_private_defaults.h"
#include "muse_speech_buffer.h"
#include "phone_bridge.h"
#include "passport_app.h"
#include "bsp_audio.h"
#include "passport_opus.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static SemaphoreHandle_t lock;
static muse_speech_buffer_t buffer;
static atomic_bool busy, stopped, started;
static uint32_t next_session;
enum { MUSE_AUDIO_CHUNK=320 };
typedef struct {uint32_t id;void *decoder;} speech_task_t;
/* One playback at a time; busy protects this handoff until task teardown. */
static speech_task_t playback;
static esp_err_t write_pcm(const int16_t *pcm,size_t n){
 bsp_audio_set_volume(passport_audio_volume_percent());return bsp_audio_write(pcm,n*sizeof(*pcm));
}
static void put32(uint8_t *p, uint32_t n) { for (int i=0;i<4;i++) p[i]=n>>(8*i); }
/* state: 0 ready, 1 playing, 2 drained, 3 cancelled, 4 failed before
 * playback (may restart), 5 failed after playback (must not switch voice). */
static bool feedback(uint32_t id, uint32_t limit, unsigned state) {
    uint8_t data[9]; put32(data,id); put32(data+4,limit); data[8]=state;
    return phone_bridge_speech_send(15, data, sizeof(data));
}
static void play(void *arg) {
    speech_task_t work=*(speech_task_t *)arg;
    uint32_t id=work.id;
    void *decoder=work.decoder;
    bool audio=bsp_audio_init()==ESP_OK;
    if(audio && bsp_audio_set_format(16000,16,1)!=ESP_OK){bsp_audio_deinit();audio=false;}
    int16_t pcm[960];
    uint8_t packet[120];
    int64_t progress=esp_timer_get_time();
    bool failed=!audio || !decoder, ready=false, rejected=false;
    if(failed)ESP_LOGW("reply_speech","Speaker/decoder allocation unavailable");
    int64_t decode_peak=0;
    unsigned played_frames=0;
    while (!stopped && !failed && phone_bridge_ready() && (passport_audio_volume_percent()>0)) {
        unsigned count; bool ended, abort, terminal;
        uint32_t consumed;
        xSemaphoreTake(lock,portMAX_DELAY);
        count=buffer.count; ended=buffer.ended; abort=buffer.abort; terminal=buffer.terminal; consumed=buffer.consumed;
        if (abort) {
            buffer.count=0; buffer.rejected=true; buffer.abort=false;
        }
        xSemaphoreGive(lock);
        if (abort) {
            if (started || terminal) { failed=true; break; }
            if (!feedback(id,0,4)) { failed=true; break; }
            rejected=true; ready=false; progress=esp_timer_get_time();
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        bool restart=rejected && !buffer.rejected;
        xSemaphoreGive(lock);
        if (restart) {
            passport_opus_decoder_destroy(decoder); decoder=passport_opus_decoder_create();
            failed=!decoder; rejected=false; progress=esp_timer_get_time();
            if (!feedback(id,MUSE_SPEECH_WINDOW,0)) failed=true;
            continue;
        }
        if (rejected) {
            if (esp_timer_get_time()-progress>30000000) { failed=true; break; }
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        if (!ready && (count>=4 || ended)) ready=true;
        if (ready && ended && !count) break;
        size_t len=0;
        if (ready) {
            xSemaphoreTake(lock,portMAX_DELAY);
            len=muse_speech_buffer_take(&buffer,packet); consumed=buffer.consumed;
            xSemaphoreGive(lock);
        }
        if (len) {
            int64_t decode_at=esp_timer_get_time();
            int decoded=passport_opus_decode(decoder,packet,len,pcm);
            int64_t decode_us=esp_timer_get_time()-decode_at;
            if (decode_us>decode_peak) decode_peak=decode_us;
            if (decoded!=960) { failed=true; break; }
            for (unsigned off=0;off<960 && !stopped;off+=MUSE_AUDIO_CHUNK) {
                if (write_pcm(pcm+off,MUSE_AUDIO_CHUNK)!=ESP_OK) { failed=true; break; }
            }
            if (failed || stopped) break;
            /* Three writes exceed the 40 ms DMA capacity: samples have reached
             * playback before acknowledging STARTED, not merely the BLE FIFO. */
            bool first=!atomic_exchange(&started,true);
            played_frames++;
            xSemaphoreTake(lock,portMAX_DELAY); buffer.started=true; xSemaphoreGive(lock);
            if (first || consumed%4==0) {
                if (!feedback(id,consumed+MUSE_SPEECH_WINDOW,1)) { failed=true; break; }
            }
            progress=esp_timer_get_time();
        } else {
            if (esp_timer_get_time()-progress>(started?30000000:60000000)) { failed=true; break; }
            /* Do not replay old DMA while a later sentence is synthesizing. */
            if (started) {
                static const int16_t silence[MUSE_AUDIO_CHUNK];
                if (write_pcm(silence,MUSE_AUDIO_CHUNK)!=ESP_OK) { failed=true; break; }
            } else vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    /* Flush the final DMA tail, also silencing cancellation within <= 40 ms. */
    static const int16_t silence[MUSE_AUDIO_CHUNK];
    if (started) for (int i=0;i<3;i++) write_pcm(silence,MUSE_AUDIO_CHUNK);
    unsigned state=stopped || !phone_bridge_ready() || !(passport_audio_volume_percent()>0)?3:failed?5:2;
    feedback(id,0,state);
    passport_opus_decoder_destroy(decoder);
    ESP_LOGI("reply_speech","frames=%u decode_peak_us=%lld stack_free=%u heap_largest=%u state=%u",
        played_frames,(long long)decode_peak,(unsigned)uxTaskGetStackHighWaterMark(NULL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),state);
    xSemaphoreTake(lock,portMAX_DELAY); buffer.session=0; xSemaphoreGive(lock);
    if(audio)bsp_audio_deinit();
    if (state==5) {
        ESP_LOGW("reply_speech","speech stopped; text remains available");
        ESP_LOGW("reply_speech","Audio unavailable; reply text retained");
    }
    atomic_store(&busy,false);
    vTaskDelete(NULL);
}
void muse_speech_init(void) { if(!lock)lock=xSemaphoreCreateMutex(); }
bool muse_speech_busy(void) { return atomic_load(&busy); }
bool muse_speech_started(void) { return atomic_load(&busy) && atomic_load(&started); }
void muse_speech_stop(void) { atomic_store(&stopped,true); }
void muse_speech_reset(void) { if (!busy) atomic_store(&stopped,false); }
void muse_speech_receive(const uint8_t *data,size_t len) {
    if (!lock || !busy || stopped) return;
    xSemaphoreTake(lock,portMAX_DELAY); muse_speech_buffer_feed(&buffer,data,len); xSemaphoreGive(lock);
}
bool muse_speech_request(const char *note,const char *message) {
    if (busy) return false;
    if (stopped || !(passport_audio_volume_percent()>0) || !phone_bridge_speech_ready()) {
        ESP_LOGW("reply_speech","Not started: stopped=%u volume=%u negotiated=%u",(unsigned)atomic_load(&stopped),passport_audio_volume_percent(),phone_bridge_speech_ready());return true;
    }
    if (!lock || next_session==UINT32_MAX) return true;
    uint32_t id=++next_session;
    cJSON *request=cJSON_CreateObject();
    if (!request) return true;
    cJSON_AddNumberToObject(request,"session",id);
    cJSON_AddNumberToObject(request,"limit",MUSE_SPEECH_WINDOW);
    cJSON_AddStringToObject(request,"note",note); cJSON_AddStringToObject(request,"message",message);
    char *body=cJSON_PrintUnformatted(request); cJSON_Delete(request);
    if (!body) return true;
    /* A real voice turn can leave only a 23 KiB largest heap block. Reserve
     * the ~18 KiB decoder before the 14 KiB task splits that block. Decoder
     * initialization is bounded; scratch-heavy decoding stays on the task. */
    ESP_LOGI("reply_speech","request free=%u largest=%u",(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    void *decoder=passport_opus_decoder_create();
    if(!decoder){free(body);ESP_LOGW("reply_speech","Decoder reservation unavailable");return true;}
    playback=(speech_task_t){.id=id,.decoder=decoder};
    xSemaphoreTake(lock,portMAX_DELAY); muse_speech_buffer_begin(&buffer,id); xSemaphoreGive(lock);
    atomic_store(&started,false); atomic_store(&busy,true);
    bool task_created=xTaskCreate(play,"reply_speech",14336,&playback,5,NULL)==pdPASS;
    bool ok=task_created && phone_bridge_speech_send(13,body,strlen(body));
    free(body);
    if (!ok) {
        atomic_store(&stopped,true);
        if (!task_created){passport_opus_decoder_destroy(decoder);playback.decoder=NULL;atomic_store(&busy,false);}
        ESP_LOGW("reply_speech","Audio unavailable; reply text retained");
    }
    return true;
}

#if PASSPORT_PRIVATE_USB_TEST_CONTROL
#include "opus_probe_packet.h"
typedef struct {unsigned stack;void *decoder;} probe_task_t;
static probe_task_t probe_task;
static void probe(void *arg){
 probe_task_t work=*(probe_task_t *)arg;unsigned stack=work.stack;void *decoder=work.decoder;
 bool audio=bsp_audio_init()==ESP_OK;
 if(audio && bsp_audio_set_format(16000,16,1)!=ESP_OK){bsp_audio_deinit();audio=false;}
 int16_t pcm[960];unsigned frames=0;
 ESP_LOGI("opus_probe","stack=%u audio=%u decoder=%u free=%u largest=%u",stack,audio,decoder!=NULL,(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 for(unsigned i=0;audio && decoder && !stopped && i<OPUS_PROBE_FRAMES;i++){
  if(passport_opus_decode(decoder,opus_probe_packets[i],sizeof(opus_probe_packets[i]),pcm)!=960)break;
  bool ok=true;for(unsigned off=0;off<960;off+=MUSE_AUDIO_CHUNK)if(write_pcm(pcm+off,MUSE_AUDIO_CHUNK)!=ESP_OK){ok=false;break;}
  if(!ok)break;
  frames++;
 }
 memset(pcm,0,sizeof(pcm));if(audio)for(unsigned i=0;i<3;i++)write_pcm(pcm,MUSE_AUDIO_CHUNK);
 passport_opus_decoder_destroy(decoder);if(audio)bsp_audio_deinit();
 ESP_LOGI("opus_probe","frames=%u stack_free=%u",frames,(unsigned)uxTaskGetStackHighWaterMark(NULL));
 atomic_store(&busy,false);vTaskDelete(NULL);
}
bool muse_speech_probe(unsigned stack){
 if(busy || (stack!=24576 && stack!=14336))return false;
 atomic_store(&stopped,false);atomic_store(&busy,true);
 ESP_LOGI("opus_probe","request stack=%u free=%u largest=%u",stack,(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
 void *decoder=passport_opus_decoder_create();
 if(!decoder){atomic_store(&busy,false);ESP_LOGW("opus_probe","Decoder reservation failed");return false;}
 probe_task=(probe_task_t){stack,decoder};
 if(xTaskCreate(probe,"opus_probe",stack,&probe_task,5,NULL)!=pdPASS){passport_opus_decoder_destroy(decoder);probe_task.decoder=NULL;atomic_store(&busy,false);ESP_LOGW("opus_probe","Task allocation failed");return false;}
 return true;
}
#else
bool muse_speech_probe(unsigned stack){(void)stack;return false;}
#endif
