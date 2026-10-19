/* Pocket Passport application glue. Security host configuration adapted from
 * FalkoWing/muse-passport muse_ble.c, Apache-2.0, copyright Meta Platforms, Inc.
 * and affiliates. See muse/LICENSE. No upstream UI or board code is copied. */
#include "passport_muse.h"
#include "passport_i18n.h"
#include <stdio.h>
#include <stdatomic.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_PASSPORT_MUSE_BLE
#include "passport_app.h"
#include "muse/ble_server.h"
#include "muse/config_store.h"
#include "muse/identity.h"
#include "muse/link_pairing.h"
#include "muse/phone_bridge.h"
#include "muse/setup_controller.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include <string.h>
void ble_store_config_init(void);
/* 0 not requested, 1 starting, 2 available, 3 startup failed. */
static atomic_uint runtime, passkey;
static atomic_bool requested, redraw;
static bool initialized;
void passport_muse_transport_changed(void){atomic_store(&redraw,true);}
static void configure_host(void){
 ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
 ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_DISP_ONLY;
 ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;ble_hs_cfg.sm_sc=1;
 ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
 ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
 ble_store_config_init();
}
static int gap_event(struct ble_gap_event *event){
 switch(event->type){
 case BLE_GAP_EVENT_PASSKEY_ACTION:
  if(event->passkey.params.action==BLE_SM_IOACT_DISP){
   struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=100000+esp_random()%900000};
   atomic_store(&passkey,io.passkey);passport_muse_transport_changed();
   int rc=ble_sm_inject_io(event->passkey.conn_handle,&io);
   if(rc){atomic_store(&passkey,0);passport_muse_transport_changed();}
   return rc;
  }
  break;
 case BLE_GAP_EVENT_DISCONNECT:case BLE_GAP_EVENT_ENC_CHANGE:
  atomic_store(&passkey,0);passport_muse_transport_changed();break;
 case BLE_GAP_EVENT_REPEAT_PAIRING:{
  /* Upstream recovery when Android forgot its bond. New credentials still
   * require authenticated passkey pairing; account records are not erased. */
  struct ble_gap_conn_desc peer;
  if(ble_gap_conn_find(event->repeat_pairing.conn_handle,&peer)==0 &&
     ble_store_util_delete_peer(&peer.peer_id_addr)==0)return BLE_GAP_REPEAT_PAIRING_RETRY;
  return BLE_GAP_REPEAT_PAIRING_IGNORE;
 }
 default:break;
 }
 return 0;
}
/* Wi-Fi belongs to the superapp Settings page. Muse reuses its authenticated
 * current link; pairing never rewrites Home/Hotspot credentials or interrupts
 * the weather service. The phone is offered the currently joined network. */
static bool current_wifi(const char *ssid,const char *password){
 (void)password;wifi_ap_record_t ap;
 return ssid && *ssid && esp_wifi_sta_get_ap_info(&ap)==ESP_OK &&
  strlen(ssid)<=32 && !strncmp((const char *)ap.ssid,ssid,sizeof(ap.ssid));
}
static cJSON *current_wifi_list(void){
 wifi_ap_record_t ap;cJSON *root=cJSON_CreateObject();if(!root)return NULL;
 cJSON_AddStringToObject(root,"type","wifi_scan_result");
 cJSON *networks=cJSON_AddArrayToObject(root,"networks");
 if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK){
  char ssid[33];memcpy(ssid,ap.ssid,32);ssid[32]=0;
  cJSON *item=cJSON_CreateObject();cJSON_AddStringToObject(item,"ssid",ssid);
  cJSON_AddNumberToObject(item,"rssi",ap.rssi);
  cJSON_AddBoolToObject(item,"secure",ap.authmode!=WIFI_AUTH_OPEN);
  cJSON_AddItemToArray(networks,item);
 }
 return root;
}
bool passport_muse_supported(void){return true;}
void passport_muse_enter(void){if(atomic_load(&runtime)!=2)atomic_store(&requested,true);}
void passport_muse_leave(void){atomic_store(&requested,false);}
unsigned passport_muse_runtime_state(void){return atomic_load(&runtime);}
unsigned passport_muse_passkey(void){return atomic_load(&passkey);}
bool passport_muse_confirm(void){return atomic_load(&runtime)==2 && muse_setup_request_confirmation();}
void passport_muse_poll(void){
 /* Old portrait widgets own this buffer until the UI has removed them. Wait
  * for that RAM to be released instead of starting Bluetooth at peak use. */
 if(atomic_load(&requested)){
  xSemaphoreTake(passport_mutex,portMAX_DELAY);bool portrait_live=passport_avatar!=NULL;xSemaphoreGive(passport_mutex);
  if(portrait_live)return;
 }
 if(atomic_exchange(&requested,false) && atomic_load(&runtime)!=2){
  atomic_store(&runtime,1);passport_muse_transport_changed();
  if(!initialized){config_store_init();identity_init();initialized=true;}
  const muse_setup_hooks_t hooks={.join_wifi=current_wifi,.scan_wifi=current_wifi_list,
   .changed=passport_muse_transport_changed};
  bool ok=link_pairing_init(identity_node_id(),identity_device_id(),identity_mac(),
    esp_app_get_description()->version,identity_sdk_token()) && phone_bridge_init() &&
    muse_setup_init(&hooks,esp_app_get_description()->version);
  if(ok){
   const ble_companion_t companion={.configure_host=configure_host,.on_gap_event=gap_event};
   ble_server_set_companion(&companion);
   ble_server_set_companion_advertising(config_setup_complete());
   ok=ble_server_start(identity_ble_name(),muse_setup_callbacks());
   if(ok)ble_server_begin_advertising();
  }
  if(!ok)phone_bridge_abort_startup();
  atomic_store(&runtime,ok?2:3);passport_muse_transport_changed();
 }
 if(atomic_load(&runtime)==2)muse_setup_poll();
 if(atomic_exchange(&redraw,false)){
  xSemaphoreTake(passport_mutex,portMAX_DELAY);passport_dirty=true;xSemaphoreGive(passport_mutex);
 }
}
void passport_muse_screen(char *out,size_t cap){
 unsigned state=atomic_load(&runtime),code=atomic_load(&passkey);
 if(code){snprintf(out,cap,T("手机蓝牙配对码\n%06u\n在 Android 输入此码","Android Bluetooth code\n%06u\nEnter this on your phone"),code);return;}
 if(state==3){snprintf(out,cap,"%s",T("蓝牙启动失败\n按确定重试\n仍失败请重启","Bluetooth start failed\nOK: retry\nRestart if it fails again"));return;}
 if(state<2){snprintf(out,cap,"%s",T("正在启动 Muse 蓝牙","Starting Muse Bluetooth"));return;}
 muse_setup_state_t setup=muse_setup_state();
 const char *status=setup==MUSE_SETUP_CONFIRM?T("确认手机配对？\n按确定批准","Pair this phone?\nPress OK to approve"):
  setup==MUSE_SETUP_JOINING?T("正在检查已连接 Wi-Fi","Checking current Wi-Fi"):
  setup==MUSE_SETUP_AUTH?T("正在保存 Muse 账号","Saving Muse account"):
  setup==MUSE_SETUP_ERROR?T("配对失败\n先在设置连接 Wi-Fi\n再用 Muse 应用重试","Pairing failed\nConnect Wi-Fi in Settings\nRetry in the Muse app"):
  phone_bridge_ready()?T("手机伴侣已连接\n按住下键说话","Phone companion ready\nHold Down to talk"):
  setup==MUSE_SETUP_PAIRED?T("Muse 账号已连接\n打开 Android 手机伴侣","Muse account paired\nOpen Android companion"):
  T("打开 Muse 应用配对\n选择当前 Wi-Fi","Pair in the Muse app\nChoose current Wi-Fi");
 snprintf(out,cap,"%s\n\n%s",identity_ble_name(),status);
}
#else
bool passport_muse_supported(void){return false;}
void passport_muse_enter(void){}
void passport_muse_leave(void){}
void passport_muse_poll(void){}
bool passport_muse_confirm(void){return false;}
unsigned passport_muse_runtime_state(void){return 0;}
unsigned passport_muse_passkey(void){return 0;}
void passport_muse_screen(char *out,size_t cap){snprintf(out,cap,"%s",T("Muse 蓝牙集成进行中","Muse Bluetooth in progress"));}
#endif
