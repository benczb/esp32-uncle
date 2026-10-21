#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PASSPORT_APP_COUNT 10
#define PASSPORT_AVATAR_W 80
#define PASSPORT_AVATAR_H 112
#define PASSPORT_AVATAR_BYTES (PASSPORT_AVATAR_W * PASSPORT_AVATAR_H * 2)
typedef enum { PAGE_HOME, PAGE_APPS, PAGE_CARD, PAGE_ATHENA, PAGE_HERMES,
 PAGE_MUSE, PAGE_RADIO, PAGE_WEATHER, PAGE_BUS, PAGE_SETTINGS, PAGE_TRAIN, PAGE_APPEARANCE, PAGE_PREFERENCES, PAGE_BRIGHTNESS, PAGE_LANGUAGE, PAGE_TRAFFIC } passport_page_t;
typedef struct { passport_page_t page; unsigned selected; unsigned station;
 bool playing; bool volume_mode; unsigned reply_page; unsigned bus_stop; unsigned wifi_profile; unsigned traffic_qr; } passport_model_t;
/* key: 0 up, 1 down, 2 OK; hold: long-press, not release. */
void passport_navigate(passport_model_t *m, unsigned key, bool hold);
size_t passport_utf8_prefix(const char *text, size_t limit);

void passport_bus_navigate(passport_model_t *m,unsigned key,unsigned stops,unsigned pages);

bool passport_wifi_navigate(passport_model_t *model,unsigned key,unsigned profiles);

/* Three attempts within a 30-second window; tick arithmetic survives wraparound. */
bool passport_wifi_should_retry(unsigned failures,uint32_t now,uint32_t deadline);

bool passport_valid_qr_url(const char *url);
bool passport_valid_clock(double unix_time);
void passport_clock_format(char *out,size_t capacity,unsigned weekday,unsigned hour,unsigned minute,bool synced);

bool passport_resolve_profile_password(char *out,size_t cap,const char *stored,const char *entered);

/* USB test: 0 status, 1..3 clicks, 4..6 holds, 7..10 Wi-Fi, 11 setup, 12 Down release, 13 force weather; -1 invalid. */
int passport_test_command(const char *line);
bool passport_relay_pcm_frame_valid(const uint8_t *frame,size_t bytes,uint32_t generation);

/* Weather is a shared service; other page work needs the selected function. */
bool passport_service_allowed(passport_page_t active,passport_page_t requested);
bool passport_browser_relay_page(passport_page_t page);
bool passport_web_required(passport_page_t page,bool setup_ap,bool relay_active);

/* Empty keeps the stored key; otherwise visible ASCII only, no header controls. */
bool passport_valid_lta_key(const char *key);

/* Assistant volume panel: returns true when the panel consumed the key. */
bool passport_assistant_volume_key(passport_model_t *model,unsigned key,bool hold,int *delta);

/* Profile card views cycle: 0 profile, 1 QR code, 2 NFC tap hint. */
#define PASSPORT_CARD_VIEWS 3

/* Screensaver: pure inactivity state, timestamps in microseconds (esp_timer).
 * The display sleeps after the idle timeout unless the caller reports the
 * device busy (audio, recording, or phone setup in progress). Any key is
 * activity: it wakes a sleeping display and restarts the idle countdown. */
#define PASSPORT_SCREENSAVER_IDLE_US 10000000LL
typedef struct { int64_t last_activity_us; bool asleep; } passport_screensaver_t;
void passport_screensaver_init(passport_screensaver_t *state,int64_t now_us);
/* Record activity; returns true when this activity woke the display. */
bool passport_screensaver_activity(passport_screensaver_t *state,int64_t now_us);
/* Advance time; returns true only on the poll that puts the display to sleep. */
bool passport_screensaver_poll(passport_screensaver_t *state,int64_t now_us,bool busy);

/* Traffic Light page: one full pedestrian crossing cycle, timed by the
 * bundled crossing recording (elapsed and total in microseconds on the
 * recording's timeline). The red standing man shows while the soft locator
 * sound plays; then the green walking man with a red countdown; the green
 * man flashes for the final seconds, as on a real Singapore crossing. */
#define PASSPORT_TRAFFIC_RED_US 3000000LL
#define PASSPORT_TRAFFIC_FLASH_US 5000000LL
#define PASSPORT_TRAFFIC_QR_COUNT 3
typedef enum { PASSPORT_TRAFFIC_PHASE_RED, PASSPORT_TRAFFIC_PHASE_GREEN,
 PASSPORT_TRAFFIC_PHASE_DONE } passport_traffic_phase_t;
passport_traffic_phase_t passport_traffic_phase(int64_t elapsed_us,int64_t total_us);
/* Seconds left in the green phase, counting down to 1; 0 outside it. */
unsigned passport_traffic_countdown(int64_t elapsed_us,int64_t total_us);
/* Whether the green man is lit at this instant (steady, then flashing). */
bool passport_traffic_green_lit(int64_t elapsed_us,int64_t total_us);
