#include "passport_muse_voice.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_log.h"
#endif
#if CONFIG_PASSPORT_MUSE_BLE
#include "passport_app.h"
#include "passport_i18n.h"
#include "muse/phone_bridge.h"
#include "muse/voice_turn.h"
#include "muse/muse_speech.h"
#include "passport_direct.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
static bool received;
static char speech_note[80],speech_replies[8][80];
static unsigned speech_count,speech_next;
static atomic_bool speech_pending;
static atomic_uint probe_stack;
static char voice_status[320];
const char *passport_muse_voice_status(void){return voice_status;}
void passport_muse_voice_message(const char *text){
 xSemaphoreTake(passport_mutex,portMAX_DELAY);
 if(passport_function_active()==PAGE_MUSE){snprintf(voice_status,sizeof(voice_status),"%s",text);passport_dirty=true;}
 xSemaphoreGive(passport_mutex);
}
static atomic_bool leaving;
void passport_muse_voice_leave(void){atomic_store(&probe_stack,0);atomic_store(&leaving,true);atomic_store(&speech_pending,false);muse_speech_stop();}
bool passport_muse_voice_ready(void){return phone_bridge_ready();}
bool passport_muse_voice_active(void){muse_turn_state_t s=muse_turn_state();return s>=MUSE_TURN_RECORDING&&s<=MUSE_TURN_REPLY;}
bool passport_muse_voice_prepare(void){
 if(!phone_bridge_ready()||passport_function_active()!=PAGE_MUSE)return false;
 xSemaphoreTake(passport_mutex,portMAX_DELAY);
 passport_reply_select(PAGE_APPS);voice_status[0]=0;bool ok=true;
 xSemaphoreGive(passport_mutex);return ok;
}
static void update(muse_turn_state_t state,muse_turn_error_t error,const char *text){
 const char *status=NULL;
 if(!text){
  if(state==MUSE_TURN_MARKING)status=T("正在发送语音","Sending voice note");
  else if(state==MUSE_TURN_ACK || state==MUSE_TURN_REPLY)status=T("等待 Muse 回复","Waiting for Muse reply");
  else if(state==MUSE_TURN_ERROR){
   switch(error){
   case MUSE_TURN_NO_MEMORY:status=T("内存不足\n请稍后重试","Low RAM\nPlease retry");break;
   case MUSE_TURN_OFFLINE:status=T("Muse 手机连接已断开","Muse phone disconnected");break;
   case MUSE_TURN_TIMEOUT:status=T("Muse 回复超时\n请在手机查看","Muse reply timed out\nCheck the phone app");break;
   case MUSE_TURN_TOO_LONG:status=T("回复过长\n请在 Muse 应用查看","Reply too long\nRead it in the Muse app");break;
   default:status=T("Muse 语音请求失败\n请重试","Muse voice request failed\nPlease retry");break;
   }
  }
 }
 if(text && muse_turn_message_id()[0] && speech_count<8){
  if(!speech_count || strcmp(speech_replies[speech_count-1],muse_turn_message_id()))
   snprintf(speech_replies[speech_count++],sizeof(speech_replies[0]),"%s",muse_turn_message_id());
 }
#ifdef ESP_PLATFORM
 if(text)ESP_LOGI("muse_speech_queue","text ready: message_id=%u queued=%u",muse_turn_message_id()[0]!=0,speech_count);
 if(state==MUSE_TURN_DONE)ESP_LOGI("muse_speech_queue","turn done: queued=%u",speech_count);
#endif
 if(state==MUSE_TURN_DONE && speech_count){
  snprintf(speech_note,sizeof(speech_note),"%s",muse_turn_note_id());
    xSemaphoreTake(passport_mutex,portMAX_DELAY);passport_reply_compact(1);xSemaphoreGive(passport_mutex);
  atomic_store(&speech_pending,true);
 }
 if(!text&&!status)return;
 xSemaphoreTake(passport_mutex,portMAX_DELAY);
 if(passport_function_active()==PAGE_MUSE){
  if(!text){snprintf(voice_status,sizeof(voice_status),"%s",status);passport_dirty=true;xSemaphoreGive(passport_mutex);return;}
  passport_reply_select(PAGE_MUSE);char *reply=passport_reply_buffer(1);
  if(reply){
   voice_status[0]=0;
   if(text){
    if(!received)reply[0]=0;
    size_t used=strlen(reply);if(used&&used+2<PASSPORT_REPLY_CAPACITY){strcat(reply,"\n\n");used+=2;}
    size_t n=passport_utf8_prefix(text,PASSPORT_REPLY_CAPACITY-used-1);memcpy(reply+used,text,n);reply[used+n]=0;received=true;
   }else if(!received || state==MUSE_TURN_ERROR)snprintf(reply,PASSPORT_REPLY_CAPACITY,"%s",status);
   passport_dirty=true;
  }
 }
 xSemaphoreGive(passport_mutex);
}
bool passport_muse_voice_begin(void){
 received=false;atomic_store(&probe_stack,0);speech_count=speech_next=0;atomic_store(&speech_pending,false);atomic_store(&leaving,false);
 if(!muse_turn_init(update)){update(MUSE_TURN_ERROR,MUSE_TURN_NO_MEMORY,NULL);return false;}
 return muse_turn_begin();
}
bool passport_muse_voice_audio(const int16_t *pcm,size_t frames){return muse_turn_audio(pcm,frames);}
bool passport_muse_voice_finish(void){return muse_turn_finish();}
void passport_muse_voice_cancel(void){muse_turn_cancel();}
unsigned passport_muse_voice_poll(void){
 if(atomic_exchange(&leaving,false)||passport_function_active()!=PAGE_MUSE){if(passport_muse_voice_active())muse_turn_cancel();xSemaphoreTake(passport_mutex,portMAX_DELAY);voice_status[0]=0;xSemaphoreGive(passport_mutex);return 0;}
 muse_turn_poll();muse_turn_state_t s=muse_turn_state();
 return s==MUSE_TURN_DONE?2:s==MUSE_TURN_ERROR?3:passport_muse_voice_active()?1:0;
}

bool passport_muse_speech_probe_request(unsigned stack){
 if(passport_function_active()!=PAGE_MUSE || passport_voice_needs_worker() || muse_speech_busy() || atomic_load(&speech_pending))return false;
 atomic_store(&probe_stack,stack);atomic_store(&speech_pending,true);return true;
}
bool passport_muse_speech_busy(void){return muse_speech_busy();}
void passport_muse_speech_stop(void){atomic_store(&probe_stack,0);atomic_store(&speech_pending,false);muse_speech_stop();}
bool passport_muse_speech_can_start(void){
 return atomic_load(&speech_pending) && !passport_audio_worker_active() && phone_bridge_audio_idle() && !muse_speech_busy();
}
void passport_muse_speech_poll(void){
 if(!passport_muse_speech_can_start())return;
 if(passport_function_active()!=PAGE_MUSE){atomic_store(&speech_pending,false);return;}
 if(!atomic_exchange(&speech_pending,false))return;
 passport_direct_release_caches();
 muse_speech_init();muse_speech_reset();
 unsigned probe=atomic_exchange(&probe_stack,0);
 if(probe){(void)muse_speech_probe(probe);return;}
 (void)muse_speech_request(speech_note,speech_replies[speech_next++]);
 if(speech_next<speech_count)atomic_store(&speech_pending,true);
}

#else
bool passport_muse_speech_probe_request(unsigned stack){(void)stack;return false;}
void passport_muse_speech_poll(void){}
bool passport_muse_speech_can_start(void){return false;}
bool passport_muse_speech_busy(void){return false;}
void passport_muse_speech_stop(void){}
const char *passport_muse_voice_status(void){return "";}
void passport_muse_voice_message(const char *text){(void)text;}
void passport_muse_voice_leave(void){}
bool passport_muse_voice_ready(void){return false;}
bool passport_muse_voice_active(void){return false;}
bool passport_muse_voice_prepare(void){return false;}
bool passport_muse_voice_begin(void){return false;}
bool passport_muse_voice_audio(const int16_t *pcm,size_t frames){(void)pcm;(void)frames;return false;}
bool passport_muse_voice_finish(void){return false;}
void passport_muse_voice_cancel(void){}
unsigned passport_muse_voice_poll(void){return 0;}
#endif
