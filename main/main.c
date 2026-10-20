#include "passport_app.h"
#include "passport_muse.h"
#include "passport_muse_voice.h"
#include "passport_i18n.h"
#include <time.h>
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "passport_private_defaults.h"
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#endif
static QueueHandle_t keys;
static key_guard_t s_key_guard;
static bool s_input_ready;
typedef struct {bsp_btn_t key;bsp_btn_ev_t event;} input_t;
static void on_key(bsp_btn_t key,bsp_btn_ev_t event,void *user) {
 (void)user;
 if(key_guard_consume(&s_key_guard,event==BSP_BTN_RELEASE,esp_timer_get_time()))return;
 if(!s_input_ready || !keys)return;
 if(event!=BSP_BTN_CLICK && event!=BSP_BTN_LONG && event!=BSP_BTN_RELEASE)return;
 input_t i={key,event};(void)xQueueSend(keys,&i,0);
}
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
static void usb_test_poll(const passport_model_t *model){
 static char line[32];static size_t length;static bool overflow;
 char bytes[32];int n=read(STDIN_FILENO,bytes,sizeof(bytes));
 for(int i=0;i<n;i++){
  char c=bytes[i];
  if(c=='\r')continue;
  if(c!='\n'){if(length+1<sizeof(line))line[length++]=c;else overflow=true;continue;}
  line[length]=0;int command=overflow?-1:passport_test_command(line);length=0;overflow=false;
  if(command==0){
   xSemaphoreTake(passport_mutex,portMAX_DELAY);
   printf("PASSPORT_TEST {\"page\":%u,\"selected\":%u,\"theme\":%u,\"brightness\":%u,\"wifi\":%d,\"wifi_profile\":%u,\"photo_saved\":%d,\"photo_bytes\":%u,\"weather_valid\":%d,\"psi\":%d,\"bus_rows\":%u,\"train_known\":%d,\"train_stale\":%d,\"train_items\":%u,\"train_bytes\":%u,\"weather_bytes\":%u,\"usage_bytes\":%u,\"hermes_bytes\":%u,\"audio\":%d,\"audio_worker\":%d,\"settings_web\":%d,\"pcm_bytes\":%u,\"volume\":%u,\"function\":%u,\"network_busy\":%d,\"voice_phase\":%u,\"voice_bytes\":%u,\"voice_peak\":%u,\"heap\":%u,\"largest\":%u}\n",
    (unsigned)model->page,model->selected,passport_config.theme,passport_brightness_get(),passport_connected,passport_wifi_profile_current(),passport_has_avatar,(unsigned)passport_avatar_allocation,(passport_weather[0]>='0'&&passport_weather[0]<='9')||(passport_weather[0]=='-'&&passport_weather[1]>='0'&&passport_weather[1]<='9'),passport_psi_value,passport_bus_rows,passport_train_known,passport_train_stale,passport_train_alerts,(unsigned)(passport_train_text?strlen(passport_train_text):0),
    (unsigned)strlen(passport_weather),(unsigned)strlen(passport_athena),(unsigned)(passport_reply_peek(0)?strlen(passport_reply_peek(0)):0),
    passport_audio_is_playing(),passport_audio_worker_active(),passport_settings_web_active(),passport_audio_bytes_written(),passport_audio_volume_percent(),passport_function_active(),!passport_network_idle(),passport_voice_phase(),passport_voice_bytes(),passport_voice_peak(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
   xSemaphoreGive(passport_mutex);
  }else if(command==14 || command==15){
   printf("PASSPORT_TEST opus-probe=%d\n",passport_muse_speech_probe_request(command==14?24576:14336));
  }else if(command==13){
   printf("PASSPORT_TEST weather-refresh=%d\n",passport_weather_refresh_request());
  }else if(command==12){
   input_t input={BSP_BTN_DOWN,BSP_BTN_RELEASE};(void)xQueueSend(keys,&input,0);
  }else if(command==11){passport_test_setup_print();
  }else if(command>=7){
   unsigned mode=command==7?1:command==8?2:command==10?3:0;
   printf("PASSPORT_TEST wifi-mode=%u result=%d\n",mode,passport_wifi_test_mode(mode));
  }else if(command>0){
   input_t input={(bsp_btn_t)((command-1)%3),command>3?BSP_BTN_LONG:BSP_BTN_CLICK};
   if(xQueueSend(keys,&input,0)!=pdTRUE)puts("PASSPORT_TEST busy");
  }else puts("PASSPORT_TEST invalid");
 }
}
#endif
void app_main(void) {
 passport_mutex=xSemaphoreCreateMutex();if(!passport_mutex)return;
 passport_config_load();
 if(esp_sleep_get_wakeup_causes() & (1u<<ESP_SLEEP_WAKEUP_GPIO))
  key_guard_arm(&s_key_guard,esp_timer_get_time(),10000000);
 if(bsp_i2c_init()!=ESP_OK || bsp_display_init()!=ESP_OK || !bsp_lvgl_init())return;
 bsp_display_backlight(passport_brightness_get());(void)bsp_battery_init();
 keys=xQueueCreate(12,sizeof(input_t));if(!keys)return;
 ESP_ERROR_CHECK(bsp_button_init(on_key,NULL));
 key_guard_sync_released(&s_key_guard,bsp_button_read_mv()>=BSP_BTN_MV_RELEASED_MIN);
 s_input_ready=true;
 passport_network_start();
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
 usb_serial_jtag_driver_config_t usb={.rx_buffer_size=256,.tx_buffer_size=256};
 bool usb_driver=usb_serial_jtag_driver_install(&usb)==ESP_OK;
 if(usb_driver)usb_serial_jtag_vfs_use_driver();
 int flags=fcntl(STDIN_FILENO,F_GETFL,0);
 bool usb_test_ready=usb_driver && flags>=0 && fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK)==0;
 puts(usb_test_ready?"PASSPORT_TEST ready":"PASSPORT_TEST unavailable");
#endif
 passport_model_t model={0};int tick=0,battery=-1;time_t displayed_minute=(time_t)-1;
 passport_screensaver_t saver;passport_screensaver_init(&saver,esp_timer_get_time());
 int64_t wake_ignore_until=0;
 for(;;) {
  if(!saver.asleep)passport_background_poll();
#if PASSPORT_PRIVATE_USB_TEST_CONTROL
  if(usb_test_ready)usb_test_poll(&model);
#endif
  input_t input;bool changed=false;
  if(xQueueReceive(keys,&input,pdMS_TO_TICKS(250))) {
   if(passport_screensaver_activity(&saver,esp_timer_get_time())){
    /* The first press after the idle timeout only wakes the display:
     * restore the backlight, re-arm fetching, and refresh the current page
     * below (changed). The press is consumed, so it cannot also navigate,
     * toggle the radio, or start a recording. One physical press produces
     * several button events (release, then click), so events arriving in
     * the short grace window after the wake are consumed as well. */
    passport_screen_awake_set(true);
    bsp_display_backlight(model.page==PAGE_BRIGHTNESS?model.reply_page:passport_brightness_get());
    wake_ignore_until=esp_timer_get_time()+500000;
    changed=true;
   }else if(esp_timer_get_time()<wake_ignore_until){
    /* Follow-on event of the press that woke the display; swallow it. */
   }else if(input.event==BSP_BTN_RELEASE){
    if(input.key==BSP_BTN_DOWN)passport_voice_release();
   }else if(model.page==PAGE_MUSE && passport_muse_supported() && input.event==BSP_BTN_CLICK && input.key==BSP_BTN_OK){
    if(!passport_muse_voice_ready() || passport_muse_passkey()){
     model.volume_mode=false;
     if(!passport_muse_confirm())passport_muse_enter();
    }else model.volume_mode=!model.volume_mode;
    changed=true;
   }else if((model.page==PAGE_HERMES || model.page==PAGE_MUSE) &&
       ((input.key==BSP_BTN_OK && input.event==BSP_BTN_CLICK) || (model.volume_mode && input.key!=BSP_BTN_OK))){
    int delta=0;passport_assistant_volume_key(&model,input.key,input.event==BSP_BTN_LONG,&delta);
    if(delta)passport_audio_volume(delta);
    changed=true;
   }else if((model.page==PAGE_HERMES || model.page==PAGE_MUSE) && input.event==BSP_BTN_LONG && input.key==BSP_BTN_DOWN){
    passport_voice_press(model.page==PAGE_MUSE);changed=true;
   }else if(model.page==PAGE_RADIO && input.event==BSP_BTN_LONG && input.key!=BSP_BTN_OK) {
    passport_audio_volume(input.key==BSP_BTN_UP?10:-10);
   } else {
    if(input.key==BSP_BTN_DOWN && input.event==BSP_BTN_LONG &&
       (model.page==PAGE_HOME || model.page==PAGE_SETTINGS))passport_open_setup();
    passport_page_t previous=model.page;
    if(model.page==PAGE_LANGUAGE && input.event==BSP_BTN_CLICK && input.key==BSP_BTN_OK){
     passport_language_request(model.reply_page);
    }else if(model.page==PAGE_BRIGHTNESS && input.event==BSP_BTN_CLICK && input.key==BSP_BTN_OK){
     passport_brightness_request(model.reply_page);
    }else if(model.page==PAGE_APPEARANCE && input.event==BSP_BTN_CLICK && input.key==BSP_BTN_OK){
     passport_theme_request(model.reply_page);
    }else if(model.page==PAGE_TRAIN && input.event==BSP_BTN_CLICK && input.key==BSP_BTN_OK){
     passport_train_refresh_request();
    }else if(model.page==PAGE_SETTINGS && input.event==BSP_BTN_CLICK && passport_wifi_profile_count()){
     if(passport_wifi_navigate(&model,input.key,passport_wifi_profile_count()))passport_wifi_profile_request(model.wifi_profile);
    }else if(model.page==PAGE_BUS && input.event==BSP_BTN_CLICK){
     unsigned stops,pages;xSemaphoreTake(passport_mutex,portMAX_DELAY);stops=passport_bus_stop_count;pages=passport_bus_pages;xSemaphoreGive(passport_mutex);
     passport_bus_navigate(&model,input.key,stops,pages);
    }else passport_navigate(&model,input.key,input.event==BSP_BTN_LONG);
    if(previous!=model.page){model.volume_mode=false;passport_function_select(model.page);}
    if(model.page==PAGE_SETTINGS && previous!=PAGE_SETTINGS){
     unsigned current=passport_wifi_profile_current(),count=passport_wifi_profile_count();model.wifi_profile=current<count?current:0;
    }
    if(model.page==PAGE_BRIGHTNESS){
     if(previous!=PAGE_BRIGHTNESS)model.reply_page=passport_brightness_get();
     bsp_display_backlight(model.reply_page);
    }else if(previous==PAGE_BRIGHTNESS)bsp_display_backlight(passport_brightness_get());
    if(model.page==PAGE_LANGUAGE && previous!=PAGE_LANGUAGE)model.reply_page=passport_language_get();
    if(model.page==PAGE_APPEARANCE && previous!=PAGE_APPEARANCE)model.reply_page=passport_config.theme;
    if(model.page==PAGE_RADIO && passport_station_count)model.station%=passport_station_count;
    passport_audio_request(model.page==PAGE_RADIO && model.playing,model.station);
    if(model.page!=PAGE_RADIO)model.playing=false;
    changed=true;
   }
  }
  if(model.page==PAGE_RADIO && model.playing && !passport_audio_is_playing()){model.playing=false;changed=true;}
  /* Screensaver, all pages: after the idle timeout the backlight goes off
   * and fetching/rendering below freeze until the next key press. Audio
   * playback, a voice interaction, or an open phone setup page counts as
   * busy and keeps the display awake. Wi-Fi is left associated in modem
   * power-save; disconnecting would cost a reconnect on every wake. */
  bool saver_busy=(model.page==PAGE_RADIO && model.playing) || passport_audio_is_playing() ||
   passport_voice_busy() || passport_settings_web_active();
  if(passport_screensaver_poll(&saver,esp_timer_get_time(),saver_busy)){
   passport_screen_awake_set(false);
   bsp_display_backlight(0);
  }
  if(tick++%120==0 && !saver.asleep){battery=bsp_battery_soc();changed=true;}
  if(model.page==PAGE_HOME && !saver.asleep){time_t minute=time(NULL)/60;if(minute!=displayed_minute){displayed_minute=minute;changed=true;}}
  if(!saver.asleep && (changed || tick%20==0)){
   if(model.page==PAGE_BUS)passport_bus_request(model.bus_stop,model.reply_page);
   else passport_network_poll(model.page);
  }
  if(!saver.asleep && (changed || passport_dirty || tick%240==0)) {
   xSemaphoreTake(passport_mutex,portMAX_DELAY);
   if(bsp_lvgl_lock(1000)){passport_ui_render(&model,battery);bsp_lvgl_unlock();passport_dirty=false;}
   xSemaphoreGive(passport_mutex);
  }
 }
}
