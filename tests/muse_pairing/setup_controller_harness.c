/* Exercise the real setup callback adapter with virtual BLE/Wi-Fi/storage.
 * Cryptography and storage namespace isolation have separate real-core tests. */
#define _POSIX_C_SOURCE 200809L
#include "setup_controller.h"
#include "link_pairing.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static uint32_t generation=10;
static bool valid=true,armed,confirmed,provisioning,complete,fail_send,fail_join,stale_join,stale_mint;
static unsigned writes,fail_write,commits,joins,disconnects,notifications,minted_wipes,scans;
static bool advertising;
static char status[64],saved_access[64],saved_refresh[64];
static char *mint_a,*mint_r;static size_t mint_a_len,mint_r_len;
void __real_free(void *);
void __wrap_free(void *p){
 if(p && (p==mint_a || p==mint_r)){
  size_t n=p==mint_a?mint_a_len:mint_r_len;
  for(size_t i=0;i<n;i++)assert(((char *)p)[i]==0);
  if(p==mint_a)mint_a=NULL;else mint_r=NULL;minted_wipes++;
 }
 __real_free(p);
}
bool config_setup_complete(void){return complete;}
bool config_set_str(const char *key,const char *value){
 assert(!strncmp(key,"access",6)||!strncmp(key,"refresh",7)||!strcmp(key,"username")||!strncmp(key,"api_",4)||!strcmp(key,"noise_host"));
 writes++;if(writes==fail_write)return false;
 if(!strcmp(key,"access_token"))snprintf(saved_access,sizeof(saved_access),"%s",value);
 if(!strcmp(key,"refresh_token"))snprintf(saved_refresh,sizeof(saved_refresh),"%s",value);
 return true;
}
bool config_erase_key(const char *key){assert(key && *key);return true;}
bool config_mark_setup_complete(void){complete=true;return true;}
bool config_clear_pairing(void){saved_access[0]=saved_refresh[0]=0;return true;}
bool config_clear_setup_complete(void){complete=false;return true;}
const char *identity_node_id(void){return "homelink-test";}
void link_pairing_add_device_info(cJSON *root){cJSON_AddNumberToObject(root,"pairing_protocol",5);}
bool link_pairing_session_is_current(uint32_t g){return valid && g && g==generation;}
uint32_t link_pairing_session_generation(void){return generation;}
bool link_pairing_confirmation_required(void){return valid && !confirmed;}
bool link_pairing_session_confirmed(void){return valid && confirmed;}
bool link_pairing_arm_confirmation(uint32_t g){if(!link_pairing_session_is_current(g))return false;armed=true;return true;}
uint32_t link_pairing_confirm_session(uint32_t g){if(!armed || !link_pairing_session_is_current(g))return 0;armed=false;confirmed=true;return ++generation;}
bool link_pairing_provisioning_session_valid(uint32_t g){return provisioning && link_pairing_session_is_current(g);}
bool link_pairing_commit_provisioning(uint32_t g,bool (*commit)(void)){
 if(!link_pairing_provisioning_session_valid(g))return false;
 commits++;return commit();
}
bool ble_server_has_connection(void){return true;}
bool ble_server_send_pairing_status(const char *s,uint32_t g){if(fail_send || !link_pairing_session_is_current(g))return false;snprintf(status,sizeof(status),"%s",s);return true;}
void ble_server_disconnect_pairing_session(uint32_t g){if(link_pairing_session_is_current(g))disconnects++;}
void ble_server_send_chunked(const char *json){cJSON *j=cJSON_Parse(json);assert(j);assert(!strcmp(cJSON_GetObjectItem(j,"type")->valuestring,"device_info"));assert(cJSON_GetObjectItem(j,"pairing_protocol")->valueint==5);cJSON_Delete(j);}
bool ble_server_send_encrypted_json(const char *json,uint32_t g){cJSON *j=cJSON_Parse(json);assert(j && !strcmp(cJSON_GetObjectItem(j,"type")->valuestring,"wifi_scan_result"));cJSON_Delete(j);return link_pairing_session_is_current(g);}
void ble_server_set_companion_advertising(bool enabled){advertising=enabled;}
static void changed(void){notifications++;}
static bool join(const char *ssid,const char *password){assert(!strcmp(ssid,"Test WiFi") && !strcmp(password,"test-only"));assert(muse_setup_busy());joins++;if(stale_join){generation++;provisioning=false;}return !fail_join;}
static cJSON *scan(void){assert(muse_setup_busy());scans++;cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"type","wifi_scan_result");cJSON_AddItemToObject(j,"networks",cJSON_CreateArray());return j;}
static bool mint(const char *source,const char *url,char **access,char **refresh){
 assert(!strcmp(source,"session-test") && !strcmp(url,"https://example.test"));
 mint_a=strdup("device-test");mint_r=strdup("refresh-test");assert(mint_a && mint_r);
 mint_a_len=strlen(mint_a);mint_r_len=strlen(mint_r);*access=mint_a;*refresh=mint_r;
 if(stale_mint){generation++;provisioning=false;}return true;
}
static muse_setup_hooks_t hooks={join,scan,mint,changed};
static const ble_callbacks_t *cb;
static void reset(void){
 valid=true;generation+=10;armed=confirmed=provisioning=complete=false;
 fail_send=fail_join=stale_join=stale_mint=false;
 writes=fail_write=commits=joins=disconnects=0;advertising=false;
 status[0]=saved_access[0]=saved_refresh[0]=0;
 assert(muse_setup_init(&hooks,"test"));cb=muse_setup_callbacks();
}
static void provision(const char *access,const char *refresh){
 cb->on_provision("Test WiFi","test-only",access,refresh,"test-user","https://ignored.test/ota",true,
  "","https://example.test","",generation);
 assert(!muse_setup_busy());
}
int main(void){
 assert(!muse_setup_init(NULL,"test"));muse_setup_hooks_t bad={0};assert(!muse_setup_init(&bad,"test"));
 reset();assert(!cb->on_ota && !cb->on_unpair);assert(!muse_setup_request_confirmation());cb->on_get_device_info();
 cb->on_wifi_scan();assert(!scans && !muse_setup_busy());
 cb->on_client_connected();assert(muse_setup_state()==MUSE_SETUP_CONNECTED);
 cb->on_pairing_client_finished(generation);assert(armed && !strcmp(status,"confirm_required"));
 assert(muse_setup_request_confirmation());uint32_t old=generation;generation+=10;armed=false;
 cb->on_pairing_client_finished(generation);assert(armed);
 muse_setup_poll();assert(!confirmed && generation!=old && muse_setup_state()==MUSE_SETUP_CONFIRM);
 assert(muse_setup_request_confirmation());muse_setup_poll();assert(confirmed && !strcmp(status,"pairing_confirmed"));
 cb->on_wifi_scan();assert(scans==1 && !muse_setup_busy());
 reset();fail_send=true;cb->on_pairing_client_finished(generation);assert(disconnects==1 && !armed && !muse_setup_request_confirmation());
 reset();cb->on_pairing_client_finished(generation);valid=false;muse_setup_poll();assert(muse_setup_state()==MUSE_SETUP_IDLE && !muse_setup_request_confirmation());
 reset();confirmed=provisioning=true;provision("access-test","refresh-test");
 assert(complete && commits==1 && advertising && !strcmp(status,"auth_ok"));
 assert(!strcmp(saved_access,"access-test") && !strcmp(saved_refresh,"refresh-test"));
 unsigned previous=writes;provision("other-test","other-refresh");assert(writes==previous && !strcmp(saved_access,"access-test"));
 reset();confirmed=provisioning=true;stale_join=true;provision("access-test","refresh-test");assert(!writes && !commits && !complete);
 reset();confirmed=provisioning=true;fail_join=true;provision("access-test","refresh-test");assert(!writes && !commits && muse_setup_state()==MUSE_SETUP_ERROR);
 reset();confirmed=provisioning=true;fail_write=2;provision("access-test","refresh-test");assert(!complete && !saved_access[0] && !saved_refresh[0] && !strcmp(status,"error_storage"));
 reset();confirmed=provisioning=true;provision("session-test","");assert(complete && !strcmp(saved_access,"device-test") && minted_wipes==2);
 reset();confirmed=provisioning=true;stale_mint=true;provision("session-test","");assert(!writes && !commits && minted_wipes==4);
 reset();hooks.mint_tokens=NULL;assert(muse_setup_init(&hooks,"test"));confirmed=provisioning=true;provision("session-test","");assert(!writes && !complete && !strcmp(status,"auth_failed"));
 assert(notifications>0);puts("PASS official Muse setup controller");return 0;
}
