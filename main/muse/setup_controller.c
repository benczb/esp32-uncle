/* Pocket Passport adapter for the official Muse v5 setup callbacks. */
#include "setup_controller.h"
#include "config_store.h"
#include "identity.h"
#include "link_pairing.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static muse_setup_hooks_t hooks;
static char firmware[48];
static atomic_uint state, prompt, confirmation;
static atomic_bool busy;
static void changed(muse_setup_state_t next){atomic_store(&state,next);if(hooks.changed)hooks.changed();}
muse_setup_state_t muse_setup_state(void){return atomic_load(&state);}
bool muse_setup_busy(void){return atomic_load(&busy);}
static bool take_operation(void){bool expected=false;return atomic_compare_exchange_strong(&busy,&expected,true);}
static void error_for(uint32_t generation,const char *status){
 if(link_pairing_session_is_current(generation)){
  (void)ble_server_send_pairing_status(status,generation);changed(MUSE_SETUP_ERROR);
 }
}
static void connected(void){if(!config_setup_complete())changed(MUSE_SETUP_CONNECTED);}
static void disconnected(void){
 atomic_store(&prompt,0);atomic_store(&confirmation,0);
 changed(config_setup_complete()?MUSE_SETUP_PAIRED:MUSE_SETUP_IDLE);
}
static void device_info(void){
 cJSON *root=cJSON_CreateObject();if(!root)return;
 cJSON_AddStringToObject(root,"type","device_info");
 cJSON_AddStringToObject(root,"node_id",identity_node_id());
 cJSON_AddStringToObject(root,"version",firmware);
 link_pairing_add_device_info(root);
 char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);
 if(json){ble_server_send_chunked(json);free(json);}
}
static void client_finished(uint32_t generation){
 if(!generation || !link_pairing_session_is_current(generation) ||
    !ble_server_has_connection() || !link_pairing_confirmation_required())return;
 if(!ble_server_send_pairing_status("confirm_required",generation)){
  ble_server_disconnect_pairing_session(generation);return;
 }
 if(!link_pairing_arm_confirmation(generation))return;
 atomic_store(&prompt,generation);changed(MUSE_SETUP_CONFIRM);
}
bool muse_setup_request_confirmation(void){
 unsigned generation=atomic_load(&prompt);if(!generation)return false;
 atomic_store(&confirmation,generation);return true;
}
void muse_setup_poll(void){
 unsigned requested=atomic_exchange(&confirmation,0);
 if(requested){
  uint32_t confirmed=link_pairing_confirm_session(requested);
  unsigned expected=requested;(void)atomic_compare_exchange_strong(&prompt,&expected,0);
  if(confirmed){
   if(ble_server_send_pairing_status("pairing_confirmed",confirmed))changed(MUSE_SETUP_CONFIRMED);
   else {error_for(confirmed,"error_pairing_unavailable");ble_server_disconnect_pairing_session(confirmed);}
  }
 }
 unsigned pending=atomic_load(&prompt);
 if(pending && !link_pairing_session_is_current(pending)){
  unsigned expected=pending;
  if(atomic_compare_exchange_strong(&prompt,&expected,0) && muse_setup_state()==MUSE_SETUP_CONFIRM)changed(MUSE_SETUP_IDLE);
 }
}
static void wifi_scan(void){
 uint32_t generation=link_pairing_session_generation();
 if(!generation || !link_pairing_session_confirmed())return;
 if(!hooks.scan_wifi || !take_operation()){error_for(generation,"error_operation_in_progress");return;}
 cJSON *root=hooks.scan_wifi();
 char *json=root?cJSON_PrintUnformatted(root):NULL;cJSON_Delete(root);
 if(json){(void)ble_server_send_encrypted_json(json,generation);free(json);}
 else error_for(generation,"error_wifi_scan");
 atomic_store(&busy,false);
}
typedef struct {
 const char *access,*refresh,*username,*api,*api_v2,*noise;
} staged_t;
static const staged_t *staged;
static bool optional(const char *key,const char *value){return value&&*value?config_set_str(key,value):config_erase_key(key);}
/* Called under the pairing lock: all storage is bound to one current session.
 * Never perform network/BLE calls or re-enter link_pairing from this function. */
static bool commit_credentials(void){
 const staged_t *s=staged;
 bool ok=s && s->access && *s->access && s->refresh && *s->refresh &&
  config_set_str("refresh_token",s->refresh) && config_set_str("access_token",s->access) &&
  optional("username",s->username) && optional("api_url",s->api) &&
  optional("api_url_v2",s->api_v2) && optional("noise_host",s->noise) &&
  config_erase_key("auth_token") && config_mark_setup_complete();
 if(!ok){(void)config_clear_pairing();(void)config_clear_setup_complete();}
 return ok;
}
static void wipe_free(char *value){
 if(!value)return;
 volatile char *p=value;size_t n=strlen(value);while(n--)*p++=0;
 free(value);
}
static void provision(const char *ssid,const char *password,const char *access,
 const char *refresh,const char *username,const char *ota_url,bool ota_force,
 const char *api,const char *api_v2,const char *noise,uint32_t generation){
 /* Optional upstream OTA is not an authorization to replace the superapp. */
 (void)ota_url;(void)ota_force;
 if(!generation || !link_pairing_provisioning_session_valid(generation))return;
 if(!take_operation()){error_for(generation,"error_operation_in_progress");return;}
 char *minted_access=NULL,*minted_refresh=NULL;
 if(config_setup_complete()){error_for(generation,"error_already_paired");goto done;}
 changed(MUSE_SETUP_JOINING);
 if(!ble_server_send_pairing_status("wifi_connecting",generation))goto done;
 if(!link_pairing_provisioning_session_valid(generation))goto done;
 if(!hooks.join_wifi(ssid,password)){error_for(generation,"wifi_failed");goto done;}
 if(!link_pairing_provisioning_session_valid(generation))goto done;
 (void)ble_server_send_pairing_status("wifi_connected",generation);
 changed(MUSE_SETUP_AUTH);
 if(!access || !*access){error_for(generation,"auth_failed");goto done;}
 if(!refresh || !*refresh){
  if(!hooks.mint_tokens || !hooks.mint_tokens(access,api_v2,&minted_access,&minted_refresh) ||
     !minted_access || !*minted_access || !minted_refresh || !*minted_refresh){
   error_for(generation,"auth_failed");goto done;
  }
  access=minted_access;refresh=minted_refresh;
 }
 staged_t values={access,refresh,username,api,api_v2,noise};staged=&values;
 bool committed=link_pairing_commit_provisioning(generation,commit_credentials);staged=NULL;
 if(!committed){error_for(generation,"error_storage");goto done;}
 changed(MUSE_SETUP_PAIRED);
 if(!ble_server_send_pairing_status("auth_ok",generation))ble_server_disconnect_pairing_session(generation);
 /* Keep Bluetooth available for the authenticated Android companion relay. */
 ble_server_set_companion_advertising(true);
done:
 staged=NULL;wipe_free(minted_access);wipe_free(minted_refresh);atomic_store(&busy,false);
}
static const ble_callbacks_t callbacks={.on_provision=provision,.on_wifi_scan=wifi_scan,
 .on_get_device_info=device_info,.on_client_connected=connected,
 .on_client_disconnected=disconnected,.on_pairing_client_finished=client_finished};
bool muse_setup_init(const muse_setup_hooks_t *configured,const char *version){
 if(!configured || !configured->join_wifi || !version || strlen(version)>=sizeof(firmware))return false;
 hooks=*configured;snprintf(firmware,sizeof(firmware),"%s",version);
 atomic_store(&prompt,0);atomic_store(&confirmation,0);atomic_store(&busy,false);
 changed(config_setup_complete()?MUSE_SETUP_PAIRED:MUSE_SETUP_IDLE);return true;
}
const ble_callbacks_t *muse_setup_callbacks(void){return &callbacks;}
