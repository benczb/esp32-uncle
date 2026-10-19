"""Production relay audio allocation/retirement under serialized producers.

Host boundaries stand in for FreeRTOS, not Android or RF acceptance.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#define SLOTS 4
#define pdPASS 1
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef struct {char data[184];} audio_message_t;
static struct {int id;bool audio,ended;} slots[SLOTS];
static QueueHandle_t audio_messages;
static TaskHandle_t bridge_audio_task;
static unsigned queued,fail_at,calls,live;
static bool locked;
static void *allocate(void){assert(locked);if(++calls==fail_at)return NULL;live++;return malloc(1);}
static QueueHandle_t xQueueCreate(unsigned n,size_t size){assert(n==48&&size==sizeof(audio_message_t));return allocate();}
static void vQueueDelete(void *q){assert(locked&&q&&live);free(q);live--;}
static unsigned uxQueueMessagesWaiting(void *q){assert(locked&&q);return queued;}
static void audio_worker(void *arg){(void)arg;}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *task){
 assert(fn==audio_worker&&name&&stack==6144&&!arg&&priority==4);*task=allocate();return *task?pdPASS:0;
}
'''
END = r'''
int main(void){
 locked=true;
 for(unsigned fail=1;fail<=2;fail++){
  fail_at=fail;calls=0;assert(!audio_start_locked());assert(!audio_messages&&!bridge_audio_task&&!live);
 }
 fail_at=0;
 for(unsigned turn=0;turn<1000;turn++){
  calls=0;assert(audio_start_locked()&&live==2&&calls==2);
  assert(audio_start_locked()&&calls==2);
  assert(!audio_retire_locked(true));
  queued=1;assert(!audio_retire_locked(false));queued=0;
  slots[0].id=1;slots[0].audio=true;slots[0].ended=false;
  assert(!audio_retire_locked(false));
  slots[0].ended=true;
  void *task=bridge_audio_task;
  assert(audio_retire_locked(false)&&!audio_messages&&!bridge_audio_task&&live==1);
  /* The retiring task frees its own stack after releasing slots_lock. */
  free(task);live--;
  slots[0].id=0;
  assert(!audio_retire_locked(false)&&!live);
 }
 return 0;
}
'''


class AudioLifecycleTest(unittest.TestCase):
    def test_lazy_allocation_faults_and_quiescent_retirement(self):
        source = (ROOT / 'main/muse/phone_bridge.c').read_text()
        retire = source[source.index('static bool audio_retire_locked('):
                        source.index('static void audio_worker(')]
        start = source[source.index('static bool audio_start_locked('):
                       source.index('/* Called before BLE callbacks')]
        with tempfile.TemporaryDirectory() as directory:
            cfile = Path(directory) / 'lifecycle.c'
            binary = Path(directory) / 'lifecycle'
            cfile.write_text(HARNESS + retire + start + END)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(cfile), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
