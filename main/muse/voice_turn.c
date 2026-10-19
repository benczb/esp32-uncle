/* Pocket Passport voice turn over the official Muse Android relay.
 * Protocol reference: FalkoWing/muse-passport muse_chat_link.c, Apache-2.0
 * (copyright Meta Platforms, Inc. and affiliates; see LICENSE).
 * Own worker/response state; no upstream UI, speech playback or board drivers. */
#include "voice_turn.h"
#include "phone_bridge.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_random.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_heap_caps.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#define ACK_CAP 2048
#define ROW_CAP 8192
#define REQUEST_US 15000000LL
#define REPLY_US 60000000LL
#define BUSY_US 180000000LL
#define QUIET_US 3000000LL
#define POLL_US 500000LL
enum {NOTE,ROW};
typedef struct {uint32_t generation;int64_t id,deadline;char *body;size_t used,cap;
 int status;bool done,overflow,no_memory;} response_t;
static response_t responses[2];
static SemaphoreHandle_t mutex;
static atomic_uint state,error;
static muse_turn_changed_t changed;
static uint32_t generation;
static uint64_t after;
static char note_id[80],speech_message[80];
const char *muse_turn_note_id(void){return note_id;}
const char *muse_turn_message_id(void){return speech_message;}
static bool after_note,replied,agent_busy;
static int64_t ended,reply_at,next_poll;
static unsigned samples;
static muse_turn_error_t request_error;
static void take(void){xSemaphoreTake(mutex,portMAX_DELAY);}
static void give(void){xSemaphoreGive(mutex);}
muse_turn_state_t muse_turn_state(void){return (muse_turn_state_t)atomic_load(&state);}
muse_turn_error_t muse_turn_error(void){return (muse_turn_error_t)atomic_load(&error);}
static void emit(muse_turn_state_t next,muse_turn_error_t why,const char *text){
 atomic_store(&state,next);atomic_store(&error,why);if(changed)changed(next,why,text);
}
/* An opaque monotonically increasing token prevents late frames using a freed
 * or replacement response. Never recycle tokens within one boot. */
static void frame(void *context,int status,const uint8_t *data,size_t bytes,bool end){
 uintptr_t token=(uintptr_t)context;unsigned slot=token&1u;uint32_t serial=token>>1;
 take();response_t *r=&responses[slot];
 if(serial && serial==r->generation && !r->done){
  if(status)r->status=status;
  size_t limit=slot==NOTE?ACK_CAP:ROW_CAP;
  if(bytes && (!data || bytes>=limit-r->used))r->overflow=true;
  else if(bytes && !r->overflow){
   size_t needed=r->used+bytes+1;
   /* Match upstream history gathering: reserve only bytes that arrived, not
    * an 8 KB row while microphone upload still owns its task and queue. */
   if(needed>r->cap){
    char *grown=realloc(r->body,needed);
    if(!grown){r->no_memory=true;r->done=true;}
    else {r->body=grown;r->cap=needed;}
   }
   if(!r->no_memory){memcpy(r->body+r->used,data,bytes);r->used+=bytes;r->body[r->used]=0;}
  }
  r->done=r->done||end||status<0;
 }
 give();
}
static void drop(unsigned slot){
 take();response_t *r=&responses[slot];int64_t id=r->id;char *body=r->body;
 memset(r,0,sizeof(*r));give();free(body);
 if(id)phone_bridge_cancel(id);
}
static void release_responses(void){drop(NOTE);drop(ROW);}
static bool request(unsigned slot,const char *verb,const char *path,bool end_body){
 drop(slot);request_error=MUSE_TURN_SEND_FAILED;
 if(generation>=0x7fffffffu){request_error=MUSE_TURN_BAD_RESPONSE;return false;}
 size_t cap=slot==NOTE?ACK_CAP:0;char *body=cap?malloc(cap):NULL;
 if(cap && !body){request_error=MUSE_TURN_NO_MEMORY;return false;}
 if(body)body[0]=0;
 uint32_t serial=++generation;
 take();responses[slot]=(response_t){.generation=serial,.body=body,.cap=cap,
  .deadline=esp_timer_get_time()+REQUEST_US};give();
 char request_id[40];snprintf(request_id,sizeof(request_id),"passport-%08" PRIx32 "-%08" PRIx32,esp_random(),serial);
 const char *headers[]={"x-request-id",request_id,"x-app-id","hatch-web",
  "Content-Type","application/json",NULL};
 int64_t id=phone_bridge_open(verb,path,headers,end_body,frame,(void *)(((uintptr_t)serial<<1)|slot));
 take();responses[slot].id=id;give();
 if(!id){if(!phone_bridge_ready())request_error=MUSE_TURN_OFFLINE;drop(slot);return false;}return true;
}
static void fail(muse_turn_error_t why){
#ifdef ESP_PLATFORM
 if(why==MUSE_TURN_NO_MEMORY)ESP_LOGW("muse_turn","Allocation failed phase=%u free=%u largest=%u",(unsigned)muse_turn_state(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
 release_responses();emit(MUSE_TURN_ERROR,why,NULL);
}
bool muse_turn_init(muse_turn_changed_t on_change){
 if(!mutex)mutex=xSemaphoreCreateMutex();
 if(!mutex)return false;
 changed=on_change;return true;
}
void muse_turn_cancel(void){if(!mutex)return;release_responses();note_id[0]=0;emit(MUSE_TURN_IDLE,MUSE_TURN_OK,NULL);}
bool muse_turn_begin(void){
 if(!mutex)return false;
 muse_turn_cancel();
 if(!phone_bridge_ready()){fail(MUSE_TURN_OFFLINE);return false;}
 after=0;after_note=replied=agent_busy=false;samples=0;note_id[0]=speech_message[0]=0;
 if(!request(NOTE,"POST","/chat/stream",false)){fail(request_error);return false;}
 /* Recording may last longer than the normal response timeout. */
 take();responses[NOTE].deadline=esp_timer_get_time()+45000000LL;give();
 emit(MUSE_TURN_RECORDING,MUSE_TURN_OK,NULL);return true;
}
bool muse_turn_audio(const int16_t *pcm,size_t frames){
 if(muse_turn_state()!=MUSE_TURN_RECORDING || !pcm || !frames || (frames&1) || frames>16000u*30-samples)return false;
 take();bool rejected=responses[NOTE].done;int64_t id=responses[NOTE].id;give();
 if(rejected || !phone_bridge_send(id,pcm,frames*2,false,0)){fail(MUSE_TURN_SEND_FAILED);return false;}
 samples+=(unsigned)frames;return true;
}
bool muse_turn_finish(void){
 if(muse_turn_state()!=MUSE_TURN_RECORDING || samples<3200)return false;
 /* Read the cursor BEFORE committing the note's final body frame. Even a fast
  * transcription/reply cannot fall behind a cursor sampled after its reply. */
 if(!request(ROW,"GET","/chat/history?limit=1&caption=1",true)){fail(request_error);return false;}
 take();responses[NOTE].deadline=esp_timer_get_time()+REQUEST_US+REPLY_US;give();
 emit(MUSE_TURN_MARKING,MUSE_TURN_OK,NULL);return true;
}
static bool completed(unsigned slot){take();bool done=responses[slot].generation&&responses[slot].done;give();return done;}
static cJSON *response_json(unsigned slot){
 response_t *r=&responses[slot];
 if(r->no_memory){fail(MUSE_TURN_NO_MEMORY);return NULL;}
 if(r->overflow){fail(MUSE_TURN_TOO_LONG);return NULL;}
 if(r->status!=200){fail(r->status<0?MUSE_TURN_OFFLINE:MUSE_TURN_BAD_RESPONSE);return NULL;}
 cJSON *j=cJSON_Parse(r->body);if(!j)fail(MUSE_TURN_BAD_RESPONSE);return j;
}
static const char *string(cJSON *j,const char *key){const char *s=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j,key));return s?s:"";}
static cJSON *result(cJSON *j){cJSON *r=cJSON_GetObjectItemCaseSensitive(j,"result");return cJSON_IsObject(r)?r:j;}
static bool sequence(cJSON *row,uint64_t *value){
 cJSON *seq=cJSON_GetObjectItemCaseSensitive(row,"seq");
 if(!cJSON_IsNumber(seq)||!isfinite(seq->valuedouble)||seq->valuedouble<0||seq->valuedouble>9007199254740991.0||floor(seq->valuedouble)!=seq->valuedouble)return false;
 *value=(uint64_t)seq->valuedouble;return true;
}
static bool page(cJSON *root,bool marking){
 cJSON *data=result(root),*events=cJSON_GetObjectItemCaseSensitive(data,"chat_events");
 if(!cJSON_IsArray(events))return false;
 agent_busy=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data,"agent_busy"))||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"agent_busy"));
 cJSON *row=cJSON_GetArrayItem(events,0);
 if(!row){if(marking)after=0;return true;}
 uint64_t seq;if(!sequence(row,&seq))return false;
 if(marking){after=seq;return true;}
 if(seq<=after)return true;
 const char *event=string(row,"event_name"),*message=string(row,"message_id");
 if(!strcmp(event,"message.user"))after_note=note_id[0]&&!strcmp(message,note_id);
 if(!strcmp(event,"message.assistant")){
  const char *reply_to=string(row,"reply_to_message_id");
  bool related=reply_to[0]?!strcmp(reply_to,note_id):after_note;
  if(related){
   if(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(row,"display_text_ready")))return true;
   const char *text=string(row,"display_text");
   if(text[0]){if(message[0] && strlen(message)<sizeof(speech_message))snprintf(speech_message,sizeof(speech_message),"%s",message);replied=true;reply_at=esp_timer_get_time();if(changed)changed(MUSE_TURN_REPLY,MUSE_TURN_OK,text);}
  }
 }
 after=seq;return true;
}
void muse_turn_poll(void){
 muse_turn_state_t current=muse_turn_state();
 if(current<MUSE_TURN_RECORDING || current>MUSE_TURN_REPLY)return;
 if(!phone_bridge_ready()){fail(MUSE_TURN_OFFLINE);return;}
 int64_t now=esp_timer_get_time();
 for(unsigned i=0;i<2;i++){
  take();bool expired=responses[i].generation&&!responses[i].done&&now>responses[i].deadline;give();
  if(expired){fail(MUSE_TURN_TIMEOUT);return;}
 }
 if(current==MUSE_TURN_RECORDING)return;
 if(current==MUSE_TURN_MARKING && completed(ROW)){
  cJSON *j=response_json(ROW);if(!j)return;
  bool ok=page(j,true);cJSON_Delete(j);drop(ROW);
  if(!ok){fail(MUSE_TURN_BAD_RESPONSE);return;}
  take();int64_t note=responses[NOTE].id;give();
  if(!phone_bridge_send(note,NULL,0,true,0)){fail(MUSE_TURN_SEND_FAILED);return;}
  ended=now;take();responses[NOTE].deadline=now+REPLY_US;give();
  emit(MUSE_TURN_ACK,MUSE_TURN_OK,NULL);current=MUSE_TURN_ACK;
 }
 if(current==MUSE_TURN_ACK && completed(NOTE)){
  cJSON *j=response_json(NOTE);if(!j)return;
  const char *id=string(result(j),"message_id");bool ok=id[0]&&strlen(id)<sizeof(note_id);
  if(ok)snprintf(note_id,sizeof(note_id),"%s",id);
  cJSON_Delete(j);drop(NOTE);
  if(!ok){fail(MUSE_TURN_BAD_RESPONSE);return;}
  next_poll=now;emit(MUSE_TURN_REPLY,MUSE_TURN_OK,NULL);current=MUSE_TURN_REPLY;
 }
 if(current!=MUSE_TURN_REPLY)return;
 /* Upload is now committed. Let its lazy worker/48-block queue retire before
  * requesting reply text or allocating the reply JSON tree. */
 if(!phone_bridge_audio_idle())return;
 if(completed(ROW)){
  cJSON *j=response_json(ROW);if(!j)return;
  bool ok=page(j,false);cJSON_Delete(j);drop(ROW);
  if(!ok){fail(MUSE_TURN_BAD_RESPONSE);return;}
  next_poll=now+POLL_US;
 }
 if(now-ended>BUSY_US || (!replied&&!agent_busy&&now-ended>REPLY_US)){fail(MUSE_TURN_TIMEOUT);return;}
 if(replied&&!agent_busy&&now-reply_at>QUIET_US){release_responses();emit(MUSE_TURN_DONE,MUSE_TURN_OK,NULL);return;}
 take();bool pending=responses[ROW].generation!=0;give();
 if(!pending && now>=next_poll){
  char path[100];snprintf(path,sizeof(path),"/chat/history?limit=1&after_seq=%" PRIu64 "&caption=1",after);
  if(!request(ROW,"GET",path,true))fail(request_error);
 }
}
