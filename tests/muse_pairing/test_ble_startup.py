"""Fault injection for the production BLE startup function, not radio acceptance."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HARNESS=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_MUSE_PHONE_BRIDGE 1
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define configMAX_PRIORITIES 25
#define pdPASS 1
#define ESP_OK 0
#define ESP_ERR_NO_MEM 257
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static size_t heap_free=100000,heap_largest=65536;
static size_t heap_caps_get_free_size(unsigned caps){assert(caps==3);return heap_free;}
static size_t heap_caps_get_largest_free_block(unsigned caps){assert(caps==3);return heap_largest;}
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef void *TaskHandle_t;
typedef struct {int placeholder;} ble_callbacks_t;
struct ble_gatt_svc_def {int unused;};
static const struct ble_gatt_svc_def s_svcs[1],companion_svcs[1],bridge_svcs[1];
static struct {const struct ble_gatt_svc_def *svcs;void (*configure_host)(void);} s_companion={companion_svcs,NULL};
static struct {void (*sync_cb)(void);void (*reset_cb)(int);} ble_hs_cfg;
static bool s_started,s_start_cleanup_failed,s_shutting_down,s_synced,s_advertising_enabled,s_advertising_active;
static ble_callbacks_t s_cb;
static const char *s_device_name;
static void *s_rx_mutex,*s_tx_mutex;
static unsigned calls,fail_at,live,port_live,host_live,deinits;
static bool fail_cleanup;
static bool step(void){return ++calls!=fail_at;}
static void *xSemaphoreCreateMutex(void){if(!step())return NULL;void *p=malloc(1);assert(p);live++;return p;}
static void vSemaphoreDelete(void *p){assert(p && live);free(p);live--;}
static int nimble_port_init(void){assert(!port_live);if(!step())return -1;port_live=1;return ESP_OK;}
static int nimble_port_deinit(void){assert(port_live && !host_live);deinits++;if(fail_cleanup)return -1;port_live=0;return ESP_OK;}
static void ble_svc_gap_init(void){assert(port_live);}
static void ble_svc_gatt_init(void){assert(port_live);}
static int ble_gatts_count_cfg(const struct ble_gatt_svc_def *svcs){assert(port_live && svcs);return step()?0:-1;}
static int ble_gatts_add_svcs(const struct ble_gatt_svc_def *svcs){assert(port_live && svcs);return step()?0:-1;}
static const struct ble_gatt_svc_def *phone_bridge_services(void){return bridge_svcs;}
static int ble_svc_gap_device_name_set(const char *name){assert(name && *name);return step()?0:-1;}
static void on_sync(void){}
static void on_reset(int reason){(void)reason;}
static void host_task(void *arg){(void)arg;}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out){
 assert(fn==host_task && name && stack==4096 && !arg && priority==21 && out && s_started && port_live);
 if(!step()){*out=NULL;return 0;}host_live=1;*out=&host_live;return pdPASS;
}
'''
END=r'''
int main(int argc,char **argv){
 assert(argc==4);fail_at=(unsigned)atoi(argv[1]);fail_cleanup=atoi(argv[2]);
 s_companion.svcs=atoi(argv[3])?companion_svcs:NULL;unsigned last=s_companion.svcs?11:9;
 if(fail_at==99){
  fail_at=0;heap_free=49151;assert(!ble_server_start("MuseGadget-test",NULL)&&!calls&&!port_live);
  heap_free=100000;heap_largest=32767;assert(!ble_server_start("MuseGadget-test",NULL)&&!calls&&!port_live);
  heap_largest=65536;assert(ble_server_start("MuseGadget-test",NULL)&&port_live);return 0;
 }
 assert(!ble_server_start(NULL,NULL) && !calls);
 assert(!ble_server_start("",NULL) && !calls);
 ble_callbacks_t cb={123};
 bool ok=ble_server_start("MuseGadget-test",&cb);
 if(fail_at){
  assert(!ok && !s_started && !live && !host_live && !s_rx_mutex && !s_tx_mutex);
  assert(!s_device_name && s_cb.placeholder==0);
  assert(deinits==(fail_at>3?1u:0u));
  if(fail_cleanup){unsigned previous=calls;assert(s_start_cleanup_failed && port_live);assert(!ble_server_start("MuseGadget-test",&cb) && calls==previous);return 0;}
  assert(!port_live && !s_start_cleanup_failed);
  fail_at=0;calls=0;assert(ble_server_start("MuseGadget-test",&cb));
 }else assert(ok);
 assert(s_started && live==2 && port_live && host_live && calls==last);
 assert(s_cb.placeholder==123 && ble_hs_cfg.sync_cb==on_sync && ble_hs_cfg.reset_cb==on_reset);
 assert(ble_server_start("MuseGadget-test",&cb) && calls==last && live==2);
 return 0;
}
'''
class BleStartupTest(unittest.TestCase):
    def test_start_failure_rollback_retry_and_idempotence(self):
        source=(ROOT/'main/muse/ble_server.c').read_text()
        start=source.index('bool ble_server_start(')
        end=source.index('\ntypedef struct {\n    struct ble_npl_event event;',start)
        with tempfile.TemporaryDirectory() as directory:
            cfile=Path(directory)/'startup.c';binary=Path(directory)/'startup'
            cfile.write_text(HARNESS+source[start:end]+END)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True)
            for companion in range(2):
                last=11 if companion else 9
                for fail in range(last+1):
                    with self.subTest(companion=companion,failure_step=fail):
                        subprocess.run([str(binary),str(fail),'0',str(companion)],check=True)
                subprocess.run([str(binary),str(last),'1',str(companion)],check=True)
                subprocess.run([str(binary),'99','0',str(companion)],check=True)
