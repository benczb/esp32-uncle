#pragma once
#include "ble_server.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum {
 MUSE_SETUP_IDLE, MUSE_SETUP_CONNECTED, MUSE_SETUP_CONFIRM,
 MUSE_SETUP_CONFIRMED, MUSE_SETUP_JOINING, MUSE_SETUP_AUTH,
 MUSE_SETUP_PAIRED, MUSE_SETUP_ERROR
} muse_setup_state_t;
typedef struct {
 /* Worker hooks, never called from a button callback. Network ownership stays
  * in the application. Scan returns an owned wifi_scan_result JSON object. */
 bool (*join_wifi)(const char *ssid,const char *password);
 cJSON *(*scan_wifi)(void);
 /* Optional legacy session-token exchange. Outputs are owned by this module
  * and wiped on free. Modern Muse supplies the device token pair directly. */
 bool (*mint_tokens)(const char *source,const char *api_url,
                     char **access,char **refresh);
 void (*changed)(void);
} muse_setup_hooks_t;
/* Initialize after config_store/identity/link_pairing, before BLE startup. */
bool muse_setup_init(const muse_setup_hooks_t *hooks,const char *version);
const ble_callbacks_t *muse_setup_callbacks(void);
muse_setup_state_t muse_setup_state(void);
bool muse_setup_busy(void);
/* Nonblocking physical-button request. Poll on a worker sends BLE records. */
bool muse_setup_request_confirmation(void);
void muse_setup_poll(void);
