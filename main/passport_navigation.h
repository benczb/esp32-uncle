#pragma once
#include "passport_features.h"
typedef struct {
 passport_turn_t turn;
 char status[80];
 bool enabled,valid,stale;
 int64_t updated_us,overlay_until_us;
} passport_navigation_t;
/* Request from UI; all BLE lifecycle work runs on the network worker. */
void passport_navigation_toggle(void);
void passport_navigation_poll(void);
void passport_navigation_synced(void);
void passport_navigation_reset(void);
void passport_navigation_snapshot(passport_navigation_t *out);
