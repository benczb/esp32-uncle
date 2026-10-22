/* Komoot BLE protocol reference: jstiefel/esp32_komoot_ble, Connection.h and
 * esp32_komoot_ble.cpp. New ESP-IDF/NimBLE implementation; no Arduino code or
 * bitmap assets are included. The phone supplies location and route guidance. */
#include "passport_navigation.h"
#include "passport_app.h"
#include "passport_muse.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static passport_navigation_t state={.status="OK: Connect to Komoot"};
static portMUX_TYPE state_mux=portMUX_INITIALIZER_UNLOCKED;
static atomic_bool wanted;
static void status(const char *value){
 portENTER_CRITICAL(&state_mux);passport_feature_copy(state.status,sizeof(state.status),value);state.valid=false;portEXIT_CRITICAL(&state_mux);
 passport_dirty=true;
}
void passport_navigation_toggle(void){atomic_store(&wanted,!atomic_load(&wanted));}
void passport_navigation_snapshot(passport_navigation_t *out){
 portENTER_CRITICAL(&state_mux);*out=state;portEXIT_CRITICAL(&state_mux);
 out->enabled=atomic_load(&wanted);
 out->stale=out->valid && esp_timer_get_time()-out->updated_us>10000000;
}
#if CONFIG_PASSPORT_NAVIGATION_BLE
#include "esp_heap_caps.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/task.h"
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(0x6c,0x43,0xb3,0x1d,0x17,0x0f,0xb2,0xa2,0xa8,0x4f,0x2f,0xd9,0x28,0xe1,0xc1,0x71);
static const ble_uuid128_t turn_uuid=BLE_UUID128_INIT(0x26,0x30,0x48,0x57,0x0a,0x27,0x35,0xb2,0x6e,0x4f,0xcb,0x9b,0x05,0xd6,0x3d,0x50);
static atomic_bool ready,started,reading,refresh,subscribed_ready;
static atomic_uint connection=BLE_HS_CONN_HANDLE_NONE,value_handle;
static uint16_t service_start,service_end,descriptor_end;
static uint8_t read_buffer[256];static size_t read_length;
static bool read_invalid;
static int64_t next_scan,next_read;
static int gap(struct ble_gap_event *event,void *arg);
void passport_navigation_synced(void){if(!ble_hs_util_ensure_addr(0))atomic_store(&ready,true);}
void passport_navigation_reset(void){atomic_store(&ready,false);atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);atomic_store(&value_handle,0);atomic_store(&reading,false);atomic_store(&subscribed_ready,false);status("Bluetooth reconnecting");}
static void disconnect(void){
 uint16_t conn=atomic_load(&connection);if(conn!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
}
static void publish(void){
 passport_turn_t turn;
 if(read_invalid || !passport_komoot_decode(read_buffer,read_length,&turn)){status("Waiting for Komoot route");return;}
 int64_t now=esp_timer_get_time();
 portENTER_CRITICAL(&state_mux);
 bool changed=!state.valid || state.turn.instruction!=turn.instruction || state.turn.direction!=turn.direction || strcmp(state.turn.street,turn.street);
 state.turn=turn;state.valid=true;state.updated_us=now;
 snprintf(state.status,sizeof(state.status),"%s",turn.direction==3?"Arrived":"Komoot connected");
 if(changed)state.overlay_until_us=now+6000000;
 portEXIT_CRITICAL(&state_mux);passport_dirty=true;
}
static int read_result(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
 (void)arg;
 if(conn!=atomic_load(&connection))return 0;
 if(!error->status && attr){
  size_t n=OS_MBUF_PKTLEN(attr->om);
  if(attr->offset!=read_length || n>sizeof(read_buffer)-read_length)read_invalid=true;
  else {os_mbuf_copydata(attr->om,0,n,read_buffer+read_length);read_length+=n;}
 }else{
  if(error->status==BLE_HS_EDONE && atomic_load(&wanted))publish();
  else if(error->status!=BLE_HS_EDONE)status("Komoot read failed; reconnecting");
  atomic_store(&reading,false);
 }
 return 0;
}
static int subscribed(uint16_t conn,const struct ble_gatt_error *error,struct ble_gatt_attr *attr,void *arg){
 (void)attr;(void)arg;
 if(conn!=atomic_load(&connection))return 0;
 if(error->status){status("Komoot subscription failed");disconnect();}else {atomic_store(&subscribed_ready,true);atomic_store(&refresh,true);}
 return 0;
}
static int descriptors(uint16_t conn,const struct ble_gatt_error *error,uint16_t chr,const struct ble_gatt_dsc *dsc,void *arg){
 (void)chr;(void)arg;
 if(conn!=atomic_load(&connection))return 0;
 if(!error->status && dsc && ble_uuid_u16(&dsc->uuid.u)==0x2902){
  const uint8_t on[]={1,0};
  if(ble_gattc_write_flat(conn,dsc->handle,on,2,subscribed,NULL))disconnect();
  return BLE_HS_EDONE;
 }
 if(error->status){status("Komoot notifications unavailable");disconnect();}
 return 0;
}
static int characteristics(uint16_t conn,const struct ble_gatt_error *error,const struct ble_gatt_chr *chr,void *arg){
 (void)arg;
 if(conn!=atomic_load(&connection))return 0;
 if(!error->status && chr){
  if(atomic_load(&value_handle) && chr->def_handle>atomic_load(&value_handle) && descriptor_end==service_end)descriptor_end=chr->def_handle-1;
  if(!ble_uuid_cmp(&chr->uuid.u,&turn_uuid.u))atomic_store(&value_handle,chr->val_handle);
 }else if(error->status==BLE_HS_EDONE && atomic_load(&value_handle)){
  if(ble_gattc_disc_all_dscs(conn,atomic_load(&value_handle),descriptor_end,descriptors,NULL))disconnect();
 }else {status("Komoot direction service unavailable");disconnect();}
 return 0;
}
static int services(uint16_t conn,const struct ble_gatt_error *error,const struct ble_gatt_svc *svc,void *arg){
 (void)arg;
 if(conn!=atomic_load(&connection))return 0;
 if(!error->status && svc){service_start=svc->start_handle;service_end=svc->end_handle;}
 else if(error->status==BLE_HS_EDONE && service_start){
  descriptor_end=service_end;
  if(ble_gattc_disc_all_chrs(conn,service_start,service_end,characteristics,NULL))disconnect();
 }else {status("Komoot service not found");disconnect();}
 return 0;
}
static int gap(struct ble_gap_event *event,void *arg){
 (void)arg;
 switch(event->type){
 case BLE_GAP_EVENT_DISC:{
  if(!atomic_load(&wanted))break;
  struct ble_hs_adv_fields fields;
  if(ble_hs_adv_parse_fields(&fields,event->disc.data,event->disc.length_data))break;
  for(unsigned i=0;i<fields.num_uuids128;i++)if(!ble_uuid_cmp(&fields.uuids128[i].u,&service_uuid.u)){
   ble_gap_disc_cancel();uint8_t address_type;
   if(!ble_hs_id_infer_auto(0,&address_type)){
    status("Connecting to Komoot");
    ble_gap_connect(address_type,&event->disc.addr,10000,NULL,gap,NULL);
   }
   break;
  }break;
 }
 case BLE_GAP_EVENT_CONNECT:
  if(!event->connect.status){
   atomic_store(&connection,event->connect.conn_handle);atomic_store(&value_handle,0);atomic_store(&subscribed_ready,false);service_start=0;
   if(!atomic_load(&wanted)){disconnect();break;}
   status("Reading Komoot service");
   if(ble_gattc_disc_svc_by_uuid(event->connect.conn_handle,&service_uuid.u,services,NULL))disconnect();
  }else status("Komoot unavailable; retrying");
  break;
 case BLE_GAP_EVENT_DISCONNECT:
  atomic_store(&connection,BLE_HS_CONN_HANDLE_NONE);atomic_store(&value_handle,0);atomic_store(&reading,false);atomic_store(&subscribed_ready,false);
  status(atomic_load(&wanted)?"Komoot disconnected; retrying":"Navigation stopped");break;
 case BLE_GAP_EVENT_NOTIFY_RX:
  if(event->notify_rx.conn_handle==atomic_load(&connection) && event->notify_rx.attr_handle==atomic_load(&value_handle))atomic_store(&refresh,true);
  break;
 default:break;
 }
 return 0;
}
#if !CONFIG_PASSPORT_MUSE_BLE
static void host_task(void *arg){(void)arg;nimble_port_run();vTaskDelete(NULL);}
static void reset_host(int reason){(void)reason;passport_navigation_reset();}
static bool start_host(void){
 if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<49152 || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<32768)return false;
 if(nimble_port_init()!=ESP_OK)return false;
 ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("Pocket Passport");
 ble_hs_cfg.sync_cb=passport_navigation_synced;
 ble_hs_cfg.reset_cb=reset_host;
 TaskHandle_t task=NULL;
 if(xTaskCreate(host_task,"navigation_ble",4096,NULL,configMAX_PRIORITIES-4,&task)!=pdPASS){nimble_port_deinit();return false;}
 return true;
}
#endif
void passport_navigation_poll(void){
 static bool previous;bool enabled=atomic_load(&wanted);
 if(enabled!=previous){previous=enabled;status(enabled?"Connecting to Komoot":"Navigation stopped");}
 if(!enabled){
  if(atomic_load(&ready)){if(ble_gap_disc_active())ble_gap_disc_cancel();ble_gap_conn_cancel();disconnect();}
  return;
 }
 if(!atomic_load(&started)){
  /* Release portrait RAM in the UI before allocating the controller. */
  xSemaphoreTake(passport_mutex,portMAX_DELAY);bool portrait_live=passport_avatar!=NULL;xSemaphoreGive(passport_mutex);
  if(portrait_live)return;
#if CONFIG_PASSPORT_MUSE_BLE
  passport_muse_enter();passport_muse_poll();
  if(passport_muse_runtime_state()==2)atomic_store(&started,true);
#else
  if(start_host())atomic_store(&started,true);else {status("Bluetooth needs free memory; retrying");return;}
#endif
 }
 if(!atomic_load(&ready))return;
 int64_t now=esp_timer_get_time();uint16_t conn=atomic_load(&connection);
 if(conn==BLE_HS_CONN_HANDLE_NONE){
  if(now<next_scan || ble_gap_disc_active() || ble_gap_conn_active())return;
  uint8_t address_type;if(ble_hs_id_infer_auto(0,&address_type))return;
  struct ble_gap_disc_params params={.itvl=160,.window=48,.passive=0,.filter_duplicates=1};
  status("Start navigation in Komoot");ble_gap_disc(address_type,5000,&params,gap,NULL);next_scan=now+8000000;
 }else if(atomic_load(&subscribed_ready) && !atomic_load(&reading) && (now>=next_read || atomic_exchange(&refresh,false))){
  read_length=0;read_invalid=false;atomic_store(&reading,true);next_read=now+3000000;
  if(ble_gattc_read_long(conn,atomic_load(&value_handle),0,read_result,NULL)){atomic_store(&reading,false);status("Komoot read failed");}
 }
}
#else
void passport_navigation_synced(void){}
void passport_navigation_reset(void){}
void passport_navigation_poll(void){if(atomic_exchange(&wanted,false))status("Enable Navigation BLE in build");}
#endif
