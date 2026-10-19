"""Actual Muse app glue with fake radio/storage boundaries, not RF acceptance."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
HEADERS={
'freertos/FreeRTOS.h':'''#pragma once
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffu
''',
'freertos/semphr.h':'''#pragma once
#include "FreeRTOS.h"
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
''',
'esp_app_desc.h':'''#pragma once
typedef struct {char version[32];} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
''',
'esp_random.h':'''#pragma once
#include <stdint.h>
uint32_t esp_random(void);
''',
'esp_wifi.h':'''#pragma once
#include <stdint.h>
#define ESP_OK 0
#define WIFI_AUTH_OPEN 0
typedef struct {uint8_t ssid[33];int rssi,authmode;} wifi_ap_record_t;
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
''',
'host/ble_hs.h':'''#pragma once
#include <stdint.h>
#define BLE_GAP_EVENT_PASSKEY_ACTION 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_ENC_CHANGE 3
#define BLE_GAP_EVENT_REPEAT_PAIRING 4
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_SM_IOACT_DISP 3
#define BLE_SM_IO_CAP_DISP_ONLY 4
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
struct ble_gap_event {int type;struct {struct {int action;} params;uint16_t conn_handle;} passkey;struct {uint16_t conn_handle;} repeat_pairing;};
struct ble_gap_conn_desc {int peer_id_addr;};
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int ble_store_util_delete_peer(const int *);
struct ble_sm_io {int action;uint32_t passkey;};
struct test_ble_cfg {void (*store_status_cb)(void);int sm_io_cap,sm_bonding,sm_mitm,sm_sc,sm_our_key_dist,sm_their_key_dist;};
extern struct test_ble_cfg ble_hs_cfg;
void ble_store_util_status_rr(void);
int ble_sm_inject_io(uint16_t,struct ble_sm_io *);
''',
'host/ble_sm.h':'#pragma once\n#include "ble_hs.h"\n',
'host/ble_store.h':'#pragma once\n#include "ble_hs.h"\n',
}
HARNESS=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "passport_app.h"
#include "passport_muse.h"
#include "muse/ble_server.h"
#include "muse/setup_controller.h"
#include "host/ble_hs.h"
#include "esp_app_desc.h"
#include "esp_wifi.h"
struct test_ble_cfg ble_hs_cfg;
SemaphoreHandle_t passport_mutex=(void *)1;
volatile bool passport_dirty;
uint8_t *passport_avatar;
static int failure,starts,config_inits,identity_inits,confirmations,polls,locks,bond_inits,inject_failure,advertising,bond_deletes,lookup_failure,relay_aborts;
static bool online=true,paired,relay_ready;
static muse_setup_state_t setup_state=MUSE_SETUP_IDLE;
static muse_setup_hooks_t hooks;
static ble_companion_t companion;
unsigned passport_language_get(void){return 1;}
const char *passport_tr(const char *zh,const char *en){(void)zh;return en;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned t){assert(h&&t==portMAX_DELAY);locks++;return 1;}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h);return 1;}
void config_store_init(void){config_inits++;}
bool config_setup_complete(void){return paired;}
void identity_init(void){identity_inits++;}
const char *identity_node_id(void){return "homelink-test";}
const char *identity_device_id(void){return "hatch-link:test";}
const char *identity_mac(void){return "00:11:22:33:44:55";}
const char *identity_sdk_token(void){return NULL;}
const char *identity_ble_name(void){return "MuseGadget-TEST";}
const esp_app_desc_t *esp_app_get_description(void){static const esp_app_desc_t app={"test"};return &app;}
bool link_pairing_init(const char *n,const char *d,const char *m,const char *v,const char *s){assert(n&&d&&m&&v&&!s);return failure!=1;}
bool phone_bridge_init(void){return failure!=2;}
void phone_bridge_abort_startup(void){relay_aborts++;}
bool phone_bridge_ready(void){return relay_ready;}
bool muse_setup_init(const muse_setup_hooks_t *h,const char *v){assert(h&&v);hooks=*h;return failure!=3;}
const ble_callbacks_t *muse_setup_callbacks(void){static const ble_callbacks_t cb={0};return &cb;}
muse_setup_state_t muse_setup_state(void){return setup_state;}
void muse_setup_poll(void){polls++;}
bool muse_setup_request_confirmation(void){confirmations++;return setup_state==MUSE_SETUP_CONFIRM;}
void ble_server_set_companion(const ble_companion_t *c){companion=*c;}
void ble_server_set_companion_advertising(bool value){assert(value==paired);}
bool ble_server_start(const char *name,const ble_callbacks_t *cb){assert(!strcmp(name,"MuseGadget-TEST")&&cb);starts++;if(failure==4)return false;companion.configure_host();return true;}
void ble_server_begin_advertising(void){advertising++;}
int ble_gap_conn_find(uint16_t c,struct ble_gap_conn_desc *p){(void)c;p->peer_id_addr=123;return lookup_failure;}
int ble_store_util_delete_peer(const int *p){assert(*p==123);bond_deletes++;return 0;}
void ble_store_config_init(void){bond_inits++;}
void ble_store_util_status_rr(void){}
uint32_t esp_random(void){return 42;}
int ble_sm_inject_io(uint16_t connection,struct ble_sm_io *io){assert(connection==7&&io->action==BLE_SM_IOACT_DISP&&io->passkey==100042);return inject_failure;}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){if(!online)return -1;memset(ap,0,sizeof(*ap));strcpy((char *)ap->ssid,"Current Network");ap->rssi=-42;ap->authmode=3;return ESP_OK;}
int main(int argc,char **argv){
 assert(argc==2);failure=atoi(argv[1]);assert(passport_muse_supported());
 passport_muse_poll();assert(!starts&&!config_inits&&!polls);assert(!passport_muse_confirm());
 passport_muse_enter();assert(!starts&&!config_inits);
 passport_muse_leave();passport_muse_poll();assert(!starts&&!config_inits);
 passport_muse_enter();passport_avatar=(void *)1;passport_muse_poll();assert(!starts&&!config_inits);
 passport_avatar=NULL;passport_muse_poll();assert(config_inits==1&&identity_inits==1);
 if(failure){assert(passport_muse_runtime_state()==3 && relay_aborts==1);char error[384];passport_muse_screen(error,sizeof(error));assert(strstr(error,"failed"));failure=0;passport_muse_enter();passport_muse_poll();}
 assert(passport_muse_runtime_state()==2&&bond_inits==1&&passport_dirty&&advertising==1);
 assert(ble_hs_cfg.sm_bonding&&ble_hs_cfg.sm_mitm&&ble_hs_cfg.sm_sc&&ble_hs_cfg.sm_io_cap==BLE_SM_IO_CAP_DISP_ONLY);
 int previous=starts;passport_muse_enter();passport_muse_poll();assert(starts==previous&&config_inits==1);
 assert(hooks.join_wifi("Current Network","unused-current-link-password"));
 assert(!hooks.join_wifi("Other Network","")&&!hooks.join_wifi("","")&&!hooks.join_wifi(NULL,""));
 cJSON *list=hooks.scan_wifi();cJSON *networks=cJSON_GetObjectItem(list,"networks");assert(cJSON_GetArraySize(networks)==1);assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(networks,0),"ssid")->valuestring,"Current Network"));cJSON_Delete(list);
 online=false;assert(!hooks.join_wifi("Current Network",""));list=hooks.scan_wifi();assert(cJSON_GetArraySize(cJSON_GetObjectItem(list,"networks"))==0);cJSON_Delete(list);
 setup_state=MUSE_SETUP_CONFIRM;assert(passport_muse_confirm()&&confirmations==1);
 char text[384];passport_muse_screen(text,sizeof(text));assert(strstr(text,"approve"));
 struct ble_gap_event event={.type=BLE_GAP_EVENT_PASSKEY_ACTION,.passkey={.params={BLE_SM_IOACT_DISP},.conn_handle=7}};
 int previous_locks=locks;assert(!companion.on_gap_event(&event));assert(locks==previous_locks&&passport_muse_passkey()==100042);
 passport_muse_screen(text,sizeof(text));assert(strstr(text,"100042"));
 event.type=BLE_GAP_EVENT_ENC_CHANGE;assert(!companion.on_gap_event(&event)&&!passport_muse_passkey());
 event.type=BLE_GAP_EVENT_PASSKEY_ACTION;inject_failure=9;assert(companion.on_gap_event(&event)==9&&!passport_muse_passkey());
 event.type=BLE_GAP_EVENT_REPEAT_PAIRING;assert(companion.on_gap_event(&event)==BLE_GAP_REPEAT_PAIRING_RETRY&&bond_deletes==1);
 lookup_failure=1;assert(companion.on_gap_event(&event)==BLE_GAP_REPEAT_PAIRING_IGNORE&&bond_deletes==1);
 relay_ready=true;setup_state=MUSE_SETUP_PAIRED;passport_muse_screen(text,sizeof(text));assert(strstr(text,"Phone companion ready"));
 assert(polls>=2&&locks>=1);puts("PASS actual Muse application runtime");
}
'''
class AppRuntimeTest(unittest.TestCase):
    def test_worker_start_retry_current_wifi_confirmation_and_passkey(self):
        idf=Path(os.environ.get('IDF_PATH',ROOT/'.toolchains/esp-idf-v5.5.3'))
        cjson=idf/'components/json/cJSON'
        if not (cjson/'cJSON.c').is_file():self.skipTest('ESP-IDF cJSON source required')
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            for name,body in HEADERS.items():
                path=work/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(body)
            harness=work/'runtime.c';harness.write_text(HARNESS);binary=work/'runtime'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DCONFIG_PASSPORT_MUSE_BLE=1',
                '-I'+str(work),'-I'+str(ROOT/'main'),'-I'+str(cjson),str(harness),
                str(ROOT/'main/passport_muse.c'),str(cjson/'cJSON.c'),'-o',str(binary)],check=True)
            for failure in range(5):
                result=subprocess.run([str(binary),str(failure)],text=True,capture_output=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('PASS actual Muse application runtime',result.stdout)
