#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "passport_model.h"
#include "passport_reply.h"
#include "passport_photo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
typedef struct {
 char ssid[33], password[65], name[49], tagline[97], contact[129];
 char bridge[160], token[97], city[24];
 uint8_t theme;
} passport_config_t;
extern passport_config_t passport_config;
extern SemaphoreHandle_t passport_mutex;
extern bool passport_connected;
extern char passport_ip[20];
extern bool passport_has_avatar;
extern volatile bool passport_dirty;
extern char passport_weather[320], passport_athena[1800];
extern char passport_station[64], passport_network_status[128];
extern unsigned passport_station_count;
void passport_ui_render(const passport_model_t *model, int battery);
void passport_network_start(void);
void passport_config_load(void);
void passport_network_poll(passport_page_t page);
void passport_audio_request(bool playing, unsigned station);

extern char passport_qr_url[257];
void passport_language_request(unsigned language);
void passport_theme_request(unsigned theme);
unsigned passport_brightness_get(void);
bool passport_brightness_save_failed(void);
void passport_brightness_request(unsigned level);
void passport_open_setup(void);

bool passport_audio_is_playing(void);
unsigned passport_audio_bytes_written(void);
int passport_wifi_test_mode(unsigned mode);
void passport_test_setup_print(void);

void passport_audio_volume(int delta);
unsigned passport_audio_volume_percent(void);

extern int passport_psi_value;
extern char passport_haze_status[80], passport_haze_meta[96];

#define PASSPORT_MAX_BUS_STOPS 10
typedef struct {char code[6],name[73];} passport_bus_stop_t;
typedef struct {char service[16],destination[80],eta[64];} passport_bus_row_t;
extern unsigned passport_bus_stop_count,passport_bus_pages,passport_bus_rows;
extern passport_bus_row_t passport_bus_data[4];
extern char passport_bus_title[96],passport_bus_meta[96];
void passport_bus_request(unsigned stop,unsigned page);

unsigned passport_wifi_profile_count(void);
unsigned passport_wifi_profile_current(void);
const char *passport_wifi_profile_label(unsigned index);
void passport_wifi_profile_request(unsigned index);

/* Push-to-talk runs on the existing audio worker, never the button callback. */
unsigned passport_voice_phase(void);
unsigned passport_voice_bytes(void);
unsigned passport_voice_peak(void);
bool passport_voice_busy(void);
/* Recording/upload, or Muse's pending relay reply, needs the audio owner. */
bool passport_voice_needs_worker(void);
bool passport_audio_worker_active(void);
bool passport_settings_web_active(void);
bool passport_weather_refresh_request(void);
void passport_voice_press(unsigned agent);
void passport_voice_release(void);
void passport_voice_cancel(void);
void passport_voice_run(void *pcm,size_t capacity);
void passport_voice_poll(void);
bool passport_phone_relay_active(void);
char *passport_voice_relay_poll(void);
bool passport_voice_relay_ack(unsigned id,int status,const char *body);
bool passport_network_idle(void);
bool passport_bridge_snapshot(const char *path,char *url,size_t urlcap,char *token,size_t tokencap);

void passport_function_select(passport_page_t page);
unsigned passport_function_active(void);

extern char *passport_train_text;
extern char passport_train_summary[160],passport_train_status[96];
extern unsigned passport_train_alerts;
extern bool passport_train_known,passport_train_stale;
void passport_train_refresh_request(void);

void passport_reply_audio_stop(void);

/* Main-loop worker supervision; never called while holding passport_mutex. */
void passport_background_poll(void);
