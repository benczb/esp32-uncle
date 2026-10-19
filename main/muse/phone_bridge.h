/* Passport adaptation of the original Android voice/reply-speech protocol. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Only after failed serialized startup, before a BLE host task has started.
 * Never call for a running host, page exit, or ordinary disconnect. */
void phone_bridge_abort_startup(void);
typedef void (*muse_link_req_cb)(void *,int,const uint8_t *,size_t,bool);
/* Application schedules a redraw; this hook must not access LVGL directly. */
void passport_muse_transport_changed(void);
struct ble_gatt_svc_def;
struct ble_gap_event;
/* Serialized startup, before registering BLE callbacks; false is retryable. */
bool phone_bridge_init(void);
const struct ble_gatt_svc_def *phone_bridge_services(void);
int phone_bridge_gap_event(struct ble_gap_event *event);
bool phone_bridge_ready(void);
int64_t phone_bridge_open(const char *verb, const char *path, const char *const *headers,
                          bool end_body, muse_link_req_cb cb, void *ctx);
bool phone_bridge_send(int64_t id, const void *data, size_t len, bool end_body, int wait_ms);
void phone_bridge_cancel(int64_t id);

bool phone_bridge_speech_ready(void);
bool phone_bridge_speech_send(uint8_t type,const void *data,size_t len);
bool phone_bridge_audio_idle(void);
