"""Actual button-to-BSP-to-Muse voice path, using virtual mic/phone boundaries."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from host_cjson import cjson_flags
from test_app_runtime import HEADERS
ROOT=Path(__file__).resolve().parents[2]
EXTRA={
'mbedtls/base64.h':'''#pragma once
#include <stddef.h>
#include <assert.h>
static inline int mbedtls_base64_encode(unsigned char *out,size_t cap,size_t *used,const unsigned char *in,size_t n){
 (void)out;(void)cap;(void)used;(void)in;(void)n;assert(!"Muse must not use the Hermes browser relay");return -1;
}
''',
'esp_timer.h':'#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
'esp_wifi.h':'#pragma once\n#define WIFI_PS_NONE 0\n#define WIFI_PS_MIN_MODEM 1\nint esp_wifi_set_ps(int);\n',
'freertos/task.h':'#pragma once\n#define pdMS_TO_TICKS(x) (x)\nvoid vTaskDelay(unsigned);\n',
'bsp_audio.h':'''#pragma once
#include <stddef.h>
int bsp_audio_init(void);
int bsp_audio_deinit(void);
int bsp_audio_set_format(int,int,int);
int bsp_audio_read(void *,size_t);
''',
'esp_http_client.h':'''#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_OK 0
#define HTTP_METHOD_POST 1
typedef void *esp_http_client_handle_t;
typedef struct {const char *url;int timeout_ms,buffer_size;bool keep_alive_enable;} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
int esp_http_client_set_method(esp_http_client_handle_t,int);
int esp_http_client_set_header(esp_http_client_handle_t,const char *,const char *);
int esp_http_client_open(esp_http_client_handle_t,int);
int esp_http_client_write(esp_http_client_handle_t,const char *,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char *,int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
int esp_http_client_close(esp_http_client_handle_t);
int esp_http_client_cleanup(esp_http_client_handle_t);
'''
}
HARNESS=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "passport_app.h"
#include "passport_muse_voice.h"
#include "muse/phone_bridge.h"
#include "muse/voice_turn.h"
#include "esp_http_client.h"
SemaphoreHandle_t passport_mutex=(void *)1;
volatile bool passport_dirty;
void passport_reply_audio_stop(void){}
bool passport_audio_worker_active(void){return false;}
void passport_direct_release_caches(void){}
bool muse_speech_probe(unsigned stack){(void)stack;return false;}
void muse_speech_init(void){}
bool muse_speech_busy(void){return false;}
void muse_speech_stop(void){}
void muse_speech_reset(void){}
bool muse_speech_request(const char *n,const char *m){assert(n&&m);return true;}
bool phone_bridge_audio_idle(void){return true;}
static passport_page_t active=PAGE_MUSE;
static int64_t clock_us=1000;
static unsigned mic_reads,release_at=4,cancel_at,mic_init,mic_deinit,transferred,final_frames,http_calls;
static bool connected=true,mic_fail,mutex_fail;
typedef struct {muse_link_req_cb cb;void *ctx;char path[128];bool cancelled;} req_t;
static req_t req[64];static unsigned request_count;
unsigned passport_language_get(void){return 1;}
const char *passport_tr(const char *zh,const char *en){(void)zh;return en;}
unsigned passport_function_active(void){return active;}
bool passport_phone_relay_active(void){return false;}
bool passport_network_idle(void){return true;}
bool passport_bridge_snapshot(const char *p,char *u,size_t n,char *t,size_t k){(void)p;(void)u;(void)n;(void)t;(void)k;assert(!"Muse must not use Hermes HTTP");return false;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return mutex_fail?NULL:(void *)2;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned wait){assert(h&&wait==portMAX_DELAY);return 1;}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h);return 1;}
int64_t esp_timer_get_time(void){return clock_us;}
uint32_t esp_random(void){return 123;}
void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;}
int esp_wifi_set_ps(int p){(void)p;return ESP_OK;}
int bsp_audio_init(void){mic_init++;return ESP_OK;}
int bsp_audio_deinit(void){mic_deinit++;return ESP_OK;}
int bsp_audio_set_format(int rate,int bits,int channels){assert(rate==16000&&bits==16&&channels==1);return mic_fail?-1:ESP_OK;}
int bsp_audio_read(void *p,size_t n){int16_t *samples=p;for(size_t i=0;i<n/2;i++)samples[i]=1234;mic_reads++;clock_us+=(int64_t)(n/2)*1000000/16000;if(cancel_at&&mic_reads==cancel_at)passport_voice_cancel();else if(release_at&&mic_reads==release_at)passport_voice_release();return ESP_OK;}
bool phone_bridge_ready(void){return connected;}
int64_t phone_bridge_open(const char *v,const char *p,const char *const *h,bool end,muse_link_req_cb cb,void *ctx){(void)h;assert(!strcmp(v,"GET")?end:!end);assert(request_count+1<64);unsigned id=++request_count;req[id]=(req_t){.cb=cb,.ctx=ctx};snprintf(req[id].path,sizeof(req[id].path),"%s",p);return id;}
bool phone_bridge_send(int64_t id,const void *p,size_t n,bool end,int wait){assert(id>0&&(unsigned)id<=request_count&&!req[id].cancelled&&!wait);if(end){assert(!p&&!n);final_frames++;}else{assert(p&&n&&!(n%4));transferred+=(unsigned)n;}return true;}
void phone_bridge_cancel(int64_t id){assert(id>0&&(unsigned)id<=request_count);req[id].cancelled=true;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){(void)c;http_calls++;return NULL;}
int esp_http_client_set_method(esp_http_client_handle_t h,int m){(void)h;(void)m;return 0;}
int esp_http_client_set_header(esp_http_client_handle_t h,const char *k,const char *v){(void)h;(void)k;(void)v;return 0;}
int esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;(void)n;return -1;}
int esp_http_client_write(esp_http_client_handle_t h,const char *p,int n){(void)h;(void)p;(void)n;return -1;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){(void)h;return -1;}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return 500;}
int esp_http_client_read(esp_http_client_handle_t h,char *p,int n){(void)h;(void)p;(void)n;return 0;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return false;}
int esp_http_client_close(esp_http_client_handle_t h){(void)h;return 0;}
int esp_http_client_cleanup(esp_http_client_handle_t h){(void)h;return 0;}
static void deliver(unsigned id,const char *body){assert(id&&id<=request_count);req[id].cb(req[id].ctx,200,(const uint8_t *)body,strlen(body),true);passport_voice_poll();}
static void prepare(void){passport_muse_voice_cancel();passport_voice_cancel();passport_voice_poll();active=PAGE_MUSE;passport_reply_select(PAGE_APPS);mic_reads=mic_init=mic_deinit=transferred=final_frames=request_count=0;release_at=4;cancel_at=0;mic_fail=false;memset(req,0,sizeof(req));}
int main(void){
 uint8_t pcm[2048];prepare();mutex_fail=true;passport_voice_press(1);passport_voice_run(pcm,sizeof(pcm));assert(passport_voice_phase()==5&&!mic_init&&strstr(passport_muse_voice_status(),"Low RAM"));mutex_fail=false;
 prepare();passport_voice_press(1);assert(passport_voice_phase()==1&&passport_voice_needs_worker());passport_voice_run(pcm,sizeof(pcm));
 assert(passport_voice_phase()==4&&passport_voice_needs_worker()&&mic_reads==4&&mic_init==1&&mic_deinit==1&&transferred==8192&&!final_frames&&!http_calls);
 assert(passport_voice_bytes()==8192&&passport_voice_peak()==1234);
 assert(!passport_reply_peek(1)); /* No 6.4 KB reply allocation during capture/upload. */
 unsigned marker=request_count;deliver(marker,"{\"result\":{\"chat_events\":[]}}");assert(final_frames==1);
 deliver(1,"{\"result\":{\"message_id\":\"note-1\"}}");
 deliver(request_count,"{\"result\":{\"chat_events\":[{\"seq\":1,\"event_name\":\"message.assistant\",\"reply_to_message_id\":\"note-1\",\"display_text_ready\":true,\"display_text\":\"Hello from Muse\"}]}}");
 assert(!strcmp(passport_reply_peek(1),"Hello from Muse"));clock_us+=3000001;passport_voice_poll();assert(passport_voice_phase()==0);
 prepare();cancel_at=2;passport_voice_press(1);passport_voice_run(pcm,sizeof(pcm));assert(passport_voice_phase()==0&&mic_deinit==1&&!final_frames&&req[1].cancelled);
 prepare();passport_voice_press(1);passport_voice_release();passport_voice_run(pcm,sizeof(pcm));assert(!mic_init&&!request_count&&passport_voice_phase()==0);
 prepare();mic_fail=true;passport_voice_press(1);passport_voice_run(pcm,sizeof(pcm));assert(passport_voice_phase()==5&&mic_deinit==1&&!mic_reads&&!final_frames&&req[1].cancelled);
 prepare();passport_voice_press(1);passport_voice_run(pcm,sizeof(pcm));assert(passport_voice_phase()==4);active=PAGE_APPS;passport_reply_select(PAGE_APPS);passport_voice_cancel();passport_voice_poll();assert(passport_voice_phase()==0&&!passport_reply_peek(1)&&req[1].cancelled);
 prepare();passport_voice_press(1);passport_voice_run(pcm,sizeof(pcm));assert(passport_voice_phase()==4);
 active=PAGE_APPS;passport_reply_select(PAGE_APPS);passport_muse_voice_leave();passport_voice_cancel();
 active=PAGE_MUSE;passport_voice_poll();assert(passport_voice_phase()==0&&!passport_voice_needs_worker()&&!passport_muse_voice_active()&&req[1].cancelled&&!passport_reply_peek(1));
 prepare();connected=false;passport_voice_press(1);assert(passport_voice_phase()==5&&!passport_voice_busy()&&!passport_voice_needs_worker()&&!request_count);assert(!http_calls);
 puts("PASS actual Muse button/microphone/reply path, release, cancel, short press, codec failure and inactive cleanup");
}
'''
class VoiceInputTest(unittest.TestCase):
    def test_hermes_relay_request_ownership_timeout_and_stale_ack(self):
        source=(ROOT/'main/passport_voice.c').read_text()
        start=source.index('static struct {unsigned serial,id;')
        end=source.index('unsigned passport_voice_phase(',start)
        harness=r'''#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
static int passport_mutex,held;static int64_t now;static unsigned mode,seen;static bool active=true,encode_fail;
static void xSemaphoreTake(int mutex,int wait){(void)mutex;(void)wait;assert(!held);held=1;}
static void xSemaphoreGive(int mutex){(void)mutex;assert(held);held=0;}
static int64_t esp_timer_get_time(void){return now;}
static bool passport_phone_relay_active(void){return active;}
static int mbedtls_base64_encode(unsigned char *out,size_t cap,size_t *used,const void *in,size_t n){
 if(encode_fail){return -1;}assert(n==3&&!memcmp(in,"abc",3)&&cap>=5);memcpy(out,"YWJj",4);*used=4;return 0;
}
static void vTaskDelay(unsigned);
'''
        checks=r'''
static void vTaskDelay(unsigned ms){
 now+=(int64_t)ms*1000;
 if(mode==2){active=false;return;}
 if(mode==1)return;
 char *body=passport_voice_relay_poll();assert(body);cJSON *j=cJSON_Parse(body);free(body);
 unsigned id=(unsigned)cJSON_GetObjectItem(j,"id")->valuedouble;seen=id;
 assert(!strcmp(cJSON_GetObjectItem(j,"body")->valuestring,"YWJj"));cJSON_Delete(j);
 assert(!passport_voice_relay_ack(id+1,200,"{}"));
 assert(!passport_voice_relay_ack(id,99,"{}"));
 assert(passport_voice_relay_ack(id,200,"{\"ok\":true}"));
 assert(!passport_voice_relay_ack(id,200,"{}"));
}
int main(void){
 char response[256];
 assert(relay_post("/v1/voice/start","abc",3,"application/json",response,sizeof(response))==200);
 assert(!strcmp(response,"{\"ok\":true}")&&!relay.id&&!relay.body&&!held);
 assert(!passport_voice_relay_ack(seen,200,"{}"));
 char *empty=passport_voice_relay_poll();assert(empty&&!strcmp(empty,"{}"));free(empty);
 mode=1;assert(!relay_post("/v1/voice/start","abc",3,"application/json",response,sizeof(response)));assert(!relay.id&&!relay.body&&now>=6000000);
 mode=2;active=true;assert(!relay_post("/v1/voice/start","abc",3,"application/json",response,sizeof(response)));assert(!relay.id&&!relay.body);
 mode=0;active=true;relay.serial=UINT_MAX;assert(relay_post("/v1/voice/start","abc",3,"application/json",response,sizeof(response))==200&&seen==1);
 assert(!relay_post("/v1/voice/start","abc",4097,"application/json",response,sizeof(response)));
 relay.id=123;relay.body="abc";relay.bytes=3;encode_fail=true;assert(!passport_voice_relay_poll());assert(!held&&relay.id==123);
 return 0;
}
'''
        cflags,ldflags=cjson_flags()
        with tempfile.TemporaryDirectory() as directory:
            cfile=Path(directory)/'relay.c';binary=Path(directory)/'relay';cfile.write_text(harness+source[start:end]+checks)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',*cflags,str(cfile),*ldflags,'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_actual_ptt_path(self):
        idf=Path(os.environ.get('IDF_PATH',ROOT/'.toolchains/esp-idf-v5.5.3'))
        cjson=idf/'components/json/cJSON'
        if not (cjson/'cJSON.c').is_file():self.skipTest('ESP-IDF cJSON source required')
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            all_headers={name:HEADERS[name] for name in ['freertos/FreeRTOS.h','freertos/semphr.h','esp_random.h']}
            all_headers['sdkconfig.h']='#pragma once\n#define CONFIG_PASSPORT_MUSE_BLE 1\n';all_headers.update(EXTRA);all_headers['freertos/semphr.h']+='\nSemaphoreHandle_t xSemaphoreCreateMutex(void);\n'
            for name,body in all_headers.items():
                path=work/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(body)
            harness=work/'input.c';harness.write_text(HARNESS);binary=work/'input'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DCONFIG_PASSPORT_MUSE_BLE=1',
                '-I'+str(work),'-I'+str(ROOT/'main'),'-I'+str(cjson),str(harness),
                str(ROOT/'main/passport_voice.c'),str(ROOT/'main/passport_muse_voice.c'),str(ROOT/'main/muse/voice_turn.c'),
                str(ROOT/'main/passport_reply.c'),str(ROOT/'main/passport_model.c'),str(cjson/'cJSON.c'),'-lm','-o',str(binary)],check=True)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS actual Muse button/microphone/reply path',result.stdout)
