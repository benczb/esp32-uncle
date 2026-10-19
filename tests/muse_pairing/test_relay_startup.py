"""Fault-inject the actual relay startup transaction, without a BLE radio.

Only the startup functions are compiled here; C3 probe builds cover their real
FreeRTOS types. This is not a replacement for Android pairing acceptance.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef struct { int unused; } message_t;
typedef struct { char data[184]; } audio_message_t;
typedef struct {char data[264];} speech_message_t;
static SemaphoreHandle_t tx_lock, ack_sem, slots_lock;
static QueueHandle_t messages, audio_messages, speech_messages;
static TaskHandle_t bridge_audio_task, bridge_message_task;
static unsigned calls, fail_at, live, deleted_tasks;
static void *allocate(void) {
    calls++;
    if (calls==fail_at) return NULL;
    void *p=malloc(1); assert(p); live++; return p;
}
static void release(void *p) { assert(p && live); free(p); live--; }
static void *xSemaphoreCreateMutex(void) { return allocate(); }
static void *xSemaphoreCreateBinary(void) { return allocate(); }
static void *xQueueCreate(unsigned n,size_t size) { assert(n && size); return allocate(); }
static void vQueueDelete(void *p) { release(p); }
static void vSemaphoreDelete(void *p) { release(p); }
static void vTaskDelete(void *p) { deleted_tasks++; release(p); }
static void worker(void *arg) { (void)arg; }
#define pdPASS 1
#define ESP_LOGW(...) ((void)0)
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,
                       unsigned priority,TaskHandle_t *handle) {
    assert(fn && name && stack==4096 && !arg && priority==4 && handle);
    *handle=allocate(); return *handle?pdPASS:0;
}
'''
END = r'''
int main(int argc,char **argv) {
    assert(argc==2);fail_at=(unsigned)atoi(argv[1]);
    bool ok=phone_bridge_init();
    if (fail_at) {
        assert(!ok && !live);
        assert(!tx_lock && !ack_sem && !slots_lock && !messages && !audio_messages && !speech_messages);
        assert(deleted_tasks==0);
        fail_at=0;calls=0;
        assert(phone_bridge_init());
    } else assert(ok);
    assert(live==6 && calls==6);
    assert(phone_bridge_init());
    assert(live==6 && calls==6); /* Idempotence: no second task or allocation. */
    phone_bridge_abort_startup();
    assert(!live && !tx_lock && !ack_sem && !slots_lock && !messages && !audio_messages && !speech_messages);
    unsigned deleted=deleted_tasks;
    phone_bridge_abort_startup();assert(deleted_tasks==deleted);
    calls=0;assert(phone_bridge_init() && live==6 && calls==6);
    phone_bridge_abort_startup();assert(!live);
    return 0;
}
'''


class RelayStartupTest(unittest.TestCase):
    def test_all_failures_release_and_allow_retry(self):
        source = (ROOT / 'main/muse/phone_bridge.c').read_text()
        startup = source[source.index('static bool bridge_initialized;'):
                         source.index('int phone_bridge_gap_event(')]
        with tempfile.TemporaryDirectory() as directory:
            cfile = Path(directory) / 'startup.c'
            binary = Path(directory) / 'startup'
            cfile.write_text(HARNESS + startup + END)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(cfile), '-o', str(binary)], check=True)
            for allocation in range(7):
                with self.subTest(failed_operation=allocation):
                    subprocess.run([str(binary), str(allocation)], check=True)
