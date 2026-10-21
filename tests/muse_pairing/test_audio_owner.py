"""Production common-audio task startup with fault-injected task allocation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#define pdPASS 1
#define ESP_LOGW(...) ((void)0)
static atomic_bool audio_desired,audio_worker_running,traffic_desired;
static bool voice_needed,fail,speech_busy;
static bool passport_muse_speech_busy(void){return speech_busy;}
static unsigned calls;
static bool passport_voice_needs_worker(void){return voice_needed;}
static void audio_worker(void *arg){(void)arg;}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *out){
 assert(fn==audio_worker&&name&&stack==6144&&!arg&&priority==3&&!out);calls++;return fail?0:pdPASS;
}
'''
END = r'''
int main(void){
 audio_worker_start_if_needed();assert(!calls&&!audio_worker_running);
 audio_desired=true;speech_busy=true;audio_worker_start_if_needed();assert(!calls);speech_busy=false;fail=true;
 audio_worker_start_if_needed();assert(calls==1&&!audio_worker_running);
 fail=false;audio_worker_start_if_needed();assert(calls==2&&audio_worker_running);
 audio_worker_start_if_needed();assert(calls==2);
 audio_desired=false;audio_worker_running=false;
 for(unsigned i=0;i<1000;i++){
  unsigned previous=calls;
  audio_worker_start_if_needed();assert(calls==previous&&!audio_worker_running);
  voice_needed=true;audio_worker_start_if_needed();assert(calls==previous+1&&audio_worker_running);
  audio_worker_start_if_needed();assert(calls==previous+1);
  voice_needed=false;audio_worker_running=false;
 }
 /* The traffic-light recording is a third legitimate audio demand. */
 traffic_desired=true;{unsigned previous=calls;audio_worker_start_if_needed();assert(calls==previous+1&&audio_worker_running);}
 return 0;
}
'''


class AudioOwnerTest(unittest.TestCase):
    def test_demand_fault_retry_and_single_owner(self):
        source = (ROOT / 'main/passport_network.c').read_text()
        start = source.index('static void audio_worker_start_if_needed(')
        end = source.index('bool passport_network_idle(', start)
        with tempfile.TemporaryDirectory() as directory:
            cfile = Path(directory) / 'owner.c'
            binary = Path(directory) / 'owner'
            cfile.write_text(HARNESS + source[start:end] + END)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(cfile), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
