#include "passport_app.h"
#include "passport_features.h"
#include "passport_navigation.h"
#include "passport_muse.h"
#include "passport_muse_voice.h"
#include "passport_i18n.h"
#include "passport_private_defaults.h"
#include "passport_direct.h"
#include "passport_train_fetch.h"
#include "passport_usage.h"
#include "esp_netif_sntp.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
passport_spotify_t passport_spotify={.state="loading",.title="Connect Spotify at companion /tools"};
passport_reader_t passport_reader={.state="loading",.title="Upload books at companion /tools"};
passport_config_t passport_config;
char passport_qr_url[257];
SemaphoreHandle_t passport_mutex;
bool passport_connected,passport_has_avatar;
char passport_ip[20]="192.168.4.1";
volatile bool passport_dirty=true;
char passport_weather[320],passport_athena[1800];
char passport_station[64],passport_network_status[128];
unsigned passport_station_count=10;
unsigned passport_bus_stop_count=1,passport_bus_pages=1,passport_bus_rows;
passport_bus_row_t passport_bus_data[4];
char passport_bus_title[96]="82009 · Eunos Int",passport_bus_meta[96]="确定刷新 · 上下翻页";
typedef struct {uint32_t count;passport_bus_stop_t stops[PASSPORT_MAX_BUS_STOPS];} bus_config_t;
static bus_config_t bus_config={.count=1,.stops={{.code="82009",.name="Eunos Int"}}};
static atomic_uint bus_generation;
static unsigned bus_selected_stop,bus_selected_page;
static bool valid_bus_code(const char *code){
 size_t n=strlen(code);if(n<3 || n>5)return false;
 for(size_t i=0;i<n;i++)if(code[i]<'0'||code[i]>'9')return false;
 return true;
}
int passport_psi_value=-1;
char passport_haze_status[80]="选择区域",passport_haze_meta[96]="NEA · 24h PSI";
typedef struct {char region[12];double latitude,longitude;uint8_t has_location;} haze_location_t;
static haze_location_t haze_location={.region="unset"};
static bool valid_haze_region(const char *r){
 const char *regions[]={"unset","auto","central","north","south","east","west"};
 for(unsigned i=0;i<7;i++)if(!strcmp(r,regions[i]))return true;
 return false;
}
static nvs_handle_t store;
static atomic_uint display_brightness=70;
static atomic_bool brightness_save_failed;
unsigned passport_brightness_get(void){return atomic_load(&display_brightness);}
bool passport_brightness_save_failed(void){return atomic_load(&brightness_save_failed);}
static char lta_account_key[129];
static char setup_code[8];
static char setup_password[20];
static atomic_bool setup_active;
static atomic_uint wifi_failure_reason;
static atomic_uint weather_generation;
static bool copy_weather(cJSON *j);
static void localize_network_status(void);
static void localize_cached_status(void);
static void lock(void);
static void unlock(void);
static QueueHandle_t work_queue;
typedef struct {unsigned kind,index;} control_t;
static QueueHandle_t control_queue;
bool passport_weather_refresh_request(void){
 if(passport_phone_relay_active()){atomic_fetch_add(&weather_generation,1);return true;}
 control_t request={.kind=5};return control_queue&&xQueueSend(control_queue,&request,0)==pdTRUE;
}
static atomic_uint audio_generation;
static atomic_bool audio_desired;
static atomic_uint audio_station;
static atomic_bool reply_audio;
static atomic_uint speech_end_generation;
static char speech_id[33],last_speech_id[33];
static void copy_reply(int index,cJSON *j);
static atomic_uint audio_bytes_written;
unsigned passport_audio_bytes_written(void){return atomic_load(&audio_bytes_written);}
static char station_names[20][64];
static unsigned last_station;
static atomic_uint phone_relay_deadline;
static atomic_bool phone_relay_enabled;
typedef struct {uint32_t generation;uint16_t bytes;uint8_t pcm[1024];} phone_pcm_t;
static QueueHandle_t phone_pcm;
static bool phone_relay_active(void){
 uint32_t deadline=atomic_load(&phone_relay_deadline),now=(uint32_t)(esp_timer_get_time()/1000);
 return atomic_load(&phone_relay_enabled) && deadline && (int32_t)(deadline-now)>0;
}
bool passport_phone_relay_active(void){return phone_relay_active();}
static atomic_uint active_function, function_epoch;
unsigned passport_function_active(void){return atomic_load(&active_function);}
static atomic_bool screen_awake=true;
void passport_screen_awake_set(bool awake){atomic_store(&screen_awake,awake);}
bool passport_screen_awake(void){return atomic_load(&screen_awake);}
static atomic_bool network_busy,network_worker_running,network_speech_handoff;
static int64_t network_speech_ready_at;
static int64_t network_voice_ready_at;
static atomic_bool network_photo_handoff;
static int64_t network_photo_ready_at;
static bool network_speech_was_busy;
static httpd_handle_t settings_server;
static atomic_bool settings_web_ready;
bool passport_settings_web_active(void){return atomic_load(&settings_web_ready);}
static atomic_bool audio_worker_running;
bool passport_audio_worker_active(void){return atomic_load(&audio_worker_running);}
static void audio_worker(void *unused);
static void audio_worker_start_if_needed(void){
 if(passport_muse_speech_busy() || (!atomic_load(&audio_desired) && !passport_voice_needs_worker()) ||
    atomic_exchange(&audio_worker_running,true))return;
 if(xTaskCreate(audio_worker,"passport_pcm",6144,NULL,3,NULL)!=pdPASS){
  atomic_store(&audio_worker_running,false);
  ESP_LOGW("passport","Audio worker allocation failed; retrying on next poll");
 }
}
bool passport_network_idle(void){
 return !atomic_load(&network_busy) && (!passport_voice_busy() ||
  (!atomic_load(&network_worker_running) && esp_timer_get_time()>=network_voice_ready_at));
}
void passport_function_select(passport_page_t page){
 unsigned previous=atomic_exchange(&active_function,page);
 if(previous==(unsigned)page)return;
 atomic_fetch_add(&function_epoch,1);
 if(!passport_browser_relay_page(page)){
  atomic_store(&phone_relay_enabled,false);atomic_store(&phone_relay_deadline,0);
 }
 if(previous==PAGE_HERMES || previous==PAGE_MUSE)passport_voice_cancel();
 lock();passport_reply_select(page==PAGE_MUSE && passport_muse_supported()?PAGE_APPS:page);unlock();
 if(previous==PAGE_MUSE){passport_muse_leave();passport_muse_voice_leave();}
 if(page==PAGE_MUSE)passport_muse_enter();
 if(page!=PAGE_RADIO){atomic_store(&reply_audio,false);atomic_store(&audio_desired,false);atomic_fetch_add(&audio_generation,1);}
}

static void phone_relay_renew(void){atomic_store(&phone_relay_deadline,((uint32_t)(esp_timer_get_time()/1000)+8000)|1u);}
bool passport_audio_is_playing(void){return atomic_load(&audio_desired);}
static bool audio_ok;
static atomic_int audio_volume=50;
unsigned passport_audio_volume_percent(void){return (unsigned)atomic_load(&audio_volume);}
void passport_audio_volume(int delta){
 int value=atomic_load(&audio_volume)+delta;
 if(value<0)value=0;
 if(value>100)value=100;
 atomic_store(&audio_volume,value);
 xSemaphoreTake(passport_mutex,portMAX_DELAY);passport_dirty=true;xSemaphoreGive(passport_mutex);
}
typedef struct {const char *id,*ssid,*password;} private_wifi_t;
static const private_wifi_t wifi_profiles[]=PASSPORT_PRIVATE_WIFI_PROFILES;
static char wifi_password_override[2][65];
static const char *wifi_password_key(unsigned index){return !strcmp(wifi_profiles[index].id,"home")?"pass_home":"pass_outside";}
static const char *wifi_password(unsigned index){return wifi_password_override[index][0]?wifi_password_override[index]:wifi_profiles[index].password;}
static atomic_uint wifi_current;
static atomic_bool wifi_switching;
unsigned passport_wifi_profile_count(void){return PASSPORT_PRIVATE_WIFI_COUNT;}
unsigned passport_wifi_profile_current(void){return atomic_load(&wifi_current);}
const char *passport_wifi_profile_label(unsigned index){
 if(index>=passport_wifi_profile_count())return T("自定义","Custom");
 return !strcmp(wifi_profiles[index].id,"home")?T("在家","Home"):T("手机热点","Phone Hotspot");
}
static unsigned wifi_profile_for_ssid(const char *ssid){
 for(unsigned i=0;i<passport_wifi_profile_count();i++)if(!strcmp(ssid,wifi_profiles[i].ssid))return i;
 return passport_wifi_profile_count();
}
static const char *TAG="passport";
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
static atomic_int wifi_test_ps=-1;
int passport_wifi_test_mode(unsigned mode){
 if(mode>3)return ESP_ERR_INVALID_ARG;
 uint8_t protocol=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G;
 if(mode!=2)protocol|=WIFI_PROTOCOL_11N;
 atomic_store(&wifi_test_ps,mode==3?WIFI_PS_MIN_MODEM:mode?WIFI_PS_NONE:-1);
 esp_err_t error=esp_wifi_set_ps(mode && mode!=3?WIFI_PS_NONE:WIFI_PS_MIN_MODEM);
 return error==ESP_OK?esp_wifi_set_protocol(WIFI_IF_STA,protocol):error;
}
#endif
static void lock(void){xSemaphoreTake(passport_mutex,portMAX_DELAY);}
static void unlock(void){xSemaphoreGive(passport_mutex);}
/* Call while holding passport_mutex. Saved custom service URLs take precedence. */
static const char *bridge_base(void){
 unsigned profile=atomic_load(&wifi_current);
 if(profile<passport_wifi_profile_count() && !strcmp(wifi_profiles[profile].id,"home") &&
    PASSPORT_PRIVATE_LAN_BRIDGE_URL[0] && !strcmp(passport_config.bridge,PASSPORT_PRIVATE_BRIDGE_URL))return PASSPORT_PRIVATE_LAN_BRIDGE_URL;
 return passport_config.bridge;
}
bool passport_bridge_snapshot(const char *path,char *url,size_t urlcap,char *token,size_t tokencap){
 lock();bool connected=passport_connected && bridge_base()[0];
 int n=snprintf(url,urlcap,"%s%s",bridge_base(),path);
 int k=snprintf(token,tokencap,"Bearer %s",passport_config.token);unlock();
 return connected && n>=0 && (size_t)n<urlcap && k>=0 && (size_t)k<tokencap;
}
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
void passport_test_setup_print(void){
 lock();printf("PASSPORT_SETUP {\"ip\":\"%s\",\"code\":\"%s\"}\n",passport_ip,setup_code);unlock();
}
#endif
void passport_config_load(void) {
 strcpy(passport_config.name,"我的护照");strcpy(passport_config.tagline,"保持好奇");
 strcpy(passport_config.city,"singapore");
 esp_err_t err=nvs_flash_init();
 if(err!=ESP_OK){ESP_LOGE(TAG,"System NVS unavailable: %s",esp_err_to_name(err));return;}
 err=nvs_flash_init_partition("profile");if(err!=ESP_OK)return;
 if(nvs_open_from_partition("profile","passport",NVS_READWRITE,&store)!=ESP_OK)return;
 size_t lta_size=sizeof(lta_account_key);
 if(nvs_get_str(store,"lta_api_key",lta_account_key,&lta_size)!=ESP_OK || !passport_valid_lta_key(lta_account_key))lta_account_key[0]=0;
 uint8_t brightness=70;
 if(nvs_get_u8(store,"brightness",&brightness)==ESP_OK && brightness>=10 && brightness<=100 && brightness%10==0)atomic_store(&display_brightness,brightness);
 uint8_t language=0;if(nvs_get_u8(store,"language",&language)==ESP_OK)passport_language_set(language);
 size_t qr_size=sizeof(passport_qr_url);
 if(nvs_get_str(store,"qr_url",passport_qr_url,&qr_size)!=ESP_OK)snprintf(passport_qr_url,sizeof(passport_qr_url),"%s",PASSPORT_PRIVATE_QR_URL);
 size_t n=sizeof(passport_config);passport_config_t loaded;
 if(nvs_get_blob(store,"config",&loaded,&n)==ESP_OK && n==sizeof(loaded)) {
  /* Stored fields are validated before persistence. Reject unterminated blobs. */
  if(memchr(loaded.ssid,0,sizeof(loaded.ssid)) && memchr(loaded.password,0,sizeof(loaded.password)) &&
     memchr(loaded.name,0,sizeof(loaded.name)) && memchr(loaded.tagline,0,sizeof(loaded.tagline)) &&
     memchr(loaded.contact,0,sizeof(loaded.contact)) && memchr(loaded.bridge,0,sizeof(loaded.bridge)) &&
     memchr(loaded.token,0,sizeof(loaded.token)) && memchr(loaded.city,0,sizeof(loaded.city)))passport_config=loaded;
 }
 if(passport_config.theme>2)passport_config.theme=0;
 /* Private provisioning fills missing fields only; later phone edits win. */
 bool bridge_seeded=false;
 if(!passport_config.bridge[0] && PASSPORT_PRIVATE_BRIDGE_URL[0]){
  snprintf(passport_config.bridge,sizeof(passport_config.bridge),"%s",PASSPORT_PRIVATE_BRIDGE_URL);bridge_seeded=true;
 }
 if(!passport_config.token[0] && PASSPORT_PRIVATE_BRIDGE_TOKEN[0] &&
    !strcmp(passport_config.bridge,PASSPORT_PRIVATE_BRIDGE_URL)){
  snprintf(passport_config.token,sizeof(passport_config.token),"%s",PASSPORT_PRIVATE_BRIDGE_TOKEN);bridge_seeded=true;
 }
 if(bridge_seeded){
  esp_err_t saved=nvs_set_blob(store,"config",&passport_config,sizeof(passport_config));
  if(saved==ESP_OK)saved=nvs_commit(store);
  if(saved!=ESP_OK)ESP_LOGW(TAG,"Bridge settings persistence failed: %s",esp_err_to_name(saved));
 }
#if PASSPORT_PRIVATE_WIFI_COUNT > 0
 /* Apply new local profiles once, retaining all other profile data and later UI edits. */
 char applied_seed[17]={0};size_t seed_size=sizeof(applied_seed);
 if(nvs_get_str(store,"wifi_seed",applied_seed,&seed_size)!=ESP_OK ||
    strcmp(applied_seed,PASSPORT_PRIVATE_WIFI_REVISION)){
  const private_wifi_t *profile=&wifi_profiles[PASSPORT_PRIVATE_WIFI_DEFAULT];
  snprintf(passport_config.ssid,sizeof(passport_config.ssid),"%s",profile->ssid);
  snprintf(passport_config.password,sizeof(passport_config.password),"%s",profile->password);
  esp_err_t saved=nvs_set_blob(store,"config",&passport_config,sizeof(passport_config));
  if(saved==ESP_OK)saved=nvs_set_str(store,"wifi_seed",PASSPORT_PRIVATE_WIFI_REVISION);
  if(saved==ESP_OK)saved=nvs_commit(store);
  if(saved!=ESP_OK)ESP_LOGW(TAG,"Private Wi-Fi preset persistence failed: %s",esp_err_to_name(saved));
 }
#endif
 for(unsigned i=0;i<passport_wifi_profile_count();i++){
  size_t password_size=sizeof(wifi_password_override[i]);
  if(nvs_get_str(store,wifi_password_key(i),wifi_password_override[i],&password_size)!=ESP_OK)wifi_password_override[i][0]=0;
 }
 atomic_store(&wifi_current,wifi_profile_for_ssid(passport_config.ssid));
 n=sizeof(bus_config_t);bus_config_t buses;
 if(nvs_get_blob(store,"bus",&buses,&n)==ESP_OK && n==sizeof(buses) && buses.count<=PASSPORT_MAX_BUS_STOPS){
  bool valid=true;
  for(unsigned i=0;i<buses.count;i++){
   if(!memchr(buses.stops[i].code,0,sizeof(buses.stops[i].code)) || !memchr(buses.stops[i].name,0,sizeof(buses.stops[i].name)) || !valid_bus_code(buses.stops[i].code))valid=false;
  }
  if(valid){bus_config=buses;passport_bus_stop_count=buses.count;}
 }
 n=sizeof(haze_location_t);haze_location_t location;
 if(nvs_get_blob(store,"haze",&location,&n)==ESP_OK && n==sizeof(location) &&
    memchr(location.region,0,sizeof(location.region)) && valid_haze_region(location.region) &&
    isfinite(location.latitude) && isfinite(location.longitude))haze_location=location;
 /* Seed an explicitly chosen private region once; preserve existing user choices. */
 if(PASSPORT_PRIVATE_PSI_REGION[0] && !strcmp(haze_location.region,"unset")){
  char seeded[12]={0};size_t size=sizeof(seeded);
  if(nvs_get_str(store,"psi_seed",seeded,&size)!=ESP_OK || strcmp(seeded,PASSPORT_PRIVATE_PSI_REGION)){
   haze_location_t chosen={0};snprintf(chosen.region,sizeof(chosen.region),"%s",PASSPORT_PRIVATE_PSI_REGION);
   esp_err_t saved=nvs_set_blob(store,"haze",&chosen,sizeof(chosen));
   if(saved==ESP_OK)saved=nvs_set_str(store,"psi_seed",PASSPORT_PRIVATE_PSI_REGION);
   if(saved==ESP_OK)saved=nvs_commit(store);
   if(saved==ESP_OK)haze_location=chosen;
   else ESP_LOGW(TAG,"Private PSI region persistence failed: %s",esp_err_to_name(saved));
  }
 }
 n=0;passport_has_avatar=nvs_get_blob(store,"avatar",NULL,&n)==ESP_OK && n==PASSPORT_AVATAR_BYTES;
}
extern const char settings_start[] asm("_binary_passport_settings_html_start");
extern const char settings_end[] asm("_binary_passport_settings_html_end");
static bool auth(httpd_req_t *r) {
 char supplied[12]={0};
 return httpd_req_get_hdr_value_str(r,"X-Passport-Code",supplied,sizeof(supplied))==ESP_OK && !strcmp(supplied,setup_code);
}
static esp_err_t json_send(httpd_req_t *r,const char *text){httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,text);}
static esp_err_t fail(httpd_req_t *r,const char *status,const char *text){httpd_resp_set_status(r,status);return json_send(r,text);}
static int receive_all(httpd_req_t *r,char *buf,int cap) {
 if(r->content_len<=0 || r->content_len>=cap)return -1;
 int n=0;while(n<r->content_len){int got=httpd_req_recv(r,buf+n,r->content_len-n);if(got<=0)return -1;n+=got;}buf[n]=0;return n;
}
static esp_err_t settings_get(httpd_req_t *r) {
 httpd_resp_set_type(r,"text/html; charset=utf-8");
 httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' blob:; connect-src 'self' http: https:; frame-ancestors 'none'");
 return httpd_resp_send(r,settings_start,settings_end-settings_start-1);
}
static esp_err_t config_get(httpd_req_t *r) {
 if(!auth(r))return fail(r,"401 Unauthorized","{\"error\":\"设置码不正确\"}");
 cJSON *j=cJSON_CreateObject();if(!j)return ESP_ERR_NO_MEM;
 lock();
 #define FIELD(k) cJSON_AddStringToObject(j,#k,passport_config.k)
 FIELD(name);FIELD(tagline);FIELD(contact);FIELD(ssid);FIELD(bridge);FIELD(city);
 #undef FIELD
 cJSON *stops=cJSON_AddArrayToObject(j,"bus_stops");
 for(unsigned i=0;i<bus_config.count;i++){cJSON *stop=cJSON_CreateObject();cJSON_AddStringToObject(stop,"code",bus_config.stops[i].code);cJSON_AddStringToObject(stop,"name",bus_config.stops[i].name);cJSON_AddItemToArray(stops,stop);}
 cJSON *profiles=cJSON_AddArrayToObject(j,"wifi_profiles");
 for(unsigned i=0;i<passport_wifi_profile_count();i++){
  cJSON *profile=cJSON_CreateObject();cJSON_AddStringToObject(profile,"id",wifi_profiles[i].id);
  cJSON_AddStringToObject(profile,"label",passport_wifi_profile_label(i));cJSON_AddItemToArray(profiles,profile);
 }
 unsigned active=wifi_profile_for_ssid(passport_config.ssid);
 cJSON_AddStringToObject(j,"selected_wifi_profile",active<passport_wifi_profile_count()?wifi_profiles[active].id:"custom");
 cJSON_AddBoolToObject(j,"lta_key_configured",lta_account_key[0]!=0);
 cJSON_AddNumberToObject(j,"language",passport_language_get());
 cJSON_AddStringToObject(j,"qr_url",passport_qr_url);
 cJSON_AddBoolToObject(j,"connected",passport_connected);
 cJSON_AddBoolToObject(j,"clock_synced",time(NULL)>=1700000000);
 cJSON_AddNumberToObject(j,"theme",passport_config.theme);
 cJSON_AddStringToObject(j,"psi_region",haze_location.region);
 if(haze_location.has_location){cJSON_AddNumberToObject(j,"latitude",haze_location.latitude);cJSON_AddNumberToObject(j,"longitude",haze_location.longitude);}
 unlock();
 char *s=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!s)return ESP_ERR_NO_MEM;
 esp_err_t e=json_send(r,s);free(s);return e;
}
static bool set_field(cJSON *j,const char *key,char *dest,size_t cap,bool secret) {
 cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);if(!v)return true;
 if(!cJSON_IsString(v) || strlen(v->valuestring)>=cap)return false;
 if(secret && !v->valuestring[0])return true;
 strcpy(dest,v->valuestring);return true;
}
static esp_err_t config_post(httpd_req_t *r) {
 if(!auth(r))return fail(r,"401 Unauthorized","{\"error\":\"设置码不正确\"}");
 char *body=malloc(4096);if(!body)return ESP_ERR_NO_MEM;
 if(receive_all(r,body,4096)<0){free(body);return fail(r,"413 Content Too Large","{}");}
 cJSON *j=cJSON_Parse(body);free(body);if(!j)return fail(r,"400 Bad Request","{}");
 lock();passport_config_t next=passport_config;haze_location_t next_haze=haze_location;bus_config_t next_bus=bus_config;
 unsigned next_language=passport_language_get();char next_qr[257];snprintf(next_qr,sizeof(next_qr),"%s",passport_qr_url);
 char next_lta[129];snprintf(next_lta,sizeof(next_lta),"%s",lta_account_key);
 bool valid=set_field(j,"lta_api_key",next_lta,sizeof(next_lta),true) && passport_valid_lta_key(next_lta);
 valid=valid && set_field(j,"qr_url",next_qr,sizeof(next_qr),false);
 valid=valid && passport_valid_qr_url(next_qr);
 cJSON *language=cJSON_GetObjectItemCaseSensitive(j,"language");
 if(language){if(!cJSON_IsNumber(language)||(language->valuedouble!=0&&language->valuedouble!=1))valid=false;else next_language=language->valueint;}
 cJSON *stops=cJSON_GetObjectItemCaseSensitive(j,"bus_stops");
 if(stops){
  if(!cJSON_IsArray(stops)||cJSON_GetArraySize(stops)>PASSPORT_MAX_BUS_STOPS)valid=false;
  else {memset(&next_bus,0,sizeof(next_bus));cJSON *entry;
   cJSON_ArrayForEach(entry,stops){
    passport_bus_stop_t *stop=&next_bus.stops[next_bus.count];
    cJSON *code=cJSON_GetObjectItemCaseSensitive(entry,"code");
    if(!cJSON_IsString(code)||strlen(code->valuestring)>=sizeof(stop->code)||!valid_bus_code(code->valuestring)){valid=false;break;}
    strcpy(stop->code,code->valuestring);
    if(!set_field(entry,"name",stop->name,sizeof(stop->name),false)){valid=false;break;}
    for(unsigned i=0;i<next_bus.count;i++)if(!strcmp(stop->code,next_bus.stops[i].code))valid=false;
    next_bus.count++;
   }
  }
 }
 #define FIELD(k,s) valid=valid && set_field(j,#k,next.k,sizeof(next.k),s)
 FIELD(name,false);FIELD(tagline,false);FIELD(contact,false);FIELD(ssid,false);FIELD(password,true);
 FIELD(bridge,false);FIELD(token,true);FIELD(city,false);
 #undef FIELD
 unsigned chosen_profile=passport_wifi_profile_count();
 cJSON *entered_password=cJSON_GetObjectItemCaseSensitive(j,"password");
 cJSON *selected_profile=cJSON_GetObjectItemCaseSensitive(j,"wifi_profile");
 if(selected_profile){
  bool found=false;
  if(cJSON_IsString(selected_profile))for(unsigned i=0;i<passport_wifi_profile_count();i++)if(!strcmp(selected_profile->valuestring,wifi_profiles[i].id)){
   snprintf(next.ssid,sizeof(next.ssid),"%s",wifi_profiles[i].ssid);valid=valid && passport_resolve_profile_password(next.password,sizeof(next.password),wifi_password(i),cJSON_IsString(entered_password)?entered_password->valuestring:NULL);chosen_profile=i;found=true;break;
  }
  if(!found)valid=false;
 }
 if(!selected_profile)chosen_profile=wifi_profile_for_ssid(next.ssid);
 valid=valid && set_field(j,"psi_region",next_haze.region,sizeof(next_haze.region),false) && valid_haze_region(next_haze.region);
 cJSON *lat=cJSON_GetObjectItemCaseSensitive(j,"latitude"),*lon=cJSON_GetObjectItemCaseSensitive(j,"longitude");
 if(lat || lon){
  if(!cJSON_IsNumber(lat)||!cJSON_IsNumber(lon)||!isfinite(lat->valuedouble)||!isfinite(lon->valuedouble)||
     lat->valuedouble < -90 || lat->valuedouble > 90 || lon->valuedouble < -180 || lon->valuedouble > 180)valid=false;
  else {next_haze.latitude=lat->valuedouble;next_haze.longitude=lon->valuedouble;next_haze.has_location=1;}
 }
 if(!strcmp(next_haze.region,"auto") && !next_haze.has_location)valid=false;
 cJSON *t=cJSON_GetObjectItemCaseSensitive(j,"theme");if(t){if(!cJSON_IsNumber(t)||t->valuedouble<0||t->valuedouble>2||t->valuedouble!=t->valueint)valid=false;else next.theme=t->valueint;}
 const char *cities[]={"singapore","johor-bahru","kuala-lumpur","penang"};bool city_ok=false;
 for(unsigned i=0;i<4;i++)if(!strcmp(next.city,cities[i]))city_ok=true;
 valid=valid && city_ok && (!next.bridge[0] || !strncmp(next.bridge,"http://",7));
 if(valid && chosen_profile<passport_wifi_profile_count() && cJSON_IsString(entered_password) && entered_password->valuestring[0]){
  valid=store && nvs_set_str(store,wifi_password_key(chosen_profile),next.password)==ESP_OK;
 }
 if(valid && store && nvs_set_str(store,"lta_api_key",next_lta)==ESP_OK && nvs_set_str(store,"qr_url",next_qr)==ESP_OK && nvs_set_u8(store,"language",next_language)==ESP_OK && nvs_set_blob(store,"config",&next,sizeof(next))==ESP_OK && nvs_set_blob(store,"haze",&next_haze,sizeof(next_haze))==ESP_OK && nvs_set_blob(store,"bus",&next_bus,sizeof(next_bus))==ESP_OK && nvs_commit(store)==ESP_OK){if(chosen_profile<passport_wifi_profile_count() && cJSON_IsString(entered_password) && entered_password->valuestring[0])snprintf(wifi_password_override[chosen_profile],sizeof(wifi_password_override[chosen_profile]),"%s",next.password);passport_language_set(next_language);localize_cached_status();snprintf(passport_qr_url,sizeof(passport_qr_url),"%s",next_qr);passport_weather[0]=passport_athena[0]=0;passport_config=next;passport_photo_invalidate();snprintf(lta_account_key,sizeof(lta_account_key),"%s",next_lta);atomic_store(&wifi_current,wifi_profile_for_ssid(next.ssid));haze_location=next_haze;bus_config=next_bus;passport_bus_stop_count=next_bus.count;passport_bus_rows=0;passport_bus_pages=1;atomic_fetch_add(&bus_generation,1);passport_psi_value=-1;strcpy(passport_haze_status,T("正在更新","Updating"));passport_dirty=true;}
 else valid=false;
 unlock();cJSON_Delete(j);
 if(!valid)return fail(r,"400 Bad Request","{\"error\":\"资料无效或保存失败。地址需为局域网 http:// 地址\"}");
 return json_send(r,"{\"ok\":true}");
}
static esp_err_t clock_post(httpd_req_t *r){
 if(!auth(r))return fail(r,"401 Unauthorized","{}");
 char body[96];if(receive_all(r,body,sizeof(body))<0)return fail(r,"400 Bad Request","{}");
 cJSON *j=cJSON_Parse(body),*value=cJSON_GetObjectItemCaseSensitive(j,"unix_time");
 bool valid=cJSON_IsNumber(value)&&passport_valid_clock(value->valuedouble);
 if(valid){struct timeval tv={.tv_sec=(time_t)value->valuedouble};valid=settimeofday(&tv,NULL)==0;}
 cJSON_Delete(j);return valid?json_send(r,"{\"ok\":true}"):fail(r,"400 Bad Request","{}");
}
static esp_err_t avatar_post(httpd_req_t *r) {
 if(!auth(r))return fail(r,"401 Unauthorized","{}");
 if(r->content_len!=PASSPORT_AVATAR_BYTES)return fail(r,"400 Bad Request","{}");
 uint8_t *data=malloc(PASSPORT_AVATAR_BYTES);if(!data)return fail(r,"503 Service Unavailable","{}");
 int n=0;while(n<r->content_len){int got=httpd_req_recv(r,(char *)data+n,r->content_len-n);if(got<=0){free(data);return ESP_FAIL;}n+=got;}
 lock();esp_err_t e=store?nvs_set_blob(store,"avatar",data,n):ESP_FAIL;
 if(e==ESP_OK)e=nvs_commit(store);
 if(e==ESP_OK){passport_has_avatar=true;passport_dirty=true;
  passport_photo_invalidate();
 }
 unlock();free(data);
 return e==ESP_OK?json_send(r,"{\"ok\":true}"):fail(r,"500 Internal Server Error","{\"error\":\"头像保存失败\"}");
}
typedef struct {int kind;passport_page_t page;char *text;char agent[8];unsigned bus_stop,bus_page,generation,wifi_profile,epoch;} work_t;
bool passport_feature_key(unsigned page,unsigned action){
 if((page!=PAGE_SPOTIFY && page!=PAGE_READER) || action>(page==PAGE_READER?4u:2u))return false;
 work_t work={.kind=8,.page=page,.wifi_profile=action,.epoch=atomic_load(&function_epoch)};
 return work_queue && xQueueSend(work_queue,&work,0)==pdTRUE;
}

static esp_err_t chat_post(httpd_req_t *r) {
 if(!auth(r))return fail(r,"401 Unauthorized","{}");
 char body[2400];if(receive_all(r,body,sizeof(body))<0)return fail(r,"400 Bad Request","{}");
 cJSON *j=cJSON_Parse(body);if(!j)return fail(r,"400 Bad Request","{}");
 cJSON *a=cJSON_GetObjectItem(j,"agent"),*t=cJSON_GetObjectItem(j,"text");
 bool valid=cJSON_IsString(a)&&cJSON_IsString(t)&&strlen(t->valuestring)>0&&strlen(t->valuestring)<2100&&(!strcmp(a->valuestring,"hermes")||!strcmp(a->valuestring,"muse"));
 work_t work={.kind=1,.epoch=atomic_load(&function_epoch)};if(valid){strcpy(work.agent,a->valuestring);work.text=strdup(t->valuestring);}cJSON_Delete(j);
 if(!valid)return fail(r,"400 Bad Request","{}");
 if(!work.text)return fail(r,"503 Service Unavailable","{}");
 work.page=!strcmp(work.agent,"muse")?PAGE_MUSE:PAGE_HERMES;
 if(!passport_service_allowed(passport_function_active(),work.page)||passport_voice_busy()){free(work.text);return fail(r,"409 Conflict","{\"error\":\"Open the assistant on Passport first\"}");}
 if(!work_queue || xQueueSend(work_queue,&work,0)!=pdTRUE){free(work.text);return fail(r,"503 Service Unavailable","{\"error\":\"正在处理，请稍后重试\"}");}
 return json_send(r,"{\"ok\":true}");
}
static esp_err_t relay_post(httpd_req_t *r){
 if(!auth(r))return fail(r,"401 Unauthorized","{}");
 char type[40]={0};httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type));
 if(!strcmp(type,"application/octet-stream")){
  if(r->content_len<6 || r->content_len>1028 || (r->content_len&1))return fail(r,"400 Bad Request","{}");
  uint8_t body[1029];int n=receive_all(r,(char *)body,sizeof(body));if(n<6)return fail(r,"400 Bad Request","{}");
  uint32_t generation=atomic_load(&audio_generation);
  if(!phone_relay_active() || !atomic_load(&audio_desired) || !passport_relay_pcm_frame_valid(body,n,generation))return fail(r,"409 Conflict","{}");
  phone_pcm_t packet={.generation=generation,.bytes=n-4};memcpy(packet.pcm,body+4,packet.bytes);
  lock();
  bool current=phone_relay_active() && atomic_load(&audio_desired) && generation==atomic_load(&audio_generation);
  if(current && !phone_pcm)phone_pcm=xQueueCreate(4,sizeof(phone_pcm_t));
  bool queued=current && phone_pcm && xQueueSend(phone_pcm,&packet,0)==pdTRUE;
  unlock();
  if(!current)return fail(r,"409 Conflict","{}");
  if(!queued)return fail(r,"503 Service Unavailable","{}");
  phone_relay_renew();return json_send(r,"{\"ok\":true}");
 }
 if(r->content_len<=0 || r->content_len>7600)return fail(r,"400 Bad Request","{}");
 char *body=malloc(r->content_len+1);if(!body)return fail(r,"503 Service Unavailable","{}");
 int n=receive_all(r,body,r->content_len+1);cJSON *j=n>=0?cJSON_Parse(body):NULL;free(body);
 cJSON *action=cJSON_GetObjectItem(j,"action");
 if(cJSON_IsString(action)){
  const char *name=action->valuestring;bool valid=false;
  if(!strcmp(name,"start")){
   if(!passport_browser_relay_page(passport_function_active())){
    cJSON_Delete(j);return fail(r,"409 Conflict","{\"error\":\"Open the assistant on Passport first\"}");
   }
   cJSON *stations=cJSON_GetObjectItem(j,"stations");int count=cJSON_GetArraySize(stations);
   valid=cJSON_IsArray(stations)&&count>0&&count<=20;
   for(int i=0;valid&&i<count;i++){
    cJSON *station=cJSON_GetArrayItem(stations,i);
    valid=cJSON_IsString(cJSON_GetObjectItem(station,"name"))&&cJSON_IsString(cJSON_GetObjectItem(station,"country"));
   }
   if(valid){
    {lock();
     for(int i=0;i<count;i++){cJSON *station=cJSON_GetArrayItem(stations,i);snprintf(station_names[i],64,"%.8s / %.48s",cJSON_GetObjectItem(station,"country")->valuestring,cJSON_GetObjectItem(station,"name")->valuestring);}
     passport_station_count=count;snprintf(passport_station,sizeof(passport_station),"%s",station_names[last_station%count]);passport_dirty=true;unlock();
     atomic_store(&audio_desired,false);atomic_fetch_add(&audio_generation,1);atomic_store(&phone_relay_enabled,true);phone_relay_renew();
    }
   }
  }else if(!strcmp(name,"stop")){
   atomic_store(&phone_relay_enabled,false);atomic_store(&phone_relay_deadline,0);atomic_store(&audio_desired,false);atomic_fetch_add(&audio_generation,1);valid=true;
  }else if(!strcmp(name,"poll") && atomic_load(&phone_relay_enabled)){
   phone_relay_renew();char reply[384];lock();snprintf(reply,sizeof(reply),"{\"playing\":%s,\"station\":%u,\"generation\":%u,\"page\":%u,\"speech_id\":\"%s\",\"weather_generation\":%u,\"city\":\"%s\",\"voice_phase\":%u,\"screen_awake\":%s}",atomic_load(&audio_desired)?"true":"false",atomic_load(&audio_station),atomic_load(&audio_generation),passport_function_active(),atomic_load(&reply_audio)?speech_id:"",atomic_load(&weather_generation),passport_config.city,passport_voice_phase(),passport_screen_awake()?"true":"false");unlock();cJSON_Delete(j);return json_send(r,reply);
  }else if(!strcmp(name,"voice_poll") && phone_relay_active()){
   char *pending=passport_voice_relay_poll();cJSON_Delete(j);
   if(!pending)return fail(r,"503 Service Unavailable","{}");
   esp_err_t result=json_send(r,pending);free(pending);phone_relay_renew();return result;
  }else if(!strcmp(name,"voice_ack") && phone_relay_active()){
   cJSON *id=cJSON_GetObjectItem(j,"id"),*status=cJSON_GetObjectItem(j,"status"),*body=cJSON_GetObjectItem(j,"body");
   valid=cJSON_IsNumber(id)&&id->valuedouble>=1&&id->valuedouble<=UINT32_MAX&&floor(id->valuedouble)==id->valuedouble&&cJSON_IsNumber(status)&&status->valuedouble>=100&&status->valuedouble<=599&&floor(status->valuedouble)==status->valuedouble&&cJSON_IsString(body)&&passport_voice_relay_ack((unsigned)id->valuedouble,status->valueint,body->valuestring);
  }else if(!strcmp(name,"weather") && phone_relay_active()){
   cJSON *generation=cJSON_GetObjectItem(j,"generation"),*city=cJSON_GetObjectItem(j,"city");
   lock();bool current=cJSON_IsNumber(generation) && generation->valuedouble==atomic_load(&weather_generation) && cJSON_IsString(city) && !strcmp(city->valuestring,passport_config.city);unlock();
   valid=current && copy_weather(cJSON_GetObjectItem(j,"data"));
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
   if(valid)puts("PASSPORT_TEST weather-refresh-done");
#endif
  }else if(!strcmp(name,"usage") && phone_relay_active()){
   cJSON *data=cJSON_GetObjectItem(j,"data");valid=cJSON_IsArray(cJSON_GetObjectItem(data,"usage"));
   if(valid && passport_function_active()==PAGE_ATHENA){lock();passport_usage_format(data,time(NULL),passport_language_get(),passport_athena,sizeof(passport_athena));passport_dirty=true;unlock();}
  }else if(!strcmp(name,"speech_end")){
   cJSON *g=cJSON_GetObjectItem(j,"generation");valid=cJSON_IsNumber(g);
   if(valid && g->valuedouble==atomic_load(&audio_generation) && atomic_load(&reply_audio))
    atomic_store(&speech_end_generation,atomic_load(&audio_generation));
  }else if(!strcmp(name,"radio_error")){
   cJSON *generation=cJSON_GetObjectItem(j,"generation");valid=cJSON_IsNumber(generation);
   if(valid && generation->valuedouble==atomic_load(&audio_generation)){
    atomic_store(&audio_desired,false);lock();snprintf(passport_station,sizeof(passport_station),"%s",T("连接失败，请重试","Offline, please retry"));passport_dirty=true;unlock();
   }
  }
  cJSON_Delete(j);return valid?json_send(r,"{\"ok\":true}"):fail(r,"400 Bad Request","{}");
 }
 cJSON *a=cJSON_GetObjectItem(j,"agent"),*t=cJSON_GetObjectItem(j,"text");
 bool valid=cJSON_IsString(a)&&cJSON_IsString(t)&&(!strcmp(a->valuestring,"hermes")||!strcmp(a->valuestring,"muse"));
 if(valid && (!passport_service_allowed(passport_function_active(),!strcmp(a->valuestring,"muse")?PAGE_MUSE:PAGE_HERMES)||passport_voice_busy())){cJSON_Delete(j);return fail(r,"409 Conflict","{\"error\":\"Open the assistant on Passport first\"}");}
 if(valid){int agent=!strcmp(a->valuestring,"muse");copy_reply(agent,j);}
 cJSON_Delete(j);return valid?json_send(r,"{\"ok\":true}"):fail(r,"400 Bad Request","{}");
}
static atomic_uint wifi_failures;
static atomic_uint wifi_deadline;
static atomic_bool wifi_gave_up;
static uint32_t wifi_now(void){return (uint32_t)(esp_timer_get_time()/1000);}
static esp_err_t wifi_connect_station(void){
 /* Private tests can isolate power-save compatibility without changing credentials. */
 wifi_ps_type_t power=WIFI_PS_MIN_MODEM;
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
 int override=atomic_load(&wifi_test_ps);if(override>=0)power=(wifi_ps_type_t)override;
#endif
 esp_err_t error=esp_wifi_set_ps(power);
 return error==ESP_OK?esp_wifi_connect():error;
}
static void wifi_begin_attempt(void){atomic_store(&wifi_gave_up,false);atomic_store(&wifi_failures,0);uint32_t deadline=wifi_now()+30000;atomic_store(&wifi_deadline,deadline?deadline:1);}
static void wifi_failed(unsigned reason){
 atomic_store(&wifi_failure_reason,reason);atomic_store(&wifi_gave_up,true);atomic_store(&wifi_deadline,0);
 lock();passport_connected=false;
 snprintf(passport_network_status,sizeof(passport_network_status),T("Wi-Fi 失败 (%u)\n确定重试","Wi-Fi failed (%u)\nOK: retry"),reason);
 passport_dirty=true;unlock();
 ESP_LOGW(TAG,"Wi-Fi failed after bounded attempts; reason=%u",reason);
}
/* Caller holds passport_mutex; only known system messages are translated. */
static void localize_network_status(void){
 if(atomic_load(&setup_active)&&!passport_connected)snprintf(passport_network_status,sizeof(passport_network_status),T("热点 Pocket-Passport\n密码 %s\n设置码 %s","Pocket-Passport\nKey %s\nCode %s"),setup_password,setup_code);
 else if(passport_connected)snprintf(passport_network_status,sizeof(passport_network_status),T("已连接 Wi-Fi\n设置码 %s","Wi-Fi connected\nSetup code %s"),setup_code);
 else if(atomic_load(&wifi_gave_up))snprintf(passport_network_status,sizeof(passport_network_status),T("Wi-Fi 失败 (%u)\n确定重试","Wi-Fi failed (%u)\nOK: retry"),atomic_load(&wifi_failure_reason));
 else if(atomic_load(&wifi_deadline))snprintf(passport_network_status,sizeof(passport_network_status),T("正在连接 %s","Connecting to %s"),passport_wifi_profile_label(passport_wifi_profile_current()));
 else snprintf(passport_network_status,sizeof(passport_network_status),T("设置码 %s","Setup code %s"),setup_code);
}
/* Called with the application mutex held (or before workers start). Generated
 * display strings must not retain the previous language while data refreshes. */
static void localize_cached_status(void){
 localize_network_status();passport_weather[0]=passport_athena[0]=0;
 passport_psi_value=-1;
 snprintf(passport_haze_status,sizeof(passport_haze_status),"%s",T("正在更新","Updating"));
 snprintf(passport_haze_meta,sizeof(passport_haze_meta),"%s",T("NEA · 空气质量","NEA | air quality"));
 passport_bus_rows=0;
 snprintf(passport_bus_title,sizeof(passport_bus_title),"%s",T("巴士到站","Bus arrivals"));
 snprintf(passport_bus_meta,sizeof(passport_bus_meta),"%s",T("确定刷新 · 上下翻页","OK: Refresh | Up/Down: Page"));
 passport_train_status[0]=0;passport_train_refresh_request();
}
static void wifi_timeout_check(void){
 uint32_t deadline=atomic_load(&wifi_deadline);
 if(deadline && !passport_wifi_should_retry(0,wifi_now(),deadline)){atomic_store(&wifi_switching,true);wifi_failed(0);(void)esp_wifi_disconnect();atomic_store(&wifi_switching,false);}
}
static const char *public_failure(void){
 if(!passport_connected)return T("Wi-Fi 未连接\n请先连接网络","Wi-Fi offline\nConnect in Settings");
 switch(passport_direct_error()){
 case DIRECT_KEY:return T("请在手机设置填写 LTA 密钥","Add LTA key in phone settings");
 case DIRECT_AUTH:return T("LTA 密钥无效或无权限","LTA key rejected / denied");
 case DIRECT_CLOCK:return T("时间未同步\n手机设置页同步时间","Clock not synced\nOpen phone settings");
 case DIRECT_MEMORY:return T("内存不足\n请稍后重试","Low RAM\nRetry shortly");
 case DIRECT_CONNECT:return T("网络或安全连接失败\n检查手机网络","Network / TLS failed\nCheck phone internet");
 case DIRECT_HTTP:return T("数据服务返回错误\n稍后重试","Service HTTP error\nRetry later");
 default:return T("数据读取失败\n确定重试","Data request failed\nOK: retry");
 }
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
 (void)arg;
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START && passport_config.ssid[0]){wifi_begin_attempt();wifi_connect_station();}
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){lock();passport_connected=false;passport_dirty=true;unlock();if(!atomic_load(&wifi_switching) && !atomic_load(&wifi_gave_up)){
  if(!atomic_load(&wifi_deadline))wifi_begin_attempt();
  wifi_event_sta_disconnected_t *event=data;unsigned reason=event?event->reason:0;
  ESP_LOGW(TAG,"Wi-Fi disconnected; reason=%u",reason);
  if(passport_wifi_should_retry(atomic_fetch_add(&wifi_failures,1)+1,wifi_now(),atomic_load(&wifi_deadline)))wifi_connect_station();else wifi_failed(reason);
 }}
 if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){(void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);atomic_store(&wifi_deadline,0);atomic_store(&wifi_failures,0);esp_netif_sntp_start();ip_event_got_ip_t *e=data;lock();passport_connected=true;snprintf(passport_ip,sizeof(passport_ip),IPSTR,IP2STR(&e->ip_info.ip));snprintf(passport_network_status,sizeof(passport_network_status),T("已连接 Wi-Fi\n设置码 %s","Wi-Fi connected\nSetup code %s"),setup_code);passport_dirty=true;unlock();}
}
void passport_open_setup(void) {
 wifi_config_t ap={0};snprintf((char *)ap.ap.ssid,sizeof(ap.ap.ssid),"Pocket-Passport");
 snprintf((char *)ap.ap.password,sizeof(ap.ap.password),"%s",setup_password);
 ap.ap.ssid_len=strlen((char *)ap.ap.ssid);ap.ap.channel=1;ap.ap.max_connection=2;ap.ap.authmode=WIFI_AUTH_WPA2_PSK;
 if(esp_wifi_set_mode(WIFI_MODE_APSTA)==ESP_OK && esp_wifi_set_config(WIFI_IF_AP,&ap)==ESP_OK){
  atomic_store(&setup_active,true);lock();snprintf(passport_ip,sizeof(passport_ip),"192.168.4.1");
  snprintf(passport_network_status,sizeof(passport_network_status),T("热点 Pocket-Passport\n密码 %s\n设置码 %s","Pocket-Passport\nKey %s\nCode %s"),setup_password,setup_code);passport_dirty=true;unlock();
 }
}
/* Each worker owns its HTTP handles and buffers. No LVGL calls in callbacks. */
static cJSON *bridge_request(const char *path,const char *post) {
 char url[256],token[110];bool connected;
 lock();snprintf(url,sizeof(url),"%s%s",bridge_base(),path);snprintf(token,sizeof(token),"Bearer %s",passport_config.token);connected=passport_connected && bridge_base()[0];unlock();
 if(!connected)return NULL;
 esp_http_client_config_t cfg={.url=url,.timeout_ms=3500,.buffer_size=1024,.keep_alive_enable=false};
 esp_http_client_handle_t h=esp_http_client_init(&cfg);if(!h)return NULL;
 esp_http_client_set_header(h,"Authorization",token);
 if(post){esp_http_client_set_method(h,HTTP_METHOD_POST);esp_http_client_set_header(h,"Content-Type","application/json");}
 cJSON *result=NULL;
 if(esp_http_client_open(h,post?strlen(post):0)==ESP_OK) {
  bool sent=!post || esp_http_client_write(h,post,strlen(post))==(int)strlen(post);
  int64_t declared=sent?esp_http_client_fetch_headers(h):-1;
  if(declared>=0 && esp_http_client_get_status_code(h)>=200 && esp_http_client_get_status_code(h)<300) {
   int limit=!strncmp(path,"/v1/weather?",12)?2048:10000;
   int capacity=declared>0 && declared<limit?(int)declared+1:limit;
   char *body=malloc(capacity);if(body){int used=0,n;
    while(used<capacity-1 && (n=esp_http_client_read(h,body+used,capacity-1-used))>0)used+=n;
    body[used]=0;if(esp_http_client_is_complete_data_received(h))result=cJSON_Parse(body);free(body);
   }
  }
 }
 esp_http_client_close(h);esp_http_client_cleanup(h);return result;
}
static const char *weather_name(int code) {
 if(code<0)return T("状态未知","Unknown");
 if(code==0)return T("晴","Clear");
 if(code<=3)return T("多云","Cloudy");
 if(code==45 || code==48)return T("有雾","Fog");
 if(code>=51 && code<=67)return T("有雨","Rain");
 if(code>=71 && code<=77)return T("有雪","Snow");
 if(code>=80 && code<=82)return T("阵雨","Showers");
 if(code>=95)return T("雷雨","Storms");
 return T("天气","Weather");
}
static cJSON *fetch_weather(const char *city,bool prefer_bridge){
 /* Bluetooth and the colour portrait can leave too little contiguous RAM for
  * another TLS handshake. The configured bridge provides the same payload. */
 cJSON *j=prefer_bridge?NULL:passport_direct_weather(city);
 if(j)return j;
 const char *selected=!strcmp(city,"johor-bahru")?"johor-bahru":!strcmp(city,"kuala-lumpur")?"kuala-lumpur":!strcmp(city,"penang")?"penang":"singapore";
 char path[64];snprintf(path,sizeof(path),"/v1/weather?city=%s",selected);
 return bridge_request(path,NULL);
}
static bool copy_weather(cJSON *j){
 cJSON *c=cJSON_GetObjectItem(j,"current"),*t=cJSON_GetObjectItem(c,"temperature_2m"),*h=cJSON_GetObjectItem(c,"relative_humidity_2m"),*w=cJSON_GetObjectItem(c,"wind_speed_10m"),*code=cJSON_GetObjectItem(c,"weather_code");
 if(!cJSON_IsNumber(t)||!cJSON_IsNumber(h)||!cJSON_IsNumber(w)||!isfinite(t->valuedouble)||!isfinite(h->valuedouble)||!isfinite(w->valuedouble)||h->valuedouble<0||h->valuedouble>100||w->valuedouble<0)return false;
 lock();snprintf(passport_weather,sizeof(passport_weather),T("%.1f°C\n%s%s\n湿度 %d%%\n风 %.0f km/h","%.1f°C\n%s%s\nRH %d%%\nWind %.0f km/h"),t->valuedouble,weather_name(cJSON_IsNumber(code)?code->valueint:-1),cJSON_IsTrue(cJSON_GetObjectItem(j,"stale"))?T("(缓存)","(cached)"):"",h->valueint,w->valuedouble);passport_dirty=true;unlock();return true;
}
static const char *str(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItem(j,key);return cJSON_IsString(v)?v->valuestring:"";}
static void copy_reply(int index,cJSON *j) {
 const char *value=str(j,"text");lock();
 char *reply=passport_reply_buffer(index);
 if(passport_voice_busy() || !reply){unlock();return;}
 if(!strcmp(str(j,"state"),"working"))strcpy(reply,T("正在思考，请稍候...","Thinking, please wait..."));
 else {size_t n=passport_utf8_prefix(value,PASSPORT_REPLY_CAPACITY-1);memcpy(reply,value,n);reply[n]=0;}
 passport_dirty=true;
 const char *id=str(j,"speech_id");bool valid=index==0 && !strcmp(str(j,"state"),"done") && strlen(id)==32;
 for(unsigned i=0;valid && i<32;i++)if(!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f')))valid=false;
 if(valid && strcmp(last_speech_id,id)){
  snprintf(last_speech_id,sizeof(last_speech_id),"%s",id);
  if(passport_audio_volume_percent()){
   snprintf(speech_id,sizeof(speech_id),"%s",id);atomic_store(&reply_audio,true);
   atomic_fetch_add(&audio_generation,1);atomic_store(&audio_bytes_written,0);atomic_store(&audio_desired,true);
  }
 }
 unlock();
}
static const char *haze_category(const char *category){
 if(!strcmp(category,"good"))return T("良好","Good");
 if(!strcmp(category,"moderate"))return T("中等","Moderate");
 if(!strcmp(category,"unhealthy"))return T("不健康","Unhealthy");
 if(!strcmp(category,"very_unhealthy"))return T("非常不健康","V. unhealthy");
 if(!strcmp(category,"hazardous"))return T("危险","Hazardous");
 return T("未知","Unknown");
}
static const char *haze_region_name(const char *region){
 if(!strcmp(region,"central"))return T("中区","Central");
 if(!strcmp(region,"north"))return T("北区","North");
 if(!strcmp(region,"south"))return T("南区","South");
 if(!strcmp(region,"east"))return T("东区","East");
 if(!strcmp(region,"west"))return T("西区","West");
 return T("新加坡","Singapore");
}
static cJSON *fetch_haze(const char *city,const haze_location_t *location){
 cJSON *j=passport_direct_haze(city,location->region,location->latitude,location->longitude,location->has_location);
 if(j)return j;
 /* Preserve the owner's region/location when TLS cannot fit alongside BLE. */
 char path[160];
 snprintf(path,sizeof(path),"/v1/haze?city=%s&region=%s",city,location->region);
 if(location->has_location){size_t n=strlen(path);snprintf(path+n,sizeof(path)-n,"&lat=%.6f&lon=%.6f",location->latitude,location->longitude);}
 return bridge_request(path,NULL);
}
static void poll_haze(void){
 char city[24];haze_location_t location;lock();snprintf(city,sizeof(city),"%s",passport_config.city);location=haze_location;bool online=passport_connected;unlock();
 cJSON *j=online?fetch_haze(city,&location):NULL;
 lock();passport_psi_value=-1;
 if(!j){strcpy(passport_haze_status,T("连接失败","Offline"));strcpy(passport_haze_meta,T("NEA · 离线","NEA | offline"));}
 else if(cJSON_IsTrue(cJSON_GetObjectItem(j,"available"))){
  cJSON *value=cJSON_GetObjectItem(j,"value");
  if(cJSON_IsNumber(value)&&value->valuedouble>=0&&value->valuedouble<=9999){
   passport_psi_value=value->valueint;
   bool stale=cJSON_IsTrue(cJSON_GetObjectItem(j,"stale"));
   snprintf(passport_haze_status,sizeof(passport_haze_status),"%s",stale?T("已过期","Stale"):haze_category(str(j,"category")));
   const char *stamp=str(j,"timestamp");char at[6]="--:--";
   if(strlen(stamp)>=16){memcpy(at,stamp+11,5);at[5]=0;}
   snprintf(passport_haze_meta,sizeof(passport_haze_meta),"NEA %s %s%s",haze_region_name(str(j,"region")),at,stale?T(" · 缓存"," | cached"):"");
  }else {strcpy(passport_haze_status,T("数据无效","Invalid data"));strcpy(passport_haze_meta,"NEA · 24h PSI");}
 }else {
  const char *reason=str(j,"reason");
  snprintf(passport_haze_status,sizeof(passport_haze_status),"%s",!strcmp(reason,"location_required")?T("选择区域","Choose region"):!strcmp(reason,"outside_singapore")?T("仅限新加坡","Singapore only"):T("暂不可用","Unavailable"));
  strcpy(passport_haze_meta,"NEA · 24h PSI");
 }
 passport_dirty=true;unlock();cJSON_Delete(j);
}
void passport_bus_request(unsigned stop,unsigned page){
 if(!work_queue || !passport_screen_awake())return;
 lock();
 if(stop!=bus_selected_stop || page!=bus_selected_page){
  bus_selected_stop=stop;bus_selected_page=page;atomic_fetch_add(&bus_generation,1);
  passport_bus_rows=0;strcpy(passport_bus_title,T("正在加载巴士站...","Loading bus stop..."));strcpy(passport_bus_meta,T("正在读取到站时间","Loading arrivals"));passport_dirty=true;
 }
 work_t work={.kind=2,.page=PAGE_BUS,.bus_stop=stop,.bus_page=page,.generation=atomic_load(&bus_generation),.epoch=atomic_load(&function_epoch)};
 unlock();(void)xQueueSend(work_queue,&work,0);
}
static void poll_bus(const work_t *work){
 passport_bus_stop_t stop;lock();
 if(!bus_config.count){passport_bus_rows=0;passport_bus_pages=1;strcpy(passport_bus_title,T("尚未设置巴士站","No bus stops saved"));strcpy(passport_bus_meta,T("请在手机设置添加站点","Add stops on phone"));passport_dirty=true;unlock();return;}
 stop=bus_config.stops[work->bus_stop%bus_config.count];bool online=passport_connected;char key[129];snprintf(key,sizeof(key),"%s",lta_account_key);unlock();
 cJSON *j=online?passport_direct_bus(stop.code,work->bus_page,key):NULL;
 memset(key,0,sizeof(key));
 lock();if(work->generation!=atomic_load(&bus_generation)||work->epoch!=atomic_load(&function_epoch)||passport_function_active()!=PAGE_BUS){unlock();cJSON_Delete(j);return;}
 snprintf(passport_bus_title,sizeof(passport_bus_title),"%s · %s",stop.code,stop.name[0]?stop.name:T("巴士站","Bus stop"));
 passport_bus_rows=0;
 if(!j || !cJSON_IsTrue(cJSON_GetObjectItem(j,"available"))){passport_bus_pages=1;snprintf(passport_bus_meta,sizeof(passport_bus_meta),"%s",public_failure());}
 else {
  cJSON *pages=cJSON_GetObjectItem(j,"pages");passport_bus_pages=cJSON_IsNumber(pages)&&pages->valueint>0&&pages->valueint<=24?pages->valueint:1;
  cJSON *row;cJSON_ArrayForEach(row,cJSON_GetObjectItem(j,"rows")){
   if(passport_bus_rows>=4)break;
   passport_bus_row_t *dest=&passport_bus_data[passport_bus_rows++];
   snprintf(dest->service,sizeof(dest->service),"%.12s",str(row,"service"));
   const char *label=str(row,"destination");size_t n=passport_utf8_prefix(label,sizeof(dest->destination)-1);memcpy(dest->destination,label,n);dest->destination[n]=0;
   dest->eta[0]=0;
   cJSON *eta=cJSON_GetObjectItem(row,"eta");int count=cJSON_GetArraySize(eta);
   for(int i=0;i<3;i++){
    cJSON *v=i<count?cJSON_GetArrayItem(eta,i):NULL;
    const char *value=cJSON_IsString(v)?v->valuestring:"--";
    cJSON *terminating=cJSON_GetArrayItem(cJSON_GetObjectItem(row,"terminating"),i);
    char cell[20];snprintf(cell,sizeof(cell),"%s%.4s%s",i?"  ":"",value,cJSON_IsTrue(terminating)?"T":"");
    strncat(dest->eta,cell,sizeof(dest->eta)-strlen(dest->eta)-1);
   }
  }
  const char *stamp=str(j,"updated_at");char at[6]="--:--";if(strlen(stamp)>=16){memcpy(at,stamp+11,5);at[5]=0;}
  snprintf(passport_bus_meta,sizeof(passport_bus_meta),"%u/%u · %s%s",work->bus_page%passport_bus_pages+1,passport_bus_pages,at,cJSON_IsTrue(cJSON_GetObjectItem(j,"stale"))?T(" · 已过期"," | stale"):"");
  if(!passport_bus_rows)strcpy(passport_bus_meta,T("此站暂无到站数据","No arrivals at this stop"));
 }
 passport_dirty=true;unlock();cJSON_Delete(j);
}
static void queue_control(unsigned kind,unsigned index){
 control_t control={kind,index};
 if(!control_queue || xQueueSend(control_queue,&control,0)!=pdTRUE){
  lock();snprintf(passport_network_status,sizeof(passport_network_status),"%s",T("正在处理，请稍后重试","Busy, retry shortly"));passport_dirty=true;unlock();
 }
}
void passport_wifi_profile_request(unsigned profile){
 if(profile>=passport_wifi_profile_count())return;
 queue_control(3,profile);
}
void passport_brightness_request(unsigned level){
 if(level<10 || level>100 || level%10)return;
 control_t control={7,level};
 bool queued=control_queue && xQueueSend(control_queue,&control,0)==pdTRUE;
 atomic_store(&brightness_save_failed,!queued);passport_dirty=true;
}
void passport_theme_request(unsigned theme){if(theme<3)queue_control(6,theme);}
void passport_language_request(unsigned language){
 if(language>1)return;
 queue_control(4,language);
}
static void connect_wifi_profile(unsigned profile){
 if(profile>=passport_wifi_profile_count())return;
 lock();passport_config_t next=passport_config;
 snprintf(next.ssid,sizeof(next.ssid),"%s",wifi_profiles[profile].ssid);
 snprintf(next.password,sizeof(next.password),"%s",wifi_password(profile));
 bool saved=store&&nvs_set_blob(store,"config",&next,sizeof(next))==ESP_OK&&nvs_commit(store)==ESP_OK;
 if(saved){passport_config=next;atomic_store(&wifi_current,profile);snprintf(passport_network_status,sizeof(passport_network_status),T("正在连接 %s","Connecting to %s"),passport_wifi_profile_label(profile));}
 else strcpy(passport_network_status,T("Wi-Fi 设置保存失败","Wi-Fi save failed"));
 passport_dirty=true;unlock();if(!saved)return;
 wifi_config_t sta={0};strcpy((char *)sta.sta.ssid,next.ssid);strcpy((char *)sta.sta.password,next.password);
 atomic_store(&wifi_switching,true);(void)esp_wifi_disconnect();
 esp_err_t error=esp_wifi_set_config(WIFI_IF_STA,&sta);
 wifi_begin_attempt();atomic_store(&wifi_switching,false);if(error==ESP_OK)error=wifi_connect_station();
 if(error!=ESP_OK){atomic_store(&wifi_deadline,0);atomic_store(&wifi_gave_up,true);lock();strcpy(passport_network_status,T("Wi-Fi 切换失败，请重试","Wi-Fi switch failed"));passport_dirty=true;unlock();}
}
static bool read_saved_photo(uint8_t *data,size_t bytes,void *context){
 (void)context;size_t n=bytes;
 bool ok=store && nvs_get_blob(store,"avatar",data,&n)==ESP_OK && n==bytes;
 if(ok)passport_photo_apply_theme(data,bytes,passport_config.theme);
 return ok;
}
static bool load_page_photo(void){
 static int64_t retry;
 bool failed=false;
 lock();
 if(esp_timer_get_time()<retry && !passport_photo_reserved()){unlock();return false;}
 if(passport_photo_load(passport_function_active(),passport_has_avatar,read_saved_photo,NULL))passport_dirty=true;
 else if(passport_has_avatar && !passport_avatar &&
         !passport_photo_refresh_pending() && (passport_function_active()==PAGE_HOME || passport_function_active()==PAGE_CARD)){
  retry=esp_timer_get_time()+5000000;failed=true;
 }
 unlock();
 return failed;
}
/* Network-worker ownership: never wait for HTTP shutdown while holding the
 * application mutex, and never replace an unconfirmed live server handle. */
char *passport_train_text;
char passport_train_summary[160],passport_train_status[96];
unsigned passport_train_alerts;
bool passport_train_known,passport_train_stale;
static atomic_bool train_refresh;
void passport_train_refresh_request(void){atomic_store(&train_refresh,true);}
static const char *train_failure(passport_direct_error_t e){
 switch(e){
 case DIRECT_KEY:return T("请在设置保存 LTA 密钥","Save LTA key in Settings");
 case DIRECT_AUTH:return T("LTA 密钥无权限","LTA key rejected / denied");
 case DIRECT_MEMORY:return T("内存不足，请稍后重试","Low memory; retry later");
 case DIRECT_CLOCK:return T("等待网络时间同步","Waiting for clock sync");
 case DIRECT_RESPONSE:return T("列车数据无效或过期","Invalid / expired train feed");
 default:return T("列车数据连接失败","Train feed connection failed");
 }
}
static void poll_train(bool details){
 char key[129];lock();snprintf(key,sizeof(key),"%s",lta_account_key);unlock();
 passport_train_feed_t feeds[2]={{0}};passport_direct_error_t errors[2];bool ok[2];
 unsigned count=0;char headline[160]={0};char *body=NULL;size_t used=0;
 for(unsigned i=0;i<2;i++){
  ok[i]=passport_train_fetch(i!=0,key,(uint64_t)time(NULL),&feeds[i],&errors[i]);
  if(ok[i]){
   count+=feeds[i].alerts;
   if(!headline[0]&&feeds[i].headline[0])snprintf(headline,sizeof(headline),"%s",feeds[i].headline);
   if(details && feeds[i].text){
    if(feeds[i].length>PASSPORT_TRAIN_TEXT_LIMIT-used){ok[i]=false;errors[i]=DIRECT_RESPONSE;}
    else {char *p=realloc(body,used+feeds[i].length+1);if(!p){ok[i]=false;errors[i]=DIRECT_MEMORY;}else {body=p;memcpy(body+used,feeds[i].text,feeds[i].length+1);used+=feeds[i].length;}}
   }
  }
  passport_train_free(&feeds[i]);
 }
 memset(key,0,sizeof(key));
 ESP_LOGI("train_alerts","GTFS service=%d trip=%d errors=%d/%d items=%u",ok[0],ok[1],errors[0],errors[1],count);
 lock();
 bool complete=ok[0]&&ok[1];
 if(complete||count){passport_train_alerts=count;snprintf(passport_train_summary,sizeof(passport_train_summary),"%s",headline);}
 if(complete)passport_train_known=true;
 passport_train_stale=!complete;
 if(complete){time_t now=time(NULL);struct tm t;localtime_r(&now,&t);snprintf(passport_train_status,sizeof(passport_train_status),T("LTA · %02d:%02d · %u 条","LTA %02d:%02d | %u items"),t.tm_hour,t.tm_min,count);}
 else snprintf(passport_train_status,sizeof(passport_train_status),"%s",train_failure(ok[0]?errors[1]:errors[0]));
 if(details && passport_function_active()==PAGE_TRAIN){free(passport_train_text);passport_train_text=body;body=NULL;}
 passport_dirty=true;unlock();free(body);
}
static void settings_web_poll(void){
 bool wanted=passport_web_required(passport_function_active(),atomic_load(&setup_active),
                                  phone_relay_active());
 if(settings_server && (!wanted || !atomic_load(&settings_web_ready))){
  atomic_store(&settings_web_ready,false);
  if(httpd_stop(settings_server)!=ESP_OK){ESP_LOGW("passport","Settings web shutdown pending");return;}
  settings_server=NULL;
 }
 if(!wanted || settings_server)return;
 httpd_config_t http=HTTPD_DEFAULT_CONFIG();http.stack_size=8192;http.max_uri_handlers=7;
 httpd_handle_t candidate=NULL;
 if(httpd_start(&candidate,&http)!=ESP_OK){ESP_LOGW("passport","Settings web startup failed; retrying");return;}
 settings_server=candidate;
 const httpd_uri_t routes[]={
  {.uri="/",.method=HTTP_GET,.handler=settings_get},
  {.uri="/config",.method=HTTP_GET,.handler=config_get},
  {.uri="/config",.method=HTTP_POST,.handler=config_post},
  {.uri="/time",.method=HTTP_POST,.handler=clock_post},
  {.uri="/avatar",.method=HTTP_POST,.handler=avatar_post},
  {.uri="/chat",.method=HTTP_POST,.handler=chat_post},
  {.uri="/relay",.method=HTTP_POST,.handler=relay_post}};
 for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++){
  if(httpd_register_uri_handler(settings_server,&routes[i])!=ESP_OK){
   ESP_LOGW("passport","Settings web route startup failed; rolling back");
   if(httpd_stop(settings_server)==ESP_OK)settings_server=NULL;
   return;
  }
 }
 atomic_store(&settings_web_ready,true);
}
static void network_worker(void *unused) {
 (void)unused;work_t work={0};int64_t weather_due=0,train_due=0;unsigned train_page=PAGE_HOME;
 for(;;) {
  /* The consumer owns queued text, including discarded stale jobs. */
  free(work.text);work.text=NULL;
  atomic_store(&network_busy,true);
  settings_web_poll();
  atomic_store(&network_busy,false);
  if(passport_voice_busy() || (atomic_load(&audio_desired) && (phone_relay_active() || atomic_load(&reply_audio)))){
   /* Recording suspends network work; phone playback gets weather through
    * the HTTP server. Release this idle stack before allocating audio DMA. */
   audio_worker_start_if_needed();
   network_voice_ready_at=esp_timer_get_time()+100000;
   atomic_store(&network_worker_running,false);
   vTaskDelete(NULL);return;
  }
  if(!passport_voice_busy() && passport_muse_speech_can_start()){
   free(work.text);work.text=NULL;
   network_speech_ready_at=esp_timer_get_time()+100000;
   atomic_store(&network_speech_handoff,true);
   atomic_store(&network_worker_running,false);
   vTaskDelete(NULL);return;
  }
  if(passport_muse_speech_busy()){vTaskDelay(pdMS_TO_TICKS(100));continue;}
  audio_worker_start_if_needed();
  if(passport_voice_busy()){vTaskDelay(pdMS_TO_TICKS(100));continue;}
  if(load_page_photo()){
   network_photo_ready_at=esp_timer_get_time()+100000;
   atomic_store(&network_photo_handoff,true);atomic_store(&network_worker_running,false);
   vTaskDelete(NULL);return;
  }
  passport_muse_poll();
  passport_navigation_poll();
  wifi_timeout_check();
  unsigned current=passport_function_active();
  bool request=atomic_exchange(&train_refresh,false);
  if(current!=train_page){
   if(current==PAGE_TRAIN)train_due=0;
   else {lock();free(passport_train_text);passport_train_text=NULL;unlock();}
   train_page=current;
  }
  if(request)train_due=0;
  if((current==PAGE_HOME||current==PAGE_TRAIN) && passport_connected && passport_screen_awake() && esp_timer_get_time()>=train_due){
   train_due=esp_timer_get_time()+120000000;
   atomic_store(&network_busy,true);poll_train(current==PAGE_TRAIN);atomic_store(&network_busy,false);continue;
  }
  control_t control;
  if(control_queue && xQueueReceive(control_queue,&control,0)==pdTRUE)work=(work_t){.kind=control.kind,.wifi_profile=control.index};
  else if(passport_screen_awake() && esp_timer_get_time()>=weather_due){
   work=(work_t){.page=PAGE_WEATHER,.epoch=atomic_load(&function_epoch)};
   weather_due=esp_timer_get_time()+120000000;
  }else if(xQueueReceive(work_queue,&work,pdMS_TO_TICKS(1000))!=pdTRUE)continue;
  atomic_store(&network_busy,true);
  settings_web_poll();
  if(passport_voice_busy())continue;
  /* Page fetches queued just before the display slept are dropped, not
   * fetched in the background; the main loop re-requests the current page
   * when a key wakes the display. Control work (kinds 3-7) still runs. */
  if(!passport_screen_awake() && work.kind<=2)continue;
  bool forced_weather=work.kind==5;
  if(forced_weather){
   atomic_fetch_add(&weather_generation,1);
   passport_direct_weather_refresh();work=(work_t){.page=PAGE_WEATHER,.epoch=atomic_load(&function_epoch)};
   lock();passport_weather[0]=0;passport_dirty=true;unlock();
  }
  if(work.kind<=2 && (!passport_service_allowed(passport_function_active(),work.page)||
      (work.page!=PAGE_WEATHER && work.epoch!=atomic_load(&function_epoch))))continue;
  if(work.page==PAGE_SPOTIFY || work.page==PAGE_READER){
   if(work.epoch!=atomic_load(&function_epoch) || passport_function_active()!=(unsigned)work.page)continue;
   bool spotify=work.page==PAGE_SPOTIFY;
   const char *path=spotify?"/v1/spotify":"/v1/reader";
   char body[48];const char *post=NULL;
   if(work.kind==8){
    static const char *reader_actions[]={"previous","next","open","size","shelf"};
    static const char *spotify_actions[]={"previous","next","toggle"};
    unsigned action=work.wifi_profile;
    if(action>(spotify?2u:4u))continue;
    snprintf(body,sizeof(body),"{\"action\":\"%s\"}",spotify?spotify_actions[action]:reader_actions[action]);
    post=body;path=spotify?"/v1/spotify/control":"/v1/reader/control";
   }
   cJSON *j=bridge_request(path,post);
   if(work.epoch==atomic_load(&function_epoch)){
    lock();
    if(spotify){
     passport_feature_copy(passport_spotify.state,sizeof(passport_spotify.state),j?str(j,"state"):"offline");
     passport_feature_copy(passport_spotify.title,sizeof(passport_spotify.title),j?str(j,"title"):"Check companion, Spotify login and active player");
     passport_feature_copy(passport_spotify.artist,sizeof(passport_spotify.artist),j?str(j,"artist"):"");
     passport_feature_copy(passport_spotify.device,sizeof(passport_spotify.device),j?str(j,"device"):"");
     if(j){passport_spotify.progress_ms=(unsigned)fmax(0,cJSON_GetNumberValue(cJSON_GetObjectItem(j,"progress_ms")));passport_spotify.duration_ms=(unsigned)fmax(0,cJSON_GetNumberValue(cJSON_GetObjectItem(j,"duration_ms")));}
    }else if(j){
     passport_feature_copy(passport_reader.state,sizeof(passport_reader.state),str(j,"state"));
     passport_feature_copy(passport_reader.title,sizeof(passport_reader.title),str(j,"title"));
     passport_feature_copy(passport_reader.text,sizeof(passport_reader.text),str(j,"text"));
     passport_reader.count=(unsigned)fmax(0,cJSON_GetNumberValue(cJSON_GetObjectItem(j,"count")));
     passport_reader.index=(unsigned)fmax(0,cJSON_GetNumberValue(cJSON_GetObjectItem(j,"index")));
     passport_reader.page=(unsigned)fmax(0,cJSON_GetNumberValue(cJSON_GetObjectItem(j,"page")));
     passport_reader.pages=(unsigned)fmax(0,cJSON_GetNumberValue(cJSON_GetObjectItem(j,"pages")));
     passport_reader.large=cJSON_IsTrue(cJSON_GetObjectItem(j,"large"));
    }else passport_feature_copy(passport_reader.state,sizeof(passport_reader.state),"offline");
    passport_dirty=true;unlock();
   }
   cJSON_Delete(j);continue;
  }
  if(work.kind==7){
   unsigned level=work.wifi_profile;
   lock();bool saved=level>=10 && level<=100 && level%10==0 && store &&
    nvs_set_u8(store,"brightness",level)==ESP_OK && nvs_commit(store)==ESP_OK;
   if(saved)atomic_store(&display_brightness,level);
   atomic_store(&brightness_save_failed,!saved);passport_dirty=true;unlock();continue;
  }
  if(work.kind==6){
   lock();passport_config_t next=passport_config;next.theme=work.wifi_profile;
   if(next.theme<3 && store && nvs_set_blob(store,"config",&next,sizeof(next))==ESP_OK && nvs_commit(store)==ESP_OK){passport_config=next;passport_photo_invalidate();passport_dirty=true;}
   else {snprintf(passport_network_status,sizeof(passport_network_status),"%s",T("外观保存失败","Appearance save failed"));passport_dirty=true;}
   unlock();continue;
  }
  if(work.kind==4){lock();if(store && nvs_set_u8(store,"language",work.wifi_profile)==ESP_OK && nvs_commit(store)==ESP_OK){passport_language_set(work.wifi_profile);localize_cached_status();passport_dirty=true;}unlock();continue;}
  if(work.kind==3){connect_wifi_profile(work.wifi_profile);continue;}
  if(work.kind==2){poll_bus(&work);continue;}
  if(work.kind==0 && phone_relay_active() && (work.page==PAGE_ATHENA || work.page==PAGE_RADIO || work.page==PAGE_HERMES))continue;
  if(work.kind==1){passport_reply_audio_stop();cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"text",work.text);char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);
   char path[40];snprintf(path,sizeof(path),"/v1/chat/%s",work.agent);j=body?bridge_request(path,body):NULL;free(body);
   if(work.epoch!=atomic_load(&function_epoch)){cJSON_Delete(j);continue;}
   int a=!strcmp(work.agent,"muse");lock();char *reply=passport_reply_buffer(a);if(reply)strcpy(reply,j?T("正在思考，请稍候...","Thinking, please wait..."):T("发送失败，请检查电脑服务","Send failed: check bridge"));passport_dirty=true;unlock();cJSON_Delete(j);continue;
  }
  if(work.page==PAGE_HOME || work.page==PAGE_WEATHER){
   /* The phone owns the upstream weather request while its PCM relay runs. */
   if(phone_relay_active())continue;
   if(work.page==PAGE_HOME)poll_haze();
   char city[24];lock();snprintf(city,sizeof(city),"%s",passport_config.city);bool online=passport_connected;unlock();
   cJSON *j=NULL;
   if(online){
    j=fetch_weather(city,atomic_load(&audio_desired));
   }
   if(!copy_weather(j)){lock();strcpy(passport_weather,public_failure());passport_dirty=true;unlock();}cJSON_Delete(j);
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
   if(forced_weather)puts("PASSPORT_TEST weather-refresh-done");
#endif
  } else if(work.page==PAGE_ATHENA){cJSON *j=bridge_request("/v1/usage",NULL);
   if(work.epoch!=atomic_load(&function_epoch)){cJSON_Delete(j);continue;}
   lock();if(!j)snprintf(passport_athena,sizeof(passport_athena),T("连接失败\n请检查电脑服务%s","Offline\nCheck computer bridge%s"),passport_athena[0]?T("\n旧数据已过期","\nPrevious data is stale"):"");
   else passport_usage_format(j,time(NULL),passport_language_get(),passport_athena,sizeof(passport_athena));
   passport_dirty=true;unlock();cJSON_Delete(j);
  } else if(work.page==PAGE_HERMES || work.page==PAGE_MUSE){bool muse=work.page==PAGE_MUSE;cJSON *j=bridge_request(muse?"/v1/reply/muse":"/v1/reply/hermes",NULL);if(j){if(work.epoch==atomic_load(&function_epoch))copy_reply(muse,j);cJSON_Delete(j);}}
  else if(work.page==PAGE_RADIO){cJSON *j=bridge_request("/v1/stations",NULL);if(j && work.epoch!=atomic_load(&function_epoch)){cJSON_Delete(j);continue;}if(j){lock();unsigned n=0;cJSON *s;cJSON_ArrayForEach(s,cJSON_GetObjectItem(j,"stations")){if(n>=20)break;snprintf(station_names[n],64,"%s / %s",str(s,"country"),str(s,"name"));n++;}if(n){passport_station_count=n;snprintf(passport_station,sizeof(passport_station),"%s",station_names[last_station%n]);}passport_dirty=true;unlock();cJSON_Delete(j);}}
  else if(work.page==PAGE_SETTINGS){cJSON *j=bridge_request("/v1/status",NULL);if(j){cJSON *t=cJSON_GetObjectItem(j,"time");if(cJSON_IsNumber(t)){struct timeval tv={.tv_sec=(time_t)t->valuedouble};settimeofday(&tv,NULL);}cJSON_Delete(j);}}
 }
}
static void network_worker_start(void){
 if(!work_queue || atomic_exchange(&network_worker_running,true))return;
 if(xTaskCreate(network_worker,"passport_net",8192,NULL,3,NULL)!=pdPASS)atomic_store(&network_worker_running,false);
}
/* Main task only, outside passport_mutex. The retired network task relinquishes
 * all cache/HTTP ownership before publishing its handoff. */
void passport_background_poll(void){
 if(passport_voice_busy() || (atomic_load(&audio_desired) && (phone_relay_active() || atomic_load(&reply_audio)))){
  wifi_timeout_check();audio_worker_start_if_needed();return;
 }
 if(atomic_load(&network_photo_handoff)){
  if(atomic_load(&network_worker_running) || esp_timer_get_time()<network_photo_ready_at)return;
  unsigned page=passport_function_active();lock();
  if(passport_has_avatar && !passport_avatar && !passport_photo_refresh_pending() && (page==PAGE_HOME || page==PAGE_CARD))passport_photo_reserve();
  unlock();atomic_store(&network_photo_handoff,false);
 }
 if(atomic_load(&network_speech_handoff)){
  if(atomic_load(&network_worker_running) || esp_timer_get_time()<network_speech_ready_at)return;
  if(network_speech_was_busy && !passport_muse_speech_busy()){
   network_speech_was_busy=false;network_speech_ready_at=esp_timer_get_time()+100000;return;
  }
  if(passport_function_active()!=PAGE_MUSE || passport_voice_busy())passport_muse_speech_stop();
  else if(!passport_muse_speech_busy())passport_muse_speech_poll();
  if(passport_muse_speech_busy()){network_speech_was_busy=true;return;}
  atomic_store(&network_speech_handoff,false);
 }
 network_worker_start();
}
void passport_network_poll(passport_page_t page) {
 if(page==PAGE_MUSE && passport_muse_supported())return;
 if(!work_queue || !passport_screen_awake())return;
 if(!passport_service_allowed(passport_function_active(),page))return;
 work_t work={.page=page,.epoch=atomic_load(&function_epoch)};(void)xQueueSend(work_queue,&work,0);
}
void passport_reply_audio_stop(void){
 passport_muse_speech_stop();atomic_store(&reply_audio,false);atomic_store(&audio_desired,false);atomic_fetch_add(&audio_generation,1);
}
void passport_audio_request(bool playing,unsigned station) {
 if(passport_function_active()==PAGE_HERMES || passport_function_active()==PAGE_MUSE)return;
 atomic_store(&reply_audio,false);
 playing=playing && passport_function_active()==PAGE_RADIO;
 if(playing!=atomic_load(&audio_desired) || station!=atomic_load(&audio_station)) {
  atomic_store(&audio_station,station);atomic_store(&audio_desired,playing);atomic_store(&audio_bytes_written,0);atomic_fetch_add(&audio_generation,1);
 }
 lock();last_station=station;
 if(passport_station_count && station<20 && station_names[station][0])snprintf(passport_station,sizeof(passport_station),"%s",station_names[station]);
 passport_dirty=true;unlock();
}
static void audio_flush_tail(uint8_t *pcm,unsigned generation){
 memset(pcm,0,1024);
 for(unsigned i=0;i<3 && generation==atomic_load(&audio_generation) && atomic_load(&audio_desired);i++)
  if(bsp_audio_write(pcm,1024)!=ESP_OK)break;
}
static void audio_worker(void *unused) {
 (void)unused;union {uint8_t bytes[2048];phone_pcm_t packet;} scratch;uint8_t *pcm=scratch.bytes;
 unsigned phone_format_generation=0;
 for(;;){
  passport_voice_poll();
  if(passport_voice_busy()){
   if(audio_ok){bsp_audio_deinit();audio_ok=false;phone_format_generation=0;}
   passport_voice_run(scratch.bytes,sizeof(scratch.bytes));continue;
  }
  if(atomic_load(&phone_relay_enabled) && !phone_relay_active()){
   atomic_store(&phone_relay_enabled,false);atomic_store(&audio_desired,false);atomic_fetch_add(&audio_generation,1);
  }
  if(!atomic_load(&audio_desired)){
   lock();
   if(!atomic_load(&audio_desired) && phone_pcm){vQueueDelete(phone_pcm);phone_pcm=NULL;}
   unlock();
   if(audio_ok){
    esp_err_t released=bsp_audio_deinit();audio_ok=false;phone_format_generation=0;
    if(released!=ESP_OK)ESP_LOGW(TAG,"Audio resource release failed: %s",esp_err_to_name(released));
   }
   if(!passport_voice_needs_worker()){
    atomic_store(&audio_worker_running,false);
    vTaskDelete(NULL);return;
   }
   vTaskDelay(pdMS_TO_TICKS(100));continue;
  }
  unsigned generation=atomic_load(&audio_generation),station=atomic_load(&audio_station);
  if((phone_relay_active() || atomic_load(&reply_audio)) && (atomic_load(&network_worker_running) || esp_timer_get_time()<network_voice_ready_at)){vTaskDelay(pdMS_TO_TICKS(20));continue;}
  if(atomic_load(&phone_relay_enabled)){
   if(!phone_relay_active()){atomic_store(&audio_desired,false);continue;}
   phone_pcm_t *packet=&scratch.packet;
   lock();bool received=phone_pcm && xQueueReceive(phone_pcm,packet,0)==pdTRUE;unlock();
   if(!received){
    if(atomic_load(&reply_audio) && atomic_load(&speech_end_generation)==generation){
     if(audio_ok)audio_flush_tail(pcm,generation);
     if(generation==atomic_load(&audio_generation)){atomic_store(&audio_desired,false);atomic_store(&reply_audio,false);}
    }else vTaskDelay(pdMS_TO_TICKS(20));
    continue;
   }
   if(packet->generation==generation && generation==atomic_load(&audio_generation) && atomic_load(&audio_desired)){
    if(phone_format_generation!=generation){
     if(!audio_ok)audio_ok=bsp_audio_init()==ESP_OK;
     if(!audio_ok || bsp_audio_set_format(16000,16,1)!=ESP_OK){atomic_store(&audio_desired,false);continue;}
     phone_format_generation=generation;
    }
    bsp_audio_set_volume(atomic_load(&audio_volume));
    if(bsp_audio_write(packet->pcm,packet->bytes)!=ESP_OK)atomic_store(&audio_desired,false);
    else atomic_fetch_add(&audio_bytes_written,packet->bytes);
   }
   continue;
  }
  char url[320],token[110];lock();
  if(atomic_load(&reply_audio))snprintf(url,sizeof(url),"%s/v1/speech/hermes?id=%s",bridge_base(),speech_id);
  else snprintf(url,sizeof(url),"%s/v1/radio?station=%u",bridge_base(),station);
  snprintf(token,sizeof(token),"Bearer %s",passport_config.token);unlock();
  esp_http_client_config_t cfg={.url=url,.timeout_ms=5000,.buffer_size=1024};
  esp_http_client_handle_t h=esp_http_client_init(&cfg);bool success=false;
  if(h){esp_http_client_set_header(h,"Authorization",token);
   if(esp_http_client_open(h,0)==ESP_OK && esp_http_client_fetch_headers(h)>=0 && esp_http_client_get_status_code(h)==200){
    /* Failed bridge requests must not reserve persistent codec/I2S DMA RAM. */
    if(!audio_ok)audio_ok=bsp_audio_init()==ESP_OK;
    if(!audio_ok || bsp_audio_set_format(16000,16,1)!=ESP_OK){esp_http_client_close(h);esp_http_client_cleanup(h);atomic_store(&audio_desired,false);continue;}
    success=true;int n,carry=-1;
    while(generation==atomic_load(&audio_generation) && atomic_load(&audio_desired)){
     int prefix=carry>=0?1:0;
     if(prefix)pcm[0]=(uint8_t)carry;
     n=esp_http_client_read(h,(char *)pcm+prefix,sizeof(scratch.bytes)-prefix);
     if(n<=0 || generation!=atomic_load(&audio_generation) || !atomic_load(&audio_desired))break;
     n+=prefix;carry=(n&1)?pcm[--n]:-1;
     bsp_audio_set_volume(atomic_load(&audio_volume));
     if(n && bsp_audio_write(pcm,n)!=ESP_OK){success=false;break;}
     if(n)atomic_fetch_add(&audio_bytes_written,n);
    }
   }
  }
  if(h){esp_http_client_close(h);esp_http_client_cleanup(h);}
  if(success && atomic_load(&reply_audio) && audio_ok)audio_flush_tail(pcm,generation);
  if(generation==atomic_load(&audio_generation)){atomic_store(&audio_desired,false);if(atomic_exchange(&reply_audio,false))continue;lock();snprintf(passport_station,sizeof(passport_station),"%.32s\n%s",station_names[last_station%20],success?T("播放已结束","Playback ended"):T("连接失败，请重试","Offline, please retry"));passport_dirty=true;unlock();}
 }
}
void passport_network_start(void) {
 snprintf(setup_code,sizeof(setup_code),"%06lu",(unsigned long)(esp_random()%1000000));
 snprintf(setup_password,sizeof(setup_password),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
 setenv("TZ","SGT-8",1);tzset();
 ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
 esp_netif_create_default_wifi_sta();esp_netif_create_default_wifi_ap();
 esp_sntp_config_t ntp=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(3, ESP_SNTP_SERVER_LIST("time.cloudflare.com","time.google.com","pool.ntp.org"));ntp.start=false;ESP_ERROR_CHECK(esp_netif_sntp_init(&ntp));
 wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&cfg));
 ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
 ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
 ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
 wifi_config_t sta={0};strcpy((char *)sta.sta.ssid,passport_config.ssid);strcpy((char *)sta.sta.password,passport_config.password);
 ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&sta));ESP_ERROR_CHECK(esp_wifi_start());
 localize_cached_status();
 if(!passport_config.ssid[0])passport_open_setup();
 work_queue=xQueueCreate(2,sizeof(work_t));
 control_queue=xQueueCreate(4,sizeof(control_t));
 network_worker_start();
 passport_network_poll(PAGE_SETTINGS);
}
