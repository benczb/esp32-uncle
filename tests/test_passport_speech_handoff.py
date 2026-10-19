"""Actual supervisor: one owner, idle cleanup gap, cancellation and OOM retry."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include "passport_model.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#define pdPASS 1
static atomic_bool network_worker_running,network_speech_handoff,network_busy,audio_desired,reply_audio;
static atomic_bool network_photo_handoff;
static int64_t network_photo_ready_at;
static bool passport_has_avatar,refresh,locked,reserve_fail;
static void *passport_avatar;
static unsigned reservations;
static void lock(void){assert(!locked);locked=true;}
static void unlock(void){assert(locked);locked=false;}
static bool passport_photo_refresh_pending(void){assert(locked);return refresh;}
static bool passport_photo_reserve(void){assert(locked&&!network_worker_running);reservations++;return !reserve_fail;}
static int64_t network_speech_ready_at,network_voice_ready_at,now;
static bool network_speech_was_busy;
static void *work_queue=(void *)1;
static bool busy,voice,fail,silent,phone;
static unsigned active=PAGE_MUSE,created,requests,stops,audio_starts;
static int64_t esp_timer_get_time(void){return now;}
static unsigned passport_function_active(void){return active;}
static bool passport_voice_busy(void){return voice;}
static void audio_worker_start_if_needed(void){audio_starts++;}
static bool phone_relay_active(void){return phone;}
static void wifi_timeout_check(void){}
static bool passport_muse_speech_busy(void){return busy;}
static void passport_muse_speech_stop(void){stops++;}
static void passport_muse_speech_poll(void){assert(!network_worker_running);requests++;busy=!silent;}
static void network_worker(void *p){(void)p;}
static int xTaskCreate(void(*fn)(void *),const char *name,unsigned bytes,void *arg,unsigned priority,void *handle){
 assert(fn==network_worker && name && bytes==8192 && !arg && priority==3 && !handle);
 created++;return fail?0:pdPASS;
}
'''
END=r'''
int main(void){
 network_busy=true;assert(!passport_network_idle());network_busy=false;
 voice=true;network_worker_running=true;assert(!passport_network_idle());
 network_worker_running=false;network_voice_ready_at=200;now=100;assert(!passport_network_idle());
 now=200;assert(passport_network_idle());voice=false;now=0;
 network_speech_handoff=true;network_speech_ready_at=100000;
 passport_background_poll();assert(!requests && !created);
 now=100000;passport_background_poll();assert(requests==1 && busy && !created);
 for(unsigned i=0;i<20;i++)passport_background_poll();
 assert(!created && requests==1);
 active=PAGE_RADIO;passport_background_poll();assert(stops==1 && !created && busy);
 busy=false;passport_background_poll();assert(network_speech_handoff && !network_worker_running);
 now+=100000;passport_background_poll();assert(!network_speech_handoff && network_worker_running && created==1);
 passport_background_poll();assert(created==1);
 network_worker_running=false;network_speech_handoff=true;voice=true;
 passport_background_poll();assert(stops==2 && created==1 && !network_worker_running && audio_starts==1);
 network_worker_running=false;network_speech_handoff=true;voice=false;active=PAGE_MUSE;silent=true;
 passport_background_poll();assert(requests==2 && created==2 && network_worker_running);
 network_worker_running=false;fail=true;passport_background_poll();assert(!network_worker_running && created==3);
 fail=false;passport_background_poll();assert(network_worker_running && created==4);
 network_worker_running=false;phone=true;audio_desired=true;
 passport_background_poll();assert(!network_worker_running&&created==4&&audio_starts==2);
 audio_desired=false;passport_background_poll();assert(network_worker_running&&created==5);
 network_worker_running=false;phone=false;audio_desired=true;reply_audio=true;
 passport_background_poll();assert(!network_worker_running&&created==5&&audio_starts==3);
 reply_audio=false;passport_background_poll();assert(network_worker_running&&created==6);
 /* Photo memory is reserved only after the retired worker cleanup gap. */
 network_photo_handoff=true;network_photo_ready_at=now+100000;active=PAGE_HOME;passport_has_avatar=true;
 passport_background_poll();assert(network_photo_handoff&&!reservations);
 network_worker_running=false;passport_background_poll();assert(network_photo_handoff&&!reservations);
 now+=100000;passport_background_poll();assert(!network_photo_handoff&&reservations==1&&network_worker_running);
 /* Navigation, invalidation, and an existing photo must not allocate. */
 for(unsigned i=0;i<3;i++){
  network_worker_running=false;network_photo_handoff=true;
  active=i==0?PAGE_RADIO:PAGE_CARD;refresh=i==1;passport_avatar=i==2?(void *)1:NULL;
  passport_background_poll();assert(!network_photo_handoff&&reservations==1&&network_worker_running);
 }
 refresh=false;passport_avatar=NULL;active=PAGE_CARD;reserve_fail=true;
 network_worker_running=false;network_photo_handoff=true;
 passport_background_poll();assert(!network_photo_handoff&&reservations==2&&network_worker_running&&!locked);
}
'''
class HandoffTest(unittest.TestCase):
 def test_supervisor(self):
  source=(ROOT/'main/passport_network.c').read_text()
  idle=source[source.index('bool passport_network_idle(void)'):source.index('void passport_function_select(')]
  source=idle+source[source.index('static void network_worker_start(void)'):source.index('void passport_network_poll(')]
  with tempfile.TemporaryDirectory() as directory:
   p=Path(directory);(p/'test.c').write_text(HARNESS+source+END)
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'main'),str(p/'test.c'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':unittest.main()
