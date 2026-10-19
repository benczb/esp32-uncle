"""Real Muse voice-turn state machine with virtual Android relay and clock."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_app_runtime import HEADERS
ROOT=Path(__file__).resolve().parents[2]
HARNESS=r'''
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "muse/voice_turn.h"
#include "muse/phone_bridge.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
static int fail_mutex,fail_malloc,allocations,live,fail_open,fail_send,ends,changes;
static int64_t clock_us=1000;
static bool ready=true,audio_idle=true;
static size_t max_allocation=16384;
static char text_seen[256];
static muse_turn_error_t error_seen;
typedef struct {muse_link_req_cb callback;void *ctx;char path[128];bool cancelled;} req_t;
static req_t requests[512];static int next_id;
static void *turn_malloc(size_t n){allocations++;if((fail_malloc&&allocations==fail_malloc)||n>max_allocation)return NULL;void *p=malloc(n);assert(p);live++;return p;}
static void turn_free(void *p){if(p){assert(live>0);live--;free(p);}}
static void *turn_realloc(void *old,size_t n){
 allocations++;if((fail_malloc&&allocations==fail_malloc)||n>max_allocation)return NULL;
 void *p=realloc(old,n);assert(p);if(!old)live++;return p;
}
#define realloc turn_realloc
#define malloc turn_malloc
#define free turn_free
#include "muse/voice_turn.c"
#undef realloc
#undef malloc
#undef free
SemaphoreHandle_t xSemaphoreCreateMutex(void){return fail_mutex?NULL:(void *)1;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned wait){assert(h&&wait==portMAX_DELAY);return 1;}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h);return 1;}
int64_t esp_timer_get_time(void){return clock_us;}
uint32_t esp_random(void){return 123;}
bool phone_bridge_ready(void){return ready;}
bool phone_bridge_audio_idle(void){return audio_idle;}
int64_t phone_bridge_open(const char *verb,const char *path,const char *const *headers,bool end,muse_link_req_cb cb,void *ctx){
 assert(headers&&headers[0]&&!strcmp(headers[2],"x-app-id")&&!strcmp(headers[3],"hatch-web"));
 assert(!strcmp(verb,"GET")?end:!end);
 if(fail_open)return 0;
 assert(next_id+1<512);int id=++next_id;requests[id]=(req_t){.callback=cb,.ctx=ctx};snprintf(requests[id].path,sizeof(requests[id].path),"%s",path);return id;
}
bool phone_bridge_send(int64_t id,const void *pcm,size_t bytes,bool end,int wait){assert(id>0&&id<=next_id&&!requests[id].cancelled&&wait==0);if(fail_send)return false;if(end){assert(!bytes&&!pcm);ends++;}else assert(pcm&&bytes&&!(bytes%4));return true;}
void phone_bridge_cancel(int64_t id){assert(id>0&&id<=next_id);requests[id].cancelled=true;}
static void event(muse_turn_state_t s,muse_turn_error_t e,const char *text){(void)s;changes++;error_seen=e;if(text)snprintf(text_seen,sizeof(text_seen),"%s",text);}
static void deliver(int id,int status,const char *body){
 req_t *r=&requests[id];assert(r->callback);size_t len=strlen(body),at=0;
 if(!len){r->callback(r->ctx,status,NULL,0,true);return;}
 while(at<len){size_t n=len-at>7?7:len-at;r->callback(r->ctx,status,(const uint8_t *)body+at,n,at+n==len);at+=n;}
}
static void reset(void){muse_turn_cancel();assert(live==0);memset(requests,0,sizeof(requests));next_id=0;ready=audio_idle=true;max_allocation=16384;fail_open=fail_send=fail_malloc=0;allocations=ends=changes=0;text_seen[0]=0;clock_us=1000;}
static int begin_finish(void){
 assert(muse_turn_begin());int note=next_id;int16_t pcm[3200]={0};assert(muse_turn_audio(pcm,3200));assert(muse_turn_finish());assert(muse_turn_state()==MUSE_TURN_MARKING&&!ends);
 return note;
}
static void mark_and_ack(int note){
 int marker=next_id;deliver(marker,200,"{\"result\":{\"chat_events\":[{\"seq\":10}]}}");muse_turn_poll();assert(ends==1&&muse_turn_state()==MUSE_TURN_ACK);
 deliver(note,200,"{\"result\":{\"message_id\":\"note-1\"}}");muse_turn_poll();assert(muse_turn_state()==MUSE_TURN_REPLY);
 assert(strstr(requests[next_id].path,"after_seq=10"));
}
static void row(const char *json){deliver(next_id,200,json);muse_turn_poll();clock_us+=500001;muse_turn_poll();}
int main(void){
 fail_mutex=1;assert(!muse_turn_init(event));fail_mutex=0;assert(muse_turn_init(event));
 reset();int note=begin_finish();int stale=next_id;mark_and_ack(note);
 row("{\"result\":{\"chat_events\":[{\"seq\":11,\"event_name\":\"message.assistant\",\"reply_to_message_id\":\"other\",\"display_text_ready\":true,\"display_text\":\"Ignore old reply\"}]}}");assert(!text_seen[0]);
 row("{\"result\":{\"chat_events\":[{\"seq\":12,\"event_name\":\"message.user\",\"message_id\":\"note-1\"}]}}");
 row("{\"result\":{\"chat_events\":[{\"seq\":13,\"event_name\":\"message.assistant\",\"display_text_ready\":false,\"display_text\":\"partial\"}]}}");assert(!text_seen[0]&&strstr(requests[next_id].path,"after_seq=12"));
 row("{\"result\":{\"chat_events\":[{\"seq\":13,\"event_name\":\"message.assistant\",\"display_text_ready\":true,\"display_text\":\"Hello Passport\"}]}}");assert(!strcmp(text_seen,"Hello Passport"));
 deliver(stale,200,"{\"result\":{\"chat_events\":[{\"seq\":9999}]}}");assert(after==13);clock_us+=3000001;muse_turn_poll();assert(muse_turn_state()==MUSE_TURN_DONE&&live==0);
 reset();ready=false;assert(!muse_turn_begin()&&error_seen==MUSE_TURN_OFFLINE&&live==0);
 reset();fail_malloc=1;assert(!muse_turn_begin()&&error_seen==MUSE_TURN_NO_MEMORY&&live==0);
 reset();fail_open=1;assert(!muse_turn_begin()&&error_seen==MUSE_TURN_SEND_FAILED&&live==0);
 reset();assert(muse_turn_begin());int16_t short_pcm[4]={0};assert(!muse_turn_finish());assert(!muse_turn_audio(NULL,4));assert(!muse_turn_audio(short_pcm,3));fail_send=1;assert(!muse_turn_audio(short_pcm,4)&&live==0);
 reset();assert(muse_turn_begin());int16_t pcm[3200]={0};assert(muse_turn_audio(pcm,3200));fail_malloc=allocations+1;
 assert(muse_turn_finish());assert(allocations==1); /* No speculative 8 KB history allocation. */
 deliver(next_id,200,"{}");muse_turn_poll();assert(error_seen==MUSE_TURN_NO_MEMORY&&live==0);
 reset();max_allocation=ACK_CAP;note=begin_finish();mark_and_ack(note);
 row("{\"result\":{\"chat_events\":[{\"seq\":11,\"event_name\":\"message.assistant\",\"reply_to_message_id\":\"note-1\",\"display_text_ready\":true,\"display_text\":\"Reply with small heap blocks\"}]}}");
 assert(!strcmp(text_seen,"Reply with small heap blocks"));clock_us+=3000001;muse_turn_poll();assert(muse_turn_state()==MUSE_TURN_DONE&&live==0);
 reset();note=begin_finish();int marker=next_id;
 deliver(marker,200,"{\"result\":{\"chat_events\":[]}}");muse_turn_poll();
 audio_idle=false;deliver(note,200,"{\"result\":{\"message_id\":\"note-1\"}}");muse_turn_poll();
 assert(muse_turn_state()==MUSE_TURN_REPLY&&next_id==marker);
 audio_idle=true;muse_turn_poll();assert(next_id==marker+1);
 reset();note=begin_finish();deliver(next_id,403,"{}");muse_turn_poll();assert(error_seen==MUSE_TURN_BAD_RESPONSE&&live==0);
 reset();note=begin_finish();deliver(next_id,200,"{\"result\":{\"chat_events\":[]}}");muse_turn_poll();deliver(note,200,"{}");muse_turn_poll();assert(error_seen==MUSE_TURN_BAD_RESPONSE&&live==0);
 reset();note=begin_finish();clock_us+=15000001;muse_turn_poll();assert(error_seen==MUSE_TURN_TIMEOUT&&live==0);
 reset();note=begin_finish();mark_and_ack(note);ready=false;muse_turn_poll();assert(error_seen==MUSE_TURN_OFFLINE&&live==0);
 reset();note=begin_finish();mark_and_ack(note);int id=next_id;char too_big[ROW_CAP+10];memset(too_big,'x',sizeof(too_big));requests[id].callback(requests[id].ctx,200,(uint8_t *)too_big,sizeof(too_big),true);muse_turn_poll();assert(error_seen==MUSE_TURN_TOO_LONG&&live==0);
 reset();note=begin_finish();mark_and_ack(note);row("{\"result\":{\"chat_events\":[{\"seq\":9007199254740992}]}}");assert(error_seen==MUSE_TURN_BAD_RESPONSE&&live==0);
 for(int cycle=0;cycle<1000;cycle++){reset();int old=begin_finish();req_t late=requests[old];muse_turn_cancel();assert(live==0);assert(muse_turn_begin());size_t before=responses[NOTE].used;late.callback(late.ctx,200,(const uint8_t *)"late",4,true);assert(responses[NOTE].used==before);muse_turn_cancel();assert(live==0);}
 puts("PASS actual Muse voice turn, cursor ordering, correlation, fragmented callbacks, faults and 1000 cancelled/replaced turns");
}
'''
class VoiceTurnTest(unittest.TestCase):
    def test_actual_turn_with_virtual_relay(self):
        idf=Path(os.environ.get('IDF_PATH',ROOT/'.toolchains/esp-idf-v5.5.3'))
        cjson=idf/'components/json/cJSON'
        if not (cjson/'cJSON.c').is_file():self.skipTest('ESP-IDF cJSON source required')
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            for name in ['freertos/FreeRTOS.h','freertos/semphr.h','esp_random.h']:
                path=work/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(HEADERS[name])
            (work/'freertos/semphr.h').write_text(HEADERS['freertos/semphr.h']+'\nSemaphoreHandle_t xSemaphoreCreateMutex(void);\n')
            (work/'esp_timer.h').write_text('#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n')
            harness=work/'turn.c';harness.write_text(HARNESS);binary=work/'turn'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(work),'-I'+str(ROOT/'main'),
                '-I'+str(cjson),str(harness),str(cjson/'cJSON.c'),'-lm','-o',str(binary)],check=True)
            result=subprocess.run([str(binary)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS actual Muse voice turn',result.stdout)
