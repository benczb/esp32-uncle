"""Real Muse request: reserve decoder before playback stack and clean failures."""
from pathlib import Path
import subprocess,tempfile,unittest
from host_cjson import cjson_flags
ROOT=Path(__file__).resolve().parents[1]
class AllocationTest(unittest.TestCase):
 def test_fragmented_heap_reservation_and_ownership(self):
  source=(ROOT/'main/muse/muse_speech.c').read_text()
  request=source[source.index('bool muse_speech_request('):source.index('#if PASSPORT_PRIVATE_USB_TEST_CONTROL')]
  harness=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdPASS 1
#define portMAX_DELAY 0
#define xSemaphoreTake(l,t) ((void)(l))
#define xSemaphoreGive(l) ((void)(l))
#define MUSE_SPEECH_WINDOW 8
static int lock=1,buffer;
static atomic_bool busy,stopped,started;
static uint32_t next_session;
typedef struct {uint32_t id;void *decoder;} speech_task_t;
static speech_task_t playback;
static bool fail_decoder,fail_task,fail_send,task_exists,negotiated=true;
static unsigned volume=50,created,destroyed,sent,task_attempts;
static int decoder_state;
static bool decoder_live;
static unsigned passport_audio_volume_percent(void){return volume;}
static bool phone_bridge_speech_ready(void){return negotiated;}
static void *passport_opus_decoder_create(void){
 /* Models the observed 23 KiB free block: a 14 KiB task allocated first
  * would leave no 18 KiB decoder block. */
 assert(!task_exists);
 if(fail_decoder)return NULL;
 assert(!decoder_live);decoder_live=true;created++;return &decoder_state;
}
static void passport_opus_decoder_destroy(void *p){assert(p==&decoder_state && decoder_live);decoder_live=false;destroyed++;}
static void muse_speech_buffer_begin(int *b,uint32_t id){assert(b==&buffer && id);}
static void play(void *arg){(void)arg;}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle){
 assert(decoder_live);assert(fn==play && !strcmp(name,"reply_speech") && stack==14336 && priority==5 && !handle);
 assert(arg==&playback && playback.decoder==&decoder_state && playback.id==next_session);task_attempts++;
 if(fail_task)return 0;
 task_exists=true;return pdPASS;
}
static bool phone_bridge_speech_send(unsigned type,const void *body,size_t bytes){
 assert(type==13 && body && bytes==strlen(body) && task_exists && decoder_live);sent++;
 cJSON *j=cJSON_Parse(body);assert(j);assert(cJSON_GetObjectItem(j,"session")->valueint==(int)next_session);cJSON_Delete(j);
 return !fail_send;
}
static void retire(void){assert(task_exists && decoder_live);passport_opus_decoder_destroy(playback.decoder);task_exists=false;busy=false;stopped=false;}
'''
  end=r'''
int main(void){
 assert(muse_speech_request("note","message"));assert(task_exists && busy && decoder_live && sent==1);assert(!muse_speech_request("note","message"));retire();
 fail_decoder=true;assert(muse_speech_request("note","message"));assert(!busy && !decoder_live && task_attempts==1);fail_decoder=false;
 fail_task=true;assert(muse_speech_request("note","message"));assert(!busy && !decoder_live && stopped && created==destroyed);fail_task=false;stopped=false;
 fail_send=true;assert(muse_speech_request("note","message"));assert(busy && stopped && decoder_live && task_exists);retire();fail_send=false;
 unsigned before=created;volume=0;assert(muse_speech_request("note","message"));assert(created==before);volume=50;
 negotiated=false;assert(muse_speech_request("note","message"));assert(created==before);negotiated=true;
 next_session=UINT32_MAX;assert(muse_speech_request("note","message"));assert(created==destroyed && !busy);
}
'''
  cflags,ldflags=cjson_flags()
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'test.c').write_text(harness+request+end)
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*cflags,str(p/'test.c'),*ldflags,'-lm','-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
